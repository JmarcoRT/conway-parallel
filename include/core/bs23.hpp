#pragma once

#include <cstddef>
#include <cstdint>

namespace conway::core {

// Aplica la regla B3/S23 a UNA sola celula en posicion logica (row, col).
// Lee las 8 vecinas de `current` usando `stride`. NO modifica `current`.
// NO realiza E/S. NO bloquea. NO asume nada sobre el recorrido.
//
// Cada motor (secuencial, OpenMP, Pthreads, MPI) invoca esta funcion
// dentro de SU propio recorrido. La regla vive aqui, una sola vez.
//
// `row` en [1, height]; `col` en [1, width] (coordenadas logicas, no de buffer).
// Indice en buffer (mismo que la implementacion previa de Game::step):
//   i          = row * stride + col
//   i-stride-1, i-stride, i-stride+1       (noroeste, norte, noreste)
//   i-1,                            i+1    (oeste, este)
//   i+stride-1, i+stride, i+stride+1      (suroeste, sur, sureste)
// El halo del buffer (cuyo valor logico es siempre 0) participa como vecino.
std::uint8_t next_cell_state(
    const std::uint8_t* current,
    std::size_t stride,
    std::size_t row,
    std::size_t col);

} // namespace conway::core
