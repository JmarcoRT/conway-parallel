#pragma once
#include "app/didactic_session.hpp"
#include <algorithm>
#include <string>
#include <vector>

namespace conway::app::didactic_view {
using Rows=std::vector<std::string>;
inline std::string bits(const std::vector<std::uint8_t>& v,std::size_t begin=0,std::size_t count=std::string::npos) {
    std::string out;
    const auto end=begin+std::min(count,v.size()-std::min(begin,v.size()));
    for(auto i=begin;i<end;++i) out+=v[i]?'1':'0';
    return out;
}
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
inline Rows global(const mpi::MpiStepTrace& t,bool result) {
    Rows rows{"GLOBAL · Reparto actual (filas base 0)","Memoria separada; no hay reparto nuevo en cada paso.","1 viva / 0 muerta · borde global fijo a 0"};
    for(const auto& r:t.ranks) {
        const auto& p=r.partition;
        rows.push_back("R"+std::to_string(p.rank)+" propias ["+std::to_string(p.firstRow)+","+std::to_string(p.endRow())+")"+(p.rank==0?" · también calcula":"")+(p.empty()?" · vacío":""));
        const auto& b=result?r.currentAfter:r.currentWithHalos;
        for(std::size_t y=0;y<p.rowCount;++y)
            rows.push_back("R"+std::to_string(p.rank)+" fila "+std::to_string(p.firstRow+y)+" | "+bits(b,(y+1)*(t.width+2)+1,t.width));
    }
    rows.push_back("Población global: "+std::to_string(result?t.aliveAfter:t.aliveBefore));
    return rows;
}
inline Rows local(const mpi::MpiStepTrace& t,std::size_t rank,bool swapped) {
    const auto& r=t.ranks.at(rank);const auto& p=r.partition;
    Rows rows{"R"+std::to_string(p.rank)+" · memoria privada"};
    if(p.rank==0)rows.push_back("Rank 0 coordina Y calcula su franja.");
    if(p.empty()) {rows.push_back("Sin filas propias; no calcula ni intercambia halos.");rows.push_back("Participa en operaciones colectivas y resultado global.");return rows;}
    rows.push_back("Propias globales ["+std::to_string(p.firstRow)+","+std::to_string(p.endRow())+")");
    auto buffer=[&](const std::string& name,const auto& b,bool validHalos) {
        rows.push_back(name);
        for(std::size_t y=0;y<p.rowCount+2;++y) {
            auto label=y==0?"HALO superior":y==p.rowCount+1?"HALO inferior":"PROPIA g"+std::to_string(p.firstRow+y-1);
            if(y==0||y==p.rowCount+1)label+=validHalos?" recibido/borde":" NO vigente";
            rows.push_back(label+" | "+bits(b,y*(t.width+2),t.width+2));
        }
    };
    if(swapped) {
        buffer("CURRENT nuevo = NEXT calculado (tras swap)",r.currentAfter,false);
        buffer("NEXT nuevo = CURRENT anterior (reutilizable)",r.currentWithHalos,false);
    } else {
        buffer("CURRENT antes del intercambio (halos antiguos)",r.currentBefore,false);
        buffer("CURRENT usado para calcular (halos vigentes)",r.currentWithHalos,true);
        buffer("NEXT calculado, antes del swap",r.currentAfter,false);
    }
    rows.push_back("Swap capturado: "+std::string(r.buffersSwapped?"sí":"no"));
    rows.push_back("Población propia: "+std::to_string(r.aliveBefore)+" -> "+std::to_string(r.aliveAfter));
    return rows;
}
inline Rows explanation(const mpi::MpiStepTrace& t,std::size_t rank,DidacticStage s) {
    const auto& r=t.ranks.at(rank);
    Rows rows{"EXPLICACIÓN · "+stage(s),"Captura MPI real; orden de presentación didáctico","No implica una cronología global entre procesos.","Reparto actual: contexto; Scatterv no ocurre en cada generación."};
    switch(s) {
    case DidacticStage::MPIState:rows.push_back("Cada proceso conserva únicamente su franja y dos halos, en memoria privada.");break;
    case DidacticStage::MPIExchangeFirst:rows.push_back("Foco: primera llamada Sendrecv registrada en cada rank con filas.");break;
    case DidacticStage::MPIExchangeSecond:rows.push_back("Foco: segunda llamada Sendrecv; CURRENT ya dispone de ambos halos.");break;
    case DidacticStage::MPICell:rows.push_back("Foco: evaluación real de una célula; consulta abajo sus ocho vecinos.");break;
    case DidacticStage::MPIBuffers:rows.push_back("Foco: NEXT calculado pasa a ser CURRENT; el buffer anterior se reutiliza.");break;
    case DidacticStage::GenerationResult:rows.push_back("Foco: generación resultante y población global capturada.");break;
    default:break;
    }
    rows.push_back("R"+std::to_string(rank)+" · intercambios reales en orden local:");
    for(std::size_t i=0;i<r.exchanges.size();++i) {
        const auto& e=r.exchanges[i];
        rows.push_back("Intercambio "+std::to_string(i+1)+" · tag "+std::to_string(e.tag));
        rows.push_back("ENVÍA R"+std::to_string(rank)+" -> "+(e.sendTo==mpi::noPeer?"borde (MPI_PROC_NULL)":"R"+std::to_string(e.sendTo))+(e.sentGlobalRow?" · fila global "+std::to_string(*e.sentGlobalRow):""));
        rows.push_back("Contenido enviado: "+(e.sent.empty()?"(sin fila)":bits(e.sent)));
        rows.push_back("RECIBE "+(e.receivedFrom==mpi::noPeer?std::string("borde (MPI_PROC_NULL)"):"R"+std::to_string(e.receivedFrom))+" -> R"+std::to_string(rank)+" · halo local "+std::to_string(e.receiveLocalRow)+(e.receivedGlobalRow?" · fila global "+std::to_string(*e.receivedGlobalRow):""));
        rows.push_back("Contenido recibido: "+(e.received.empty()?"(sin fila)":bits(e.received)));
        if(e.receivedFrom==mpi::noPeer)rows.push_back("Sin mensaje de borde; halo fijado a cero.");
    }
    if(r.partition.empty())rows.push_back("Rank vacío: sin Sendrecv ni célula propia.");
    rows.push_back("Célula seleccionada global ("+std::to_string(t.selection.row)+","+std::to_string(t.selection.column)+")");
    if(r.cell && r.cell->captured) {
        const auto& c=*r.cell;const auto& e=c.evaluation;
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
            rows.push_back(std::string(names[i])+"="+std::to_string(e.neighbors[i])+" · "+source);
        }
        rows.push_back("Regla B3/S23: antes "+std::to_string(e.before)+" · vecinos "+std::to_string(e.neighborCount)+" -> "+std::to_string(e.after));
        rows.push_back("Valor leído de CURRENT; resultado escrito en NEXT.");
    } else rows.push_back("La célula pertenece a otro rank; selecciónalo con [ / ].");
    rows.push_back("Doble buffer: todas las lecturas usan CURRENT con halos; las escrituras usan NEXT.");
    rows.push_back("Tras calcular se intercambian buffers; los halos nuevos requieren actualización en el siguiente paso.");
    rows.push_back("Resultado generación "+std::to_string(t.generationAfter)+" · población global "+std::to_string(t.aliveAfter));
    return rows;
}
}
