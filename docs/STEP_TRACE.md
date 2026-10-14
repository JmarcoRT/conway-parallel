# Contrato de StepTrace

Documento vigente. La traza opcional explica una generación; no sustituye a las
mediciones experimentales. No se cambiaron relojes, barreras ni planificación
al consolidar este contrato.

| Campo | Secuencial | OpenMP |
|---|---|---|
| `loop_duration` | Desde antes del bucle hasta después del bucle, antes de actualizar estado | Desde dentro del team antes del reparto hasta registrar la duración después del `omp for`; incluye su barrera implícita y preparación del registro |
| `step_duration` | Desde antes del bucle hasta su final, sin actualización de población, swap ni incremento | Desde antes del `parallel`, después de reservar workers, hasta después de cerrar la región y actualizar estado |

Unidades: nanosegundos de `steady_clock`, con la resolución real del entorno.
El intervalo OpenMP por trabajador excluye creación del team y barrera de cierre
del `parallel`; el global las incluye. La construcción final de StepTrace y el
callback quedan fuera de ambos intervalos globales. Sus alcances históricos son
diferentes: no deben usarse para comparar el rendimiento total de los motores.

Los rangos de filas se observan dentro del bucle estático; solo se publican
trabajadores que procesaron filas. `workers` está ordenado por identificador,
no por inicio o finalización. No hay campos de timestamps ni
`begin_worker_durations_ms`. Una duración no permite reconstruir el orden de
trabajo ni separar espera y cómputo activo; no se suman duraciones concurrentes.

`DidacticController` usa el máximo intervalo de trabajador como duración de
reproducción y revela las células mediante progreso relativo. Esa animación no
es una cronología observada ni una medición de tiempo de CPU.

El benchmark histórico y los ejecutores configurables utilizan `runOnce()` en
`src/app/benchmark_detail.hpp`: reloj externo del lote de `Game::step()` sin
sink, incluidos costes internos y observaciones de equipos. Este intervalo es
independiente de StepTrace y permanece intacto.

Pruebas relevantes: `step_trace`, `didactic_session`, `didactic_layout` y las
pruebas funcionales de simulación. Se verifican cobertura, equivalencia,
publicación y contención de intervalos; no se exigen duraciones concretas ni
umbrales de rendimiento.
