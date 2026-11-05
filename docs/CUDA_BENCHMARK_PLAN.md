# Plan de integración de CUDA en el benchmark

Estado: integración funcional validada y resumen visual simplificado al terminar. Contrato implementado: CUDA_BENCHMARK.md.
Base: develop después del PR #33, commit 468dbb764cc1aa621a91d62623eaeacbf7905a71.
Entorno principal de validación: Ubuntu 24.04 / WSL2, G++ 12, CUDA 12.5 y RTX 3050.

## 1. Apartados actuales y alcance

| Apartado visible | Comportamiento actual | Integración prevista |
|---|---|---|
| Comparación de motores | Participantes Secuencial, OpenMP y MPI; entrada cargada o generada; generaciones, calentamientos y repeticiones configurables | Añadir CUDA como participante con su bloque 8x8, 16x16 o 32x8; permitir comparar los cuatro motores |
| Escalabilidad | Tablero y generaciones fijos; variar hilos OpenMP o procesos MPI; referencia secuencial y orden rotatorio | Añadir estudio de bloques CUDA en una GPU, manteniendo entrada y generaciones fijas; no presentarlo como aumento de GPU o núcleos |
| Resultados de la sesión | Ejecución activa y último experimento; resumen, muestras y método; resultados vinculados a su configuración | Conservar muestras y resultados CUDA, su configuración, validez, tiempos y método; mantener consultas tras navegar |

Las piezas de soporte también forman parte del cambio: validaciones y presupuestos, ejecución en segundo plano, estadísticas, equivalencia, metadatos y exportación CSV por API. No existe actualmente un botón de exportación interactiva; no se añadirá en esta fase.

El benchmark histórico runBenchmark() seguirá con su contrato Secuencial/OpenMP. CUDA se integrará en el ejecutor configurable y en el estudio generalizado, compartiendo las estadísticas existentes.

## 2. Hallazgo que condiciona la medición

Game::stepCuda() mantiene el estado CPU coherente para la aplicación: cada paso obtiene un snapshot de ambos buffers desde GPU. Esto es adecuado para mostrar generaciones, pero reutilizarlo directamente en el benchmark introduciría copias por generación y la creación inicial del backend dentro del reloj.

La propuesta es una ejecución CUDA por lotes sobre CudaBackend: preparar una vez, mantener ambos buffers en GPU durante el lote y obtener un snapshot al terminar. La vista normal y la didáctica conservarán su comportamiento actual. El lote reutilizará el kernel y la regla B3/S23 existentes.

No basta con incorporar una casilla CUDA: validadores, nombres de configuración, resúmenes y CSV todavía suponen que los participantes son Secuencial, OpenMP o MPI.

## 3. Contrato de medición propuesto

La medida principal será el tiempo de pared del lote de evolución terminado. Para CUDA, incluirá encolado de trabajo y sincronización final, y excluirá creación del contexto/recursos, preparación de la entrada, carga inicial, descarga final, comparación, render y CSV.

| Dato CUDA | Qué incluye | Uso |
|---|---|---|
| evolution_wall_ms | Lote completo con su sincronización, medido desde CPU | Estadísticas y speedup respecto de la referencia secuencial del mismo experimento |
| device_interval_ms | Intervalo entre eventos del mismo stream, alrededor del lote | Diagnóstico de la ejecución GPU; no sustituye el tiempo principal ni se etiqueta como ocupación o tiempo puro de kernel |
| prepare_ms | Creación de recursos y carga inicial | Coste de preparación, separado de evolución |
| snapshot_ms | Descarga del estado final | Coste de transferencia final |
| total_operation_ms | Operación desde preparación hasta descarga y liberación, medida explícitamente y con la equivalencia fuera del reloj | Coste completo CUDA; no mezclarlo con el speedup de evolución |

La implementación fijará los límites exactos de cada reloj en el protocolo y en las pruebas. Los tiempos auxiliares no se sumarán para inventar un total. Si un coste no se mide, estará ausente, no a cero.

