#include "app/app.hpp"

#include "app/benchmark.hpp"
#include "app/controls.hpp"
#include "app/ftxui_app.hpp"
#include "app/pattern_catalog.hpp"
#include "app/simulation.hpp"
#include "app/cuda_info.hpp"
#include "app/thread_config.hpp"
#include "core/game.hpp"
#include "core/engines/openmp.hpp"
#include "input/input.hpp"
#include "ui/board_view.hpp"
#include "ui/layout.hpp"
#include "ui/simulation_layout.hpp"

#include <algorithm>
#include <chrono>
#include <clocale>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <omp.h>
#include <poll.h>
#include <unistd.h>

namespace conway::app {

namespace {

constexpr const char* kClearScreen = "\033[H\033[2J";

// La seleccion se conserva aqui aunque se cargue otro patron o se salga
// de los modulos de visualizacion y benchmark.
struct AppState {
    std::optional<InputData> input;
    std::string path;
    conway::Engine engine{conway::Engine::Sequential};
    int requestedOpenMPThreads{defaultOpenMPThreadCount()};
    int mpiProcesses{1};
    core::CudaBlockSize cudaBlock{core::kDefaultCudaBlock};
    CudaUiInfo cudaInfo;
    std::unique_ptr<SimulationSession> normal;

};

struct PathRequest {
    bool eof{false};
    bool cancelled{false};
    std::string path;
};

enum class MenuAction {
    Up,
    Down,
    Select,
    Benchmark,
    Didactic,
    Normal,
    SelectEngine,     // abre el submenu de seleccion de motor
    ConfigureThreads,
    ChangeInput,
    Exit,
    None
};

// Acciones del submenu de motor (independientes del MenuAction principal
// para que un cambio en el menu principal no afecte a este flujo).
enum class EngineMenuAction {
    Up,
    Down,
    Select,
    SetSequential,    // [S] o Enter sobre la opcion Secuencial
    SetOpenMP,        // [O] o Enter sobre la opcion OpenMP
    Back,             // [X] o Enter sobre la opcion Volver
    None
};

struct RawModeGuard {
    RawModeGuard() {
        controls::beginTerminalSession();
    }

