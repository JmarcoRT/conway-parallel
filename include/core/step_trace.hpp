#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace conway::core {

// Observación de UN trabajador durante UNA generación.
//
// En el motor secuencial hay siempre un único WorkerRecord con thread_id == 0.
// En el motor OpenMP hay un WorkerRecord por hilo del team paralelo que
// recibió al menos una iteración. Los hilos que no recibieron iteraciones
// (más hilos que filas) NO aparecen en la traza.
//
// Campos:
//   thread_id        Identificador del hilo observado. Para OpenMP coincide
//                    con omp_get_thread_num() del worker; para secuencial es 0.
//   first_row        Primera fila lógica (1..height, inclusive) que el worker
//                    procesó. Observada durante el bucle, no deducida.
//   end_row          Última fila lógica procesada + 1 (exclusivo). Observada.
//                    Invariante: la pareja (first_row, end_row) representa
//                    un rango CONTIGUO de filas. Esta representación es
//                    válida para el reparto por filas con schedule(static)
//                    que es el único utilizado por step_openmp; no debe
//                    generalizarse a planificadores que puedan asignar
//                    rangos discontinuos.
//   cells_processed  (end_row - first_row) * width. Derivado.
//   loop_duration    Intervalo observado por trabajador, en nanosegundos.
//                    Secuencial: reloj anterior al bucle hasta su final,
//                    antes de actualizar contadores e intercambiar buffers.
//                    OpenMP: reloj dentro del parallel antes del reparto,
//                    hasta registrar la duracion despues del omp for.
//                    INCLUYE la espera en la barrera implicita del for y
//                    preparacion del registro; excluye creacion del team y
//                    la barrera de cierre del parallel. No es tiempo de CPU
//                    ni una medida exclusiva de trabajo util.
//
// No se registran instantes de inicio/fin ni orden temporal entre hilos.
// workers se entrega en orden de thread_id, no de comienzo o finalizacion.
// Las duraciones son magnitudes relativas; no deben sumarse para obtener
// el tiempo del step. La reproduccion es didactica, no una cronologia real.
struct WorkerRecord {
    int          thread_id       = -1;
    std::size_t  first_row       = 0;
    std::size_t  end_row         = 0;
    std::size_t  cells_processed = 0;
    std::uint64_t loop_duration  = 0;  // std::chrono::nanoseconds desde steady_clock
};

// Traza consolidada de UNA generación.
//
// El motor la construye al final de step_* y la entrega al sink por
// movimiento. El sink se invoca una sola vez por step, en el hilo original,
// después de la barrera implícita del region paralelo (o al final del bucle
// secuencial). El sink no necesita ser thread-safe.
//
// Campos:
//   engine_name         "sequential" u "openmp".
//   requested_threads   Hilos solicitados por el llamador al motor. El motor
//                       no lo conoce; lo rellena el consumidor del sink si
//                       dispone del dato. Por defecto 0 = "desconocido o
//                       no proporcionado". NO se infiere de
//                       omp_get_max_threads().
//   effective_threads   Tamaño efectivo del team paralelo observado durante
//                       el step. Para secuencial siempre es 1.
//   width, height       Dimensiones lógicas del tablero en este step.
//   generation_before   s.generation al entrar al step.
//   generation_after    s.generation al salir del step.
//   step_duration       Intervalo global instrumentado, en nanosegundos.
//                       Secuencial: desde antes del bucle hasta su final;
//                       EXCLUYE actualizacion de aliveCount, swap e incremento.
//                       OpenMP: despues de reservar workers, antes del parallel,
//                       hasta despues de cerrar la region y actualizar estado;
//                       INCLUYE creacion del team, barreras, reduccion y commit.
//                       Ambos excluyen construccion de la traza y callback.
//                       Estos alcances historicos NO son identicos entre motores.
//   workers             Un WorkerRecord por worker activo. El vector puede
//                       estar vacio si height == 0.
//
// No se reconstruye orden entre hilos a partir de estas duraciones. StepTrace
// no es el cronometro experimental: runOnce mide externamente el lote de pasos
// sin sink, con el mismo alcance para ambos motores. Ver docs/STEP_TRACE.md.
struct StepTrace {
    std::string  engine_name;
    int          requested_threads = 0;
    int          effective_threads = 0;
    std::size_t  width             = 0;
    std::size_t  height            = 0;
    std::size_t  generation_before = 0;
    std::size_t  generation_after  = 0;
    std::uint64_t step_duration    = 0;  // std::chrono::nanoseconds
    std::vector<WorkerRecord> workers;
};

// Interfaz del sink. Implementaciones no necesitan ser thread-safe: el motor
// la invoca una sola vez por step, en el hilo original, tras la barrera
// implícita (o al final del bucle, en el caso secuencial).
class StepTraceSink {
public:
    virtual ~StepTraceSink() = default;
    virtual void onStepComplete(StepTrace&& trace) = 0;
};

// Puntero centinela explícito. Pasar este valor (o nullptr) a step_*
// significa "no instrumentar esta llamada".
inline constexpr StepTraceSink* kNoTrace = nullptr;

}  // namespace conway::core
