#include "app/didactic_session.hpp"
#include "app/simulation.hpp"
#include <omp.h>
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace conway::app {
namespace {
class CapturingSink final:public core::StepTraceSink {
public:
    std::optional<core::StepTrace> trace;
    void onStepComplete(core::StepTrace&& value) override {trace=std::move(value);}
};
class CaptureBackend final:public DidacticBackend {
    std::unique_ptr<mpi::Client> client_;
    std::optional<Game> local_;
    bool uncertain_=false;
public:
    bool ownsMPI() const noexcept override {return bool(client_) || uncertain_;}
    bool cleanupConfirmed() const noexcept override {return !client_ && !uncertain_;}
    void close() override {
        if(client_) {
            try {client_->close();}
            catch(...) {uncertain_=!client_->cleanupConfirmed();client_.reset();local_.reset();throw;}
            uncertain_=!client_->cleanupConfirmed();client_.reset();
        }
        local_.reset();
        if(uncertain_) throw std::runtime_error("Limpieza de la sesión MPI didáctica no confirmada; no se permite otra sesión.");
    }
    std::shared_ptr<const DidacticCapture> capture(const DidacticRequest& q) override {
        if(uncertain_) throw std::runtime_error("Limpieza MPI anterior no confirmada.");
        if(q.reset) {
            close();
            if(q.engine==Engine::MPI) {
                (void)mpi::trace_payload_size(q.initial->width,q.initial->height,q.mpiOptions.processes,q.selection);
                try {client_=std::make_unique<mpi::Client>(q.mpiOptions);}
                catch(const mpi::ClientError& e) {uncertain_=!e.cleanupConfirmed;throw;}
                client_->load(*q.initial);
            } else {
                if(q.engine==Engine::OpenMP && q.threads<1) throw std::invalid_argument("Número de hilos inválido.");
                local_.emplace(*q.initial);local_->setEngine(q.engine);
            }
        }
        if(q.engine==Engine::MPI) {
            if(!client_) throw std::runtime_error("Sesión MPI didáctica cerrada; reinicia explícitamente.");
            auto trace=std::make_shared<const mpi::MpiStepTrace>(client_->traceStep(q.selection));
            InputData before{trace->width,trace->height,std::vector<std::uint8_t>(trace->width*trace->height)},after=before;
            for(const auto& rank:trace->ranks) {
                const auto stride=trace->width+2;
                for(std::size_t r=0;r<rank.partition.rowCount;++r) {
                    const auto source=(r+1)*stride+1,target=(rank.partition.firstRow+r)*trace->width;
                    std::copy_n(rank.currentBefore.begin()+source,trace->width,before.cells.begin()+target);
                    std::copy_n(rank.currentAfter.begin()+source,trace->width,after.cells.begin()+target);
                }
            }
            return std::make_shared<const DidacticCapture>(DidacticCapture{
                Game(before,trace->generationBefore),Game(after,trace->generationAfter),{},std::move(trace)});
        }
        if(!local_ || local_->generation()!=q.selection.expectedGeneration) throw std::runtime_error("Generación local didáctica inesperada.");
        Game before=*local_;
        if(q.engine==Engine::OpenMP) omp_set_num_threads(q.threads);
        CapturingSink sink;local_->step(&sink);
        if(!sink.trace) throw std::runtime_error("El motor no emitió StepTrace.");
        sink.trace->requested_threads=q.threads;
        return std::make_shared<const DidacticCapture>(DidacticCapture{std::move(before),*local_,std::move(sink.trace),{}});
    }
};
}
std::unique_ptr<DidacticBackend> makeDidacticBackend() {return std::make_unique<CaptureBackend>();}
struct DidacticController::Worker {
    struct Delivery {
        std::uint64_t epoch=0;
        bool close=false,mpi=false,cleaned=true;
        std::shared_ptr<const DidacticCapture> capture;
        std::string error;
    };
    std::mutex mutex;
    std::condition_variable wake,delivered;
    std::optional<Job> job;
    std::optional<Delivery> delivery;
    bool stopping=false;
    std::unique_ptr<DidacticBackend> backend;
    std::function<void()> notify;
    std::thread thread;
    Worker(std::function<void()> signal,std::unique_ptr<DidacticBackend> source)
        :backend(std::move(source)),notify(std::move(signal)),thread([this]{run();}) {}
    ~Worker() {
        {std::lock_guard lock(mutex);stopping=true;}wake.notify_one();thread.join();
    }
    void run() {
        for(;;) {
            Job task;
            {std::unique_lock lock(mutex);wake.wait(lock,[&]{return stopping || job.has_value();});
             if(stopping) break;task=std::move(*job);job.reset();}
            Delivery result;result.epoch=task.request.epoch;result.close=task.close;
            try {
                if(task.close) backend->close();
                else {
                    result.capture=backend->capture(task.request);
                    if(!result.capture) throw std::runtime_error("El productor no entregó una captura didáctica.");
                }
            } catch(const std::exception& e) {
                result.error=e.what();
                try {backend->close();} catch(const std::exception& closeError) {result.error+="\nCierre: ";result.error+=closeError.what();}
            } catch(...) {
                result.error="Fallo desconocido durante la captura didáctica.";
                try {backend->close();} catch(...) {result.error+=" Cierre no completado.";}
            }
            result.mpi=backend->ownsMPI();result.cleaned=backend->cleanupConfirmed();
            {std::lock_guard lock(mutex);if(stopping) break;delivery=std::move(result);}
            delivered.notify_all();if(notify) notify();
        }
        try {backend->close();} catch(...) {} // Final shutdown fallback; never an ordinary UI action.
    }
};
DidacticController::DidacticController(std::function<void()> notify,std::unique_ptr<DidacticBackend> backend)
    :worker_(std::make_unique<Worker>(std::move(notify),backend?std::move(backend):makeDidacticBackend())) {}
DidacticController::~DidacticController() {worker_.reset();}
void DidacticController::setInitialInput(const InputData& input) {initial_=std::make_shared<const InputData>(input);}
const InputData& DidacticController::initialInput() const {static const InputData empty{};return initial_?*initial_:empty;}
void DidacticController::startPreparation(const InputData& input,Engine engine,int threads,mpi::ClientOptions options,std::size_t row,std::size_t col) {
    setInitialInput(input);
    if(engine==Engine::MPI) {
        const auto defaults=defaultMPIOptions(options.processes);
        if(options.launcher.empty()) options.launcher=defaults.launcher;
        if(options.worker.empty()) options.worker=defaults.worker;
    }
    configuration_={++epoch_,engine,threads,std::move(options),initial_,{0,row,col},true};
    capture_.reset();stage_=0;stageElapsed_=0;error_.clear();playing_=true;phase_=Phase::Preparing;
    enqueue({configuration_,false});
}
void DidacticController::prepareSynchronously(const InputData& input,Engine engine,int threads) {startPreparation(input,engine,threads);waitUntilReady();}
void DidacticController::submit(Job job) {
    if(job.request.engine==Engine::MPI && !job.close) {mpiActive_=true;cleanupConfirmed_=false;}
    busy_=true;
    {std::lock_guard lock(worker_->mutex);worker_->job=std::move(job);}worker_->wake.notify_one();
}
void DidacticController::enqueue(Job job) {
    if(job.request.engine==Engine::MPI && !job.close) {mpiActive_=true;cleanupConfirmed_=false;}
    if(busy_) pending_=std::move(job);else submit(std::move(job));
}
void DidacticController::poll() {
    std::optional<Worker::Delivery> result;
    {std::lock_guard lock(worker_->mutex);result=std::move(worker_->delivery);worker_->delivery.reset();}
    if(!result) return;
    busy_=false;cleanupConfirmed_=result->cleaned;mpiActive_=result->mpi;
    auto diagnose=[&](const std::string& message) {
        if(result->close && !error_.empty()) {
            if(error_.find(message)==std::string::npos) error_+="\n"+message;
        } else error_=message;
    };
    // Even an obsolete operation cannot hide uncertain MPI cleanup. A close
    // that returns without throwing must also prove cleanup before replacement.
    if(!result->cleaned && (result->close || !result->error.empty())) {
        diagnose(result->error.empty()?"Cierre didáctico sin limpieza confirmada.":result->error);
        playing_=false;phase_=Phase::Error;mpiActive_=true;pending_.reset();return;
    }
    if(!result->error.empty() && result->epoch==epoch_) {
        diagnose(result->error);playing_=false;phase_=Phase::Error;
    } else if(result->epoch==epoch_) {
        if(result->close) {
            if(result->mpi) {diagnose("Cierre didáctico conserva recursos MPI.");mpiActive_=true;phase_=Phase::Error;}
            else phase_=Phase::Closed;
        } else {
            capture_=std::move(result->capture);stage_=0;stageElapsed_=0;phase_=Phase::Ready;
        }
    }
    if(pending_) {auto job=std::move(*pending_);pending_.reset();submit(std::move(job));}
}
void DidacticController::waitUntilReady() {
    for(;;) {
        poll();
        if(!busy_ && !pending_) return;
        std::unique_lock lock(worker_->mutex);worker_->delivered.wait(lock,[&]{return worker_->delivery.has_value();});
    }
}
void DidacticController::joinWorker() {waitUntilReady();}
void DidacticController::play() {if(phase_==Phase::Ready || phase_==Phase::Preparing) playing_=true;}
void DidacticController::pause() {playing_=false;}
void DidacticController::togglePlay() {if(playing_) pause();else play();}
void DidacticController::setSpeed(double value) {if(std::isfinite(value)) speed_=std::clamp(value,0.1,8.0);}
void DidacticController::increaseSpeed() {for(auto v:{.25,.5,1.,2.,4.,8.}) if(v>speed_+1e-9) {speed_=v;return;}}
void DidacticController::decreaseSpeed() {for(auto v:{8.,4.,2.,1.,.5,.25}) if(v<speed_-1e-9) {speed_=v;return;}}
void DidacticController::setStageDuration(std::chrono::nanoseconds duration) {
    if(duration.count()<=0 || duration.count()>std::chrono::hours(1).count()*3600000000000LL)
        throw std::invalid_argument("Duración de etapa inválida.");
    // Preserve stage and proportional progress; this is presentation time only.
    stageElapsed_=static_cast<std::uint64_t>(static_cast<long double>(stageElapsed_)*duration.count()/stageDuration_);
    stageDuration_=static_cast<std::uint64_t>(duration.count());
}
std::vector<DidacticStage> DidacticController::stages() const {
    if(configuration_.engine==Engine::MPI) return {DidacticStage::MPIState,DidacticStage::MPIExchangeFirst,
        DidacticStage::MPIExchangeSecond,DidacticStage::MPICell,DidacticStage::MPIBuffers,DidacticStage::GenerationResult};
    return {DidacticStage::LocalWork,DidacticStage::GenerationResult};
}
void DidacticController::nextCapture() {
    if(busy_ || !capture_ || phase_!=Phase::Ready) return;
    auto request=configuration_;request.reset=false;request.selection.expectedGeneration=capture_->after.generation();
    phase_=Phase::Preparing;enqueue({std::move(request),false});
}
void DidacticController::nextStage() {
    if(phase_!=Phase::Ready || !capture_) return;
    if(stage_+1<stages().size()) {++stage_;stageElapsed_=0;}
    else {stageElapsed_=stageDuration_;nextCapture();}
}
void DidacticController::tick(std::chrono::nanoseconds elapsed) {
    // No wall time while capturing is charged to a freshly delivered animation.
    const bool wasPreparing=isPreparing();poll();
    if(wasPreparing || !playing_ || phase_!=Phase::Ready || elapsed.count()<=0) return;
    const long double increment=static_cast<long double>(elapsed.count())*speed_;
    if(increment>=stageDuration_-stageElapsed_) nextStage();
    else stageElapsed_+=static_cast<std::uint64_t>(increment);
    // Discard overshoot: one update can never skip the intervening stages or
    // accumulate capture requests after a delayed UI tick.
}
void DidacticController::stepStage() {poll();if(!playing_ && !busy_) nextStage();}
void DidacticController::advanceGeneration() {poll();if(!playing_ && !busy_) nextCapture();}
void DidacticController::resetToInitial() {
    if(!initial_ || phase_==Phase::Closing || phase_==Phase::Closed) return;
    configuration_.epoch=++epoch_;configuration_.initial=initial_;configuration_.selection.expectedGeneration=0;configuration_.reset=true;
    capture_.reset();stage_=0;stageElapsed_=0;error_.clear();phase_=Phase::Preparing;
    enqueue({configuration_,false});
}
void DidacticController::close() {
    if(phase_==Phase::Closing || phase_==Phase::Closed) return;
    configuration_.epoch=++epoch_;playing_=false;capture_.reset();stage_=0;stageElapsed_=0;phase_=Phase::Closing;
    enqueue({configuration_,true});
}
const Game* DidacticController::preStep() const {return capture_?&capture_->before:nullptr;}
const Game* DidacticController::postStep() const {return capture_?&capture_->after:nullptr;}
const core::StepTrace* DidacticController::stepTrace() const {return capture_ && capture_->localTrace?&*capture_->localTrace:nullptr;}
DidacticFrame DidacticController::snapshot() const {
    DidacticFrame f;f.phase=phase_;f.is_preparing=isPreparing();f.playing=playing_;f.closing=phase_==Phase::Closing;f.closed=closed();
    f.owns_mpi=mpiActive_;f.cleanup_confirmed=cleanupConfirmed_;f.error_message=error_;f.speed=speed_;
    f.engine_name=configuration_.engine==Engine::MPI?"MPI":configuration_.engine==Engine::OpenMP?"OpenMP":"Secuencial";
    f.requested_threads=configuration_.engine==Engine::MPI?0:configuration_.threads;f.logical_processors=std::max(1,omp_get_num_procs());
    f.requested_processes=configuration_.engine==Engine::MPI?configuration_.mpiOptions.processes:0;
    f.stages=stages();f.stage_index=stage_;f.stage_duration_ns=stageDuration_;f.stage_elapsed_ns=stageElapsed_;
    f.total_playback_ns=stageDuration_*f.stages.size();f.current_playback_ns=stage_*stageDuration_+stageElapsed_;
    if(!capture_) return f;
    const auto& before=capture_->before;const auto& after=capture_->after;
    f.has_data=true;f.current_generation=before.generation();f.next_generation=after.generation();
    f.captured_generation=after.generation();f.width=before.width();f.height=before.height();f.current_alive=before.aliveCount();f.next_alive=after.aliveCount();
    const auto current=f.stages[stage_];
    f.fully_revealed=current==DidacticStage::GenerationResult || current==DidacticStage::MPIBuffers;
    f.viewed_generation=current==DidacticStage::GenerationResult?after.generation():before.generation();
    f.revealed.assign(f.width*f.height,0);f.worker_id.assign(f.revealed.size(),-1);
    f.current_alive_map.reserve(f.revealed.size());f.next_alive_map.reserve(f.revealed.size());
    for(std::size_t r=0;r<f.height;++r) for(std::size_t c=0;c<f.width;++c) {
        f.current_alive_map.push_back(before.isAlive(r,c));f.next_alive_map.push_back(after.isAlive(r,c));
    }
    const double progress=f.fully_revealed?1.0:current==DidacticStage::LocalWork?static_cast<double>(stageElapsed_)/stageDuration_:0.0;
    auto reveal=[&](std::size_t first,std::size_t count,int owner) {
        const auto rows=static_cast<std::size_t>(progress*count);
        for(std::size_t r=first;r<first+rows;++r) for(std::size_t c=0;c<f.width;++c) {
            const auto index=r*f.width+c;f.revealed[index]=1;f.worker_id[index]=static_cast<std::int8_t>(owner);
        }
    };
    if(capture_->localTrace) {
        f.effective_threads=capture_->localTrace->effective_threads;
        for(std::size_t i=0;i<capture_->localTrace->workers.size();++i) {
            const auto& w=capture_->localTrace->workers[i];
            f.workers.push_back({w.thread_id,static_cast<int>(w.first_row),static_cast<int>(w.end_row),w.cells_processed,w.loop_duration,progress,
                static_cast<int>(i),configuration_.engine==Engine::Sequential});
            reveal(w.first_row-1,w.end_row-w.first_row,static_cast<int>(i));
        }
    } else if(capture_->mpiTrace) {
        f.mpi_trace=capture_->mpiTrace;f.observed_processes=static_cast<int>(f.mpi_trace->ranks.size());
        for(const auto& rank:f.mpi_trace->ranks) reveal(rank.partition.firstRow,rank.partition.rowCount,rank.partition.rank);
    }
    return f;
}
}