    ~RawModeGuard() {
        controls::endTerminalSession();
    }
};

ui::LayoutConfig currentLayoutConfig() {
    const auto size = controls::terminalSize();
    return {size.columns, size.rows, controls::supportsAnsiColors()};
}

void clearScreen() {
    if (controls::isInteractive()) {
        std::cout << kClearScreen;
    }
}

std::optional<controls::KeyPress> readKeyWithResize(
    controls::TerminalSize& previousSize,
    bool& resized
) {
    resized = false;
    while (true) {
        const auto reading = controls::readKeyFor(std::chrono::milliseconds(250));
        if (reading.ready) {
            return reading.key;
        }
        const auto currentSize = controls::terminalSize();
        if (currentSize != previousSize) {
            previousSize = currentSize;
            resized = true;
            return std::nullopt;
        }
    }
}

const std::vector<ui::MenuItem>& mainMenuItems() {
    static const std::vector<ui::MenuItem> items = {
        {"D", "Visualización didáctica"}, {"B", "Benchmark"}, {"C", "Catálogo de patrones"},
        {"M", "Seleccionar motor"}, {"R", "Configurar recursos"}, {"Q", "Salir"}
    };

    return items;
}

const std::vector<ui::MenuItem>& engineMenuItems() {
    static const std::vector<ui::MenuItem> items = {
        {"S", "Secuencial"},
        {"O", "OpenMP"},
        {"M", "MPI"},
        {"G", "CUDA"},
        {"X", "Volver"}
    };

    return items;
}

ui::ActiveInputInfo makeInputInfo(const AppState& state) {
    ui::ActiveInputInfo info;

    if (!state.input) {
        return info;
    }

    info.hasInput = true;
    info.filename = state.path;
    info.width = state.input->width;
    info.height = state.input->height;
    info.aliveCells = static_cast<std::size_t>(
        std::count(state.input->cells.begin(), state.input->cells.end(), std::uint8_t{1})
    );

    return info;
}

const char* engineName(conway::Engine e) {
    return e == conway::Engine::Sequential ? "Secuencial" : e == conway::Engine::OpenMP ? "OpenMP" : e==Engine::CUDA ? "CUDA" : "MPI";
}

bool applyNormalEngine(AppState& state,Engine engine,core::CudaBlockSize block,std::string& message) {
    if(state.normal)state.normal->pause();
    std::optional<CudaUiInfo> candidateInfo;
    if(engine==Engine::CUDA) {
        auto info=inspectCuda(state.input?state.input->width:1,state.input?state.input->height:1,block);
        if(!info.available) {message="Error: "+info.diagnostic;return false;}
        candidateInfo=std::move(info);
    }
    try {
        if(state.normal)state.normal->configure(engine,state.mpiProcesses,block);
        state.engine=engine;state.cudaBlock=block;
        if(candidateInfo)state.cudaInfo=std::move(*candidateInfo);
        return true;
    } catch(const std::exception& e) {message="Error al configurar: "+std::string(e.what());return false;}
}
bool configureCudaUI(AppState& state,std::string& message) {
    if(state.normal)state.normal->pause();
    while(true) {
        std::cout<<"BLOQUE CUDA · actual "<<cudaGeometryLabel(state.cudaBlock,state.cudaInfo)
                 <<"\n1 8x8 · 2 16x16 (predeterminado) · 3 32x8 · X cancelar\nNo indica simultaneidad ni ocupación.\n"<<message<<"\n"<<std::flush;
        const auto line=controls::readLine("");if(!line)return false;
        if(*line=="x"||*line=="X"||*line=="0")return true;
        if(*line!="1"&&*line!="2"&&*line!="3")continue;
        const core::CudaBlockSize blocks[]={core::kCudaBlock8x8,core::kCudaBlock16x16,core::kCudaBlock32x8};
        if(applyNormalEngine(state,Engine::CUDA,blocks[(*line)[0]-'1'],message)) {
            message="Bloque CUDA confirmado: "+cudaGeometryLabel(state.cudaBlock,state.cudaInfo);return true;
        }
    }
}

void renderMenu(const AppState& state, std::size_t selected, const std::string& message) {
    clearScreen();
    (void)selected;
    std::cout << "\nCONWAY PARALLEL\nPatrón confirmado: " << (state.path.empty()?"Sin patrón":state.path)
              << " · Motor: " << engineName(state.engine);
    if(state.engine==Engine::MPI)std::cout<<" · Procesos MPI: "<<state.mpiProcesses;
    else if(state.engine==Engine::OpenMP)std::cout<<" · Hilos OpenMP: "<<state.requestedOpenMPThreads;
    if(state.engine==Engine::CUDA)std::cout<<" · "<<cudaGeometryLabel(state.cudaBlock,state.cudaInfo)<<" · "<<state.cudaInfo.diagnostic;
    std::cout<<"\n";
    std::cout << "MODOS PRINCIPALES\n  V Simulación normal\n  D Visualización didáctica\n  B Benchmark\n"
              << "EXPLORAR PATRONES\n  C Catálogo de patrones\nCONFIGURACIÓN\n  M Seleccionar motor\n  R Configurar recursos\n"
              << "------------------\n  Q Salir\n" << message << "\n";
    std::cout << std::flush;
}

void renderThreadMenu(const AppState& state, std::size_t selected, const std::string& message) {
    clearScreen();
    const std::string subtitle =
        "Procesadores lógicos: " + std::to_string(availableLogicalProcessors()) +
        " | solicitados: " + std::to_string(state.requestedOpenMPThreads) +
        " | límite 1-" + std::to_string(kMaxOpenMPThreads) +
        " | no equivale a núcleos físicos";
    ui::renderLayout(
        "CONFIGURACIÓN OPENMP",
        subtitle,
        makeInputInfo(state),
        {
            {"1", "Usar 1 hilo"},
            {"2", "Usar 2 hilos"},
            {"4", "Usar 4 hilos"},
            {"8", "Usar 8 hilos"},
            {"C", "Número personalizado"},
            {"X", "Volver"}
        },
        selected,
        currentLayoutConfig(),
        message
    );
    std::cout << std::flush;
}

bool configureThreadsUI(AppState& state, std::string& message) {
    const bool interactive = controls::isInteractive();
    std::size_t selected = 0;
    const std::vector<int> presets{1, 2, 4, 8};
    auto lastSize = controls::terminalSize();

    while (true) {
        renderThreadMenu(state, selected, message);
        bool resized = false;
        std::optional<controls::KeyPress> key;
        if (interactive) {
            key = readKeyWithResize(lastSize, resized);
            if (resized) {
                continue;
            }
            if (!key) {
                return true;
            }
        }
        if (key) {
            if (key->type == controls::Key::Up) {
                selected = (selected + 5) % 6;
                continue;
            }
            if (key->type == controls::Key::Down) {
                selected = (selected + 1) % 6;
                continue;
            }
        }

        std::string choice;
        if (key && key->type == controls::Key::Enter) {
            choice = selected < presets.size()
                ? std::to_string(presets[selected])
                : selected == 4 ? "C" : "X";
        } else if (key && key->type == controls::Key::Char) {
            choice.assign(1, key->value);
        } else {
            const auto line = controls::readLine("");
            if (!line) {
                return true;
            }
            choice = *line;
        }

        if (choice == "x" || choice == "X" || choice == "0" || choice == "\x1b") {
            return true;
        }
        if (choice == "c" || choice == "C") {
            const auto value = controls::readLine(
                "Cantidad de hilos (1-" + std::to_string(kMaxOpenMPThreads) + "): ");
            if (!value) {
                return true;
            }
            const auto parsed = parseOpenMPThreadCount(*value);
            if (!parsed) {
                message = "Error: introduce un entero entre 1 y " +
                    std::to_string(kMaxOpenMPThreads) + ".";
                continue;
            }
            state.requestedOpenMPThreads = *parsed;
        } else {
            const auto parsed = parseOpenMPThreadCount(choice);
            if (!parsed ||
                std::find(presets.begin(), presets.end(), *parsed) == presets.end()) {
                message = "Error: selecciona 1, 2, 4, 8, C o X.";
                continue;
            }
            state.requestedOpenMPThreads = *parsed;
        }

        omp_set_num_threads(state.requestedOpenMPThreads);
        message = "Hilos OpenMP solicitados: " +
            std::to_string(state.requestedOpenMPThreads) + ".";
        return true;
    }
}

void renderEngineMenu(const AppState& state, std::size_t selected) {
    clearScreen();
    const std::string subtitle = std::string("Motor actual: ") + engineName(state.engine);
    ui::renderLayout(
        "MOTOR DE EJECUCION",
        subtitle.c_str(),
        makeInputInfo(state),
        engineMenuItems(),
        selected,
        currentLayoutConfig()
    );
    std::cout << "\n" << state.cudaInfo.diagnostic << std::flush;
}

PathRequest askPath() {
    PathRequest request;
    bool firstAttempt = true;

    while (true) {
        if (firstAttempt) {
            clearScreen();
            std::cout << std::flush;
            firstAttempt = false;
        }

        auto line = controls::readLine("Ruta del archivo (0 para cancelar): ");

        if (!line) {
            request.eof = true;
            return request;
        }

        if (line->empty()) {
            std::cout << "Escribe una ruta o '0' para cancelar.\n" << std::flush;
            continue;
        }

        if (*line == "0") {
            request.cancelled = true;
            return request;
        }

        request.path = *line;
        return request;
    }
}

bool browseCatalog(AppState&,std::string&);

bool ensureInput(AppState& state,std::string& message) {
    return state.input?true:browseCatalog(state,message);
}
bool changeInput(AppState& state,std::string& message) {return browseCatalog(state,message);}

// Submenu de seleccion de motor. Devuelve true si el usuario salio
// normalmente (Volver o EOF), false si no (no se usa en la practica
// porque el submenu siempre vuelve). El mensaje se rellena con la
// confirmacion del motor elegido cuando hay cambio.
bool selectEngineUI(AppState& state,std::string& message) {
    if(state.normal)state.normal->pause();
    auto info=inspectCuda(state.input?state.input->width:1,state.input?state.input->height:1,state.cudaBlock);
    if(!info.available && state.normal) {info.gpu=state.cudaInfo.gpu;info.geometry=state.cudaInfo.geometry;}
    state.cudaInfo=std::move(info);
    std::size_t selected=0;
    auto lastSize=controls::terminalSize();
    while(true) {
        renderEngineMenu(state,selected);
        if(!message.empty())std::cout<<"\n"<<message<<"\n"<<std::flush;
        std::string key;
        if(controls::isInteractive()) {
            bool resized=false;auto action=readKeyWithResize(lastSize,resized);if(resized)continue;if(!action)return true;
            if(action->type==controls::Key::Up) {selected=(selected+4)%5;continue;}
            if(action->type==controls::Key::Down) {selected=(selected+1)%5;continue;}
            if(action->type==controls::Key::Enter)key=selected==0?"S":selected==1?"O":selected==2?"M":selected==3?"G":"X";
            else key=std::string(1,action->value);
        } else {auto line=controls::readLine("");if(!line)return true;key=*line;}
        if(key=="x"||key=="X"||key=="0"||key==std::string(1,27))return true;
        Engine chosen;
        if(key=="s"||key=="S")chosen=Engine::Sequential;
        else if(key=="o"||key=="O")chosen=Engine::OpenMP;
        else if(key=="m"||key=="M")chosen=Engine::MPI;
        else if(key=="g"||key=="G")chosen=Engine::CUDA;
        else continue;
        if(applyNormalEngine(state,chosen,state.cudaBlock,message)) {
            message="Motor seleccionado: "+std::string(engineName(state.engine));return true;
        }
    }
}

MenuAction actionFromIndex(std::size_t index) {
    static constexpr MenuAction actions[]={MenuAction::Didactic,MenuAction::Benchmark,MenuAction::ChangeInput,
        MenuAction::SelectEngine,MenuAction::ConfigureThreads,MenuAction::Exit};
    return index<6?actions[index]:MenuAction::None;
}

void renderCatalogPlayback(
    const AppState& state,
    const SimulationSession& session,
    controls::TerminalSize terminal
) {
        const Game& game = session.game();
        ui::SimulationHeaderInfo header;
        header.mode = engineName(state.engine);
        header.filename = state.path;
        header.boardWidth = game.width();
        header.boardHeight = game.height();
        header.statusText = session.busy() ? "Operación pendiente" : session.isRunning() ? "En ejecución" : "Pausado";

        ui::ExecutionInfo execution;
        execution.generation = game.generation();
        execution.aliveCount = game.aliveCount();
        execution.stepProgress = "100%";
        execution.workersInfo = state.engine == Engine::OpenMP
            ? "OpenMP | disponibles (lógicos): " +
                std::to_string(availableLogicalProcessors()) +
                " | solicitados: " + std::to_string(state.requestedOpenMPThreads) +
                (game.generation() == 0 ? " | efectivos pendientes"
                    : " | efectivos observados: " +
                        std::to_string(core::last_openmp_team_size()))
            : state.engine == Engine::MPI ? "MPI | procesos: " + std::to_string(state.mpiProcesses) + " | U recuperar, Q abandonar, X menú"
            : state.engine==Engine::CUDA ? "CUDA | GPU: "+state.cudaInfo.gpu+" | "+cudaGeometryLabel(state.cudaBlock,state.cudaInfo)
            : "Motor: Secuencial";

        const std::size_t frameWidth = std::max<std::size_t>(1, terminal.columns);
        const std::size_t frameHeight = std::max<std::size_t>(1, terminal.rows);
        const bool sideBySide = frameWidth >= 86;
        const bool compact = frameWidth < 86 || frameHeight < 18;
        const std::size_t fixedRows = sideBySide ? 13 : 17;
        const bool renderSideBySide = sideBySide && !compact;
        const std::size_t availableBoardRows = std::max<std::size_t>(
            1, compact
                ? (frameHeight > 6 ? (frameHeight - 6) / 2 : 1)
                : (frameHeight > fixedRows
                    ? (sideBySide ? frameHeight - fixedRows : (frameHeight - fixedRows) / 2)
                    : 1));
        const std::size_t cellHeight = std::clamp(
            availableBoardRows / std::max<std::size_t>(1, game.height()),
            std::size_t{1}, std::size_t{3});
        const std::size_t rowsPerCell = std::max<std::size_t>(
            1, availableBoardRows / cellHeight);
        const std::size_t maxBoardRows = renderSideBySide ? 60 : 30;
        const std::size_t visibleRows = std::min(rowsPerCell, maxBoardRows);
        const std::size_t panelWidth = sideBySide
            ? (frameWidth - 6) / 2
            : (frameWidth > 4 ? frameWidth - 4 : 1);
        const std::size_t cellArea = panelWidth > 4 ? panelWidth - 4 : 1;
        const std::size_t cellWidth = std::clamp(
            cellArea / std::max<std::size_t>(1, game.width()), std::size_t{1}, std::size_t{4});
        const std::size_t visibleColumns = std::max<std::size_t>(
            1, (cellArea + 1) / (cellWidth + 1));
        std::size_t boardRows = std::min(game.height(), visibleRows);
        if ((game.height() > boardRows || game.width() > visibleColumns) &&
            boardRows * cellHeight >= availableBoardRows) {
            boardRows = std::max<std::size_t>(
                1, std::min(boardRows, (availableBoardRows - 1) / cellHeight));
        }
        const ui::BoardRenderOptions boardOptions{
            visibleColumns, boardRows, controls::supportsAnsiColors(), cellWidth, cellHeight};

        ui::BoardPanelData current{"CURRENT", ui::renderBoardLines(game, boardOptions)};
        ui::BoardPanelData next = session.preview()
            ? ui::BoardPanelData{"NEXT", ui::renderBoardLines(*session.preview(), boardOptions)}
            : ui::BoardPanelData{"Snapshot confirmado", {session.busy() ? "Operación pendiente" : "Sin vista previa especulativa", session.error()}};
        const ui::SimulationLayoutConfig layout{
            frameWidth,
            std::max(current.lines.size(), next.lines.size()),
            boardOptions.colorEnabled,
            frameHeight
        };
        ui::renderSimulationLayout(header, execution, current, next, layout);
}

bool browseCatalog(AppState& state,std::string& message) {
    const auto catalog=discoverPatterns("patterns");
    while(true) {
        std::cout<<"\nCATÁLOGO DE PATRONES · explorar no confirma\n";
        for(std::size_t i=0;i<catalog.entries.size();++i)std::cout<<i+1<<" "<<catalog.entries[i].name<<"\n";
        std::cout<<"M ruta manual · X volver sin cambiar\n"<<std::flush;
        const auto choice=controls::readLine("");if(!choice)return false;
        if(*choice=="x"||*choice=="X"||*choice=="0")return true;
        std::string path;
        if(*choice=="m"||*choice=="M") {auto request=askPath();if(request.eof)return false;if(request.cancelled)continue;path=request.path;}
        else {
            try {std::size_t end=0;auto index=std::stoul(*choice,&end);if(end==choice->size()&&index>0&&index<=catalog.entries.size())path=catalog.entries[index-1].path;}
            catch(...) {}
            if(path.empty())path=*choice;
        }
        try {
            const auto initial=loadInput(path);
            SimulationSession preview(initial,Engine::Sequential);
            AppState display;display.input=initial;display.path=path;display.engine=Engine::Sequential;
            while(true) {
                renderCatalogPlayback(display,preview,controls::terminalSize());
                std::cout<<"Vista previa: Secuencial · sin confirmar · generación "<<preview.game().generation()
                         <<"\nP o Espacio+Enter pausa/reproduce · N paso · R reinicia · U Usar patrón · X lista\n"<<std::flush;
                pollfd input{STDIN_FILENO,POLLIN,0};
                if(preview.isRunning() && ::poll(&input,1,150)==0) {preview.advanceIfRunning();continue;}
                const auto command=controls::readLine("");if(!command)return false;
                if(*command=="x"||*command=="X")break;
                if(*command=="u"||*command=="U") {state.normal.reset();state.input=initial;state.path=path;
                    if(state.engine==Engine::CUDA)state.cudaInfo=inspectCuda(initial.width,initial.height,state.cudaBlock);
                    message="Patrón confirmado: "+path+" · entrada inicial";
                    if(state.engine==Engine::CUDA && !state.cudaInfo.available)message+=" · "+state.cudaInfo.diagnostic;
                    return true;}
                if(*command=="r"||*command=="R")preview.reset();
                else if(*command=="n"||*command=="N") {if(!preview.isRunning())preview.step();}
                else if(command->empty()||*command=="p"||*command=="P")preview.toggleRunning();
            }
        } catch(const std::exception& error) {std::cout<<"Error al cargar: "<<error.what()<<" · se conserva el patrón confirmado\n";}
    }
}

bool runNormalUI(AppState& state,std::string& message) {
    if(!ensureInput(state,message))return false;
    if(!state.input)return true;
    if(state.engine==Engine::CUDA && !state.normal) {
        state.cudaInfo=inspectCuda(state.input->width,state.input->height,state.cudaBlock);
        if(!state.cudaInfo.available) {message="Error: "+state.cudaInfo.diagnostic;return true;}
    }
    if(!state.normal)state.normal=std::make_unique<SimulationSession>(*state.input,state.engine,
        defaultMPIOptions(state.mpiProcesses),std::function<void()>{},false,state.cudaBlock);
    auto& session=*state.normal;session.pause();
    while(true) {
        session.poll();renderCatalogPlayback(state,session,controls::terminalSize());
        std::cout<<"\nSIMULACIÓN NORMAL · Motor: "<<engineName(session.engine())
                 <<" · Generación "<<session.game().generation()<<" · Vivas "<<session.game().aliveCount()<<"\n";
        if(state.engine==Engine::CUDA)std::cout<<"GPU: "<<state.cudaInfo.gpu<<" · "<<cudaGeometryLabel(state.cudaBlock,state.cudaInfo)<<"\n";
        std::cout<<session.error()<<"\nN paso · P pausa/reproduce · R reinicia · M motor · B recursos · U recuperar · X menú\n"<<std::flush;
        pollfd input{STDIN_FILENO,POLLIN,0};
        if((session.isRunning()||session.busy()) && ::poll(&input,1,150)==0) {session.poll();session.advanceIfRunning();continue;}
        const auto command=controls::readLine("");if(!command)return false;
        if(*command=="x"||*command=="X") {session.pause();return true;}
        if(*command=="n"||*command=="N") {if(!session.isRunning())session.step();}
        if(*command=="p"||*command=="P"||command->empty())session.toggleRunning();
        if(*command=="r"||*command=="R")session.reset();
        if(*command=="u"||*command=="U")session.recover();
        if(*command=="m"||*command=="M") {if(!selectEngineUI(state,message))return false;}
        if(*command=="b"||*command=="B") {
            session.pause();
            if(state.engine==Engine::CUDA) {if(!configureCudaUI(state,message))return false;}
            else if(state.engine==Engine::OpenMP) {if(!configureThreadsUI(state,message))return false;}
            else if(state.engine==Engine::MPI) {
                const auto text=controls::readLine("Procesos MPI (1..1024): ");if(!text)return false;
                if(auto count=parseMPIProcesses(*text)) {session.configure(Engine::MPI,*count,state.cudaBlock);state.mpiProcesses=*count;}
            }
        }
    }
}

bool waitForModuleReturn(bool interactive) {
        if (interactive) {
            return controls::readKey().has_value();
        }
        return controls::readLine("").has_value();
}

bool runBenchmarkUI(AppState& state, std::string& message) {
        if (!ensureInput(state, message)) {
            return false;
        }
        if (!state.input) {
            return true;
        }

        constexpr int kGenerations = 20;
        constexpr int kRepetitions = 5;
        clearScreen();
        ui::renderScreen(
            "BENCHMARK EN CURSO",
            "Preparando comparación Secuencial / OpenMP",
            {"Calentamiento y " + std::to_string(kRepetitions) + " repeticiones por motor",
             "La interfaz no forma parte del tiempo medido"},
            "La operación termina automáticamente; espera unos instantes.",
            currentLayoutConfig());
        std::cout << std::flush;
        const BenchmarkResult result = runBenchmark(
            *state.input, kGenerations, kRepetitions, state.requestedOpenMPThreads);

        std::vector<std::string> report;
        report.push_back("Generaciones: " + std::to_string(result.generations) +
            "  |  Repeticiones: " + std::to_string(result.repetitions));
        report.push_back("Hilos lógicos disponibles: " +
            std::to_string(availableLogicalProcessors()) +
            "  |  OpenMP solicitados: " + std::to_string(result.requestedThreads));
        std::string effectiveThreads;
        if (result.effectiveThreadCountStable) {
            effectiveThreads = std::to_string(result.minEffectiveThreads);
        } else {
            effectiveThreads = std::to_string(result.minEffectiveThreads) + "–" +
                std::to_string(result.maxEffectiveThreads) + " (variaron entre regiones)";
        }
        report.push_back("Hilos OpenMP efectivos: " + effectiveThreads);
        std::ostringstream timings;
        timings << std::fixed << std::setprecision(3)
                << "Secuencial     " << result.sequentialMedianMs << " ms";
        report.push_back(timings.str());
        timings.str({});
        timings.clear();
        timings << std::fixed << std::setprecision(3)
                << "OpenMP         " << result.openMPMedianMs << " ms";
        report.push_back(timings.str());
        timings.str({});
        timings.clear();
        timings << std::fixed << std::setprecision(3)
                << "Speedup: " << result.speedup << "×";
        report.push_back(timings.str());
        if (result.efficiencyValid) {
            timings.str({});
            timings.clear();
            timings << std::fixed << std::setprecision(2)
                    << "Eficiencia: " << (result.efficiency * 100.0) << "%";
            report.push_back(timings.str());
        } else {
            report.push_back("Eficiencia: no disponible (hilos variables o resultados distintos)");
        }
        report.push_back("Estados equivalentes: " + std::string(result.equivalent ? "SI" : "NO"));
        if (!result.equivalent) {
            report.push_back("Interpretación: resultados distintos; no comparar rendimiento.");
        } else if (result.openMPMedianMs < result.sequentialMedianMs) {
            report.push_back("Interpretación: OpenMP fue más rápido en esta medición.");
        } else if (result.sequentialMedianMs < result.openMPMedianMs && result.speedup > 0.0) {
            timings.str({});
            timings.clear();
            timings << std::fixed << std::setprecision(2)
                    << "Interpretación: Secuencial fue " << (1.0 / result.speedup)
                    << "× más rápido.";
            report.push_back(timings.str());
        } else {
            report.push_back("Interpretación: tiempos iguales o resolución insuficiente.");
        }
        if (state.input->cells.size() < 256) {
            report.push_back("Nota: tablero pequeño; el overhead OpenMP puede dominar.");
        }
        clearScreen();
        ui::renderScreen(
            "BENCHMARK  ·  SECUENCIAL VS OPENMP",
            "Patrón: " + state.path + "  |  " + std::to_string(state.input->width) +
                " × " + std::to_string(state.input->height),
            report,
            "Presiona una tecla para volver al menú.",
            currentLayoutConfig());
        std::cout << std::flush;

        if (!waitForModuleReturn(controls::isInteractive())) {
            return false;
        }
        if (!result.equivalent) {
            message = "Error: los motores produjeron estados distintos.";
        }
        return true;
}

std::optional<MenuAction> pollMenuAction(bool,controls::TerminalSize&) {
    const auto line=controls::readLine("");if(!line)return {};
    if(*line=="b"||*line=="B"||*line=="1")return MenuAction::Benchmark;
    if(*line=="v"||*line=="V")return MenuAction::Normal;
    if(*line=="d"||*line=="D")return MenuAction::Didactic;
    if(*line=="c"||*line=="C")return MenuAction::ChangeInput;
    if(*line=="m"||*line=="M"||*line=="3")return MenuAction::SelectEngine;
    if(*line=="r"||*line=="R"||*line=="4")return MenuAction::ConfigureThreads;
    if(*line=="q"||*line=="Q"||*line=="0")return MenuAction::Exit;
    return MenuAction::None;
}

int runLoop() {
    RawModeGuard rawGuard;

    const bool interactive = controls::isInteractive();
    const std::size_t optionCount = mainMenuItems().size();

    AppState state;
    std::size_t selected = 0;
    std::string message;
    auto lastSize = controls::terminalSize();

    while (true) {
        renderMenu(state, selected, message);
        message.clear();

        auto action = pollMenuAction(interactive, lastSize);

        if (!action) {
            break;
        }

        if (*action == MenuAction::Select) {
            action = actionFromIndex(selected);
        }

        switch (*action) {
            case MenuAction::Up:
                selected = (selected + optionCount - 1) % optionCount;
                break;
            case MenuAction::Down:
                selected = (selected + 1) % optionCount;
                break;
            case MenuAction::Benchmark:
                if (!runBenchmarkUI(state, message)) {
                    return 0;
                }
                break;
            case MenuAction::Normal:
                if(!runNormalUI(state,message))return 0;break;
            case MenuAction::Didactic:
                message="Didáctico requiere una terminal interactiva con FTXUI.";break;
            case MenuAction::SelectEngine:
                if (!selectEngineUI(state, message)) return 0;
                break;
            case MenuAction::ConfigureThreads:
                if(state.normal)state.normal->pause();
                if(state.engine==Engine::CUDA) {if(!configureCudaUI(state,message))return 0;break;}
                if(state.engine==Engine::Sequential) {message="1 hilo de cálculo; sin recursos configurables.";break;}
                if(state.engine==Engine::MPI) {
                    auto value=controls::readLine("Procesos MPI (1..1024): ");if(!value)return 0;
                    auto count=parseMPIProcesses(*value);if(count) {if(state.normal)state.normal->configure(Engine::MPI,*count,state.cudaBlock);state.mpiProcesses=*count;message="Procesos MPI: "+std::to_string(*count);}
                    else message="P debe estar entre 1 y 1024.";
                    break;
                }
                if (!configureThreadsUI(state, message)) {
                    return 0;
                }
                break;
            case MenuAction::ChangeInput:
                if (!changeInput(state, message)) {
                    return 0;
                }
                break;
            case MenuAction::Exit:
                return 0;
            case MenuAction::Select:
            case MenuAction::None:
                break;
        }
    }

    return 0;
}

}

int run() {
    try {
        (void)std::setlocale(LC_CTYPE, "");
        if (controls::isInteractive() && controls::isOutputInteractive()) {
            return runFtxuiApp();
        }
        return runLoop();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}

}
