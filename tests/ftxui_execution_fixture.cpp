// Test-only gate around the real UI/backend: no timing assumptions or production env switch.
#include "../src/app/ftxui_app.cpp"
#include <fstream>

int main() {
    using namespace conway::app;
    const char* path = std::getenv("CONWAY_TEST_GATE");
    if (!path) return 2;
    DifferentialOutputBuffer output(std::cout.rdbuf());
    auto* original = std::cout.rdbuf(&output);
    {
        auto screen = ftxui::ScreenInteractive::Fullscreen();
        auto backend = [path](const ExecutionRequest& request, const ExperimentExecution::PhasePublisher& phase) {
            std::ifstream gate(path);
            char token;
            if (!gate.get(token)) throw std::runtime_error("Test gate closed");
            const auto& d = request.description;
            auto generated = conway::input::generateInput(*d.config.generated);
            auto context = captureExperimentContext({"fixture-" + std::to_string(d.id), {}, {}});
            phase(ExecutionPhase::Running);
            return StoredExperiment{d, std::move(context), runExperiment(generated, d.config.comparison)};
        };
        auto app = ftxui::Make<InteractiveApp>(screen, output, backend);
        screen.Loop(app);
    }
    std::cout.rdbuf(original);
}
