#pragma once

#include "core/game_state.hpp"
#include "input/input.hpp"
#include "mpi/partition.hpp"

#include <mpi.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace conway::mpi {

// Reparto y reensamblado del tablero entre ranks (colectivas MPI).
// Mismo flujo que el prototipo de particion: Scatterv al inicio y Gatherv al
// final ("v" porque el reparto puede ser desigual). Las partes puras
// (particion, GameState local, empaquetado) siguen en partition/local_state.

// Lanza std::length_error si width * height no cabe en los conteos int de MPI.
// Debe comprobarse en root ANTES de difundir las dimensiones.
void require_int_counts(std::size_t width, std::size_t height);

// Colectiva. `input` solo se lee en `root`; en el resto puede estar vacio.
// width/height deben ser iguales en todos los ranks (difundidos antes).
// Devuelve el GameState local de este rank (make_local_state_from_rows).
core::GameState scatter_board(const InputData& input, std::size_t width,
                              std::size_t height, int root, MPI_Comm comm);

// Colectiva. Devuelve en `root` el tablero logico row-major (width * height,
// sin halo); en el resto devuelve un vector vacio.
std::vector<std::uint8_t> gather_board(const core::GameState& local,
                                       std::size_t height, int root, MPI_Comm comm);

} // namespace conway::mpi
