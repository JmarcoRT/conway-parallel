#pragma once

#include "core/game_state.hpp"
#include "input/input.hpp"
#include "mpi/partition.hpp"

#include <cstdint>
#include <vector>

namespace conway::mpi {

// Construye el core::GameState LOCAL de un rank a partir de la entrada global.
//
// Reutiliza la representacion comun (no hay core alternativo), tal como
// describe docs/ARCHITECTURE.md para MPI:
//   width  = ancho global
//   height = filas propias del rank (part.rowCount)
//   stride = width + 2
//   current/next = stride * (height + 2)   (1 fila halo arriba y 1 abajo)
// Las filas propias ocupan las filas locales 1..height de `current`. Las filas
// halo (0 y height+1) y las columnas halo quedan a 0: en esta etapa NO hay
// intercambio de halos; el borde global ya es correcto (halo = 0) y las
// fronteras entre ranks se rellenaran en la etapa de evolucion MPI.
// aliveCount = celulas vivas de las filas propias; generation = 0.
//
// Una particion vacia (rowCount == 0) produce un GameState con height = 0.
// Lanza std::invalid_argument si la particion no corresponde a la entrada.
core::GameState make_local_state(const InputData& input,
                                 const RowPartition& part);

// Variante usada por el flujo MPI (Scatterv): construye el GameState local a
// partir de las filas propias ya recibidas, empaquetadas row-major (sin halo),
// de tamano exacto width * part.rowCount. Es la unica rutina que arma el estado
// local; make_local_state solo extrae las filas de la entrada y delega aqui.
// Lanza std::invalid_argument si el tamano de `rows` no es el esperado.
core::GameState make_local_state_from_rows(std::size_t width,
                                           const RowPartition& part,
                                           const std::vector<std::uint8_t>& rows);

// Copia las filas propias (sin halo) a un vector contiguo row-major de
// width * height bytes. Sirve para reensamblar el tablero global.
std::vector<std::uint8_t> pack_local_rows(const core::GameState& s);

} // namespace conway::mpi
