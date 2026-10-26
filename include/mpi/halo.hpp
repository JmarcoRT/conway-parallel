#pragma once

#include "mpi/partition.hpp"

namespace conway::mpi {

// Vecinos de halo de un rank en la particion horizontal.
//
// Como partition.hpp, esta cabecera NO incluye <mpi.h>: es aritmetica pura
// sobre RowPartition y se prueba sin mpirun. El motor MPI
// (core/engines/mpi.hpp) traduce kNoNeighbor a MPI_PROC_NULL.
//
//   up   = rank que posee la fila global firstRow - 1. Sus filas propias
//          llenan el halo superior (fila local 0) de este rank.
//   down = rank que posee la fila global endRow. Llena el halo inferior
//          (fila local height + 1).
//
// kNoNeighbor significa que el halo es borde global: conserva 0 (borde muerto
// fijo, no periodico, igual que GameState en el motor secuencial).
// Un rank sin filas no tiene vecinos ni es vecino de nadie: el ultimo rank
// con filas tiene down = kNoNeighbor aunque existan ranks vacios despues.
inline constexpr int kNoNeighbor = -1;

struct HaloNeighbors {
    int up{kNoNeighbor};
    int down{kNoNeighbor};

    bool hasUp()   const noexcept { return up != kNoNeighbor; }
    bool hasDown() const noexcept { return down != kNoNeighbor; }
};

// Funcion pura de (globalHeight, size, rank). Relacion simetrica: si
// halo_neighbors(A).down == B, entonces halo_neighbors(B).up == A.
HaloNeighbors halo_neighbors(const RowPartition& part);

} // namespace conway::mpi
