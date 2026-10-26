#include "app/experimental_study_view.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

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
std::string name(const ExecutionConfiguration& c) {
    if(c.engine==Engine::Sequential) return "Secuencial · referencia";
    if(c.engine==Engine::OpenMP) return "OpenMP · "+unit(*c.requestedThreads,"hilo solicitado: ","hilos solicitados: ")+std::to_string(*c.requestedThreads);
    return "MPI · "+unit(*c.requestedProcesses,"proceso solicitado: ","procesos solicitados: ")+std::to_string(*c.requestedProcesses);
}
std::string yes(bool b) {return b?"SI":"NO";}
}
std::vector<ftxui::Element> experimentalStudyRows(const ExperimentalStudy& study,const std::string& pattern,
    int tab,int columns,ftxui::Color accent,ftxui::Color positive) {
    using namespace ftxui;
    Elements rows;
    auto section=[&](std::string title){rows.push_back(text(std::move(title))|bold|color(accent));};
    auto diagnostic=[&](const std::string& label,const std::string& detail) {
        rows.push_back(paragraph(label)|color(Color::Red));
        std::istringstream words(detail);std::string word,line;
        const auto width=static_cast<std::size_t>(std::max(16,columns-4));
        while(words>>word) {
            if(!line.empty() && line.size()+word.size()+1>width) {rows.push_back(paragraph(line)|color(Color::Red));line.clear();}
            if(!line.empty()) line+=' ';line+=word;
        }
        if(!line.empty()) rows.push_back(paragraph(line)|color(Color::Red));
    };
    rows.push_back(text(std::string(tab==0?"[Resumen]":"Resumen")+" · "+(tab==1?"[Muestras]":"Muestras")+" · "+(tab==2?"[Método]":"Método"))|color(accent));
    rows.push_back(paragraph(std::string(study.complete?"Estudio completo":"ESTUDIO INCOMPLETO")+
        " · Limpieza confirmada: "+yes(study.cleanupConfirmed))|color(study.complete && study.cleanupConfirmed?positive:Color::Yellow));
    if(!study.diagnostic.empty()) diagnostic("Diagnóstico:",study.diagnostic);
    const bool sequential=std::any_of(study.configurations.begin(),study.configurations.end(),[](const auto& c){return c.engine==Engine::Sequential;});
    const bool hasMPI=std::any_of(study.configurations.begin(),study.configurations.end(),[](const auto& c){return c.engine==Engine::MPI;});
    if(!sequential) rows.push_back(paragraph("Sin participante Secuencial: speedup y eficiencia respecto de él son N/D. El oráculo solo verifica equivalencia.")|dim);
    if(tab==0) {
        section("IDENTIFICACIÓN");
        rows.push_back(paragraph("Entrada: "+pattern+" · Generaciones: "+std::to_string(study.generations)+
            " · Repeticiones: "+std::to_string(study.repetitions)));
        rows.push_back(paragraph("Calentamientos: "+std::to_string(study.warmups)+(hasMPI?" por configuración local; por sesión MPI nueva antes de cada muestra.":" por configuración local."))|dim);
        section("TIEMPO PRINCIPAL · mediana del lote en ms");
        double largest=0;
        for(const auto& s:study.summaries) if(s.statistics && std::isfinite(s.statistics->medianMs)) largest=std::max(largest,s.statistics->medianMs);
        for(const auto& s:study.summaries) {
            const auto& c=study.configurations[s.configurationIndex];
            rows.push_back(paragraph(name(c))|bold);
            rows.push_back(paragraph("Observados: "+resources(s.minimumResources,s.maximumResources)+
                (c.engine==Engine::MPI?unit(s.minimumResources==s.maximumResources?s.minimumResources.value_or(0):0," proceso"," procesos"):unit(s.minimumResources==s.maximumResources?s.minimumResources.value_or(0):0," hilo"," hilos"))+" · Muestras: "+std::to_string(s.sampleCount)+"/"+std::to_string(study.repetitions)));
            rows.push_back(paragraph("Mediana: "+metric(s.statistics?std::optional<double>(s.statistics->medianMs):std::nullopt," ms")+" · "+
                (c.engine==Engine::MPI?"máximo entre ranks":"tiempo local del lote")));
            if(columns>=60 && s.valid && s.statistics && largest>0) {
                const auto length=static_cast<int>(s.statistics->medianMs/largest*std::min(40,columns-4));
                std::string bar;for(int i=0;i<length;++i) bar+="█";
                rows.push_back(text(bar)|color(accent));
            }
            rows.push_back(paragraph("Speedup: "+metric(s.speedup,"×")+" · Eficiencia: "+metric(s.efficiency," %",100)));
            rows.push_back(paragraph("Equivalencia: "+yes(s.equivalent)+" · Validez: "+yes(s.valid))|color(s.valid?positive:Color::Yellow));
        }
        rows.push_back(paragraph("N/D: referencia ausente/inválida, equivalencia fallida, estudio incompleto, recursos variables o mediana cero. Las diferencias no prueban significancia estadística.")|dim);
    } else if(tab==1) {
        section("ESTADÍSTICAS DESCRIPTIVAS · ms");
        for(const auto& s:study.summaries) {
            rows.push_back(paragraph(name(study.configurations[s.configurationIndex]))|bold);
            const auto& v=s.statistics;
            rows.push_back(paragraph("Mediana: "+(v?number(v->medianMs):"N/D")+" · Media: "+(v?number(v->meanMs):"N/D")));
            rows.push_back(paragraph("Mínimo: "+(v?number(v->minimumMs):"N/D")+" · Máximo: "+(v?number(v->maximumMs):"N/D")));
            rows.push_back(paragraph("Desviación estándar poblacional: "+(v?number(v->populationStddevMs):"N/D")));
        }
        section("MUESTRAS · orden real de adquisición");
        for(const auto& s:study.samples) {
            const auto& c=study.configurations[s.configurationIndex];
            rows.push_back(paragraph("#"+std::to_string(s.executionIndex)+" · Ronda "+std::to_string(s.round)+
                " · Posición "+std::to_string(s.position)+" · "+name(c))|bold);
            rows.push_back(paragraph("Principal: "+number(s.elapsedMs)+" ms · Observados: "+
                (c.engine==Engine::MPI?(s.observedProcesses?std::to_string(*s.observedProcesses)+unit(*s.observedProcesses," proceso"," procesos"):"N/D"):resources(s.minEffectiveThreads,s.maxEffectiveThreads)+unit(s.minEffectiveThreads==s.maxEffectiveThreads?s.minEffectiveThreads.value_or(0):0," hilo"," hilos"))));
            rows.push_back(paragraph("Equivalencia: "+yes(s.equivalent)+" · Validez de muestra: "+yes(s.valid)));
            if(c.engine==Engine::MPI) {
                rows.push_back(paragraph("Auxiliares MPI · Carga: "+metric(s.loadMs," ms")+" · Snapshot: "+metric(s.snapshotMs," ms")));
                rows.push_back(paragraph("Solicitud del lote (incluye IPC): "+metric(s.requestMs," ms")+"; no se suma al principal."));
                rows.push_back(paragraph("Sesión #"+std::to_string(*s.sessionIndex)+" · Calentamientos por sesión: "+std::to_string(s.completedWarmups)));
            }
        }
        if(hasMPI) section("SESIONES MPI · arranque y cierre separados");
        for(std::size_t i=0;i<study.sessions.size();++i) {
            const auto& s=study.sessions[i];
            rows.push_back(paragraph("Sesión #"+std::to_string(i)+" · "+name(study.configurations[s.configurationIndex])+" · Ronda "+std::to_string(s.round)));
            rows.push_back(paragraph("Arranque: "+number(s.startupMs)+" ms · Cierre: "+number(s.closeMs)+" ms · Limpieza: "+yes(s.cleanupConfirmed)));
            rows.push_back(paragraph("Calentamientos completados: "+std::to_string(s.completedWarmups)+" · Runtime: "+s.runtime));
            if(!s.diagnostic.empty()) diagnostic("Diagnóstico de sesión:",s.diagnostic);
        }
    } else {
        section("TIEMPO PRINCIPAL");
        rows.push_back(paragraph("Local: lote completo de Game::step y observación del equipo efectivo. Excluye construcción y comparación."));
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
        rows.push_back(paragraph("Mínimo, máximo, media, mediana y desviación estándar poblacional (divisor N). Muestras inválidas y fallos conservan evidencia; no justifican métricas válidas."));
    }
    return rows;
}
}
