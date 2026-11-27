# Validación de la interfaz y del arnés de pruebas

## Alcance y auditoría

Rama `fix/ui-suite-validation`, derivada de la corrección compacta validada
manualmente. Se revisaron los diffs de `18e0379` y `44458e4`, incluyendo progreso,
ejecución asíncrona, vistas de resultados, reproducción CUDA y representación
MPI. Los callbacks de progreso permanecen fuera de los intervalos medidos.
No se modifican los motores, sus mediciones ni la rama `feat/add-cluster`.

El inventario inicial combinó las ejecuciones existentes y las pendientes:
83 pruebas, 74 aprobadas y nueve fallidas (una por timeout). Esto no era una
única ejecución completa en verde. Que un cambio fuese intencional no se tomó
como prueba de que conservara la accesibilidad.

| Prueba | Historia relevante | Diagnóstico y corrección |
|---|---|---|
| `scaling_view` | `18e0379` | La prueba ralentizaba 8 hilos pero seguía esperándolos como ganadores. Se añaden oráculos explícitos de mediana mínima, empate e invalidez; se comprueban tabla ancha y registros estrechos por separado. |
| `ftxui_didactic_mpi_views_pty` | `44458e4` | El ancho de etiquetas cambió las columnas disponibles. La prueba obtiene el intervalo real y verifica las células de ambos extremos, en vez de fijar 13 columnas. |
| `ftxui_accessibility_pty` | `bbdb233`, `208226e`, `ca41622`, `f783391` | Se decodifican píxeles y filas comprimidas. La prueba encontró un fallo real: el mínimo de altura introducido en la corrección anterior (`f783391`) era demasiado restrictivo y paneles de tres líneas devolvían un viewport vacío. El modo compacto admite una línea interior más sus bordes. También se corrigió la barra de partición que emitía fondos RGB con `NO_COLOR`. |
| `ftxui_responsive_pty` | `18e0379` | Cambió el resumen y la ubicación del identificador. Se verifican entrada, identificador accesible, pestañas y conservación al redimensionar. |
| `ftxui_result_scroll_pty` | `18e0379` | Los identificadores individuales están en detalles. Se activa D y se recorre la cantidad real de bloques hasta la muestra 16, con Inicio/Fin, flechas y pestañas conservadas. |
| `ftxui_experimental_mpi_pty` | `18e0379`, `44458e4` | El lector añadía hasta 350 ms por acción. Se usa el lector existente que termina tras cesar la salida; las observaciones silenciosas mantienen 350 ms. Además, un párrafo MPI perdía su segunda línea: se corrige la altura según el requisito resuelto tras el layout. |
| `ftxui_benchmark_pty` | `18e0379`, `44458e4` | Se comprueba el identificador en su ubicación desplazable; se conserva la cobertura de resultados, foco, errores y cierre activo. |
| `cuda_didactic_pty` | `44458e4` | Las fórmulas quedan después del resumen ampliado. La prueba usa Home/PgDown para comprobar que sean accesibles, sin avanzar generaciones por explorar. |
| `cuda_board_visual_pty` | `44458e4` | El encabezado es `POSICIÓN EXTERIOR`. Se mantiene la ausencia de una selección ficticia. Se encontró otro fallo real: el pie de ayuda ocupaba dos líneas a 40×18 y ocultaba el tablero; ahora los controles esenciales caben en una y V conserva el detalle completo. |

## Regresiones demostradas antes de aprobarlas

Las alteraciones controladas se hicieron exclusivamente en copias temporales;
ninguna queda en el código ni en los binarios entregados.

- Panel de 5×3: la prueba falló con `Compact viewport exceeds panel budget`
  antes de admitir una línea interior. El PTY también reprodujo `filas 1–0` a
  120×40 antes de corregir la interfaz.
- Párrafo estrecho: `TAIL_SENTINEL` desaparecía a ancho 37 antes de utilizar
  `row->requirement().min_y`; `Dimension::Fit` había creado un probe de altura 1
  aunque el requisito resuelto ya era de dos líneas.
- Omisión deliberada de la última fila física: la regresión de píxeles falló
  con `Monochrome viewport lost cell 20,22`; al restaurarla, pasó.
- Selección deliberada de mediana máxima: fallaron los oráculos de ganador.
  Elegir el último empate o permitir candidatos no equivalentes también falló.
- La ruta compartida de estudios OpenMP/MPI/CUDA tiene casos con medianas
  conocidas. La alteración máximo-por-mínimo falló con
  `shared study winner must use minimum median`.
- Desactivar deliberadamente el deadline hizo fallar `pty_state_wait`.
  Esta prueba adicional acepta una finalización real y rechaza aplicación
  bloqueada o terminada, con reloj simulado para no depender de rendimiento.

## Preset CUDA y booleanos de CMake

La primera ejecución completa del preset real encontró un desajuste adicional:
el preset JSON guardaba `CONWAY_ENABLE_CUDA=TRUE`, pero tres scripts PTY
interpretaban únicamente `ON`. Las pruebas de cálculo pasaban y el probe
informaba GPU disponible; los scripts interpretaban erróneamente un build sin
CUDA. CMake ahora traduce sus booleanos al protocolo explícito ON/OFF de esas
pruebas; HIDDEN conserva su significado. Se valida el preset real, no solo una
configuración temporal creada con `-DCONWAY_ENABLE_CUDA=ON`.

## Criterio de cierre y reproducción

Se mantienen los límites originales de CTest, incluido el de 360 s para
experimentos MPI. No se deshabilitan pruebas ni se convierten fallos en omisiones.
Las 83 pruebas originales más `pty_state_wait` forman una suite de 84 pruebas.
El criterio en esta máquina con GPU es 84 aprobadas, cero fallidas y cero omitidas.

```bash
cmake --preset experimental-cuda-release
cmake --build --preset experimental-cuda-release
env -u NO_COLOR ctest --preset experimental-cuda-release --parallel 2 --output-on-failure
```

Resultado final del 11 de noviembre de 2026, en la compilación real del proyecto
con el preset CUDA y RTX 3050: **84/84 aprobadas, cero fallidas y cero omitidas**,
en una única ejecución completa de 617,14 s. CTest respetó `RUN_SERIAL` para
las pruebas que lo requieren. `ftxui_experimental_mpi_pty` pasó en 121,68 s,
frente a su límite original de 360 s. Se conservan los límites originales;
la prueba nueva `pty_state_wait` tiene un límite propio de 5 s.

Las pruebas verifican equivalencia y contratos, no aceleración ni significancia
estadística. Las mediciones del arnés PTY no son resultados de benchmark.

## Comprobación pendiente en laboratorio

Esta validación usa procesos MPI locales. No certifica comunicación entre dos PC.
Antes de la demostración, registrar en ambas máquinas la revisión, compilador,
CMake, MPI y disponibilidad GPU; usar binarios y MPI compatibles. Ejecutar las
pruebas de corrección y evolución MPI en cada máquina y después comprobar
lanzamiento entre hosts, equivalencia, intercambio de halos y cierre de procesos
con la configuración de red y acceso del laboratorio. La preparación del
orquestador de clúster corresponde a su rama separada.

Si una PC carece de CUDA, la configuración CPU se obtiene con
`-DCONWAY_ENABLE_CUDA=OFF`; sus resultados no certifican equivalencia GPU.
Registrar por separado las pruebas disponibles y la causa de cualquier ausencia.
Un bloqueo de comunicación o fuga de procesos impide dar por terminada esa
validación. Un límite de rendimiento solo se documentará como tal después de
medirlo y no se ocultará ampliando el timeout.
