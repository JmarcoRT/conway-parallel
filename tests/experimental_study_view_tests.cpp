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
            check(text.find("procesos")!=text.npos && text.find("Observados")!=text.npos,"separate resources missing");
            check(text.find("N/D×")==text.npos && text.find("N/D %")==text.npos,"suffix attached to N/D");
            check(text.find("150.000 %")!=text.npos,"efficiency clamped or incorrectly formatted");
            check(text.find("referencia")!=text.npos,"sequential reference missing");
            text=rows(ComparisonTab::Samples);check(text.find("Auxiliares MPI")!=text.npos && text.find("Arranque")!=text.npos && text.find("Snapshot")!=text.npos,"MPI auxiliary times missing");
            text=rows(ComparisonTab::Method);check(text.find("MPI_Wtime")!=text.npos && text.find("sesión")!=text.npos,"timing scope missing");
            auto failed=result;failed.study->complete=false;failed.study->diagnostic="rank failure";for(auto& s:failed.study->summaries){s.valid=false;s.speedup.reset();s.efficiency.reset();}
            text=render(comparisonRows(failed,"blinker",ComparisonTab::Summary,width,ftxui::Color::Cyan,ftxui::Color::Green),width);
            check(text.find("ESTUDIO INCOMPLETO")!=text.npos && text.find("rank failure")!=text.npos && text.find("N/D")!=text.npos,"partial outcome hidden");
        }
        ScalingResult scaling;scaling.config.parallelEngine=Engine::MPI;scaling.study=study;
        auto points=scalingPlotPoints(scaling);check(points.size()==1 && points[0].requestedThreads==2,"MPI plot resources");
        auto text=render(scalingRows(scaling,"blinker",ScalingTab::Summary,80,ftxui::Color::Cyan,ftxui::Color::Green),80);
        check(text.find("Procesos →")!=text.npos && text.find("X: procesos")!=text.npos,"MPI axis mislabeled");
        result.study->configurations.erase(result.study->configurations.begin());result.study->summaries.clear();result.study->samples.clear();result.study->sessions.clear();
        text=render(comparisonRows(result,"blinker",ComparisonTab::Summary,80,ftxui::Color::Cyan,ftxui::Color::Green),80);
        check(text.find("Sin participante Secuencial")!=text.npos,"missing baseline warning");
        std::cout<<"PASS study views: participants, resources, scope, auxiliary times, partial failures, MPI axes and narrow layouts\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
