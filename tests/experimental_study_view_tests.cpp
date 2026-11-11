#include "app/comparison_view.hpp"
#include "app/scaling_view.hpp"
#include <ftxui/screen/screen.hpp>
#include <iostream>
using namespace conway;
using namespace conway::app;
std::string render(std::vector<ftxui::Element> rows,int width) {
    auto screen=ftxui::Screen::Create(ftxui::Dimensions{width,300});
    ftxui::Render(screen,ftxui::vbox(std::move(rows)));
    std::string out;for(int y=0;y<300;++y) {for(int x=0;x<width;++x) out+=screen.PixelAt(x,y).character;out+='\n';}return out;
}
int main() {
    try {
        auto check=[](bool c,const char* label){if(!c) throw std::runtime_error(label);};
        ExperimentResult result;result.input={5,3,3,0,{}};
        ExperimentalStudy study;study.complete=true;study.generations=3;study.warmups=1;study.repetitions=1;
        study.configurations={{Engine::Sequential,{},{}},{Engine::OpenMP,2,{}},{Engine::MPI,{},2}};
        for(std::size_t i=0;i<3;++i) {
            StudySummary summary;summary.configurationIndex=i;summary.sampleCount=1;
            summary.statistics=BenchmarkStatistics{10,10,10,10,0};summary.minimumResources=summary.maximumResources=i==0?1:2;
            summary.equivalent=summary.valid=true;if(i) {summary.speedup=3;summary.efficiency=1.5;}
            study.summaries.push_back(summary);
        }
        StudySample sample;sample.configurationIndex=2;sample.elapsedMs=10;sample.observedProcesses=2;
        sample.sessionIndex=0;sample.loadMs=2;sample.snapshotMs=3;sample.requestMs=12;sample.completedWarmups=1;sample.equivalent=sample.valid=true;
        study.samples.push_back(sample);
        StudySession session;session.configurationIndex=2;session.runtime="test-runtime";session.startupMs=4;session.closeMs=5;session.completedWarmups=1;session.cleanupConfirmed=true;
        study.sessions.push_back(session);result.study=study;
        for(int width:{40,80,120}) {
            auto rows=[&](ComparisonTab tab){return render(comparisonRows(result,"blinker",tab,width,ftxui::Color::Cyan,ftxui::Color::Green),width);};
            auto text=rows(ComparisonTab::Summary);
            check(text.find("Secuencial")!=text.npos && text.find("OpenMP")!=text.npos && text.find("MPI")!=text.npos,"participants missing");
            check(text.find("procesos")!=text.npos && text.find("Configuración")!=text.npos,"separate resources missing");
            check(text.find("N/D×")==text.npos && text.find("N/D %")==text.npos,"suffix attached to N/D");
            check(text.find("150.0 %")!=text.npos,"efficiency clamped or incorrectly formatted");
            check(text.find("Referencia")!=text.npos,"sequential reference missing");
            text=rows(ComparisonTab::Samples);check(text.find("ESTADÍSTICAS POR MOTOR")!=text.npos && text.find("MEDICIONES INDIVIDUALES")!=text.npos,"sample tables missing");
            check(text.find("Auxiliares MPI")==text.npos,"technical details shown by default");
            text=render(comparisonRows(result,"blinker",ComparisonTab::Samples,width,ftxui::Color::Cyan,ftxui::Color::Green,true),width);
            check(text.find("Auxiliares MPI")!=text.npos && text.find("Arranque")!=text.npos && text.find("Snapshot")!=text.npos,"MPI auxiliary times missing");
            text=rows(ComparisonTab::Method);check(text.find("QUÉ TIEMPO SE COMPARA")!=text.npos && text.find("CÓMO LEER LAS MÉTRICAS")!=text.npos,"method overview missing");
            check(text.find("MPI_Wtime")==text.npos,"technical method shown by default");
            text=render(comparisonRows(result,"blinker",ComparisonTab::Method,width,ftxui::Color::Cyan,ftxui::Color::Green,false,true),width);
            check(text.find("MPI_Wtime")!=text.npos && text.find("sesión")!=text.npos,"timing scope missing");
            auto failed=result;failed.study->complete=false;failed.study->diagnostic="rank failure";for(auto& s:failed.study->summaries){s.valid=false;s.speedup.reset();s.efficiency.reset();}
            text=render(comparisonRows(failed,"blinker",ComparisonTab::Summary,width,ftxui::Color::Cyan,ftxui::Color::Green),width);
            check(text.find("ESTUDIO INCOMPLETO")!=text.npos && text.find("rank failure")!=text.npos && text.find("N/D")!=text.npos,"partial outcome hidden");
        }
        ScalingResult scaling;scaling.config.parallelEngine=Engine::MPI;scaling.study=study;
        auto points=scalingPlotPoints(scaling);check(points.size()==1 && points[0].requestedThreads==2,"MPI plot resources");
        auto text=render(scalingRows(scaling,"blinker",ScalingTab::Summary,80,ftxui::Color::Cyan,ftxui::Color::Green),80);
        check(text.find("Procesos solicitados")!=text.npos && text.find("ACELERACIÓN SEGÚN LOS PROCESOS")!=text.npos,"MPI axis mislabeled");
        // Synthetic presentation coverage for every scaling engine and tab.
        for(auto engine:{Engine::OpenMP,Engine::MPI,Engine::CUDA}) {
            auto all=study;
            all.configurations.push_back({Engine::CUDA,{},{},core::kCudaBlock8x8});
            all.configurations.push_back({Engine::CUDA,{},{},core::kCudaBlock16x16});
            for(std::size_t i=3;i<5;++i) {
                auto summary=all.summaries.front();summary.configurationIndex=i;
                summary.speedup=3;summary.efficiency.reset();all.summaries.push_back(summary);
            }
            ScalingResult every;every.config.parallelEngine=engine;every.study=all;
            for(int width:{50,80,140}) {
                for(auto tab:{ScalingTab::Summary,ScalingTab::Samples,ScalingTab::Method}) {
                    const auto output=render(scalingRows(every,"blinker",tab,width,ftxui::Color::Cyan,ftxui::Color::Green),width);
                    check(output.find("GRÁFICO DE SPEEDUP")==output.npos,"obsolete scaling renderer still used");
                    if(tab==ScalingTab::Summary) check(output.find("Aceleración")!=output.npos && output.find("●")!=output.npos,"compact summary or measured plot missing");
                    if(tab==ScalingTab::Samples) {
                        check(output.find("ESTADÍSTICAS POR CONFIGURACIÓN")!=output.npos,"scaling statistics missing");
                        if(engine==Engine::CUDA) check(output.find("bloque 8×8")!=output.npos && output.find("bloque 16×16")!=output.npos,"CUDA block statistics indistinguishable");
                    }
                    if(tab==ScalingTab::Method) check(output.find("CÓMO LEER LA GRÁFICA")!=output.npos,"scaling graph method missing");
                }
            }
            if(engine==Engine::CUDA) {
                auto plot=scalingPlotPoints(every);
                check(plot.size()==2 && plot[0].label=="8x8" && plot[1].label=="16x16","CUDA categories missing");
            }
            every.study->complete=false;
            check(scalingPlotPoints(every).empty(),"incomplete study plotted as valid");
        }
        // Known medians for the shared study path, independent of plotting or
        // requested resource order. CUDA compares categories on the same GPU.
        for(auto engine:{Engine::OpenMP,Engine::MPI,Engine::CUDA}) {
            ScalingResult known;known.config.parallelEngine=engine;known.study=study;
            auto& k=*known.study;k.configurations.resize(1);k.summaries.resize(1);
            const int requests[]={8,1,4};
            const core::CudaBlockSize blocks[]={core::kCudaBlock8x8,core::kCudaBlock16x16,core::kCudaBlock32x8};
            const double medians[]={9,2,5};
            for(int i=0;i<3;++i) {
                ExecutionConfiguration c;c.engine=engine;
                if(engine==Engine::CUDA)c.cudaBlock=blocks[i];
                else if(engine==Engine::MPI)c.requestedProcesses=requests[i];
                else c.requestedThreads=requests[i];
                k.configurations.push_back(c);
                auto summary=study.summaries.front();summary.configurationIndex=i+1;
                summary.statistics->medianMs=medians[i];summary.speedup=10/medians[i];
                k.summaries.push_back(summary);
            }
            auto output=[&]{return render(scalingRows(known,"fixture",ScalingTab::Summary,120,ftxui::Color::Cyan,ftxui::Color::Green),120);};
            const auto winner=engine==Engine::CUDA?"bloque 16×16":engine==Engine::MPI?"1 proceso solicitado":"1 hilo solicitado";
            check(output().find(std::string("Menor tiempo observado: ")+winner)!=std::string::npos,"shared study winner must use minimum median");
            k.summaries[2].equivalent=false;
            const auto next=engine==Engine::CUDA?"bloque 32×8":engine==Engine::MPI?"4 procesos solicitados":"4 hilos solicitados";
            check(output().find(std::string("Menor tiempo observado: ")+next)!=std::string::npos,"shared study must exclude nonequivalent fastest candidate");
            k.complete=false;
            check(output().find("Sin mediciones válidas para identificar")!=std::string::npos,"incomplete study must have no winner");
        }
        result.study->configurations.erase(result.study->configurations.begin());result.study->summaries.clear();result.study->samples.clear();result.study->sessions.clear();
        text=render(comparisonRows(result,"blinker",ComparisonTab::Summary,80,ftxui::Color::Cyan,ftxui::Color::Green),80);
        check(text.find("Sin participante Secuencial")!=text.npos,"missing baseline warning");
        std::cout<<"PASS study views: participants, resources, scope, auxiliary times, partial failures, MPI axes and narrow layouts\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
