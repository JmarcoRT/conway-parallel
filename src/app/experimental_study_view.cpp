#include "app/experimental_study_view.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <ftxui/screen/string.hpp>
#include <iomanip>
#include <sstream>
#include <set>

namespace conway::app {
namespace {
std::string number(double value) {
    if(!std::isfinite(value) || value<0) return "N/D";
    std::ostringstream out;out << ((value>0 && value<0.001) || value>=1e6 ? std::scientific : std::fixed) << std::setprecision(3) << value;return out.str();
}
std::string value(std::optional<double> v,double factor=1) {return v?number(*v*factor):"N/D";}
std::string metric(std::optional<double> v, const std::string& unit, double factor=1) {
    const auto formatted=value(v,factor);
    return formatted=="N/D"?formatted:formatted+unit;
}
std::string unit(int count,const std::string& singular,const std::string& plural) {return count==1?singular:plural;}
std::string resources(std::optional<int> low,std::optional<int> high) {
    if(!low || !high) return "N/D";
    return std::to_string(*low)+(*low==*high?"":"–"+std::to_string(*high));
}
std::string configurationLabel(const ExecutionConfiguration& c) {
    if(c.engine==Engine::Sequential) return "Secuencial (referencia)";
    if(c.engine==Engine::CUDA) return c.cudaBlock?"CUDA (bloque "+std::to_string(c.cudaBlock->x)+"×"+std::to_string(c.cudaBlock->y)+")":"CUDA (N/D)";
    const auto count=c.engine==Engine::MPI?c.requestedProcesses:c.requestedThreads;
    return std::string(c.engine==Engine::MPI?"MPI (":"OpenMP (")+(count?std::to_string(*count):"N/D")+
        (c.engine==Engine::MPI?unit(count.value_or(0)," proceso solicitado)"," procesos solicitados)"):
            unit(count.value_or(0)," hilo solicitado)"," hilos solicitados)"));
}
std::string yes(bool b) {return b?"SI":"NO";}
std::string motor(const ExecutionConfiguration& c) {
    return c.engine==Engine::Sequential?"Secuencial":c.engine==Engine::OpenMP?"OpenMP":c.engine==Engine::MPI?"MPI":"CUDA";
}
std::string sampleConfiguration(const StudySample& sample,const ExecutionConfiguration& c) {
    if(c.engine==Engine::CUDA)
        return c.cudaBlock?"Bloque "+std::to_string(c.cudaBlock->x)+"×"+std::to_string(c.cudaBlock->y):"N/D";
    const auto low=c.engine==Engine::MPI?sample.observedProcesses:sample.minEffectiveThreads;
    const auto high=c.engine==Engine::MPI?sample.observedProcesses:sample.maxEffectiveThreads;
    return resources(low,high)+(c.engine==Engine::MPI?unit(low==high?low.value_or(0):0," proceso"," procesos"):
        unit(low==high?low.value_or(0):0," hilo"," hilos"));
}
// Natural widths keep every value visible; narrow terminals use labelled records.
void sampleTable(ftxui::Elements& rows,const std::vector<std::string>& headers,
    const std::vector<std::vector<std::string>>& records,int columns,ftxui::Color accent,
    ftxui::Color positive,const std::vector<bool>& numeric,int statusColumn=-1,
    const std::vector<int>& rounds={}) {
    using namespace ftxui;
    std::vector<int> widths;
    for(const auto& header:headers) widths.push_back(string_width(header));
    for(const auto& record:records)
        for(std::size_t i=0;i<record.size();++i) widths[i]=std::max(widths[i],string_width(record[i]));
    int total=static_cast<int>(headers.size()-1)*2;
    for(int width:widths) total+=width;
    const auto cellColor=[&](const std::string& cell,std::size_t i) {
        if(static_cast<int>(i)==statusColumn) return cell=="Correcto"?positive:Color::RGB(235,135,135);
        if(cell=="N/D") return Color::RGB(224,185,112);
        return Color(Color::Default);
    };
    const auto line=[&](const std::vector<std::string>& cells,bool heading) {
        Elements parts;
        for(std::size_t i=0;i<cells.size();++i) {
            if(i) parts.push_back(text("  "));
            const std::string pad(static_cast<std::size_t>(std::max(0,widths[i]-string_width(cells[i]))),' ');
            auto cell=text(numeric[i]?pad+cells[i]:cells[i]+pad);
            parts.push_back(heading?cell|bold|color(accent):cell|color(cellColor(cells[i],i)));
        }
        rows.push_back(hbox(std::move(parts)));
    };
    const bool wide=total<=std::max(1,columns-2);
    if(wide) {
        line(headers,true);
        rows.push_back(separator()|dim|size(WIDTH,EQUAL,total));
    }
    for(std::size_t r=0;r<records.size();++r) {
        if(wide) {
            if(r && (rounds.empty() || rounds[r]!=rounds[r-1])) rows.push_back(text(" "));
            line(records[r],false);
        } else {
            if(r) rows.push_back(text(" "));
            for(std::size_t i=0;i<records[r].size();++i)
                rows.push_back(paragraph(headers[i]+": "+records[r][i])|color(cellColor(records[r][i],i)));
        }
    }
}
void studyMethodDetails(ftxui::Elements& rows,const ExperimentalStudy& study,ftxui::Color accent) {
    using namespace ftxui;
    const bool hasMPI=std::any_of(study.configurations.begin(),study.configurations.end(),[](const auto& c){return c.engine==Engine::MPI;});
    const bool hasCUDA=std::any_of(study.configurations.begin(),study.configurations.end(),[](const auto& c){return c.engine==Engine::CUDA;});
    const auto section=[&](const std::string& title) {
        if(!rows.empty()) rows.push_back(text(" "));
        rows.push_back(text(title)|bold|color(accent));
    };
    section("TIEMPO PRINCIPAL");
    rows.push_back(paragraph("Local: lote completo de Game::step y observación del equipo efectivo. Excluye construcción y comparación."));
    if(hasCUDA) {
        rows.push_back(paragraph("CUDA: lote residente completo, medido por reloj CPU con sincronización final. Excluye preparación y descarga; no copia el tablero por generación."));
        rows.push_back(paragraph("Eventos CUDA: intervalo del stream, no ocupación ni tiempo puro de kernel. Preparación, descarga y total de operación se registran separados."));
        rows.push_back(paragraph("CUDA: eficiencia N/D. Los hilos por bloque no son núcleos ni hilos simultáneos; los bloques son configuraciones de una GPU."));
    }
    if(hasMPI) rows.push_back(paragraph("MPI: máximo entre ranks de intervalos MPI_Wtime. Incluye cálculo, halos, reducciones y barrera final."));
    if(hasMPI) rows.push_back(paragraph("Fuera del principal MPI: barrera inicial, agregación de tiempos, carga, IPC y snapshot."));
    if(hasMPI) section("TIEMPOS AUXILIARES MPI");
    if(hasMPI) rows.push_back(paragraph("Arranque por sesión y auxiliares son tiempos del cliente separados. La solicitud del lote incluye IPC: no forma una descomposición aditiva."));
    section("CÓMO COMPARAMOS");
    rows.push_back(paragraph("Rotación por ronda; cada calentamiento y muestra parten de la misma entrada. Equivalencia completa contra oráculo secuencial fuera del tiempo principal."));
    if(hasMPI) rows.push_back(paragraph("MPI: sesión nueva por turno y calentamientos por sesión. Cierre confirmado antes del siguiente participante."));
    if(hasMPI) rows.push_back(paragraph("Sin ranks ociosos durante mediciones locales. Los fallos no se reintentan automáticamente."));
    section("CÓMO INTERPRETAMOS");
    rows.push_back(paragraph("Speedup = mediana secuencial / mediana del participante. Eficiencia = speedup / recursos efectivos estables."));
    if(hasMPI) rows.push_back(paragraph("Recursos MPI: incluye rank 0 y ranks vacíos. La eficiencia puede superar 100 %; no es eficiencia por núcleo."));
    rows.push_back(paragraph("N/D: referencia ausente/inválida, equivalencia fallida, estudio incompleto, recursos variables o mediana cero. Las diferencias no prueban significancia estadística.")|dim);
    rows.push_back(paragraph("Mínimo, máximo, media, mediana y desviación estándar poblacional (divisor N). Muestras inválidas y fallos conservan evidencia; no justifican métricas válidas."));
}
}
std::vector<ftxui::Element> studyMethodRows(const ExperimentalStudy& study,int columns,
    ftxui::Color accent,ftxui::Color positive,bool details,std::optional<Engine> scalingEngine) {
    using namespace ftxui;
    const auto muted=Color::RGB(150,160,170);
    const auto caution=Color::RGB(224,185,112);
    const auto failure=Color::RGB(235,135,135);
    const int width=std::max(1,std::min(112,columns-2));
    Elements rows;
    rows.push_back(hbox({text("Resumen   Muestras   ")|color(accent),text("[ Método ]")|bold|color(accent)}));
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
    if(!study.complete) rows.push_back(text("ESTUDIO INCOMPLETO")|bold|color(caution));
    if(!study.cleanupConfirmed) rows.push_back(paragraph("No se confirmó la liberación de recursos.")|color(failure));
    if(!study.diagnostic.empty()) rows.push_back(paragraph("Diagnóstico: "+study.diagnostic)|color(failure));
    rows.push_back(paragraph(details?"D Ocultar detalles técnicos":"D Ver detalles técnicos")|color(muted));
    section(scalingEngine?"CÓMO SE REALIZÓ EL ESTUDIO":"CÓMO SE REALIZÓ LA COMPARACIÓN");
    if(scalingEngine) {
        std::string configurations;
        for(const auto& c:study.configurations) if(c.engine==*scalingEngine) {
            if(!configurations.empty()) configurations+=", ";
            if(c.engine==Engine::CUDA) configurations+=c.cudaBlock?std::to_string(c.cudaBlock->x)+"×"+std::to_string(c.cudaBlock->y):"N/D";
            else {const auto requested=c.engine==Engine::MPI?c.requestedProcesses:c.requestedThreads;configurations+=requested?std::to_string(*requested):"N/D";}
        }
        explanation(*scalingEngine==Engine::CUDA?"Bloques CUDA":*scalingEngine==Engine::MPI?"Procesos solicitados":"Hilos solicitados",configurations);
    }
    explanation("Calentamientos",std::to_string(study.warmups)+" (no cuentan en los resultados)");
    explanation("Repeticiones",std::to_string(study.repetitions));
    explanation("Generaciones",std::to_string(study.generations)+" por ejecución");
    explanation(scalingEngine?"Orden de configuración":"Orden de los motores",std::string("Rotativo entre rondas hacia la ")+(study.rotateRight?"derecha":"izquierda"));
    rows.push_back(text(" "));
    rows.push_back(paragraph("Cada ejecución comienza con la misma entrada.")|size(WIDTH,LESS_THAN,width));
    rows.push_back(paragraph("El estado final se verifica contra una ejecución secuencial, fuera del tiempo medido.")|size(WIDTH,LESS_THAN,width));
    const bool hasMPI=std::any_of(study.configurations.begin(),study.configurations.end(),[](const auto& c){return c.engine==Engine::MPI;});
    if(hasMPI) rows.push_back(paragraph("En MPI, los calentamientos se repiten en cada sesión.")|size(WIDTH,LESS_THAN,width));
    section("QUÉ TIEMPO SE COMPARA");
    const std::array<std::string,3> headers{"Motor","Incluye el tiempo principal","Fuera del tiempo principal"};
    const int engineWidth=14;
    const int includedWidth=(width-engineWidth-4)/2;
    const auto timingLine=[&](const std::array<std::string,3>& cells,bool heading) {
        if(width<78) {
            if(heading) return;
            rows.push_back(text(cells[0])|bold|color(accent));
            rows.push_back(paragraph("Incluye: "+cells[1]));
            rows.push_back(paragraph("Excluye: "+cells[2]));
        } else {
            Elements parts;
            const std::array<int,3> widths{engineWidth,includedWidth,width-engineWidth-includedWidth-4};
            for(std::size_t i=0;i<cells.size();++i) {
                if(i) parts.push_back(text("  "));
                auto cell=paragraph(cells[i])|size(WIDTH,EQUAL,widths[i]);
                parts.push_back(heading?cell|bold|color(accent):cell);
            }
            rows.push_back(hbox(std::move(parts)));
        }
        rows.push_back(text(" "));
    };
    timingLine(headers,true);
    std::set<Engine> describedEngines;
    for(const auto& c:study.configurations) {
        if(!describedEngines.insert(c.engine).second) continue;
        if(c.engine==Engine::CUDA)
            timingLine({motor(c),"Evolución del tablero en GPU y espera hasta terminar.","Preparación, descarga del tablero y verificación."},false);
        else if(c.engine==Engine::MPI)
            timingLine({motor(c),"Cálculo y comunicación. Tiempo del proceso más lento.","Arranque, carga y descarga del tablero, verificación y cierre."},false);
        else timingLine({motor(c),"Ejecución de las generaciones del tablero.","Preparación y verificación."},false);
    }
    rows.push_back(paragraph("El tiempo principal mide la evolución del tablero, no la operación completa.")|color(muted)|size(WIDTH,LESS_THAN,width));
    section("CÓMO LEER LAS MÉTRICAS");
    explanation("Tiempo","Mediana de las repeticiones. Menor es mejor.");
    rows.push_back(text(" "));
    explanation("Aceleración","Tiempo secuencial / tiempo del motor. 2× significa el doble de rapidez en el tiempo medido.");
    rows.push_back(text(" "));
    explanation("Eficiencia",scalingEngine==Engine::CUDA?"No aplica al comparar bloques en una misma GPU.":"Aceleración / hilos o procesos observados, expresada en porcentaje. Requiere recursos estables.");
    rows.push_back(text(" "));
    explanation("—","No aplica. Por ejemplo, la eficiencia de CUDA.");
    rows.push_back(text(" "));
    explanation("N/D","No hay datos suficientes o válidos para calcular la métrica.");
    if(scalingEngine) {
        section("CÓMO LEER LA GRÁFICA");
        explanation("Horizontal",*scalingEngine==Engine::CUDA?"Bloques CUDA como categorías, en una misma GPU.":*scalingEngine==Engine::MPI?"Procesos solicitados, con separación proporcional a sus valores.":"Hilos solicitados, con separación proporcional a sus valores.");
        rows.push_back(text(" "));
        explanation("Vertical","Aceleración respecto a Secuencial. La línea 1× indica igual tiempo.");
        rows.push_back(text(" "));
        explanation("Puntos","Configuraciones medidas. Verde indica la configuración con menor tiempo observado.");
        rows.push_back(text(" "));
        rows.push_back(paragraph(*scalingEngine==Engine::CUDA?"Cambiar el bloque no aumenta los recursos físicos de la GPU.":"Más procesos o hilos no garantizan menor tiempo.")|color(muted)|size(WIDTH,LESS_THAN,width));
    }
    if(details) {
        section("DETALLES TÉCNICOS");
        studyMethodDetails(rows,study,accent);
        if(scalingEngine) {
            rows.push_back(paragraph("Solo se dibujan aceleraciones válidas; N/D no equivale a cero. Puntos sin interpolación ni línea ideal."));
            rows.push_back(paragraph("La escala vertical usa log1p(S) solo en rangos extremos. La referencia 1× se aproxima a la fila disponible; la tabla conserva los valores exactos."));
            rows.push_back(paragraph("La conclusión usa la menor mediana válida; los empates conservan la primera configuración ordenada."));
        }
        rows.push_back(text(" "));
        rows.push_back(paragraph("Limpieza confirmada: "+yes(study.cleanupConfirmed))|color(study.cleanupConfirmed?positive:failure));
    }
    return rows;
}
std::vector<ftxui::Element> studySampleRows(const ExperimentalStudy& study,int columns,
    ftxui::Color accent,ftxui::Color positive,bool details,std::optional<Engine> scalingEngine) {
    using namespace ftxui;
    const auto muted=Color::RGB(150,160,170);
    const auto caution=Color::RGB(224,185,112);
    const auto failure=Color::RGB(235,135,135);
    Elements rows;
    rows.push_back(hbox({text("Resumen   ")|color(accent),text("[ Muestras ]")|bold|color(accent),text("   Método")|color(accent)}));
    const auto section=[&](const std::string& title) {
        rows.push_back(text(" "));
        rows.push_back(text(title)|bold|color(accent));
        rows.push_back(text(" "));
    };
    rows.push_back(text(study.complete?"Mediciones completadas":"ESTUDIO INCOMPLETO")|bold|color(study.complete?positive:caution));
    if(!study.cleanupConfirmed) rows.push_back(paragraph("No se confirmó la liberación de recursos.")|color(failure));
    if(!study.diagnostic.empty()) rows.push_back(paragraph("Diagnóstico: "+study.diagnostic)|color(failure));
    rows.push_back(paragraph(details?"D Ocultar detalles técnicos":"D Ver detalles técnicos")|color(muted));
    section(scalingEngine?"ESTADÍSTICAS POR CONFIGURACIÓN (ms)":"ESTADÍSTICAS POR MOTOR (ms)");
    std::vector<std::vector<std::string>> statistics;
    bool invalidSummary=false;
    for(const auto& s:study.summaries) {
        const auto& c=study.configurations.at(s.configurationIndex);
        const auto& v=s.statistics;
        statistics.push_back({(scalingEngine?configurationLabel(c):motor(c))+(s.valid?"":" (no válido)"),v?number(v->medianMs):"N/D",
            v?number(v->meanMs):"N/D",v?number(v->minimumMs):"N/D",v?number(v->maximumMs):"N/D",
            v?number(v->populationStddevMs):"N/D"});
        invalidSummary=invalidSummary || !s.valid;
    }
    sampleTable(rows,{scalingEngine?"Configuración":"Motor","Mediana","Media","Mínimo","Máximo","Desviación"},statistics,columns,accent,positive,
        {false,true,true,true,true,true});
    rows.push_back(text(" "));
    rows.push_back(paragraph("La mediana se usa en Resumen. La desviación indica cuánto varían las mediciones.")|color(muted));
    if(invalidSummary) rows.push_back(paragraph("Las estadísticas no válidas conservan evidencia; no justifican una comparación.")|color(caution));
    section("MEDICIONES INDIVIDUALES (ms)");
    std::vector<std::vector<std::string>> records;
    std::vector<int> rounds;
    bool differentResources=false;
    for(const auto& sample:study.samples) {
        const auto& c=study.configurations.at(sample.configurationIndex);
        std::string configuration=sampleConfiguration(sample,c);
        const auto requested=c.engine==Engine::MPI?c.requestedProcesses:c.requestedThreads;
        const auto low=c.engine==Engine::MPI?sample.observedProcesses:sample.minEffectiveThreads;
        const auto high=c.engine==Engine::MPI?sample.observedProcesses:sample.maxEffectiveThreads;
        if(requested && (!low || !high || *low!=*requested || *high!=*requested)) {
            configuration+=" (solicitados: "+std::to_string(*requested)+")";
            differentResources=true;
        }
        records.push_back({std::to_string(sample.round+1),std::to_string(sample.position+1),motor(c),configuration,
            number(sample.elapsedMs),!sample.equivalent?"No coincide":!sample.valid?"No válido":"Correcto"});
        rounds.push_back(sample.round);
    }
    sampleTable(rows,{"Ronda","Orden","Motor","Configuración observada","Tiempo","Estado"},records,columns,accent,positive,
        {true,true,false,false,true,false},5,rounds);
    if(records.empty()) rows.push_back(paragraph("Todavía no hay muestras registradas.")|color(caution));
    rows.push_back(text(" "));
    rows.push_back(paragraph("Orden real de medición. Los calentamientos no forman parte de las estadísticas.")|color(muted));
    if(differentResources) rows.push_back(paragraph("Se indica lo solicitado cuando difiere de los recursos observados.")|color(caution));
    if(!details) return rows;
    section("DETALLES TÉCNICOS POR MUESTRA");
    for(const auto& sample:study.samples) {
        const auto& c=study.configurations.at(sample.configurationIndex);
        rows.push_back(paragraph(motor(c)+"   Ronda "+std::to_string(sample.round+1)+
            "   Orden "+std::to_string(sample.position+1))|bold|color(accent));
        rows.push_back(paragraph("Identificador original: #"+std::to_string(sample.executionIndex)+
            "   Ronda base cero: "+std::to_string(sample.round)+"   Posición base cero: "+std::to_string(sample.position))|color(muted));
        rows.push_back(paragraph("Equivalencia: "+yes(sample.equivalent)+"   Validez: "+yes(sample.valid)));
        if(c.engine==Engine::CUDA && sample.cuda) {
            const auto& m=*sample.cuda;
            rows.push_back(paragraph("GPU: "+m.deviceName+"   Grid: "+std::to_string(m.gridX)+"×"+std::to_string(m.gridY)));
            rows.push_back(paragraph("Intervalo del stream: "+number(m.deviceMs)+" ms"));
            rows.push_back(paragraph("Preparación: "+number(m.prepareMs)+" ms   Descarga final: "+number(m.snapshotMs)+" ms"));
            rows.push_back(paragraph("Operación completa: "+number(m.totalMs)+" ms"));
        }
        if(c.engine==Engine::MPI) {
            rows.push_back(paragraph("Auxiliares MPI   Carga: "+metric(sample.loadMs," ms")+"   Snapshot: "+metric(sample.snapshotMs," ms")));
            rows.push_back(paragraph("Solicitud del lote (incluye IPC): "+metric(sample.requestMs," ms")));
            rows.push_back(paragraph("No se suma al tiempo principal.")|color(muted));
            rows.push_back(paragraph("Sesión original: "+(sample.sessionIndex?"#"+std::to_string(*sample.sessionIndex):"N/D")+
                "   Calentamientos por sesión: "+std::to_string(sample.completedWarmups)));
        }
        rows.push_back(text(" "));
    }
    if(!study.sessions.empty()) section("SESIONES MPI");
    for(std::size_t i=0;i<study.sessions.size();++i) {
        const auto& s=study.sessions[i];
        rows.push_back(paragraph("Sesión original #"+std::to_string(i)+"   Ronda "+std::to_string(s.round+1))|bold|color(accent));
        const auto& c=study.configurations.at(s.configurationIndex);
        rows.push_back(paragraph(motor(c)+"   Procesos solicitados: "+
            (c.requestedProcesses?std::to_string(*c.requestedProcesses):"N/D")));
        rows.push_back(paragraph("Arranque: "+number(s.startupMs)+" ms   Cierre: "+number(s.closeMs)+" ms"));
        rows.push_back(paragraph("Limpieza confirmada: "+yes(s.cleanupConfirmed))|color(s.cleanupConfirmed?positive:failure));
        rows.push_back(paragraph("Calentamientos completados: "+std::to_string(s.completedWarmups)));
        rows.push_back(paragraph("Runtime: "+s.runtime)|color(muted));
        if(!s.diagnostic.empty()) rows.push_back(paragraph("Diagnóstico de sesión: "+s.diagnostic)|color(failure));
        rows.push_back(text(" "));
    }
    return rows;
}
std::vector<ftxui::Element> studySummaryRows(const ExperimentalStudy& study,const std::string& pattern,
    int columns,ftxui::Color accent,ftxui::Color positive,std::optional<Engine> scalingEngine) {
    using namespace ftxui;
    const auto muted=Color::RGB(150,160,170);
    const auto caution=Color::RGB(224,185,112);
    const auto failure=Color::RGB(235,135,135);
    Elements rows;
    rows.push_back(hbox({text("[ Resumen ]")|bold|color(accent),text("   Muestras   Método")|color(accent)}));
    const auto usable=[&](const StudySummary& s) {
        return study.complete && study.cleanupConfirmed && s.valid && s.equivalent &&
            s.sampleCount==static_cast<std::size_t>(study.repetitions) && s.statistics &&
            std::isfinite(s.statistics->medianMs) && s.statistics->medianMs>=0;
    };
    const bool correct=!study.configurations.empty() && study.summaries.size()==study.configurations.size() &&
        std::all_of(study.summaries.begin(),study.summaries.end(),usable);
    rows.push_back(text(study.complete?(scalingEngine?"Estudio completado":"Comparación completada"):"ESTUDIO INCOMPLETO")|bold|
        color(correct?positive:caution));
    if(correct) rows.push_back(paragraph(scalingEngine?"Todas las configuraciones producen el mismo resultado":"Todos los motores producen el mismo resultado")|color(positive));
    else rows.push_back(paragraph("Revisa el estado de cada motor antes de comparar sus métricas.")|color(caution));
    if(!study.cleanupConfirmed) rows.push_back(paragraph("No se confirmó la liberación de recursos.")|color(failure));
    if(!study.diagnostic.empty()) rows.push_back(paragraph("Diagnóstico: "+study.diagnostic)|color(failure));
    rows.push_back(text(" "));
    rows.push_back(text("ENTRADA")|bold|color(accent));
    rows.push_back(paragraph(pattern));
    rows.push_back(text("Generaciones   "+std::to_string(study.generations)));
    rows.push_back(text("Repeticiones   "+std::to_string(study.repetitions)));
    rows.push_back(text(" "));
    rows.push_back(text(scalingEngine==Engine::MPI?"RESULTADOS MPI":scalingEngine==Engine::CUDA?"RESULTADOS CUDA":scalingEngine?"RESULTADOS OPENMP":"RESULTADOS")|bold|color(accent));

    using Record=std::array<std::string,6>;
    struct Entry {Record cells;bool valid;bool fastest;std::optional<double> speedup;};
    const Record headers{"Motor","Configuración","Tiempo (ms)","Aceleración","Eficiencia","Estado"};
    std::array<int,6> widths{};
    for(std::size_t i=0;i<headers.size();++i) widths[i]=string_width(headers[i]);
    std::optional<double> minimum;
    for(const auto& s:study.summaries) if(usable(s) && (!minimum || s.statistics->medianMs<*minimum)) minimum=s.statistics->medianMs;
    const auto rounded=[](double v,int precision) {
        if(!std::isfinite(v) || v<0) return std::string("N/D");
        if((v>0 && v<0.001) || v>=1e6) return number(v);
        std::ostringstream out;out<<std::fixed<<std::setprecision(precision)<<v;return out.str();
    };
    std::vector<Entry> entries;
    bool differingResources=false;
    for(const auto& s:study.summaries) {
        if(s.configurationIndex>=study.configurations.size()) continue;
        const auto& c=study.configurations[s.configurationIndex];
        const bool valid=usable(s);
        const bool reference=c.engine==Engine::Sequential;
        std::string engine=reference?"Secuencial":c.engine==Engine::OpenMP?"OpenMP":c.engine==Engine::MPI?"MPI":"CUDA";
        std::string configuration;
        if(c.engine==Engine::CUDA) {
            configuration=c.cudaBlock?"Bloque "+std::to_string(c.cudaBlock->x)+"×"+std::to_string(c.cudaBlock->y):"N/D";
        } else {
            configuration=resources(s.minimumResources,s.maximumResources)+
                (c.engine==Engine::MPI?unit(s.minimumResources==s.maximumResources?s.minimumResources.value_or(0):0," proceso"," procesos"):
                    unit(s.minimumResources==s.maximumResources?s.minimumResources.value_or(0):0," hilo"," hilos"));
            const auto requested=c.engine==Engine::MPI?c.requestedProcesses:c.requestedThreads;
            if(requested && (!s.minimumResources || !s.maximumResources || *s.minimumResources!=*requested || *s.maximumResources!=*requested)) {
                configuration+=" (solicitados: "+std::to_string(*requested)+")";
                differingResources=true;
            }
        }
        const auto speed=valid?s.speedup:std::nullopt;
        std::string state;
        if(s.sampleCount==0) state="Sin muestras";
        else if(!s.equivalent) state="No coincide";
        else if(s.sampleCount!=static_cast<std::size_t>(study.repetitions))
            state="Incompleto "+std::to_string(s.sampleCount)+"/"+std::to_string(study.repetitions);
        else state=valid?"Correcto":"No válido";
        Record cells{engine,configuration,s.statistics?number(s.statistics->medianMs):"N/D",
            reference?(valid?"Referencia":"N/D"):speed?rounded(*speed,2)+"×":"N/D",
            (reference || c.engine==Engine::CUDA)?"—":valid && s.efficiency?rounded(*s.efficiency*100,1)+" %":"N/D",state};
        for(std::size_t i=0;i<cells.size();++i) widths[i]=std::max(widths[i],string_width(cells[i]));
        entries.push_back({std::move(cells),valid,valid && minimum && s.statistics->medianMs==*minimum,speed});
    }
    const auto metricColor=[&](const Entry& entry,std::size_t column) {
        if(entry.cells[column]=="—" || entry.cells[column]=="Referencia") return muted;
        if(entry.cells[column]=="N/D") return caution;
        if(column==5) return entry.valid?positive:failure;
        if(column==2 && entry.fastest) return positive;
        if(column==3 && entry.speedup) return *entry.speedup>1?positive:*entry.speedup<1?caution:muted;
        return Color(Color::Default);
    };
    int tableWidth=10; // Two spaces between the six columns.
    for(int width:widths) tableWidth+=width;
    if(tableWidth<=std::max(1,columns-2)) {
        const auto line=[&](const Record& cells,const Entry* entry) {
            Elements parts;
            for(std::size_t i=0;i<cells.size();++i) {
                if(i) parts.push_back(text("  "));
                const auto padding=std::string(static_cast<std::size_t>(std::max(0,widths[i]-string_width(cells[i]))),' ');
                const bool numeric=i>=2 && i<=4;
                auto cell=text(numeric?padding+cells[i]:cells[i]+padding);
                parts.push_back(entry?cell|color(metricColor(*entry,i)):cell|bold|color(accent));
            }
            rows.push_back(hbox(std::move(parts)));
        };
        rows.push_back(text(" "));
        line(headers,nullptr);
        rows.push_back(separator()|color(muted)|size(WIDTH,EQUAL,tableWidth));
        for(const auto& entry:entries) {line(entry.cells,&entry);rows.push_back(text(" "));}
    } else {
        for(const auto& entry:entries) {
            rows.push_back(text(" "));
            rows.push_back(text(entry.cells[0])|bold|color(accent));
            for(std::size_t i=1;i<entry.cells.size();++i)
                rows.push_back(paragraph(headers[i]+": "+entry.cells[i])|color(metricColor(entry,i)));
        }
        rows.push_back(text(" "));
    }
    if(entries.empty()) rows.push_back(paragraph("Todavía no hay mediciones disponibles.")|color(caution));
    rows.push_back(paragraph("Tiempo mediano del lote. Menor tiempo es mejor.")|color(muted));
    rows.push_back(paragraph("Aceleración frente a Secuencial. — No aplica.")|color(muted));
    const bool reference=std::any_of(study.configurations.begin(),study.configurations.end(),[](const auto& c){return c.engine==Engine::Sequential;});
    if(!reference) rows.push_back(paragraph("Sin participante Secuencial: aceleración y eficiencia N/D.")|color(caution));
    if(differingResources) rows.push_back(paragraph("Configuración muestra los recursos observados y los solicitados cuando difieren.")|color(caution));
    return rows;
}
}
