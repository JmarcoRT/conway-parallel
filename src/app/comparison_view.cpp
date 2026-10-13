#include "app/comparison_view.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace conway::app {
namespace {
using namespace ftxui;
std::string number(double value, bool original = false) {
    std::ostringstream out;
    out << std::setprecision(original ? std::numeric_limits<double>::max_digits10 : 6) << value;
    return out.str();
}
std::string metric(long double value, const char* suffix) {
    std::ostringstream out;
    if ((value > 0 && value < 0.005L) || std::abs(value) >= 1e6L) out << std::scientific;
    else out << std::fixed;
    out << std::setprecision(2) << value << suffix;
    return out.str();
}
std::string effective(const ExperimentSummary& summary) {
    if (!summary.minEffectiveThreads || !summary.maxEffectiveThreads) return "N/D (sin observaciones)";
    return std::to_string(*summary.minEffectiveThreads) +
        (*summary.minEffectiveThreads == *summary.maxEffectiveThreads ? "" : "–" + std::to_string(*summary.maxEffectiveThreads)) +
        " (observados)";
}
bool comparable(const ExperimentSummary& summary) {
    return summary.equivalent && summary.validForComparison;
}
std::string missingSpeedup(const ExperimentSummary& s) {
    if (!s.equivalent) return "resultados no equivalentes";
    if (!s.validForComparison) return "observaciones inválidas o insuficientes";
    if (!s.sequentialStatistics || !s.openMPStatistics) return "faltan estadísticas medidas";
    if (s.sequentialStatistics->medianMs == 0 || s.openMPStatistics->medianMs == 0)
        return "mediana cero; resolución temporal insuficiente";
    return "no hay un cociente positivo representable en el resultado";
}
std::string missingEfficiency(const ExperimentSummary& s) {
    if (!comparable(s)) return missingSpeedup(s);
    if (!s.minEffectiveThreads || !s.maxEffectiveThreads) return "sin hilos efectivos observados";
    if (!s.effectiveThreadCountStable) return "el equipo OpenMP varió entre mediciones";
    if (!s.speedup) return missingSpeedup(s);
    return "métrica no disponible en el resultado";
}
} // namespace
ComparisonBars comparisonBars(const ExperimentSummary& s, int columns) {
    ComparisonBars bars;
    if (!comparable(s) || !s.sequentialStatistics || !s.openMPStatistics || columns <= 0) return bars;
    const std::array<double,2> times{s.sequentialStatistics->medianMs, s.openMPStatistics->medianMs};
    for (auto time : times) if (!std::isfinite(time) || time < 0) return bars;
    bars.maximumMs = std::max(times[0], times[1]);
    if (bars.maximumMs <= 0) return bars;
    bars.available = true;
    for (std::size_t i = 0; i < times.size(); ++i) {
        bars.cells[i] = std::clamp(static_cast<int>(std::floor((times[i] / bars.maximumMs) * columns)), 0, columns);
        bars.belowCell[i] = times[i] > 0 && bars.cells[i] == 0;
    }
    return bars;
}
std::vector<ftxui::Element> comparisonRows(const ExperimentResult& result,
    const std::string& patternName, ComparisonTab tab, int columns, Color accent, Color positive) {
    const auto& s = result.summary;
    std::vector<Element> rows;
    Elements tabs;
    const std::array<std::string,3> names{"Resumen", "Muestras", "Método"};
    for (std::size_t i = 0; i < names.size(); ++i) {
        auto label = text(" " + names[i] + " ");
        if (i == static_cast<std::size_t>(tab)) label = label | bgcolor(accent) | color(Color::RGB(25,32,38)) | bold;
        tabs.push_back(std::move(label));
    }
    rows.push_back(hbox(std::move(tabs)));
    rows.push_back(text("Comparación: Secuencial vs OpenMP") | color(accent) | bold);
    if (tab == ComparisonTab::Summary) {
        rows.push_back(paragraph("Entrada: " + (result.input.generated ? std::string("Tablero generado") : patternName) +
            " · " + std::to_string(result.input.width) + " × " + std::to_string(result.input.height) + " (ancho × alto)"));
        rows.push_back(paragraph("Generaciones: " + std::to_string(result.config.generations) +
            " · Calentamientos: " + std::to_string(result.config.warmups) + " · Repeticiones: " + std::to_string(result.config.repetitions)));
        rows.push_back(paragraph("Hilos solicitados: " + std::to_string(result.config.requestedThreads) + " · Efectivos: " + effective(s)));
        if (columns >= 100) rows.push_back(separator());
        rows.push_back(text("TIEMPOS MEDIANOS · Menor tiempo es mejor") | color(accent));
        const int width = std::max(1, columns - 38); // Labels, numeric times and borders have their own space.
        const auto bars = comparisonBars(s, width);
        const std::array<std::optional<BenchmarkStatistics>,2> stats{s.sequentialStatistics, s.openMPStatistics};
        for (std::size_t i = 0; i < stats.size(); ++i) {
            std::string bar;
            if (bars.available) {
                for (int cell = 0; cell < width; ++cell)
                    bar += cell < bars.cells[i] ? "█" : (cell == 0 && bars.belowCell[i] ? "▏" : " ");
            } else bar = "N/D";
            rows.push_back(hbox({text(i == 0 ? "Secuencial " : "OpenMP     "),
                text(bar) | color(accent) | size(WIDTH, EQUAL, width),
                text("  " + (stats[i] ? number(stats[i]->medianMs) + " ms" : "N/D (sin muestras válidas)"))}));
        }
        if (bars.available) {
            rows.push_back(paragraph("Escala lineal compartida: 0–" + number(bars.maximumMs) + " ms." +
                (bars.belowCell[0] || bars.belowCell[1] ? " ▏: tiempo positivo menor que una celda; marcador de presencia." : "")) | dim);
        } else rows.push_back(paragraph("Barras N/D: " + (!comparable(s) ? missingSpeedup(s) : "medianas no utilizables en una escala positiva")));
        const bool speedup = comparable(s) && s.speedup && std::isfinite(*s.speedup) && *s.speedup >= 0;
        const bool efficiency = comparable(s) && s.effectiveThreadCountStable
            && s.minEffectiveThreads && s.maxEffectiveThreads && *s.minEffectiveThreads > 0
            && *s.minEffectiveThreads == *s.maxEffectiveThreads
            && s.efficiency && std::isfinite(*s.efficiency) && *s.efficiency >= 0;
        rows.push_back(text("Speedup: " + (speedup ? metric(*s.speedup, "×") : "N/D") +
            " · Eficiencia: " + (efficiency ? metric(static_cast<long double>(*s.efficiency) * 100, " %") : "N/D") + " · Referencia: 1.00×") | bold);
        rows.push_back(text(std::string("Equivalencia: ") + (s.equivalent ? "SI" : "NO") +
            " · Validez: " + (s.validForComparison ? "SI" : "NO")) | color(comparable(s) ? positive : Color::Yellow));
        if (!speedup) rows.push_back(paragraph("Speedup N/D: " + missingSpeedup(s)));
        if (!efficiency) rows.push_back(paragraph("Eficiencia N/D: " + missingEfficiency(s)));
        std::string interpretation;
        if (!s.equivalent) interpretation = "Advertencia: estados finales no equivalentes. No es posible interpretar el rendimiento; los tiempos se conservan como evidencia.";
        else if (!s.validForComparison) interpretation = "Advertencia: observaciones inválidas. No es posible interpretar el rendimiento.";
        else if (s.sequentialStatistics && s.openMPStatistics) {
            const auto sequential = s.sequentialStatistics->medianMs;
            const auto openMP = s.openMPStatistics->medianMs;
            if (std::isfinite(sequential) && std::isfinite(openMP) && sequential >= 0 && openMP >= 0)
                interpretation = sequential > openMP ? "OpenMP obtuvo menor tiempo mediano en este experimento."
                    : sequential < openMP ? "Secuencial obtuvo menor tiempo mediano en este experimento."
                                          : "Ambos motores presentaron tiempos medianos iguales.";
            else interpretation = "No es posible interpretar el rendimiento con medianas inválidas.";
        } else interpretation = "Las métricas disponibles no permiten interpretar la aceleración.";
        rows.push_back(paragraph(interpretation));
        rows.push_back(paragraph("La diferencia de medianas no demuestra significancia estadística ni su causa.") | dim);
    } else if (tab == ComparisonTab::Samples) {
        rows.push_back(text("ESTADÍSTICAS DESCRIPTIVAS · milisegundos") | color(accent));
        const std::array<std::optional<BenchmarkStatistics>,2> stats{s.sequentialStatistics, s.openMPStatistics};
        for (std::size_t i = 0; i < stats.size(); ++i) {
            rows.push_back(text(i == 0 ? "Secuencial" : "OpenMP") | bold);
            if (const auto& stat = stats[i]) {
                rows.push_back(paragraph("Mediana: " + number(stat->medianMs) + " · Media: " + number(stat->meanMs) +
                    " · Mínimo: " + number(stat->minimumMs) + " · Máximo: " + number(stat->maximumMs)));
                rows.push_back(text("Desviación estándar poblacional: " + number(stat->populationStddevMs) + " ms"));
            } else rows.push_back(text("N/D: sin estadísticas válidas; muestras originales debajo."));
        }
        rows.push_back(text("MUESTRAS MEDIDAS · orden de adquisición · ms originales") | color(accent));
        for (const auto& sample : result.samples) {
            const auto& m = sample.measurement;
            rows.push_back(paragraph(std::string(m.engine == Engine::Sequential ? "Secuencial" : "OpenMP") +
                " · repetición " + std::to_string(m.repetitionIndex) + " · lote " + number(m.elapsedMs, true) +
                " ms · generaciones " + std::to_string(m.generations) + " · equivalencia " + (m.equivalent ? "SI" : "NO")));
            if (m.engine == Engine::OpenMP) rows.push_back(paragraph("Hilos solicitados: " + (m.requestedThreads ? std::to_string(*m.requestedThreads) : "N/D") +
                " · efectivos observados: " + std::to_string(m.minEffectiveThreads) + "–" + std::to_string(m.maxEffectiveThreads)));
        }
        if (result.samples.empty()) rows.push_back(text("No hay muestras medidas."));
    } else {
        rows.push_back(text("MÉTODO Y FÓRMULAS") | color(accent));
        rows.push_back(paragraph("Se cronometra el lote completo de generaciones, sin dividir el tiempo por generación."));
        rows.push_back(paragraph("Se excluyen generación/copia de la entrada, construcción de Game, oráculo de equivalencia, calentamientos, captura de contexto, estadísticas y renderizado."));
        rows.push_back(paragraph("Las muestras contienen solo repeticiones medidas. El estado final de cada ejecución se contrasta con el oráculo secuencial."));
        rows.push_back(paragraph("Orden alternado entre motores; primera ronda: " + std::string(result.config.order == MeasurementOrder::AlternatingSequentialFirst ? "Secuencial, OpenMP." : "OpenMP, Secuencial.")));
        rows.push_back(paragraph("Speedup = mediana secuencial / mediana OpenMP. 1.00× indica igualdad de rendimiento."));
        rows.push_back(paragraph("Eficiencia (%) = speedup / hilos efectivos × 100. Se usan equipos observados, nunca hilos solicitados como sustituto."));
        rows.push_back(paragraph("Métricas N/D ante resultados inválidos o medianas cero. Eficiencia N/D si varía el equipo efectivo."));
        rows.push_back(paragraph("Desviación estándar poblacional: divisor N. Las estadísticas proceden del núcleo experimental; los tiempos originales no se redondean para calcularlas."));
        rows.push_back(paragraph("Equivalencia compara estados finales. Validez determina si las observaciones permiten interpretar las métricas; no garantiza significancia estadística."));
    }
    return rows;
}
} // namespace conway::app
