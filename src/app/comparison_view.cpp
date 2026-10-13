#include "app/comparison_view.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <set>
#include <ftxui/screen/string.hpp>
#include <sstream>

namespace conway::app {
namespace {
using namespace ftxui;
std::string number(double value) {
    if (!std::isfinite(value) || value < 0) return "N/D";
    std::ostringstream out;
    out << std::setprecision(6) << value;
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
// Natural column widths preserve every digit; narrow layouts become labelled records.
void table(std::vector<Element>& rows, const std::vector<std::string>& headers,
           const std::vector<std::vector<std::string>>& data, int columns) {
    std::vector<int> widths;
    for (const auto& header : headers) widths.push_back(string_width(header));
    for (const auto& record : data)
        for (std::size_t i = 0; i < record.size(); ++i)
            widths[i] = std::max(widths[i], string_width(record[i]));
    int total = static_cast<int>(headers.size() - 1) * 2;
    for (auto width : widths) total += width;
    if (total > columns) {
        for (const auto& record : data) {
            for (std::size_t i = 0; i < record.size(); ++i)
                rows.push_back(paragraph(headers[i] + ": " + record[i]));
            rows.push_back(separator() | dim);
        }
        return;
    }
    auto line = [&](const std::vector<std::string>& record, bool heading) {
        Elements cells;
        for (std::size_t i = 0; i < record.size(); ++i) {
            if (i) cells.push_back(text(" │") | dim);
            auto cell = text(record[i]) | size(WIDTH, EQUAL, widths[i]);
            cells.push_back(heading ? cell | bold : cell);
        }
        rows.push_back(hbox(std::move(cells)));
    };
    line(headers, true);
    rows.push_back(separator() | dim);
    for (const auto& record : data) line(record, false);
}
std::string team(const BenchmarkSample& sample) {
    return std::to_string(sample.minEffectiveThreads) + "–" + std::to_string(sample.maxEffectiveThreads);
}
std::string request(const BenchmarkSample& sample) {
    return sample.requestedThreads ? std::to_string(*sample.requestedThreads) : "N/D";
}
std::string execution(const ExperimentSample& sample) {
    return "#" + std::to_string(sample.executionIndex) + (sample.measurement.engine == Engine::Sequential ? " S" : " O");
}
} // namespace
bool comparisonSamplesArePaired(const std::vector<ExperimentSample>& samples) {
    if (samples.empty() || samples.size() % 2) return false;
    std::set<int> rounds, repetitions;
    for (std::size_t i = 0; i < samples.size(); i += 2) {
        const auto& first = samples[i];
        const auto& second = samples[i + 1];
        const auto a = first.measurement.engine, b = second.measurement.engine;
        if (!((a == Engine::Sequential && b == Engine::OpenMP) || (a == Engine::OpenMP && b == Engine::Sequential))
            || first.roundIndex < 0 || first.roundIndex != second.roundIndex
            || first.measurement.repetitionIndex < 0 || first.measurement.repetitionIndex != second.measurement.repetitionIndex
            || first.positionInRound != 0 || second.positionInRound != 1
            || first.executionIndex >= second.executionIndex || second.executionIndex - first.executionIndex != 1
            || (i && samples[i - 1].executionIndex >= first.executionIndex)
            || !rounds.insert(first.roundIndex).second || !repetitions.insert(first.measurement.repetitionIndex).second)
            return false;
    }
    return true;
}
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
    auto section = [&](const std::string& title, bool first = false) {
        if (!first) {
            if (columns >= 100) rows.push_back(text(""));
            rows.push_back(separator() | dim);
        }
        rows.push_back(text(title) | color(accent) | bold);
    };
    if (tab == ComparisonTab::Summary) {
        section("IDENTIFICACIÓN", true);
        rows.push_back(paragraph("Entrada: " + (result.input.generated ? std::string("Tablero generado") : patternName) +
            " · " + std::to_string(result.input.width) + " × " + std::to_string(result.input.height) + " (ancho × alto)"));
        rows.push_back(paragraph("Generaciones: " + std::to_string(result.config.generations) +
            " · Calentamientos: " + std::to_string(result.config.warmups) + " · Repeticiones: " + std::to_string(result.config.repetitions)));
        rows.push_back(paragraph("Hilos solicitados: " + std::to_string(result.config.requestedThreads) + " · Efectivos: " + effective(s)));
        section("TIEMPOS MEDIANOS · Menor tiempo es mejor");
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
        section("INDICADORES");
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
        section("INTERPRETACIÓN");
        rows.push_back(paragraph(interpretation));
        rows.push_back(paragraph("La diferencia de medianas no demuestra significancia estadística ni su causa.") | dim);
    } else if (tab == ComparisonTab::Samples) {
        section("ESTADÍSTICAS DESCRIPTIVAS · ms", true);
        const std::array<std::optional<BenchmarkStatistics>,2> stats{s.sequentialStatistics, s.openMPStatistics};
        const std::array<std::string,5> labels{"Mediana", "Media", "Mínimo", "Máximo", "Desviación estándar poblacional"};
        using Member = double BenchmarkStatistics::*;
        const std::array<Member,5> members{&BenchmarkStatistics::medianMs, &BenchmarkStatistics::meanMs,
            &BenchmarkStatistics::minimumMs, &BenchmarkStatistics::maximumMs, &BenchmarkStatistics::populationStddevMs};
        std::vector<std::vector<std::string>> statistics;
        for (std::size_t i = 0; i < labels.size(); ++i)
            statistics.push_back({labels[i], stats[0] ? number((*stats[0]).*members[i]) : "N/D",
                                             stats[1] ? number((*stats[1]).*members[i]) : "N/D"});
        table(rows, {"Métrica (ms)", "Secuencial", "OpenMP"}, statistics, columns);
        if (!stats[0] || !stats[1]) rows.push_back(paragraph("N/D: no hay estadísticas válidas para ese motor; las mediciones se conservan debajo.") | dim);
        section("MEDICIONES INDIVIDUALES · ms");
        rows.push_back(paragraph("Índices originales desde 0. Orden: índice de ejecución y motor (S: Secuencial, O: OpenMP). Hilos efectivos observados.") | dim);
        const bool paired = comparisonSamplesArePaired(result.samples);
        std::vector<std::vector<std::string>> measurements;
        if (paired) {
            rows.push_back(paragraph("Parejas verificadas por ronda, repetición y posiciones 0/1; se conserva el orden experimental.") | dim);
            for (std::size_t i = 0; i < result.samples.size(); i += 2) {
                const auto& first = result.samples[i];
                const auto& second = result.samples[i + 1];
                const auto& seq = first.measurement.engine == Engine::Sequential ? first.measurement : second.measurement;
                const auto& omp = first.measurement.engine == Engine::OpenMP ? first.measurement : second.measurement;
                measurements.push_back({std::to_string(first.roundIndex) + "/" + std::to_string(seq.repetitionIndex),
                    number(seq.elapsedMs), number(omp.elapsedMs), team(omp), request(omp),
                    std::string(seq.equivalent ? "SI" : "NO") + "/" + (omp.equivalent ? "SI" : "NO"),
                    execution(first) + " → " + execution(second)});
            }
            table(rows, {"Ronda/Rep.", "Sec. (ms)", "OMP (ms)", "Hilos ef.", "Solic.", "Eq S/O", "Orden"}, measurements, columns);
        } else {
            rows.push_back(paragraph("Sin asociación directa verificable: filas independientes en orden de adquisición.") | dim);
            for (const auto& sample : result.samples) {
                const auto& m = sample.measurement;
                measurements.push_back({std::to_string(sample.executionIndex),
                    m.engine == Engine::Sequential ? "Secuencial" : "OpenMP", std::to_string(sample.roundIndex),
                    std::to_string(m.repetitionIndex), std::to_string(sample.positionInRound), number(m.elapsedMs),
                    m.engine == Engine::OpenMP ? team(m) : "—", m.engine == Engine::OpenMP ? request(m) : "—",
                    m.equivalent ? "SI" : "NO"});
            }
            table(rows, {"Orden", "Motor", "Ronda", "Rep.", "Pos.", "Lote (ms)", "Hilos ef.", "Solic.", "Equiv."}, measurements, columns);
        }
        if (result.samples.empty()) rows.push_back(text("No hay muestras medidas."));
        rows.push_back(paragraph("Precisión abreviada solo en pantalla; los datos originales y el CSV conservan su precisión.") | dim);
    } else {
        section("QUÉ MEDIMOS", true);
        rows.push_back(paragraph("Se cronometra el lote completo de generaciones, sin dividir el tiempo por generación."));
        rows.push_back(paragraph("Incluye los pasos del motor y la observación del equipo efectivo dentro del bucle medido."));
        rows.push_back(paragraph("Se excluyen generación/copia de la entrada, construcción de Game, oráculo, calentamientos, captura de contexto, estadísticas descriptivas y renderizado."));
        section("CÓMO COMPARAMOS");
        rows.push_back(paragraph("Misma entrada inicial para ambos motores y cada ejecución. Los calentamientos son por motor; las muestras contienen solo repeticiones medidas."));
        rows.push_back(paragraph("Orden alternado entre motores; primera ronda: " + std::string(result.config.order == MeasurementOrder::AlternatingSequentialFirst ? "Secuencial, OpenMP." : "OpenMP, Secuencial.")));
        rows.push_back(paragraph("El estado final de cada ejecución medida se contrasta con el oráculo secuencial."));
        section("CÓMO INTERPRETAMOS");
        rows.push_back(paragraph("Speedup = mediana secuencial / mediana OpenMP. 1.00× indica igualdad de rendimiento."));
        rows.push_back(paragraph("Eficiencia (%) = speedup / hilos efectivos × 100. Se usan equipos observados, nunca hilos solicitados como sustituto."));
        rows.push_back(paragraph("Equivalencia: mismo estado final. Validez: observaciones aptas para interpretar métricas."));
        rows.push_back(paragraph("Métricas N/D ante observaciones inválidas, medianas cero o límites numéricos. Eficiencia N/D si varía el equipo efectivo."));
        rows.push_back(paragraph("Desviación estándar poblacional: divisor N. Estadísticas del núcleo experimental, sin redondear tiempos antes de calcularlas."));
        rows.push_back(paragraph("Una diferencia de medianas no demuestra significancia estadística ni identifica su causa."));
    }
    return rows;
}
} // namespace conway::app
