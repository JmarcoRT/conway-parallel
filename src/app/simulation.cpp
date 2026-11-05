#include "app/simulation.hpp"
#include <omp.h>
#include <sched.h>
#include <unistd.h>
#include <algorithm>
#include <charconv>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <thread>

namespace conway::app {
namespace {
InputData cells(const Game& game) {
    InputData out{game.width(),game.height(),{}};
    out.cells.reserve(out.width*out.height);
    for(std::size_t r=0;r<out.height;++r)
        for(std::size_t c=0;c<out.width;++c) out.cells.push_back(game.isAlive(r,c));
    return out;
}
std::string mpiError(const std::exception& e) {
    std::string detail=e.what();
    if(detail.find("slots")!=std::string::npos)
        detail="El runtime no dispone de slots suficientes: reduce P o revisa su configuración.";
    else if(detail.size()>400) detail.resize(400);
    return "No se pudo completar la sesión MPI. " + detail +
        " · U: recuperar; Esc: menú; Q: abandonar. No se activa sobresuscripción.";
}
}
mpi::ClientOptions defaultMPIOptions(int processes) {
    mpi::ClientOptions result;
    result.processes=processes;
    result.launcher=CONWAY_MPI_LAUNCHER;
    std::error_code error;
    auto executable=std::filesystem::read_symlink("/proc/self/exe",error);
    result.worker=(error?std::filesystem::path("conway_mpi_worker"):executable.parent_path()/"conway_mpi_worker").string();
    return result;
}
int recommendedMPIProcesses(std::size_t height) noexcept {
    cpu_set_t allowed; CPU_ZERO(&allowed);
    int cpus=sched_getaffinity(0,sizeof(allowed),&allowed)==0?CPU_COUNT(&allowed):1;
    return static_cast<int>(std::min<std::size_t>(std::max(1,cpus-1),std::clamp<std::size_t>(height,1,1024)));
}
std::optional<int> parseMPIProcesses(std::string_view text) noexcept {
    int count=0;
    const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),count);
    if(text.empty() || error!=std::errc{} || end!=text.data()+text.size() || count<1 || count>1024) return {};
    return count;
}
struct SimulationSession::Worker {
    struct Job { Action action; Engine engine; InputData input; std::size_t generation; mpi::ClientOptions options; bool backgroundLocal{}; std::uint64_t epoch{}; core::CudaBlockSize block{core::kDefaultCudaBlock}; };
    struct Delivery { std::optional<Game> game; std::string error; bool closed{}; std::vector<int> pids; std::uint64_t epoch{}; };
    std::mutex mutex;
    std::condition_variable wake;
    std::optional<Job> job;
    std::optional<Delivery> delivery;
    bool stopping=false;
    std::function<void()> notify;
    std::thread thread;
    explicit Worker(std::function<void()> signal):notify(std::move(signal)),thread([this]{run();}) {}
    ~Worker() {
        {std::lock_guard lock(mutex);stopping=true;} wake.notify_one(); thread.join();
    }
    void run() {
        std::unique_ptr<mpi::Client> client;
        std::optional<Game> local;
        for(;;) {
            Job task;
            {std::unique_lock lock(mutex); wake.wait(lock,[&]{return stopping || job.has_value();});
             if(stopping) break; task=std::move(*job); job.reset();}
            Delivery result;result.epoch=task.epoch;
            try {
                if(task.action==Action::Close || task.engine!=Engine::MPI || task.action==Action::Configure) {
                    if(client) {
                        try {client->close();}
                        catch(...) {if(task.action==Action::Close) throw;}
                    }
                    client.reset();
                }
                if(task.action==Action::Close) {local.reset();result.closed=true;}
                else if(task.engine!=Engine::MPI) {
                    if(task.backgroundLocal) {
                        if(task.action!=Action::Step) {local.emplace(task.input,task.generation);local->setEngine(task.engine);}
                        if(!local)throw std::logic_error("Vista previa cerrada");
                        if(task.action==Action::Step)local->step();
                        result.game=*local;
                    } else {
                        result.game.emplace(task.input,task.generation);
                        if(task.engine==Engine::CUDA) result.game->setCudaBlockSize(task.block);
                        result.game->setEngine(task.engine);
                    }
                } else {
                    if(!client) {
                        if(task.action==Action::Step) throw std::runtime_error("Recupera primero la sesión MPI.");
                        if(task.input.cells.size()>mpi::wire::maxPayload-32)
                            throw std::length_error("El tablero supera el límite MPI de 64 MiB menos 32 bytes.");
                        client=std::make_unique<mpi::Client>(task.options);
                        client->load(task.input,task.generation);
                    } else if(task.action==Action::Reset) client->load(task.input,0);
                    if(task.action==Action::Step) client->advance();
                    auto snapshot=client->snapshot();
                    result.game.emplace(snapshot.input,snapshot.generation);
                    result.pids=client->workerPids();
                }
            } catch(const std::exception& e) {
                result.game.reset();
                result.error=task.engine==Engine::CUDA ? std::string("Error CUDA: ")+e.what()+" · U recuperar; Esc menú. Sin fallback." : task.backgroundLocal?std::string("Error de vista previa: ")+e.what():mpiError(e); client.reset();
                result.closed=task.action==Action::Close;
            }
            {std::lock_guard lock(mutex); delivery=std::move(result);}
            if(notify) notify();
        }
        client.reset();
    }
};
SimulationSession::SimulationSession(const InputData& input,Engine engine,mpi::ClientOptions options,std::function<void()> notify,bool backgroundLocal,core::CudaBlockSize block)
    :initialInput_(input),engine_(engine),cudaBlock_(block),game_(input),options_(std::move(options)),notify_(std::move(notify)),backgroundLocal_(backgroundLocal) {
    if(backgroundLocal_ && engine_!=Engine::Sequential)throw std::invalid_argument("La vista previa asíncrona requiere Secuencial");
    if(engine_==Engine::MPI || backgroundLocal_) submit(Action::Configure);
    else {
        game_.setEngine(engine_);
        if(engine_==Engine::CUDA) {
            try {game_.setCudaBlockSize(cudaBlock_);}
            catch(const std::exception& e) {error_=std::string("Error CUDA: ")+e.what();}
        }
        updatePreview();
    }
}
SimulationSession::~SimulationSession()=default;
const Game& SimulationSession::game() const noexcept {return game_;}
bool SimulationSession::isRunning() const noexcept {return running_;}
void SimulationSession::updatePreview() {
    preview_.reset();
    if(backgroundLocal_)return;
    if(engine_!=Engine::MPI && engine_!=Engine::CUDA && !closed_) {
        try {preview_=game_;preview_->step();}
        catch(const std::exception& e) {preview_.reset();running_=false;error_=std::string("Error de vista previa: ")+e.what();}
    }
}
void SimulationSession::submit(Action action) {
    running_=action==Action::Step?running_:false;
    if(busy_) {if(action!=Action::Step && pending_!=Action::Close) pending_=action;return;}
    if(action==Action::Close && !worker_) {closed_=true;preview_.reset();return;}
    error_.clear(); closed_=false;
    if(!worker_) worker_=std::make_unique<Worker>(notify_);
    Worker::Job job{action,engine_,action==Action::Reset?initialInput_:
                    action==Action::Step || action==Action::Close?InputData{}:cells(game_),
                    action==Action::Reset?0:game_.generation(),options_,backgroundLocal_,epoch_,cudaBlock_};
    {std::lock_guard lock(worker_->mutex); worker_->job=std::move(job);}
    busy_=true; preview_.reset(); worker_->wake.notify_one();
}
void SimulationSession::poll() {
    if(!worker_ || !busy_) return;
    std::optional<Worker::Delivery> result;
    {std::lock_guard lock(worker_->mutex);result=std::move(worker_->delivery);worker_->delivery.reset();}
    if(!result) return;
    busy_=false;
    if(backgroundLocal_ && result->epoch!=epoch_) {
        if(pending_) {auto action=*pending_;pending_.reset();submit(action);}
        return;
    }
    if(result->game) game_=std::move(*result->game);
    if(!result->pids.empty()) pids_=std::move(result->pids);
    error_=std::move(result->error); closed_=result->closed;
    if(!error_.empty()) running_=false;
    if(pending_) {
        const auto next=*pending_;pending_.reset();
        try {submit(next);}
        catch(const std::exception& e) {running_=false;error_=std::string("Error de configuración pendiente: ")+e.what();}
    } else updatePreview();
}
void SimulationSession::step() {
    if(busy_ || closed_ || !error_.empty()) return;
    if(engine_==Engine::MPI || backgroundLocal_) submit(Action::Step);
    else {
        if(engine_!=Engine::CUDA) {game_.step();updatePreview();return;}
        try {game_.step();updatePreview();}
        catch(const std::exception& e) {running_=false;preview_.reset();error_=std::string("Error CUDA: ")+e.what()+" · U recuperar; Esc menú. Sin fallback.";}
    }
}
void SimulationSession::toggleRunning() noexcept {if(!closed_ && error_.empty()) running_=!running_;}
void SimulationSession::advanceIfRunning() {if(running_) step();}
void SimulationSession::reset() {
    running_=false;
    if(backgroundLocal_)++epoch_;
    if(engine_==Engine::MPI || backgroundLocal_ || busy_) submit(Action::Reset);
    else {
        try {
            Game candidate(initialInput_);
            if(engine_==Engine::CUDA)candidate.setCudaBlockSize(cudaBlock_);
            candidate.setEngine(engine_);game_=std::move(candidate);error_.clear();updatePreview();
        } catch(const std::exception& e) {error_=std::string("Error al reiniciar: ")+e.what();}
    }
}
void SimulationSession::configure(Engine engine,int processes,core::CudaBlockSize block) {
    if(backgroundLocal_ && engine!=Engine::Sequential)throw std::invalid_argument("La vista previa solo usa Secuencial");
    if(processes<1 || processes>1024) throw std::invalid_argument("P debe estar entre 1 y 1024.");
    running_=false;
    const bool needsWorker=backgroundLocal_ || engine_==Engine::MPI || engine==Engine::MPI || busy_;
    // Validation precedes configuration publication and never advances the board.
    if(engine==Engine::CUDA) {
        if(needsWorker) {Game validation(game_);validation.setCudaBlockSize(block);}
        else game_.setCudaBlockSize(block);
    }
    const auto oldEngine=engine_;
    const auto oldProcesses=options_.processes;
    const auto oldBlock=cudaBlock_;
    engine_=engine; options_.processes=processes; cudaBlock_=block;
    if(needsWorker) {
        try {submit(Action::Configure);}
        catch(...) {engine_=oldEngine;options_.processes=oldProcesses;cudaBlock_=oldBlock;throw;}
    } else {game_.setEngine(engine_);error_.clear();updatePreview();}
}
void SimulationSession::recover() {
    running_=false;
    if(engine_==Engine::CUDA && !busy_) {
        try {game_.setCudaBlockSize(cudaBlock_);game_.setEngine(Engine::CUDA);error_.clear();closed_=false;}
        catch(const std::exception& e) {error_=std::string("Error CUDA: ")+e.what();}
    } else submit(Action::Configure);
}
void SimulationSession::close() {if(closed_) return;running_=false;if(backgroundLocal_)++epoch_;submit(Action::Close);}
} // namespace conway::app
