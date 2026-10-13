#include "app/comparison_view.hpp"
#include <ftxui/screen/screen.hpp>
#include <iostream>
#include <limits>

using namespace conway::app;
ExperimentResult measured(double sequential, double parallel, bool variable = false, bool equivalent = true) {
    ExperimentResult r{};
    r.config = {3,1,2,4};
    r.input = {7,3,0,42,{}};
    r.completedWarmupRuns = 2;
    for (int i = 0; i < 2; ++i) {
        r.samples.push_back({{conway::Engine::Sequential,i,sequential,3,{},1,1,equivalent},i,0,static_cast<std::size_t>(3+i*2)});
        r.samples.push_back({{conway::Engine::OpenMP,i,parallel,3,4,2,variable && i ? 3 : 2,equivalent},i,1,static_cast<std::size_t>(4+i*2)});
    }
    r.summary = summarizeExperimentSamples(r.samples);
    return r;
}
std::string rendered(const ExperimentResult& r, ComparisonTab tab, int width = 120) {
    auto screen = ftxui::Screen::Create(ftxui::Dimensions{width,100});
    ftxui::Render(screen, ftxui::vbox(comparisonRows(r,"real-input.txt",tab,width,
        ftxui::Color::RGB(83,191,208),ftxui::Color::RGB(98,232,137))));
    std::string text;
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < width; ++x) text += screen.PixelAt(x,y).character;
        text += '\n';
    }
    return text;
}
int main() {
    int failures = 0;
    auto check = [&](bool value, const char* label) { if (!value) { ++failures; std::cerr << "FAIL: " << label << '\n'; } };
    auto faster = measured(2,1);
    auto bars = comparisonBars(faster.summary,40);
    check(bars.available && bars.maximumMs == 2 && bars.cells == std::array<int,2>{40,20}, "shared linear scale");
    auto text = rendered(faster,ComparisonTab::Summary);
    check(text.find("OpenMP obtuvo menor") != text.npos && text.find("2.00×") != text.npos
        && text.find("100.00 %") != text.npos && text.find("Referencia: 1.00×") != text.npos, "backend metrics and percent, actual effective threads");
    check(text.find("solicitados: 4") != text.npos && text.find("Efectivos: 2 (observados)") != text.npos, "requested and observed distinguished");
    auto slower = measured(1,2);
    text = rendered(slower,ComparisonTab::Summary,80);
    check(text.find("Secuencial obtuvo menor") != text.npos && text.find("0.50×") != text.npos, "slower OpenMP is not an error");
    auto equal = measured(3,3);
    check(comparisonBars(equal.summary,41).cells == std::array<int,2>{41,41}, "equal bars");
    check(rendered(equal,ComparisonTab::Summary).find("tiempos medianos iguales") != text.npos, "equal time interpretation");
    auto zero = measured(0,0);
    text = rendered(zero,ComparisonTab::Summary);
    check(!comparisonBars(zero.summary,40).available && text.find("Speedup: N/D") != text.npos
        && text.find("resolución temporal insuficiente") != text.npos, "unavailable speedup and reason");
    auto variable = measured(2,1,true);
    text = rendered(variable,ComparisonTab::Summary);
    check(text.find("Efectivos: 2–3") != text.npos && text.find("equipo OpenMP varió") != text.npos
        && text.find("Eficiencia: N/D") != text.npos, "varying observed teams disable efficiency");
    auto unobserved = faster;
    unobserved.summary.minEffectiveThreads.reset();
    unobserved.summary.maxEffectiveThreads.reset();
    unobserved.summary.efficiency.reset();
    text = rendered(unobserved,ComparisonTab::Summary);
    check(text.find("Eficiencia: N/D") != text.npos && text.find("sin hilos efectivos observados") != text.npos,
          "missing efficiency reports missing observations, never requested-thread substitution");
    auto noSamples = faster;
    noSamples.samples.clear();
    noSamples.summary = summarizeExperimentSamples(noSamples.samples);
    check(!comparisonBars(noSamples.summary,40).available
        && rendered(noSamples,ComparisonTab::Samples).find("No hay muestras medidas") != text.npos,
          "empty observations explicitly unavailable");
    auto invalid = measured(2,1,false,false);
    text = rendered(invalid,ComparisonTab::Summary);
    check(!comparisonBars(invalid.summary,40).available && text.find("estados finales no equivalentes") != text.npos
        && text.find("Equivalencia: NO") != text.npos && text.find("Speedup: N/D") != text.npos
        && text.find("OpenMP obtuvo menor") == text.npos, "non-equivalence keeps evidence without performance conclusion");
    auto tiny = measured(1e-300,1e300);
    bars = comparisonBars(tiny.summary,40);
    check(bars.available && bars.cells[0] == 0 && bars.belowCell[0] && bars.cells[1] == 40, "tiny positive time flagged below-cell, not erased");
    text = rendered(tiny,ComparisonTab::Summary);
    check(text.find("1e-300 ms") != text.npos && text.find("marcador de presencia") != text.npos, "scientific notation and marker explanation");
    auto huge = measured(std::numeric_limits<double>::max(),std::numeric_limits<double>::max()/2);
    check(comparisonBars(huge.summary,80).cells == std::array<int,2>{80,40}, "large finite values do not overflow geometry");
    auto hugeMetric = measured(1e300,1);
    text = rendered(hugeMetric,ComparisonTab::Summary);
    check(text.find("1.00e+300×") != text.npos && text.find("5.00e+301 %") != text.npos,
          "large backend metrics stay compact and finite in percentage presentation");
    check(!comparisonBars(faster.summary,0).available, "empty width");
    auto malformed = faster;
    malformed.summary.sequentialStatistics->medianMs = std::numeric_limits<double>::infinity();
    check(!comparisonBars(malformed.summary,40).available, "non-finite medians cannot draw bars");
    auto precise = measured(0.12345678901234566,0.24691357802469132);
    const auto originalSamples = precise.samples;
    text = rendered(precise,ComparisonTab::Samples);
    check(text.find("0.12345678901234566") != text.npos && text.find("0.24691357802469133") != text.npos,
          "original samples rendered with round-trip precision");
    check(text.find("Desviación estándar poblacional") != text.npos && text.find("Media:") != text.npos
        && text.find("Mínimo:") != text.npos && text.find("Máximo:") != text.npos, "backend descriptive statistics");
    check(text.find("efectivos observados: 2–2") != text.npos && text.find("equivalencia SI") != text.npos, "per-sample observations");
    check(text.find("Secuencial · repetición 0") < text.find("OpenMP · repetición 0")
        && text.find("OpenMP · repetición 0") < text.find("Secuencial · repetición 1"), "acquisition order preserved");
    check(precise.samples[0].measurement.elapsedMs == originalSamples[0].measurement.elapsedMs, "render leaves original data unchanged");
    text = rendered(faster,ComparisonTab::Method);
    check(text.find("construcción de Game") != text.npos && text.find("calentamientos") != text.npos
        && text.find("divisor N") != text.npos && text.find("Speedup =") != text.npos, "method explains interval, exclusions and formulas");
    std::cout << (failures ? "FAIL" : "PASS") << ": comparison geometry and presentation\n";
    return failures ? 1 : 0;
}
