// Dependency injection only: an already-owned real MPI didactic controller.
// Its new rank views are deliberately absent; exercise application safety gates.
#include "../src/app/ftxui_app.cpp"
int main(int argc,char** argv) {
    if(argc!=3) return 2;
    using namespace conway::app;
    DifferentialOutputBuffer output(std::cout.rdbuf());auto* original=std::cout.rdbuf(&output);
    {
        auto screen=ftxui::ScreenInteractive::Fullscreen();
        auto didactic=std::make_unique<DidacticController>([&]{screen.PostEvent(ftxui::Event::Custom);});
        conway::mpi::ClientOptions options;options.launcher=argv[1];options.worker=argv[2];options.processes=1;
        didactic->startPreparation(InputData{3,2,{0,1,0,0,1,0}},conway::Engine::MPI,1,options);
        didactic->pause();
        auto app=ftxui::Make<InteractiveApp>(screen,output,ExperimentExecution::Backend{},std::move(didactic));
        screen.Loop(app);
    }
    std::cout.rdbuf(original);
}