El lote usará el stream propio del backend, con sincronización antes de iniciar la medida si hay trabajo previo y al finalizar antes de parar el reloj. Los eventos se crearán fuera del intervalo. Se conservará la comprobación de errores de lanzamiento y ejecución. No se harán snapshots ni consultas de propiedades por generación.

Un evento GPU puede incluir pausas entre operaciones del stream: su intervalo no se interpretará automáticamente como suma de tiempos de kernel. Otras cargas del equipo pueden afectar los tiempos; no se prometerá aceleración para tableros pequeños.

Referencia técnica: NVIDIA, CUDA 12.5.1, Best Practices Guide, apartados Timing y Data Transfer Between Host and Device:
https://docs.nvidia.com/cuda/archive/12.5.1/cuda-c-best-practices-guide/index.html#timing

## 4. Configuración, repetibilidad y validez

- Añadir un bloque CUDA explícito a ExecutionConfiguration. Los campos de hilos OpenMP y procesos MPI deben estar ausentes para CUDA. No reutilizar ninguno de ellos para almacenar hilos de bloque.
- Usar identificadores inequívocos, por ejemplo cuda:16x16. Sustituir los fallbacks que interpretan cualquier motor adicional como MPI o Secuencial por casos explícitos.
- La configuración del benchmark será independiente de la selección de motor de la simulación. La solicitud se congela al ejecutar y el resultado conserva esa configuración.
- Comprobar compilación CUDA, disponibilidad real del dispositivo y validez del bloque antes de empezar el experimento. Sin GPU o toolkit, explicar la indisponibilidad; nunca ejecutar CPU bajo la etiqueta CUDA.
- Mantener una entrada inmutable, generada una vez o tomada del patrón; mismo tablero inicial, generaciones y referencia para todos los participantes.
- Reiniciar desde la generación inicial en cada calentamiento y muestra. Cada muestra CUDA tendrá recursos preparados fuera del reloj; la inicialización y carga no se esconderán en la primera generación.
- Conservar el orden de adquisición y la rotación de configuraciones. Los calentamientos se excluyen de las estadísticas y también se verifican contra la referencia.
- Comparar dimensiones, generación, población y todas las celdas finales fuera del reloj. No usar solo fingerprint o conteo de vivos como prueba de equivalencia.
- Proteger por separado el presupuesto de memoria CPU y la estimación de payload GPU, con aritmética comprobada. Tener VRAM disponible al consultar no garantiza una reserva posterior.
- Ante un error, conservar evidencias parciales y diagnóstico, detener el estudio y limpiar recursos. Sin reintentos ni fallback silenciosos. Si la limpieza no puede confirmarse tras un fallo del driver, no registrar una confirmación ficticia.
- Reutilizar la protección que impide simulación, catálogo o didáctica calculando mientras hay un experimento activo. Antes de medir, liberar o contabilizar explícitamente los recursos GPU de una simulación CUDA pausada, conservando su estado CPU y configuración para reconstruirlos al volver; no dejar memoria GPU retenida fuera del presupuesto.

## 5. Métricas

Conservar mínimo, máximo, mediana, media y desviación estándar poblacional. El speedup será mediana secuencial / mediana de evolución CUDA, únicamente con referencia incluida, comparación válida y medianas positivas.

La eficiencia CUDA será N/D en esta primera integración. Dividir el speedup por 64 o 256 hilos de bloque, por posiciones lanzadas o por núcleos CUDA no reproduce la métrica de eficiencia de OpenMP/MPI. Tampoco esos valores indican hilos ejecutándose simultáneamente.

El estudio CUDA dentro de Escalabilidad se describirá como comparación de tamaños de bloque en una GPU: 8x8, 16x16 y 32x8. No es una prueba de escalabilidad fuerte añadiendo recursos físicos. Cada bloque tendrá identidad propia y compartirá la referencia secuencial del estudio.

## 6. Resultados y procedencia

Los resultados conservarán bloque, grid, dimensiones, posiciones exteriores, dispositivo observado, nombre de GPU, capacidad de cómputo y memoria, versiones runtime/driver API y datos de compilación CUDA (nvcc, arquitectura y flags).

