#pragma once

#include <cstddef>

namespace conway::mpi {

// Particion horizontal (por bloques de filas contiguas) del tablero logico.
//
// Esta cabecera NO incluye <mpi.h> ni llama a MPI: es aritmetica pura sobre
// (alto_global, numero_de_procesos, rank). Asi se puede probar sin mpirun y
// el core sigue sin depender de MPI (ver docs/ARCHITECTURE.md, seccion 10).
//
// Estrategia (distribucion por bloques, "las sobrantes al principio"):
//   base  = alto_global / size
//   resto = alto_global % size
//   el rank r recibe base + 1 filas si r < resto, y base filas en otro caso.
//   firstRow(r) = r * base + min(r, resto)
// Propiedades: bloques contiguos, en orden de rank, sin huecos ni solapes,
// y la diferencia entre dos ranks cualesquiera es como maximo 1 fila.
// Si size > alto_global, los ultimos ranks reciben 0 filas (particion vacia).
struct RowPartition {
    int         rank{0};
    int         size{1};
    std::size_t globalHeight{0};
    std::size_t firstRow{0};   // indice global (base 0) de la primera fila propia
    std::size_t rowCount{0};   // numero de filas propias (alto local)

    std::size_t endRow() const noexcept { return firstRow + rowCount; }  // exclusivo
    bool        empty()  const noexcept { return rowCount == 0; }
};

// Lanza std::invalid_argument si size < 1 o rank fuera de [0, size).
RowPartition partition_rows(std::size_t globalHeight, int size, int rank);

} // namespace conway::mpi
