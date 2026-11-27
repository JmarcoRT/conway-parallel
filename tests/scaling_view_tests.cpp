#include "app/scaling_view.hpp"
#include <ftxui/screen/screen.hpp>
#include <algorithm>
#include <iostream>
#include <limits>
using namespace conway::app;
// Synthetic visual fixtures only: no performance evidence.
ScalingResult fixture(std::vector<std::int64_t> requests={8,1,4,2},int repetitions=2) {
    ScalingResult r{}; r.config.requestedThreads=requests; r.config.repetitions=repetitions;
    r.config.warmups=0; r.config.generations=3; r.input={7,3,0,42,{}};
    for (int round=0;round<repetitions;++round) {
        const auto base=r.samples.size(), count=requests.size()+1;
        const auto reference=base+(count-round%count)%count;
        for (std::size_t pos=0;pos<count;++pos) {
            auto index=(pos+round)%count;
            int threads=index ? static_cast<int>(requests[index-1]) : 1;
            r.samples.push_back({{{index ? conway::Engine::OpenMP : conway::Engine::Sequential,round,index ? 8.0/threads : 8.0,3,index ? std::optional<int>{threads} : std::nullopt,threads,threads,true},round,static_cast<int>(pos),r.samples.size()+1},index,reference});
        }
    }
    r.summary=summarizeScalingSamples(r.samples,r.config); return r;
}
std::string render(const ScalingResult& r,ScalingTab tab,int width=120,bool details=false,bool methodDetails=false) {
    const int height=(details || methodDetails)?1600:500;
    auto screen=ftxui::Screen::Create(ftxui::Dimensions{width,height});
    ftxui::Render(screen,ftxui::vbox(scalingRows(r,"synthetic.txt",tab,width,ftxui::Color::RGB(83,191,208),ftxui::Color::RGB(98,232,137),details,methodDetails)));
    std::string out;
    for (int y=0;y<height;++y) { for(int x=0;x<width;++x) out+=screen.PixelAt(x,y).character; out+='\n'; }
    return out;
}
int main() {
    int failures=0;
    auto check=[&](bool v,const char* name) { if (!v) {++failures;std::cerr<<"FAIL: "<<name<<'\n';} };
    auto r=fixture(); const auto original=r.samples;
    auto p=scalingPlotPoints(r);
    check(p.size()==4 && p[0].requestedThreads==1 && p[1].requestedThreads==2 && p[2].requestedThreads==4 && p[3].requestedThreads==8,"sorted 1 2 4 8 real points");
    check(p[0].speedup==1 && p[3].speedup==8,"backend speedups unchanged");
    for (int width : {80,120}) {
        auto text=render(r,ScalingTab::Summary,width);
        for (const char* heading : {"ENTRADA","RESULTADOS OPENMP","ACELERACIÓN SEGÚN LOS HILOS","Menor tiempo observado"}) check(text.find(heading)!=text.npos,"summary blocks both widths");
        check(text.find("Referencia secuencial   8.000 ms")!=text.npos,"shared baseline once");
        check(text.find("Hilos solicitados")!=text.npos && text.find("Eficiencia")!=text.npos && text.find("1×")!=text.npos,"table units and baseline");
        check(text.find("●")!=text.npos && text.find("escala proporcional")!=text.npos,"measured points and proportional axis");
        text=render(r,ScalingTab::Samples,width);
        check(text.find("Secuencial (referencia)")!=text.npos && text.find("Desviación")!=text.npos,"shared statistics and population deviation");
        check(text.find("MEDICIONES INDIVIDUALES")!=text.npos && text.find("Identificador original")==text.npos,"compact samples hide original identifiers");
        text=render(r,ScalingTab::Samples,width,true);
        check(text.find("Identificador original: #1")<text.find("Identificador original: #2") && text.find("Identificador original: #2")<text.find("Identificador original: #6"),"details preserve rotated acquisition order");
        text=render(r,ScalingTab::Method,width);
        check(text.find("CÓMO SE REALIZÓ EL ESTUDIO")!=text.npos && text.find("QUÉ SIGNIFICAN LOS RESULTADOS")!=text.npos && text.find("CÓMO LEER LA GRÁFICA")!=text.npos,"method sections");
        check(text.find("S(p)=p")==text.npos,"technical formulas hidden by default");
        text=render(r,ScalingTab::Method,width,false,true);
        check(text.find("teórica")!=text.npos && text.find("no se dibuja")!=text.npos,"ideal explicitly theoretical and omitted");
    }
    check(r.samples[1].execution.measurement.elapsedMs==original[1].execution.measurement.elapsedMs && r.samples[1].configurationIndex==original[1].configurationIndex,"render does not mutate data");
    auto custom=fixture({7,3,11}); p=scalingPlotPoints(custom);
    check(p.size()==3 && p.front().requestedThreads==3 && p.back().requestedThreads==11,"custom list sorted only for presentation");
    r.summary.points[0].summary.equivalent=false;
    r.summary.points[1].summary.speedup.reset();
    r.summary.points[2].summary.validForComparison=false;
    check(scalingPlotPoints(r).size()==1,"invalid nonequivalent unavailable points omitted not zero");
    check(render(r,ScalingTab::Summary).find("N/D")!=std::string::npos,"unavailable explicit");
    r=fixture(); auto& limited=r.summary.points[0].summary;
    limited.minEffectiveThreads=2; limited.maxEffectiveThreads=2;
    auto& variable=r.summary.points[1].summary;
    variable.minEffectiveThreads=1;variable.maxEffectiveThreads=2;variable.effectiveThreadCountStable=false;variable.efficiency.reset();
    auto text=render(r,ScalingTab::Summary);
    check(text.find("se observaron 2 hilos")!=text.npos && text.find("equipo variable")!=text.npos && text.find("eficiencia N/D")!=text.npos,"limited variable teams explicit");
    r.summary.points[2].summary.speedup=1e-300; r.summary.points[3].summary.speedup=1e300;
    text=render(r,ScalingTab::Summary);
    check(text.find("log1p(S)")!=text.npos && text.find("1.000e-300")!=text.npos && text.find("1.000e+300")!=text.npos,"extreme finite range logarithmic plot and intact values");
    r=fixture();
    r.summary.points[0].summary.speedup=std::numeric_limits<double>::infinity();
    r.summary.points[1].summary.speedup=std::numeric_limits<double>::quiet_NaN();
    r.summary.points[2].summary.speedup=0;
    r.summary.points[3].summary.speedup=-1;
    check(scalingPlotPoints(r).empty(),"nonfinite zero and negative speedups not plotted");
    check(render(r,ScalingTab::Summary).find("Gráfico N/D")!=text.npos,"all invalid graph unavailable");
    r=fixture(); r.summary.points[0].summary.openMPStatistics->medianMs=16;
    check(render(r,ScalingTab::Summary).find("Menor tiempo observado: 4 hilos")!=text.npos,"conclusion uses measured median without inferring a trend");
    // Explicit oracles: requested threads are deliberately not in sorted order.
    // A small speedup cannot make a slower median the winner.
    auto point=[](ScalingResult& value,int requested)->ExperimentSummary& {
        return std::find_if(value.summary.points.begin(),value.summary.points.end(),
            [&](const auto& p){return p.requestedThreads==requested;})->summary;
    };
    r=fixture();
    point(r,8).openMPStatistics->medianMs=16;
    check(render(r,ScalingTab::Summary).find("Menor tiempo observado: 4 hilos")!=text.npos,
          "winner follows minimum median rather than largest request or stale speedup");
    r=fixture();
    point(r,1).openMPStatistics->medianMs=0.5;
    point(r,2).openMPStatistics->medianMs=0.5;
    check(render(r,ScalingTab::Summary).find("Menor tiempo observado: 1 hilos")!=text.npos,
          "equal medians choose first configuration sorted by requested threads");
    point(r,1).equivalent=false;
    check(render(r,ScalingTab::Summary).find("Menor tiempo observado: 2 hilos")!=text.npos,
          "nonequivalent fastest candidate cannot win");
    point(r,2).validForComparison=false;
    point(r,8).openMPStatistics->medianMs=std::numeric_limits<double>::quiet_NaN();
    check(render(r,ScalingTab::Summary).find("Menor tiempo observado: 4 hilos")!=text.npos,
          "invalid and nonfinite medians cannot win");
    point(r,4).openMPStatistics->medianMs=-1;
    check(render(r,ScalingTab::Summary).find("Sin mediciones válidas para identificar")!=text.npos,
          "no winner when every candidate is invalid");
    // A narrow labelled layout has no table divider; a wide table must have one.
    for(int width:{80,180}) {
        auto screen=ftxui::Screen::Create(ftxui::Dimensions{width,500});
        const auto cyan=ftxui::Color::RGB(83,191,208);
        ftxui::Render(screen,ftxui::vbox(scalingRows(fixture(),"synthetic",ScalingTab::Summary,width,cyan,ftxui::Color::Green)));
        int separators=0,headings=0;
        for(int y=0;y<500;++y)for(int x=0;x<width;++x) {
            const auto& pixel=screen.PixelAt(x,y);
            if(pixel.character=="─" && pixel.dim)++separators;
            if(pixel.bold && pixel.foreground_color==cyan)++headings;
        }
        check(headings>=3 && (width==80 || separators>0),"responsive hierarchy and wide table divider");
    }
    r=fixture({1,2,4,8},20);
    text=render(r,ScalingTab::Samples,120,true);
    check(text.find("Identificador original: #100")!=text.npos && text.find("Ronda base cero: 19")!=text.npos,"many measurements retained");
    r.samples[1].execution.measurement.elapsedMs=0.039999999999999996;
    text=render(r,ScalingTab::Samples);
    check(text.find("0.040")!=text.npos && text.find("0.039999999999999996")==text.npos && r.samples[1].execution.measurement.elapsedMs==0.039999999999999996,"rounding only visual");
    r.samples.clear();r.summary={};
    check(scalingPlotPoints(r).empty() && render(r,ScalingTab::Summary).find("Gráfico N/D")!=text.npos && render(r,ScalingTab::Samples).find("No hay muestras medidas")!=text.npos,"empty explicit");
    std::cout<<(failures ? "FAIL" : "PASS")<<": synthetic scaling presentation\n";
    return failures ? 1 : 0;
}
