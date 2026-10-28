#include "app/experimental_study_view.hpp"
#include "app/scaling_view.hpp"
#include <ftxui/screen/string.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace conway::app {
namespace {
using namespace ftxui;
std::string number(long double value) {
    if (!std::isfinite(value) || value < 0) return "N/D";
    std::ostringstream out;
    out << (((value>0 && value<0.001) || value>=1e6) ? std::scientific : std::fixed)
        << std::setprecision(3) << value;
    return out.str();
}
std::string available(std::optional<double> value, long double factor = 1) {
    return value ? number(static_cast<long double>(*value) * factor) : "N/D";
}
std::string yes(bool value) { return value ? "SI" : "NO"; }
bool validSpeedup(const ExperimentSummary& s) {
    return s.equivalent && s.validForComparison && s.speedup && std::isfinite(*s.speedup) && *s.speedup > 0;
}
std::string threads(const ExperimentSummary& s) {
    if (!s.minEffectiveThreads || !s.maxEffectiveThreads) return "N/D";
    return std::to_string(*s.minEffectiveThreads) +
        (*s.minEffectiveThreads == *s.maxEffectiveThreads ? "" : "–" + std::to_string(*s.maxEffectiveThreads));
}
std::vector<const ScalingPoint*> sorted(const ScalingResult& result) {
    std::vector<const ScalingPoint*> points;
    for (const auto& p : result.summary.points) points.push_back(&p);
    std::stable_sort(points.begin(),points.end(),[](auto a,auto b) { return a->requestedThreads < b->requestedThreads; });
    return points;
}
void table(Elements& rows, const std::vector<std::string>& headers,
           const std::vector<std::vector<std::string>>& data, int columns) {
    std::vector<int> widths;
    for (const auto& h : headers) widths.push_back(string_width(h));
    for (const auto& record : data)
        for (std::size_t i=0;i<record.size();++i) widths[i]=std::max(widths[i],string_width(record[i]));
    int width = static_cast<int>(headers.size()-1)*2;
    for (auto w : widths) width += w;
    if (width > columns) {
        for (const auto& record : data) {
            for (std::size_t i=0;i<record.size();++i) rows.push_back(paragraph(headers[i]+": "+record[i]));
            rows.push_back(separator() | dim);
        }
        return;
    }
    auto line = [&](const auto& record,bool heading) {
        Elements cells;
        for (std::size_t i=0;i<record.size();++i) {
            if (i) cells.push_back(text(" │") | dim);
            auto cell=text(record[i]) | size(WIDTH,EQUAL,widths[i]);
            cells.push_back(heading ? cell | bold : cell);
        }
        rows.push_back(hbox(std::move(cells)));
    };
    line(headers,true); rows.push_back(separator() | dim);
    for (const auto& record : data) line(record,false);
}
void graph(Elements& rows,const ScalingResult& result,int columns,Color accent,bool compact=false) {
    const auto points=scalingPlotPoints(result);
    const std::string resource=result.config.parallelEngine==Engine::MPI?"procesos":"hilos";
    if (points.empty()) { rows.push_back(text("Gráfico N/D: no hay speedups válidos.")); return; }
    long double maximum=1, minimum=1;
    for (const auto& p : points) { maximum=std::max(maximum,static_cast<long double>(p.speedup)); minimum=std::min(minimum,static_cast<long double>(p.speedup)); }
    const bool logarithmic=maximum/minimum>1000;
    auto transform=[&](long double v) { return logarithmic ? std::log1p(v) : v; };
    const auto top=transform(maximum);
    constexpr int height=6;
    int slot=3;
    for (const auto& p : points) slot=std::max(slot,static_cast<int>(std::to_string(p.requestedThreads).size())+2);
    const int capacity=std::max(1,(columns-16)/slot);
    rows.push_back(paragraph(std::string("Eje Y: speedup · X: "+resource+" · escala ")+(logarithmic ? "log1p(S)" : "lineal")+(compact ? "" : ". Referencia 1.00×: igual tiempo que el secuencial.")) | dim);
    // Discrete categories, labelled with the actual request. Split horizontally
    // into groups rather than clipping labels; every group uses the same Y scale.
    for (std::size_t start=0;start<points.size();start+=capacity) {
        const auto count=std::min<std::size_t>(capacity,points.size()-start);
        const int width=static_cast<int>(count)*slot;
        const auto level=[&](long double v) { return std::clamp(static_cast<int>(std::round(transform(v)/top*height)),0,height); };
        const int reference=level(1);
        for (int y=height;y>=0;--y) {
            long double tick=top*y/height;
            if (logarithmic) tick=std::expm1(tick);
            std::string line(static_cast<std::size_t>(width),y==reference ? '-' : ' ');
            for (std::size_t i=0;i<count;++i)
                if (level(points[start+i].speedup)==y) line[i*slot+slot/2]='*';
            rows.push_back(hbox({text(number(tick)) | size(WIDTH,EQUAL,14),text("│"),text(line) | color(accent)}));
        }
        std::string labels;
        for (std::size_t i=0;i<count;++i) {
            auto value=std::to_string(points[start+i].requestedThreads);
            const auto pad=(slot-static_cast<int>(value.size()))/2;
            labels+=std::string(pad,' ')+value+std::string(slot-pad-value.size(),' ');
        }
        rows.push_back(hbox({text(result.config.parallelEngine==Engine::MPI?"Procesos →":"Hilos →") | size(WIDTH,EQUAL,15),text(labels)}));
    }
    rows.push_back(paragraph(compact ? "*: medido · --: 1.00× (celda cercana). X: "+resource+" solicitados, categorías ordenadas." : "*: speedup válido observado; --: referencia 1.00× (celda más cercana). Eje X: "+resource+" solicitados como categorías ordenadas; sin interpolación ni línea ideal. N/D se omite, nunca se dibuja como cero.") | dim);
}
}
std::vector<ScalingPlotPoint> scalingPlotPoints(const ScalingResult& result) {
    std::vector<ScalingPlotPoint> points;
    if(result.study) {
        for(const auto& summary:result.study->summaries) {
            const auto& c=result.study->configurations[summary.configurationIndex];
            if(c.engine==Engine::MPI && summary.valid && summary.speedup && std::isfinite(*summary.speedup) && *summary.speedup>0)
                points.push_back({*c.requestedProcesses,*summary.speedup});
        }
        std::stable_sort(points.begin(),points.end(),[](const auto& a,const auto& b){return a.requestedThreads<b.requestedThreads;});
        return points;
    }
    for (const auto* p : sorted(result)) if (validSpeedup(p->summary)) points.push_back({p->requestedThreads,*p->summary.speedup});
    return points;
}
std::vector<Element> scalingRows(const ScalingResult& result,const std::string& patternName,
    ScalingTab tab,int columns,Color accent,Color positive) {
    if(result.study) {
        auto rows=experimentalStudyRows(*result.study,(result.input.generated?std::string("Tablero generado"):patternName)+" · "+std::to_string(result.input.width)+" × "+std::to_string(result.input.height),static_cast<int>(tab),columns,accent,positive);
        if(tab==ScalingTab::Summary) {
            rows.push_back(paragraph("Referencia secuencial compartida por ronda."));
            rows.push_back(text("GRÁFICO DE SPEEDUP")|bold|color(accent));
            graph(rows,result,columns,accent);
        }
        return rows;
    }

    Elements rows,tabs;
    const std::array<std::string,3> names{"Resumen","Muestras","Método"};
    for (std::size_t i=0;i<names.size();++i) {
        auto label=text(" "+names[i]+" ");
        tabs.push_back(i==static_cast<std::size_t>(tab) ? label | bgcolor(accent) | color(Color::RGB(25,32,38)) | bold : label);
    }
    rows.push_back(hbox(std::move(tabs)));
    auto section=[&](const std::string& name,bool first=false) {
        if (!first) { if (columns>=100) rows.push_back(text("")); rows.push_back(separator() | dim); }
        rows.push_back(text(name) | color(accent) | bold);
    };
    const auto points=sorted(result);
    const auto& s=result.summary;
    if (tab==ScalingTab::Summary) {
        section("IDENTIFICACIÓN",true);
        rows.push_back(paragraph("Entrada: "+(result.input.generated ? std::string("Tablero generado") : patternName)+" · "+std::to_string(result.input.width)+" × "+std::to_string(result.input.height)+" (ancho × alto)"));
        rows.push_back(paragraph("Generaciones: "+std::to_string(result.config.generations)+" · Calentamientos: "+std::to_string(result.config.warmups)+" · Repeticiones: "+std::to_string(result.config.repetitions)));
        std::string requests;
        for (auto t : result.config.requestedThreads) { if (!requests.empty()) requests+=", "; requests+=std::to_string(t); }
        rows.push_back(paragraph("Hilos solicitados: "+requests+" · Configuraciones OpenMP: "+std::to_string(s.points.size())));
        rows.push_back(text("Equivalencia: "+yes(s.equivalent)+" · Validez: "+yes(s.validForComparison)) | color(s.equivalent && s.validForComparison ? positive : Color::Yellow));
        section("REFERENCIA SECUENCIAL");
        rows.push_back(paragraph("Mediana secuencial compartida: "+(s.sequentialStatistics ? number(s.sequentialStatistics->medianMs)+" ms" : "N/D")+". Referencia única para todos los speedups."));
        std::vector<std::vector<std::string>> data;
        for (auto p : points) {
            const auto& v=p->summary;
            const bool efficiency=validSpeedup(v) && v.effectiveThreadCountStable && v.minEffectiveThreads && v.maxEffectiveThreads && *v.minEffectiveThreads>0 && *v.minEffectiveThreads==*v.maxEffectiveThreads;
            data.push_back({std::to_string(p->requestedThreads),threads(v),v.openMPStatistics ? number(v.openMPStatistics->medianMs) : "N/D",
                validSpeedup(v) ? available(v.speedup) : "N/D",efficiency ? available(v.efficiency,100) : "N/D",yes(v.equivalent),yes(v.validForComparison)});
        }
        const std::vector<std::string> headers{"Solic.","Efect.","OMP (ms)","Speedup ×","Efic. %","Equiv.","Válido"};
        int tableWidth=static_cast<int>(headers.size()-1)*2;
        for (std::size_t i=0;i<headers.size();++i) {
            int width=string_width(headers[i]);
            for (const auto& record : data) width=std::max(width,string_width(record[i]));
            tableWidth+=width;
        }
        const bool beside=columns>=118 && points.size()<=4 && !points.empty() && tableWidth<=64;
        if (beside) {
            rows.push_back(separator() | dim);
            Elements left{ text("TABLA DE ESCALABILIDAD") | color(accent) | bold };
            Elements right{ text("GRÁFICO DE SPEEDUP") | color(accent) | bold };
            table(left,headers,data,64);
            graph(right,result,columns-66,accent,true);
            rows.push_back(hbox({vbox(std::move(left)) | size(WIDTH,EQUAL,64),text("  "),vbox(std::move(right)) | flex}));
            rows.push_back(paragraph("Eje X categórico, sin interpolación ni línea ideal. N/D se omite del gráfico; no equivale a cero. Escala Y compartida; detalle numérico en la tabla.") | dim);
        } else {
            section("TABLA DE ESCALABILIDAD");
            table(rows,headers,data,columns);
        }
        for (auto p : points) {
            const auto& v=p->summary;
            if (!v.minEffectiveThreads || !v.maxEffectiveThreads) rows.push_back(paragraph(std::to_string(p->requestedThreads)+" solicitados: equipo efectivo N/D, sin observaciones."));
            else if (!v.effectiveThreadCountStable || *v.minEffectiveThreads!=*v.maxEffectiveThreads) rows.push_back(paragraph(std::to_string(p->requestedThreads)+" solicitados: equipo variable "+threads(v)+"; eficiencia N/D."));
            else if (*v.minEffectiveThreads!=p->requestedThreads) rows.push_back(paragraph(std::to_string(p->requestedThreads)+" solicitados: equipo efectivo limitado/diferente ("+threads(v)+"); eficiencia basada en el equipo observado."));
        }
        if (!beside) { section("GRÁFICO DE SPEEDUP"); graph(rows,result,columns,accent); }
        section("INTERPRETACIÓN");
        const ScalingPoint* fastest=nullptr; const ScalingPoint* best=nullptr;
        bool monotonic=true; std::optional<double> previous; std::size_t valid=0;
        for (auto p : points) if (validSpeedup(p->summary) && p->summary.openMPStatistics && std::isfinite(p->summary.openMPStatistics->medianMs) && p->summary.openMPStatistics->medianMs>=0) {
            ++valid;
            if (!fastest || p->summary.openMPStatistics->medianMs<fastest->summary.openMPStatistics->medianMs) fastest=p;
            if (!best || *p->summary.speedup>*best->summary.speedup) best=p;
            if (previous && p->summary.openMPStatistics->medianMs>*previous) monotonic=false;
            previous=p->summary.openMPStatistics->medianMs;
        }
        if (fastest) rows.push_back(paragraph("Menor mediana OpenMP: "+std::to_string(fastest->requestedThreads)+" hilos solicitados ("+number(fastest->summary.openMPStatistics->medianMs)+" ms). Mayor speedup observado: "+available(best->summary.speedup)+"× con "+std::to_string(best->requestedThreads)+" solicitados. Los empates conservan la primera configuración ordenada."));
        else rows.push_back(text("N/D: no hay configuraciones válidas para interpretar rendimiento."));
        if (valid>=2) rows.push_back(paragraph(std::string("Entre las configuraciones válidas medidas, al aumentar hilos el tiempo mediano ")+(monotonic ? "no aumentó (comportamiento monótono no estricto)." : "no mejoró de forma monótona.")));
        if (valid<points.size()) rows.push_back(paragraph("Se excluyen de la interpretación configuraciones inválidas o con métricas N/D; no se infiere su rendimiento."));
        rows.push_back(paragraph("Las diferencias no demuestran significancia estadística ni identifican causas. Resolución del gráfico limitada a celdas; consulte la tabla.") | dim);
    } else if (tab==ScalingTab::Samples) {
        section("ESTADÍSTICAS · ms",true);
        std::vector<std::vector<std::string>> statistics;
        auto append=[&](std::string motor,std::string requested,std::string effective,const std::optional<BenchmarkStatistics>& v) {
            statistics.push_back({motor,requested,effective,v ? number(v->medianMs) : "N/D",v ? number(v->meanMs) : "N/D",v ? number(v->minimumMs) : "N/D",v ? number(v->maximumMs) : "N/D",v ? number(v->populationStddevMs) : "N/D"});
        };
        std::optional<int> sequentialMin,sequentialMax;
        for (const auto& sample : result.samples) if (sample.execution.measurement.engine==Engine::Sequential) {
            const auto& m=sample.execution.measurement;
            sequentialMin=sequentialMin ? std::min(*sequentialMin,m.minEffectiveThreads) : m.minEffectiveThreads;
            sequentialMax=sequentialMax ? std::max(*sequentialMax,m.maxEffectiveThreads) : m.maxEffectiveThreads;
        }
        const auto sequentialTeam=sequentialMin && sequentialMax
            ? std::to_string(*sequentialMin)+(*sequentialMin==*sequentialMax ? "" : "–"+std::to_string(*sequentialMax)) : "N/D";
        append("Sec. compartida","—",sequentialTeam,s.sequentialStatistics);
        for (auto p : points) append("OpenMP",std::to_string(p->requestedThreads),threads(p->summary),p->summary.openMPStatistics);
        table(rows,{"Motor","Solic.","Efect.","Mediana","Media","Mínimo","Máximo","DE pobl."},statistics,columns);
        rows.push_back(paragraph("DE pobl.: desviación estándar poblacional (divisor N). Referencia secuencial compartida, almacenada una sola vez.") | dim);
        section("MUESTRAS INDIVIDUALES · ms");
        rows.push_back(paragraph("Índices desde 0. Orden original de adquisición, sin emparejar ni ordenar. Ref.: índice de la muestra secuencial compartida; validez según la API.") | dim);
        for (const auto& sample : result.samples) {
            const auto& e=sample.execution; const auto& m=e.measurement;
            bool valid=sample.sequentialReferenceIndex<result.samples.size() && sample.validForComparison(result.samples[sample.sequentialReferenceIndex]);
            // Each compact record remains a scroll target, with wrap at narrow widths.
            rows.push_back(paragraph("#"+std::to_string(e.executionIndex)+" · Ronda "+std::to_string(e.roundIndex)+" · Pos. "+std::to_string(e.positionInRound)+" · Rep. "+std::to_string(m.repetitionIndex)+" · "+(m.engine==Engine::Sequential ? "Secuencial compartida" : "OpenMP")+" · Solicitados: "+(m.requestedThreads ? std::to_string(*m.requestedThreads) : "—")+" · Efectivos: "+std::to_string(m.minEffectiveThreads)+"–"+std::to_string(m.maxEffectiveThreads)+" · Lote: "+number(m.elapsedMs)+" ms · Equiv.: "+yes(m.equivalent)+" · Válido: "+yes(valid)+" · Ref.: "+std::to_string(sample.sequentialReferenceIndex)));
        }
        if (result.samples.empty()) rows.push_back(text("No hay muestras medidas."));
        rows.push_back(paragraph("Precisión abreviada solo en pantalla; muestras, estadísticas y CSV originales intactos.") | dim);
    } else {
        section("QUÉ MEDIMOS",true);
        rows.push_back(paragraph("Tiempo total del lote de generaciones. Incluye pasos del motor y observación del equipo efectivo dentro del bucle medido."));
        rows.push_back(paragraph("Excluye generación/copia de entrada, construcción de Game, oráculo, calentamientos, contexto, estadísticas y renderizado."));
        section("CÓMO ESTUDIAMOS LA ESCALABILIDAD");
        rows.push_back(paragraph("Misma entrada inicial en todas las ejecuciones. Una serie secuencial compartida, no repetida por configuración OpenMP."));
        rows.push_back(paragraph("Hilos solicitados son configuración; efectivos son observaciones. Calentamientos por configuración; solo repeticiones medidas aparecen como muestras."));
        rows.push_back(paragraph(std::string("Orden reproducible por rotación ")+(result.config.order==ScalingOrder::RotateLeft ? "a la izquierda" : "a la derecha")+" de las configuraciones en cada ronda. Muestras conserva ronda, posición y ejecución reales."));
        section("CÓMO INTERPRETAMOS");
        rows.push_back(paragraph("Speedup S(p) = Ts / Tp: medianas secuencial y OpenMP. Referencia 1.00×: mismo tiempo que el secuencial."));
        rows.push_back(paragraph("Eficiencia E(p) = S(p) / p × 100 %, con p efectivo observado cuando la API permite calcularla. Equipos variables: eficiencia N/D."));
        rows.push_back(paragraph("S(p)=p sería una referencia ideal teórica, no una medición; no se dibuja en este gráfico."));
        rows.push_back(paragraph("Equivalencia: mismo estado final que el oráculo secuencial. Validez: observaciones aptas, incluida la referencia de la ronda. No implica aceleración."));
        rows.push_back(paragraph("N/D indica datos ausentes/invalidos, medianas cero o límites numéricos; nunca equivale a cero. Desviación estándar poblacional: divisor N."));
        rows.push_back(paragraph("Solo configuraciones válidas se interpretan. Diferencias no prueban causalidad ni significancia estadística; no extrapolamos fuera de las configuraciones medidas."));
    }
    return rows;
}
}
