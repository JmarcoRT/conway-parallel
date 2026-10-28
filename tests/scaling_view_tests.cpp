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
std::string render(const ScalingResult& r,ScalingTab tab,int width=120) {
    auto screen=ftxui::Screen::Create(ftxui::Dimensions{width,500});
    ftxui::Render(screen,ftxui::vbox(scalingRows(r,"synthetic.txt",tab,width,ftxui::Color::RGB(83,191,208),ftxui::Color::RGB(98,232,137))));
    std::string out;
    for (int y=0;y<500;++y) { for(int x=0;x<width;++x) out+=screen.PixelAt(x,y).character; out+='\n'; }
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
        for (const char* heading : {"IDENTIFICACIÓN","REFERENCIA SECUENCIAL","TABLA DE ESCALABILIDAD","GRÁFICO DE SPEEDUP","INTERPRETACIÓN"}) check(text.find(heading)!=text.npos,"summary blocks both widths");
        check(text.find("Mediana secuencial compartida: 8.000 ms")!=text.npos,"shared baseline once");
        check(text.find("Solic.")!=text.npos && text.find("Efic. %")!=text.npos && text.find("1.00×")!=text.npos,"table units and baseline");
        check(text.find("*")!=text.npos && text.find("sin interpolación")!=text.npos,"real ASCII graph and categorical explanation");
        text=render(r,ScalingTab::Samples,width);
        check(text.find("Sec. compartida")!=text.npos && text.find("DE pobl.")!=text.npos,"shared statistics and population deviation");
        check(text.find("#1 · Ronda 0")<text.find("#2 · Ronda 0") && text.find("#2 · Ronda 0")<text.find("#6 · Ronda 1"),"samples preserve rotated acquisition order");
        text=render(r,ScalingTab::Method,width);
        check(text.find("QUÉ MEDIMOS")!=text.npos && text.find("CÓMO ESTUDIAMOS LA ESCALABILIDAD")!=text.npos && text.find("CÓMO INTERPRETAMOS")!=text.npos,"method sections");
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
    check(text.find("limitado/diferente")!=text.npos && text.find("equipo variable")!=text.npos && text.find("eficiencia N/D")!=text.npos,"limited variable teams explicit");
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
    check(render(r,ScalingTab::Summary).find("no mejoró de forma monótona")!=text.npos,"nonmonotonic measured medians described without causality");
    auto screen=ftxui::Screen::Create(ftxui::Dimensions{80,500});
    const auto cyan=ftxui::Color::RGB(83,191,208);
    ftxui::Render(screen,ftxui::vbox(scalingRows(r,"synthetic",ScalingTab::Summary,80,cyan,ftxui::Color::Green)));
    int separators=0, headings=0;
    for (int y=0;y<500;++y) {
        if (screen.PixelAt(0,y).character=="─" && screen.PixelAt(0,y).dim) ++separators;
        if (screen.PixelAt(0,y).bold && screen.PixelAt(0,y).foreground_color==cyan) ++headings;
    }
    check(separators>=4 && headings>=5,"dim dividers and identity cyan hierarchy");
    r=fixture({1,2,4,8},20);
    text=render(r,ScalingTab::Samples);
    check(text.find("#100 · Ronda 19")!=text.npos,"many measurements retained");
    r.samples[1].execution.measurement.elapsedMs=0.039999999999999996;
    text=render(r,ScalingTab::Samples);
    check(text.find("0.040 ms")!=text.npos && text.find("0.039999999999999996")==text.npos && r.samples[1].execution.measurement.elapsedMs==0.039999999999999996,"rounding only visual");
    r.samples.clear();r.summary={};
    check(scalingPlotPoints(r).empty() && render(r,ScalingTab::Summary).find("Gráfico N/D")!=text.npos && render(r,ScalingTab::Samples).find("No hay muestras medidas")!=text.npos,"empty explicit");
    std::cout<<(failures ? "FAIL" : "PASS")<<": synthetic scaling presentation\n";
    return failures ? 1 : 0;
}