Las versiones del runtime, de la API del driver y del compilador se etiquetarán por separado. No se inferirá una versión concreta del driver Windows a partir de cudaDriverGetVersion.

Extender los validadores de muestras/resúmenes y su almacenamiento; los campos exclusivamente CPU o MPI estarán ausentes en CUDA. La ruta de CSV requerirá una versión nueva para estudios con CUDA y sus columnas adicionales, conservando los formatos actuales para estudios antiguos. Registrar el alcance de los relojes y el protocolo usado; no mezclar muestras de métodos distintos como si fueran equivalentes.

El preset actual experimental-release fija G++ 14. Añadir un perfil CUDA reproducible con G++ 12 y nvcc 12.5, usando el mismo compilador C++ para todos los motores de esa campaña. No mezclar objetos ni mediciones de los dos perfiles.

## 7. Orden de implementación

1. **Contrato y modelo.** Cerrar los límites del cronometraje, configuración de bloques, campos observados, validez y esquema de exportación. Documentar el protocolo y sus pruebas esperadas.
2. **Lote CUDA.** Añadir la ejecución residente por lotes, eventos y sincronización, conservando step() y las vistas existentes. Probar equivalencia, estado final y limpieza ante fallos.
3. **Ejecutor experimental.** Incorporar CUDA a comparación y estudio de bloques; mantener entrada, calentamientos, rotación, referencia, presupuestos y evidencias de fallos.
4. **Estadísticas y persistencia.** Incorporar muestras CUDA, métricas válidas, N/D para eficiencia, metadatos, CSV y retención de resultados.
5. **Conexión funcional con la interfaz.** Añadir la selección CUDA y sus bloques a los formularios y presentar los datos nuevos dentro de las vistas existentes. Mantener la interfaz receptiva y los resultados al navegar.
6. **Validación integrada.** Probar en Ubuntu 24.04 con GPU real y también con CUDA desactivado/dispositivo oculto; revisar regresiones de Secuencial, OpenMP y MPI.
7. **Simplificación visual, al final.** Con los datos y cálculos aprobados, reducir texto del benchmark, separar resumen y ayuda, revisar espacios y colores. Esta etapa tendrá su propia revisión con el usuario.

## 8. Pruebas y criterio de aceptación

- Equivalencia de lotes con Secuencial y con pasos CUDA individuales: varios tamaños, 17x17, formas degeneradas y tableros irregulares, todos los presets y múltiples generaciones.
- Estado reiniciado entre muestras y calentamientos; buffers coherentes; ausencia de copias a CPU por generación en la ruta de lote; conservación de la semántica de las vistas existentes.
- Fallos de reserva, eventos, lanzamiento, ejecución, sincronización y snapshot; recursos liberados o estado no confirmado declarado; evidencia parcial sin métricas engañosas.
- Estadísticas, referencias, órdenes y speedup comprobados con observaciones sintéticas; tiempos nulos/no finitos, diferencias de estado y ausencia de referencia correctamente clasificados. Eficiencia CUDA siempre ausente.
- Metadatos y CSV comprobados mediante parsing, identificadores distintos por bloque y ausencia de valores inventados; compatibilidad de estudios CPU/MPI existentes.
- Flujo real desde los tres apartados: ejecutar, consultar estado, cambiar de pantalla, recuperar el resultado y reconocer indisponibilidad CUDA. Terminales amplia y pequeña, con y sin color.
- Campaña funcional con Secuencial/OpenMP/MPI/CUDA, además de subconjuntos; estudio de los tres bloques y un tablero suficientemente grande para observar los costes. No convertir una aceleración mínima en requisito de prueba ni prometer un preset ganador.

Se considerará integrado cuando los cuatro motores puedan compararse desde la misma aplicación, las muestras sean equivalentes y auditables, los tiempos tengan un alcance explícito, los errores sean claros y las regresiones existentes pasen. La limpieza visual se hará después de esa aprobación funcional.
