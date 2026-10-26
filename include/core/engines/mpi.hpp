#pragma once

#include "core/game_state.hpp"
#include "core/step_trace.hpp"

#include <mpi.h>

#include <cstddef>

namespace conway::core {

// Motor MPI: calcula UNA generacion sobre el GameState LOCAL de este rank.
//
// Mismo patron que step_sequential/step_openmp (docs/CONTRIBUTING.md, sec. 7):
//   - recorrido propio por filas locales 1..height;
//   - la regla NO se duplica: cada celula usa next_cell_state (core/bs23.hpp);
//   - lee solo de `current`, escribe solo las celulas logicas de `next`;
//   - fija aliveCount, hace swap_buffers(s) e incrementa generation.
//
// Antes del recorrido intercambia las filas frontera con los ranks vecinos:
//   fila local 1       -> rank `up`   (alli llena su halo inferior)
//   fila local height  -> rank `down` (alli llena su halo superior)
//   halo local 0          <- ultima fila propia de `up`
//   halo local height + 1 <- primera fila propia de `down`
// Solo viajan las `width` celulas logicas de cada fila (no las columnas halo).
// Un vecino kNoHaloNeighbor se traduce a MPI_PROC_NULL: ese halo es borde
// global, nunca se escribe y conserva 0 (borde muerto fijo, no periodico).
//
// aliveCount queda con las vivas LOCALES (mismo contrato que make_local_state).
// El total global se obtiene cuando se necesita con mpi_global_alive(), para
// no imponer una colectiva por generacion.
//
// Este archivo es el unico del core que incluye <mpi.h>; GameState, B3/S23 y
// los otros motores siguen sin depender de MPI. Los vecinos los calcula el
// modulo mpi (mpi::halo_neighbors) a partir de la particion.
inline constexpr int kNoHaloNeighbor = -1;

// Estrategia de intercambio. Ambas producen exactamente el mismo tablero.
//   Blocking: dos MPI_Sendrecv y despues el recorrido de todas las filas.
//             Referencia simple; la comunicacion no se solapa con computo.
//   Overlap:  MPI_Irecv/MPI_Isend de los halos, recorrido de las filas
//             interiores 2..height-1 (no leen halos), MPI_Waitall y por
//             ultimo las filas frontera 1 y height. Oculta la latencia del
//             intercambio detras del computo interior cuando hay filas
//             suficientes por rank.
enum class MpiHaloMode { Blocking, Overlap };

struct MpiStepContext {
    MPI_Comm comm = MPI_COMM_WORLD;
    int up = kNoHaloNeighbor;    // rank que posee la fila global anterior
    int down = kNoHaloNeighbor;  // rank que posee la fila global siguiente
    MpiHaloMode mode = MpiHaloMode::Blocking;
};

// Observaciones de UN step en ESTE rank (MPI_Wtime, segundos). Sirven para
// separar comunicacion y computo al estudiar la escalabilidad; no sustituyen
// al cronometro externo del lote de generaciones.
//   haloSeconds     tiempo dentro de las llamadas de intercambio de halos
//                   (incluye esperar al vecino). En Overlap: publicar las
//                   peticiones + MPI_Waitall; el computo interior no cuenta.
//   computeSeconds  tiempo del recorrido B3/S23 de las filas locales (en
//                   Overlap, interior + frontera).
//   messagesSent    mensajes enviados a vecinos reales (0, 1 o 2).
//   bytesSent       bytes de celulas enviados (messagesSent * width).
struct MpiStepStats {
    double haloSeconds = 0.0;
    double computeSeconds = 0.0;
    std::size_t messagesSent = 0;
    std::size_t bytesSent = 0;
};

// Lanza std::length_error si width no cabe en un conteo int de MPI.
// Si sink != kNoTrace entrega al final una StepTrace LOCAL de este rank:
// engine_name "mpi", effective_threads 1, height = alto local y un unico
// WorkerRecord (thread_id = rank en ctx.comm, filas locales 1..height) si el
// rank tiene filas. El sink se invoca en cada rank, en su propio proceso.
MpiStepStats step_mpi(GameState& s, const MpiStepContext& ctx,
                      StepTraceSink* sink = kNoTrace);

// Suma de aliveCount de todos los ranks de `comm` (MPI_Allreduce). Colectiva:
// todos los ranks deben llamarla.
std::size_t mpi_global_alive(const GameState& s, MPI_Comm comm);

}  // namespace conway::core
