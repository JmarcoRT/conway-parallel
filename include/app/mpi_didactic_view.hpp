#pragma once
#include "app/didactic_session.hpp"
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace conway::app::didactic_view {
// Presentation-only data. No evolution, communication or controller state here.
struct Grid {
    std::size_t width{},height{};
    std::vector<std::uint8_t> values,known,emphasis;
    std::vector<int> owners;
    std::vector<std::string> labels;
    // Presentation border: 0 own/message, 1 halo, 2 fixed global edge.
    std::vector<std::uint8_t> rowKinds;
};
struct Block {std::string text;std::optional<Grid> grid;};
using Blocks=std::vector<Block>;
inline void text(Blocks& out,std::string value) {out.push_back({std::move(value),{}});}
inline void grid(Blocks& out,std::string title,Grid value) {out.push_back({std::move(title),std::move(value)});}
inline std::string stage(DidacticStage s) {
    switch(s) {
    case DidacticStage::MPIState:return "Reparto actual";
    case DidacticStage::MPIExchangeFirst:return "Intercambio 1";
    case DidacticStage::MPIExchangeSecond:return "Intercambio 2";
    case DidacticStage::MPICell:return "Célula y vecinos";
    case DidacticStage::MPIBuffers:return "Cambio de buffers";
    case DidacticStage::GenerationResult:return "Resultado";
    default:return "Trabajo local";
    }
}
inline int exchangesVisible(DidacticStage s) {
    return s==DidacticStage::MPIState?0:s==DidacticStage::MPIExchangeFirst?1:2;
}
inline Grid blank(std::size_t w,std::size_t h,int owner=-1) {
    return {w,h,std::vector<std::uint8_t>(w*h),std::vector<std::uint8_t>(w*h),
        std::vector<std::uint8_t>(w*h),std::vector<int>(w*h,owner),{},std::vector<std::uint8_t>(h)};
}
inline void highlight(Grid& g,std::size_t row,std::size_t column) {
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
        const auto y=static_cast<long long>(row)+dy,x=static_cast<long long>(column)+dx;
        if(y>=0&&x>=0&&y<static_cast<long long>(g.height)&&x<static_cast<long long>(g.width))
            g.emphasis[y*g.width+x]=dy==0&&dx==0?2:1;
    }
}
inline Blocks global(const mpi::MpiStepTrace& t,DidacticStage s) {
    Blocks out;const bool result=s==DidacticStage::GenerationResult;
    auto g=blank(t.width,t.height);
    for(const auto& r:t.ranks) {
        const auto& p=r.partition;const auto& buffer=result?r.currentAfter:r.currentBefore;
        for(std::size_t y=0;y<p.rowCount;++y) {
            g.labels.push_back("R"+std::to_string(p.rank)+" g"+std::to_string(p.firstRow+y));
            for(std::size_t x=0;x<t.width;++x) {
                const auto i=(p.firstRow+y)*t.width+x;
                g.values[i]=buffer[(y+1)*(t.width+2)+x+1];g.known[i]=1;g.owners[i]=p.rank;
            }
        }
    }
    if(s==DidacticStage::MPICell)highlight(g,t.selection.row,t.selection.column);
    grid(out,"GLOBAL G"+std::to_string(result?t.generationAfter:t.generationBefore)+" · captura reconstruida",std::move(g));
    text(out,"Reparto actual; vista reunida para el usuario, no memoria compartida.");
    for(const auto& r:t.ranks)text(out,"R"+std::to_string(r.partition.rank)+" · filas ["+std::to_string(r.partition.firstRow)+","+std::to_string(r.partition.endRow())+")"+(r.partition.rank==0?" · también calcula":"")+(r.partition.empty()?" · vacío":""));
    text(out,"Población global: "+std::to_string(result?t.aliveAfter:t.aliveBefore));
    return out;
}
inline Blocks local(const mpi::MpiStepTrace& t,std::size_t rank,DidacticStage s) {
    const auto& r=t.ranks.at(rank);const auto& p=r.partition;Blocks out;
    text(out,"R"+std::to_string(rank)+" · memoria privada · cálculo secuencial local");
    if(p.empty()) {text(out,"Sin filas propias; sin cálculo local ni Sendrecv. Participa en colectivas.");return out;}
    const bool result=s==DidacticStage::GenerationResult;
    auto g=blank(t.width,p.rowCount+2,static_cast<int>(rank));
    g.rowKinds.front()=p.firstRow==0?2:1;
    g.rowKinds.back()=p.endRow()==t.height?2:1;
    const auto& buffer=result?r.currentAfter:r.currentBefore;
    // Own rows are snapshots. Halos are unavailable until their presentation stage.
    for(std::size_t y=1;y<=p.rowCount;++y)for(std::size_t x=0;x<t.width;++x) {
        const auto i=y*t.width+x;g.values[i]=buffer[y*(t.width+2)+x+1];g.known[i]=1;
    }
    std::string top="H↑ no vigente",bottom="H↓ no vigente";
    if(!result && exchangesVisible(s)>0) {
        for(std::size_t i=0;i<r.exchanges.size();++i) {
            const auto& e=r.exchanges[i];const auto y=e.receiveLocalRow;
            if(i>=static_cast<std::size_t>(exchangesVisible(s)) && e.receivedFrom!=mpi::noPeer)continue;
            for(std::size_t x=0;x<t.width;++x) {
                const auto index=y*t.width+x;
                g.values[index]=e.receivedFrom==mpi::noPeer?0:e.received[x];g.known[index]=1;g.owners[index]=e.receivedFrom;
            }
            (y==0?top:bottom)=e.receivedFrom==mpi::noPeer?"H borde fijo 0":"H de R"+std::to_string(e.receivedFrom)+" g"+std::to_string(*e.receivedGlobalRow);
        }
    }
    g.labels.push_back(top);
    for(std::size_t y=0;y<p.rowCount;++y)g.labels.push_back("R"+std::to_string(rank)+" g"+std::to_string(p.firstRow+y));
    g.labels.push_back(bottom);
    if(s==DidacticStage::MPICell && r.cell && r.cell->captured)highlight(g,r.cell->row,r.cell->column-1);
    grid(out,std::string(result?"CURRENT nuevo G":"CURRENT G")+std::to_string(result?t.generationAfter:t.generationBefore)+(s==DidacticStage::MPIExchangeFirst?" · composición didáctica":" · captura real"),std::move(g));
    text(out,s==DidacticStage::MPIExchangeFirst?"Composición didáctica: snapshot previo + primer mensaje recibido; no es un snapshot intermedio.":
        result?"Snapshot real tras swap. Halos no vigentes para esta generación.":
        exchangesVisible(s)==2?"Valores de CURRENT con halos de la captura real; bordes laterales fijos a 0.":"Snapshot previo real; halos antiguos ocultos. Bordes laterales fijos a 0.");
    out.back().text+=" Bordes: continuo propias; discontinuo halo; punteado borde global.";

    if(s==DidacticStage::MPICell || s==DidacticStage::MPIBuffers) {
        auto next=blank(t.width,p.rowCount,static_cast<int>(rank));
        for(std::size_t y=0;y<p.rowCount;++y) {
            next.labels.push_back("R"+std::to_string(rank)+" g"+std::to_string(p.firstRow+y));
            for(std::size_t x=0;x<t.width;++x) {
                const auto i=y*t.width+x;
                next.values[i]=r.currentAfter[(y+1)*(t.width+2)+x+1];
                next.known[i]=s==DidacticStage::MPIBuffers || (r.cell && r.cell->captured && r.cell->row==y+1 && r.cell->column==x+1);
            }
        }
        grid(out,s==DidacticStage::MPICell?"NEXT · solo célula evaluada":"NEXT calculado G"+std::to_string(t.generationAfter),std::move(next));
    }
    if(s==DidacticStage::MPIBuffers || result)text(out,"Swap capturado: "+std::string(r.buffersSwapped?"sí":"no")+" · NEXT calculado pasa a CURRENT; CURRENT anterior se reutiliza como NEXT.");
    if(result)text(out,"Población propia: "+std::to_string(r.aliveAfter));
    return out;
}
inline Blocks explanation(const mpi::MpiStepTrace& t,std::size_t rank,DidacticStage s) {
    Blocks out;const auto& r=t.ranks.at(rank);
    text(out,"EXPLICACIÓN · "+stage(s));
    text(out,"Progreso de la explicación; no porcentaje de ejecución observado.");
    if(s==DidacticStage::MPIState) {
        text(out,"Cada rank conserva su franja en memoria privada. R0 también calcula. Reparto actual es contexto, sin Scatterv en cada generación.");
        return out;
    }
    if(s==DidacticStage::MPIExchangeFirst || s==DidacticStage::MPIExchangeSecond) {
        const auto call=s==DidacticStage::MPIExchangeFirst?0u:1u;
        text(out,"Llamada local Sendrecv "+std::to_string(call+1)+": cada rank puede enviar a un vecino y recibir de otro. No es un único mensaje global ni una cronología entre ranks.");
        // Every actual directed message once, from the recorded sender. Include
        // all ranks, even when the selected rank is empty or at a global edge.
        for(const auto& sender:t.ranks) {
            if(sender.exchanges.size()<=call)continue;
            const auto& e=sender.exchanges[call];
            if(e.sendTo==mpi::noPeer) {text(out,"R"+std::to_string(sender.partition.rank)+" · envío a borde MPI_PROC_NULL: sin mensaje.");continue;}
            auto strip=blank(t.width,1,sender.partition.rank);strip.values=e.sent;std::fill(strip.known.begin(),strip.known.end(),1);
            strip.labels={"fila g"+std::to_string(*e.sentGlobalRow)};
            grid(out,"ENVÍA R"+std::to_string(sender.partition.rank)+" -> R"+std::to_string(e.sendTo)+" · tag "+std::to_string(e.tag),std::move(strip));
        }
        if(r.partition.empty())text(out,"R"+std::to_string(rank)+": sin filas ni llamadas Sendrecv.");
        else {
            const auto& e=r.exchanges.at(call);
            text(out,e.receivedFrom==mpi::noPeer?"Recepción del rank seleccionado: borde MPI_PROC_NULL; halo fijo a cero.":
                "RECIBE R"+std::to_string(e.receivedFrom)+" -> R"+std::to_string(rank)+" · halo "+(e.receiveLocalRow==0?"superior":"inferior")+" · fila global "+std::to_string(*e.receivedGlobalRow));
        }
        return out;
    }
    if(s==DidacticStage::MPICell) {
        text(out,"Célula seleccionada global ("+std::to_string(t.selection.row)+","+std::to_string(t.selection.column)+") · borde doble: célula; borde grueso: vecinos.");
        if(r.cell && r.cell->captured) {
            const auto& e=r.cell->evaluation;
            static constexpr const char* names[]={"NW","N","NE","W","E","SW","S","SE"};
            static constexpr int dy[]={-1,-1,-1,0,0,1,1,1},dx[]={-1,0,1,-1,1,-1,0,1};
            for(int i=0;i<8;++i) {
                const auto gy=static_cast<long long>(t.selection.row)+dy[i],gx=static_cast<long long>(t.selection.column)+dx[i];
                std::string source="propia R"+std::to_string(rank);
                if(gy<0||gx<0||gy>=static_cast<long long>(t.height)||gx>=static_cast<long long>(t.width))source="borde global = 0";
                else if(gy<static_cast<long long>(r.partition.firstRow)||gy>=static_cast<long long>(r.partition.endRow())) {
                    source="halo recibido";
                    for(const auto& exchange:r.exchanges)if(exchange.receivedGlobalRow && *exchange.receivedGlobalRow==static_cast<std::size_t>(gy))source+=" de R"+std::to_string(exchange.receivedFrom);
                }
                text(out,std::string(names[i])+"="+std::to_string(e.neighbors[i])+" · "+source);
            }
            text(out,"Regla B3/S23 capturada: antes "+std::to_string(e.before)+" · vecinos "+std::to_string(e.neighborCount)+" -> "+std::to_string(e.after));
            text(out,"Lectura de CURRENT; escritura de la célula en NEXT. Otras células de NEXT permanecen ocultas.");
        } else text(out,"Esta célula pertenece a otro rank; selecciónalo con [ / ].");
        return out;
    }
    if(s==DidacticStage::MPIBuffers)text(out,"NEXT completo capturado; swap confirmado. No se observa el orden individual de cálculo de las células.");
    if(s==DidacticStage::GenerationResult)text(out,"Resultado generación "+std::to_string(t.generationAfter)+" · población global "+std::to_string(t.aliveAfter)+". Los halos nuevos requieren actualización en el siguiente paso.");
    return out;
}
}
