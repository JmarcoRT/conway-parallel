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
void sampleTable(Elements& rows,const std::vector<std::string>& headers,
    const std::vector<std::vector<std::string>>& records,int columns,Color accent,Color positive,
    const std::vector<bool>& numeric,int statusColumn=-1,const std::vector<int>& rounds={}) {
    std::vector<int> widths;
    for(const auto& header:headers) widths.push_back(string_width(header));
    for(const auto& record:records)
        for(std::size_t i=0;i<record.size();++i) widths[i]=std::max(widths[i],string_width(record[i]));
    int total=static_cast<int>(headers.size()-1)*2;
    for(int width:widths) total+=width;
    const auto tint=[&](const std::string& cell,std::size_t column) {
        if(static_cast<int>(column)==statusColumn) return cell=="Correcto"?positive:Color::RGB(235,135,135);
        if(cell=="N/D") return Color::RGB(224,185,112);
        return Color(Color::Default);
    };
    const auto line=[&](const auto& cells,bool heading) {
        Elements parts;
        for(std::size_t i=0;i<cells.size();++i) {
            if(i) parts.push_back(text("  "));
            const std::string padding(static_cast<std::size_t>(std::max(0,widths[i]-string_width(cells[i]))),' ');
            auto cell=text(numeric[i]?padding+cells[i]:cells[i]+padding);
            parts.push_back(heading?cell|bold|color(accent):cell|color(tint(cells[i],i)));
        }
        rows.push_back(hbox(std::move(parts)));
    };
    const bool wide=total<=std::max(1,columns-2);
    if(wide) {line(headers,true);rows.push_back(separator()|dim|size(WIDTH,EQUAL,total));}
    for(std::size_t r=0;r<records.size();++r) {
        if(r && (!wide || rounds.empty() || rounds[r]!=rounds[r-1])) rows.push_back(text(" "));
        if(wide) line(records[r],false);
        else for(std::size_t i=0;i<records[r].size();++i)
            rows.push_back(paragraph(headers[i]+": "+records[r][i])|color(tint(records[r][i],i)));
    }
}
std::string observedThreads(int minimum,int maximum) {
    return std::to_string(minimum)+(minimum==maximum?"":"–"+std::to_string(maximum));
}
Elements openMPSamples(const ScalingResult& result,int columns,Color accent,Color positive,bool details) {
    const auto muted=Color::RGB(150,160,170);
    const auto caution=Color::RGB(224,185,112);
    const auto failure=Color::RGB(235,135,135);
    Elements rows{hbox({text("Resumen   ")|color(accent),text("[ Muestras ]")|bold|color(accent),text("   Método")|color(accent)})};
    rows.push_back(text(result.samples.empty()?"Sin muestras registradas":"Mediciones completadas")|bold|color(accent));
    rows.push_back(paragraph(details?"D Ocultar detalles técnicos":"D Ver detalles técnicos")|color(muted));
    const auto section=[&](const std::string& title) {
        rows.push_back(text(" "));
        rows.push_back(text(title)|bold|color(accent));
        rows.push_back(text(" "));
    };
    section("ESTADÍSTICAS POR CONFIGURACIÓN (ms)");
    std::optional<int> low,high;
    bool referenceValid=true;
    std::size_t referenceCount=0;
    for(const auto& sample:result.samples) if(sample.execution.measurement.engine==Engine::Sequential) {
        const auto& m=sample.execution.measurement;
        low=low?std::min(*low,m.minEffectiveThreads):m.minEffectiveThreads;
        high=high?std::max(*high,m.maxEffectiveThreads):m.maxEffectiveThreads;
        referenceValid=referenceValid && sample.execution.validForComparison();
        ++referenceCount;
    }
    referenceValid=referenceValid && referenceCount==static_cast<std::size_t>(result.config.repetitions) && referenceCount>0;
    std::vector<std::vector<std::string>> statistics;
    bool invalidStatistics=!referenceValid;
    const auto append=[&](std::string configuration,std::string observed,const std::optional<BenchmarkStatistics>& v,bool valid) {
        if(!valid) configuration+=" (no válido)";
        invalidStatistics=invalidStatistics || !valid;
        statistics.push_back({configuration,observed,v?number(v->medianMs):"N/D",v?number(v->meanMs):"N/D",
            v?number(v->minimumMs):"N/D",v?number(v->maximumMs):"N/D",v?number(v->populationStddevMs):"N/D"});
    };
    append("Secuencial (referencia)",low && high?observedThreads(*low,*high):"N/D",result.summary.sequentialStatistics,referenceValid);
    for(const auto* point:sorted(result)) append("OpenMP ("+std::to_string(point->requestedThreads)+
        (point->requestedThreads==1?" hilo solicitado)":" hilos solicitados)"),threads(point->summary),point->summary.openMPStatistics,
        point->summary.validForComparison && point->summary.equivalent);
    sampleTable(rows,{"Configuración","Hilos observados","Mediana","Media","Mínimo","Máximo","Desviación"},statistics,columns,
        accent,positive,{false,true,true,true,true,true,true});
    rows.push_back(text(" "));
    rows.push_back(paragraph("La mediana se usa en Resumen. La desviación indica cuánto varían las mediciones.")|color(muted));
    if(invalidStatistics) rows.push_back(paragraph("Las estadísticas no válidas conservan evidencia; no justifican una comparación.")|color(caution));
    section("MEDICIONES INDIVIDUALES (ms)");
    std::vector<std::vector<std::string>> records;
    std::vector<int> rounds;
    for(const auto& sample:result.samples) {
        const auto& e=sample.execution;const auto& m=e.measurement;
        std::string state="Correcto";
        if(!m.equivalent) state="No coincide";
        else if(!e.validForComparison()) state="No válido";
        else if(sample.sequentialReferenceIndex>=result.samples.size()) state="Sin referencia";
        else if(!result.samples[sample.sequentialReferenceIndex].execution.validForComparison()) state="Referencia no válida";
        else if(!sample.validForComparison(result.samples[sample.sequentialReferenceIndex])) state="Referencia incompatible";
        records.push_back({std::to_string(e.roundIndex+1),std::to_string(e.positionInRound+1),
            m.engine==Engine::Sequential?"Secuencial":"OpenMP",m.requestedThreads?std::to_string(*m.requestedThreads):"No aplica",
            observedThreads(m.minEffectiveThreads,m.maxEffectiveThreads),number(m.elapsedMs),state});
        rounds.push_back(e.roundIndex);
    }
    sampleTable(rows,{"Ronda","Orden","Motor","Hilos solicitados","Hilos observados","Tiempo","Estado"},records,columns,
        accent,positive,{true,true,false,true,true,true,false},6,rounds);
    if(records.empty()) rows.push_back(paragraph("No hay muestras medidas.")|color(caution));
    rows.push_back(text(" "));
    rows.push_back(paragraph("Orden real de medición. Una referencia secuencial por ronda, compartida por las configuraciones OpenMP.")|color(muted));
    rows.push_back(paragraph("La aceleración usa las medianas de las series, no un promedio de aceleraciones por ronda.")|color(muted));
    if(!details) return rows;
    section("DETALLES TÉCNICOS");
    rows.push_back(paragraph("Los identificadores originales incluyen los calentamientos en su numeración. Estos no forman parte de las estadísticas.")|color(muted));
    rows.push_back(paragraph("Desviación estándar poblacional: divisor N. La precisión abreviada es solo visual; datos y CSV conservan su precisión.")|color(muted));
    rows.push_back(text(" "));
    for(std::size_t i=0;i<result.samples.size();++i) {
        const auto& sample=result.samples[i];const auto& e=sample.execution;const auto& m=e.measurement;
        const bool hasReference=sample.sequentialReferenceIndex<result.samples.size();
        const bool valid=hasReference && sample.validForComparison(result.samples[sample.sequentialReferenceIndex]);
        rows.push_back(paragraph(std::string(m.engine==Engine::Sequential?"Secuencial":"OpenMP")+"   Ronda "+
            std::to_string(e.roundIndex+1)+"   Orden "+std::to_string(e.positionInRound+1))|bold|color(accent));
        rows.push_back(paragraph("Identificador original: #"+std::to_string(e.executionIndex)+"   Índice de muestra: "+std::to_string(i)));
        rows.push_back(paragraph("Ronda base cero: "+std::to_string(e.roundIndex)+"   Posición base cero: "+std::to_string(e.positionInRound)));
        rows.push_back(paragraph("Repetición base cero: "+std::to_string(m.repetitionIndex)+"   Configuración original: "+std::to_string(sample.configurationIndex)));
        rows.push_back(paragraph("Índice de referencia: "+std::to_string(sample.sequentialReferenceIndex)+"   Ejecución de referencia: "+
            (hasReference?"#"+std::to_string(result.samples[sample.sequentialReferenceIndex].execution.executionIndex):"N/D")));
        rows.push_back(paragraph("Equivalencia: "+yes(m.equivalent)+"   Válido para comparar: "+yes(valid))|color(valid?positive:failure));
        rows.push_back(text(" "));
    }
    return rows;
}
std::string rounded(double value,int precision) {
    if(!std::isfinite(value) || value<0) return "N/D";
    if((value>0 && value<0.001) || value>=1e6) return number(value);
    std::ostringstream out;out<<std::fixed<<std::setprecision(precision)<<value;return out.str();
}
void scalingGraph(Elements& rows,const ScalingResult& result,int columns,Color accent,Color positive,
    std::optional<int> fastest) {
    const auto points=scalingPlotPoints(result);
    const auto muted=Color::RGB(150,160,170);
    const bool cuda=result.config.parallelEngine==Engine::CUDA;
    const bool mpi=result.config.parallelEngine==Engine::MPI;
    rows.push_back(text(cuda?"ACELERACIÓN SEGÚN EL BLOQUE":mpi?"ACELERACIÓN SEGÚN LOS PROCESOS":"ACELERACIÓN SEGÚN LOS HILOS")|bold|color(accent));
    rows.push_back(text(" "));
    if(points.empty()) {rows.push_back(paragraph("Gráfico N/D: no hay aceleraciones válidas.")|color(muted));return;}
    long double maximum=1,minimum=1;
    int maxRequest=1;
    for(const auto& point:points) {
        maximum=std::max(maximum,static_cast<long double>(point.speedup));
        minimum=std::min(minimum,static_cast<long double>(point.speedup));
        maxRequest=std::max(maxRequest,point.requestedThreads);
    }
    const bool logarithmic=maximum/minimum>1000;
    const auto transform=[&](long double v){return logarithmic?std::log1p(v):v;};
    long double top=transform(maximum);
    if(!logarithmic) {
        const long double raw=maximum/6;
        const long double magnitude=std::pow(10.0L,std::floor(std::log10(raw)));
        const long double normalized=raw/magnitude;
        long double step=10;
        for(long double candidate:{1.0L,2.0L,2.5L,5.0L,10.0L})
            if(normalized<=candidate) {step=candidate;break;}
        top=std::ceil(maximum/(step*magnitude))*step*magnitude;
    }
    constexpr int height=12;
    std::array<std::string,height+1> tickLabels;
    int tickWidth=1;
    for(int level=0;level<=height;level+=2) {
        long double tick=top*level/height;
        if(logarithmic) tick=std::expm1(tick);
        auto& label=tickLabels[level];
        if(tick>=1e6) label=number(tick)+"×";
        else {
            std::ostringstream out;out<<std::fixed<<std::setprecision(2)<<tick;label=out.str();
            while(label.back()=='0') label.pop_back();
            if(label.back()=='.') label.pop_back();
            label+="×";
        }
        tickWidth=std::max(tickWidth,string_width(label)+1);
    }
    const int width=std::clamp(columns-tickWidth-2,12,72);
    const auto y=[&](long double v){return std::clamp(static_cast<int>(std::round(transform(v)/top*height)),0,height);};
    const auto x=[&](int requested){
        const auto category=std::find_if(points.begin(),points.end(),[&](const auto& point){return point.requestedThreads==requested;});
        const auto position=cuda?(points.size()==1?0.5L:static_cast<long double>(std::distance(points.begin(),category))/(points.size()-1)):
            static_cast<long double>(requested)/maxRequest;
        return std::clamp(static_cast<int>(std::round(position*(width-1))),0,width-1);
    };
    const int baseline=y(1);
    if(logarithmic) rows.push_back(paragraph("Escala vertical log1p(S) por amplitud de valores.")|color(muted));
    bool collision=false;
    for(int level=height;level>=0;--level) {
        const auto& label=tickLabels[level];
        const std::string padding(static_cast<std::size_t>(tickWidth-1-string_width(label)),' ');
        Elements line{text(padding+label+" ")|color(muted),text("│")|color(muted)};
        for(int col=0;col<width;++col) {
            int hits=0;bool best=false;
            for(const auto& point:points) if(x(point.requestedThreads)==col && y(point.speedup)==level) {
                ++hits;best=best || (fastest && point.requestedThreads==*fastest);
            }
            collision=collision || hits>1;
            line.push_back(text(hits>1?"◆":hits?"●":level==baseline?"┄":" ")|
                color(hits?(best?positive:accent):muted));
        }
        rows.push_back(hbox(std::move(line)));
    }
    std::string axis;for(int i=0;i<width;++i) axis+="─";
    rows.push_back(hbox({text(std::string(tickWidth,' ')),text("└"+axis)|color(muted)}));
    // Stagger crowded labels so every requested count remains visible.
    std::vector<std::string> labels;
    for(const auto& point:points) {
        const auto label=point.label.empty()?std::to_string(point.requestedThreads):point.label;
        const int begin=std::clamp(x(point.requestedThreads)-static_cast<int>(label.size())/2,0,width-static_cast<int>(label.size()));
        std::size_t row=0;
        for(;row<labels.size();++row) {
            bool free=true;
            for(int col=std::max(0,begin-1);col<std::min(width,begin+static_cast<int>(label.size())+1);++col)
                free=free && labels[row][col]==' ';
            if(free) break;
        }
        if(row==labels.size()) labels.emplace_back(width,' ');
        labels[row].replace(begin,label.size(),label);
    }
    for(const auto& label:labels) rows.push_back(text(std::string(tickWidth+1,' ')+label)|color(muted));
    rows.push_back(text(" "));
    rows.push_back(paragraph(cuda?"Bloques CUDA (categorías en una GPU)":mpi?"Procesos solicitados (escala proporcional)":"Hilos solicitados (escala proporcional)")|color(muted));
    rows.push_back(paragraph("● Medición    1× Igual tiempo que Secuencial")|color(muted));
    rows.push_back(paragraph("Verde: configuración con menor tiempo observado.")|color(muted));
    if(collision) rows.push_back(paragraph("◆ Mediciones en la misma celda; valores exactos en la tabla.")|color(muted));
    const auto configurationCount=result.study?static_cast<std::size_t>(std::count_if(result.study->configurations.begin(),result.study->configurations.end(),[&](const auto& c){return c.engine==result.config.parallelEngine;})):result.summary.points.size();
    if(points.size()<configurationCount) rows.push_back(paragraph("Se omiten las configuraciones sin aceleración válida; no equivalen a cero.")|color(muted));
}
Elements openMPSummary(const ScalingResult& result,const std::string& patternName,int columns,Color accent,Color positive) {
    const auto muted=Color::RGB(150,160,170);
    const auto caution=Color::RGB(224,185,112);
    const auto failure=Color::RGB(235,135,135);
    const auto points=sorted(result);
    const auto& summary=result.summary;
    Elements rows{hbox({text("[ Resumen ]")|bold|color(accent),text("   Muestras   Método")|color(accent)})};
    rows.push_back(text(result.samples.empty()?"Sin muestras registradas":"Escalabilidad completada")|bold|color(accent));
    rows.push_back(text(summary.validForComparison && summary.equivalent?"Todos los resultados son correctos":"Revisa el estado de las configuraciones")|
        color(summary.validForComparison && summary.equivalent?positive:caution));
    rows.push_back(text(" "));
    rows.push_back(text("ENTRADA")|bold|color(accent));
    rows.push_back(paragraph((result.input.generated?std::string("Tablero generado"):patternName)+" de "+
        std::to_string(result.input.width)+" × "+std::to_string(result.input.height)));
    rows.push_back(paragraph("Generaciones   "+std::to_string(result.config.generations)+" por ejecución"));
    rows.push_back(paragraph("Repeticiones   "+std::to_string(result.config.repetitions)));
    rows.push_back(text(" "));
    rows.push_back(paragraph("Referencia secuencial   "+(summary.sequentialStatistics?number(summary.sequentialStatistics->medianMs)+" ms":"N/D"))|color(accent));
    rows.push_back(text(" "));
    const ScalingPoint* fastest=nullptr;
    for(const auto* point:points) {
        const auto& s=point->summary;
        if(validSpeedup(s) && s.openMPStatistics && std::isfinite(s.openMPStatistics->medianMs) && s.openMPStatistics->medianMs>=0 &&
            (!fastest || s.openMPStatistics->medianMs<fastest->summary.openMPStatistics->medianMs)) fastest=point;
    }
    using Record=std::array<std::string,6>;
    const Record headers{"Hilos solicitados","Hilos observados","Tiempo (ms)","Aceleración","Eficiencia","Estado"};
    std::array<int,6> widths{};
    for(std::size_t i=0;i<headers.size();++i) widths[i]=string_width(headers[i]);
    std::vector<Record> records;
    for(const auto* point:points) {
        const auto& s=point->summary;
        const bool efficiency=validSpeedup(s) && s.effectiveThreadCountStable && s.minEffectiveThreads && s.maxEffectiveThreads &&
            *s.minEffectiveThreads>0 && *s.minEffectiveThreads==*s.maxEffectiveThreads && s.efficiency;
        Record record{std::to_string(point->requestedThreads),threads(s),s.openMPStatistics?number(s.openMPStatistics->medianMs):"N/D",
            validSpeedup(s)?rounded(*s.speedup,2)+"×":"N/D",efficiency?rounded(*s.efficiency*100,1)+" %":"N/D",
            !s.equivalent?"No coincide":s.validForComparison?"Correcto":"No válido"};
        for(std::size_t i=0;i<record.size();++i) widths[i]=std::max(widths[i],string_width(record[i]));
        records.push_back(std::move(record));
    }
    int tableWidth=10;for(int width:widths) tableWidth+=width;
    const bool beside=!points.empty() && columns>=tableWidth+60;
    Elements tableRows{text("RESULTADOS OPENMP")|bold|color(accent),text(" ")};
    const auto line=[&](const Record& cells,const ScalingPoint* point) {
        Elements parts;
        for(std::size_t i=0;i<cells.size();++i) {
            if(i) parts.push_back(text("  "));
            const std::string padding(static_cast<std::size_t>(std::max(0,widths[i]-string_width(cells[i]))),' ');
            auto cell=text(i<5?padding+cells[i]:cells[i]+padding);
            Color tint=Color::Default;
            if(point) {
                if(cells[i]=="N/D") tint=caution;
                else if(i==5) tint=cells[i]=="Correcto"?positive:failure;
                else if(i==2 && validSpeedup(point->summary) && fastest && point->summary.openMPStatistics && point->summary.openMPStatistics->medianMs==fastest->summary.openMPStatistics->medianMs) tint=positive;
                else if(i==3 && validSpeedup(point->summary)) tint=*point->summary.speedup>1?positive:*point->summary.speedup<1?caution:muted;
            }
            parts.push_back(point?cell|color(tint):cell|bold|color(accent));
        }
        tableRows.push_back(hbox(std::move(parts)));
    };
    if(tableWidth<=columns-2) {
        line(headers,nullptr);tableRows.push_back(separator()|dim|size(WIDTH,EQUAL,tableWidth));
        for(std::size_t r=0;r<records.size();++r) {line(records[r],points[r]);tableRows.push_back(text(" "));}
    } else {
        for(const auto& record:records) {
            tableRows.push_back(paragraph("Hilos solicitados: "+record[0])|bold|color(accent));
            for(std::size_t i=1;i<record.size();++i)
                tableRows.push_back(paragraph(headers[i]+": "+record[i])|color(i==5?(record[i]=="Correcto"?positive:failure):Color(Color::Default)));
            tableRows.push_back(text(" "));
        }
    }
    tableRows.push_back(paragraph("Tiempo mediano del lote. Menor es mejor.")|color(muted));
    for(const auto* point:points) {
        const auto& s=point->summary;
        if(!s.minEffectiveThreads || !s.maxEffectiveThreads)
            tableRows.push_back(paragraph(std::to_string(point->requestedThreads)+" solicitados: sin observaciones de hilos.")|color(caution));
        else if(!s.effectiveThreadCountStable || *s.minEffectiveThreads!=*s.maxEffectiveThreads)
            tableRows.push_back(paragraph(std::to_string(point->requestedThreads)+" solicitados: equipo variable; eficiencia N/D.")|color(caution));
        else if(*s.minEffectiveThreads!=point->requestedThreads)
            tableRows.push_back(paragraph(std::to_string(point->requestedThreads)+" solicitados: se observaron "+threads(s)+" hilos. Eficiencia basada en los observados.")|color(caution));
    }
    Elements chartRows;
    scalingGraph(chartRows,result,beside?columns-tableWidth-4:columns,accent,positive,fastest?std::optional<int>(fastest->requestedThreads):std::nullopt);
    if(beside) {
        // Keep each line a scroll target, including in wide, short terminals.
        const auto count=std::max(tableRows.size(),chartRows.size());
        for(std::size_t i=0;i<count;++i) {
            auto left=i<tableRows.size()?std::move(tableRows[i]):text(" ");
            auto right=i<chartRows.size()?std::move(chartRows[i]):text(" ");
            rows.push_back(hbox({left|size(WIDTH,EQUAL,tableWidth),text("    "),right|flex}));
        }
    }
    else {
        rows.insert(rows.end(),std::make_move_iterator(tableRows.begin()),std::make_move_iterator(tableRows.end()));
        rows.push_back(text(" "));
        rows.insert(rows.end(),std::make_move_iterator(chartRows.begin()),std::make_move_iterator(chartRows.end()));
    }
    rows.push_back(text(" "));
    if(fastest) {
        rows.push_back(paragraph("Menor tiempo observado: "+std::to_string(fastest->requestedThreads)+" hilos solicitados")|bold|color(positive));
        rows.push_back(paragraph(number(fastest->summary.openMPStatistics->medianMs)+" ms. Aceleración de "+rounded(*fastest->summary.speedup,2)+"× frente a Secuencial."));
    } else rows.push_back(paragraph("Sin mediciones válidas para identificar el menor tiempo.")|color(caution));
    return rows;
}

}
std::vector<ScalingPlotPoint> scalingPlotPoints(const ScalingResult& result) {
    std::vector<ScalingPlotPoint> points;
    if(result.study) {
        if(!result.study->complete || !result.study->cleanupConfirmed) return points;
        int category=0;
        for(std::size_t index=0;index<result.study->configurations.size();++index) {
            const auto& c=result.study->configurations[index];
            if(c.engine!=result.config.parallelEngine) continue;
            ++category;
            const auto summary=std::find_if(result.study->summaries.begin(),result.study->summaries.end(),[&](const auto& s){return s.configurationIndex==index;});
            if(summary==result.study->summaries.end() || !summary->valid || !summary->equivalent ||
               !summary->speedup || !std::isfinite(*summary->speedup) || *summary->speedup<=0) continue;
            if(c.engine==Engine::CUDA && c.cudaBlock) points.push_back({category,*summary->speedup,std::to_string(c.cudaBlock->x)+"x"+std::to_string(c.cudaBlock->y)});
            else if(c.engine==Engine::MPI && c.requestedProcesses) points.push_back({*c.requestedProcesses,*summary->speedup,{}});
            else if(c.engine==Engine::OpenMP && c.requestedThreads) points.push_back({*c.requestedThreads,*summary->speedup,{}});
        }
        std::stable_sort(points.begin(),points.end(),[](const auto& a,const auto& b){return a.requestedThreads<b.requestedThreads;});
        return points;
    }
    for (const auto* p : sorted(result)) if (validSpeedup(p->summary)) points.push_back({p->requestedThreads,*p->summary.speedup,{}});
    return points;
}
std::vector<Element> scalingRows(const ScalingResult& result,const std::string& patternName,
    ScalingTab tab,int columns,Color accent,Color positive,bool sampleDetails,bool methodDetails) {
    if(result.study) {
        const auto& study=*result.study;
        const auto engine=result.config.parallelEngine;
        if(tab==ScalingTab::Samples) return studySampleRows(study,columns,accent,positive,sampleDetails,engine);
        if(tab==ScalingTab::Method) return studyMethodRows(study,columns,accent,positive,methodDetails,engine);
        const auto input=(result.input.generated?std::string("Tablero generado"):patternName)+" de "+std::to_string(result.input.width)+" × "+std::to_string(result.input.height);
        auto rows=studySummaryRows(study,input,columns,accent,positive,engine);
        rows.push_back(text(" "));
        const StudySummary* fastest=nullptr;
        std::optional<int> bestCoordinate;
        int category=0;
        for(std::size_t index=0;index<study.configurations.size();++index) {
            const auto& c=study.configurations[index];
            if(c.engine!=engine) continue;
            ++category;
            const auto point=std::find_if(study.summaries.begin(),study.summaries.end(),[&](const auto& s){return s.configurationIndex==index;});
            if(!study.complete || !study.cleanupConfirmed || point==study.summaries.end() || !point->valid || !point->equivalent ||
               !point->statistics || !std::isfinite(point->statistics->medianMs) || point->statistics->medianMs<0 ||
               !point->speedup || !std::isfinite(*point->speedup) || *point->speedup<=0) continue;
            if(!fastest || point->statistics->medianMs<fastest->statistics->medianMs) {
                fastest=&*point;
                bestCoordinate=engine==Engine::CUDA?std::optional<int>(category):engine==Engine::MPI?c.requestedProcesses:c.requestedThreads;
            }
        }
        scalingGraph(rows,result,columns,accent,positive,bestCoordinate);
        rows.push_back(text(" "));
        if(engine==Engine::CUDA) rows.push_back(paragraph("Se comparan bloques en una misma GPU. La eficiencia no aplica.")|dim);
        if(fastest) {
            const auto& c=study.configurations[fastest->configurationIndex];
            const std::string configuration=engine==Engine::CUDA?"bloque "+std::to_string(c.cudaBlock->x)+"×"+std::to_string(c.cudaBlock->y):
                std::to_string(bestCoordinate.value_or(0))+(engine==Engine::MPI?(bestCoordinate==1?" proceso solicitado":" procesos solicitados"):(bestCoordinate==1?" hilo solicitado":" hilos solicitados"));
            rows.push_back(paragraph("Menor tiempo observado: "+configuration)|bold|color(positive));
            rows.push_back(paragraph(number(fastest->statistics->medianMs)+" ms. Aceleración de "+rounded(*fastest->speedup,2)+"× frente a Secuencial."));
        } else rows.push_back(paragraph("Sin mediciones válidas para identificar el menor tiempo.")|color(Color::RGB(224,185,112)));
        return rows;
    }

    if(tab==ScalingTab::Summary)
        return openMPSummary(result,patternName,columns,accent,positive);
    if(tab==ScalingTab::Samples)
        return openMPSamples(result,columns,accent,positive,sampleDetails);

    const auto muted=Color::RGB(150,160,170);
    const int width=std::max(1,std::min(112,columns-2));
    Elements rows{hbox({text("Resumen   Muestras   ")|color(accent),text("[ Método ]")|bold|color(accent)})};
    rows.push_back(paragraph(methodDetails?"D Ocultar detalles técnicos":"D Ver detalles técnicos")|color(muted));
    if(!result.summary.validForComparison || !result.summary.equivalent)
        rows.push_back(paragraph("Hay observaciones no válidas. Revisa sus estados en Muestras.")|color(Color::RGB(224,185,112)));
    const auto section=[&](const std::string& title) {
        rows.push_back(text(" "));
        rows.push_back(text(title)|bold|color(accent));
        rows.push_back(text(" "));
    };
    const auto explanation=[&](const std::string& label,const std::string& description) {
        if(width<65) {
            rows.push_back(text(label)|bold);
            rows.push_back(paragraph(description));
        } else rows.push_back(hbox({text(label)|size(WIDTH,EQUAL,22),
            paragraph(description)|size(WIDTH,EQUAL,width-22)}));
    };
    section("CÓMO SE REALIZÓ EL ESTUDIO");
    std::string requests;
    for(auto requested:result.config.requestedThreads) {
        if(!requests.empty()) requests+=", ";
        requests+=std::to_string(requested);
    }
    explanation("Hilos solicitados",requests.empty()?"N/D":requests);
    explanation("Generaciones",std::to_string(result.config.generations)+" por ejecución");
    explanation("Calentamientos",std::to_string(result.config.warmups)+" por configuración (no cuentan en los resultados)");
    explanation("Repeticiones",std::to_string(result.config.repetitions));
    explanation("Orden",std::string("Rotativo hacia la ")+(result.config.order==ScalingOrder::RotateLeft?"izquierda":"derecha"));
    rows.push_back(text(" "));
    rows.push_back(paragraph("Todas las ejecuciones comienzan con la misma entrada.")|size(WIDTH,LESS_THAN,width));
    rows.push_back(paragraph("Cada ronda incluye una referencia secuencial compartida por las configuraciones OpenMP.")|size(WIDTH,LESS_THAN,width));
    section("QUÉ SIGNIFICAN LOS RESULTADOS");
    explanation("Tiempo","Mediana del tiempo empleado en ejecutar las generaciones. Excluye preparación y calentamientos.");
    rows.push_back(text(" "));
    explanation("Aceleración","Tiempo secuencial / tiempo OpenMP. Mayor es mejor. 1× significa igual tiempo que Secuencial.");
    rows.push_back(text(" "));
    explanation("Eficiencia","Aceleración / hilos observados, expresada en porcentaje. Requiere un equipo estable; no representa uso de CPU.");
    rows.push_back(text(" "));
    explanation("Correcto","Resultado equivalente y medición válida. No significa necesariamente más rápido.");
    rows.push_back(text(" "));
    explanation("N/D","La métrica no pudo calcularse con datos válidos. No equivale a cero.");
    rows.push_back(text(" "));
    rows.push_back(paragraph("Más hilos no garantizan menor tiempo.")|color(muted));
    section("CÓMO LEER LA GRÁFICA");
    explanation("Horizontal","Hilos solicitados, con separación proporcional a sus valores.");
    rows.push_back(text(" "));
    explanation("Vertical","Aceleración respecto a Secuencial.");
    rows.push_back(text(" "));
    explanation("Puntos","Configuraciones medidas.");
    rows.push_back(text(" "));
    explanation("Línea 1×","Igual tiempo que Secuencial.");
    rows.push_back(text(" "));
    explanation("Verde","Configuración con menor tiempo observado.");
    if(methodDetails) {
        section("DETALLES TÉCNICOS");
        section("QUÉ MEDIMOS");
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
        section("CÓMO LEER LA GRÁFICA");
        rows.push_back(paragraph("Eje X: hilos solicitados en escala proporcional. Los puntos son mediciones; sin interpolación ni línea ideal. La referencia 1× se dibuja en la fila más cercana."));
        rows.push_back(paragraph("Escala vertical lineal; log1p(S) solo para rangos extremos, indicada en el gráfico. Mediciones cercanas pueden compartir celda; la tabla conserva el detalle numérico."));
        rows.push_back(paragraph("La conclusión usa el menor tiempo mediano entre configuraciones válidas con aceleración disponible. En un empate se muestra la primera por número de hilos solicitados."));
    }
    return rows;
}
}
