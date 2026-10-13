# Generador reproducible de entradas — Fase 2B

El target independiente `conway_generated_input` usa únicamente C++ estándar.
Su API está en `input/generated_input.hpp`, namespace `conway::input`:

```cpp
auto generated = conway::input::generateInput({2000, 2000, 3000, 42});
// generated.input: InputData, propietario de un único vector de células.
// generated.metadata: dimensiones, semilla, densidad solicitada,
// alive_count, observed_density (fracción entre 0 y 1), version.
```

No está conectado a FTXUI ni a `runBenchmark()`. El futuro ejecutor puede
usar `generated.input` por referencia sin copiar el tablero. La generación,
la construcción de Game y la consulta de metadatos quedan fuera del cronómetro.

## Contrato del algoritmo v1

Versión: `mt19937-row-major-rejection-v1`. Semilla de 32 bits, incluyendo cero,
mediante el constructor estándar `std::mt19937(seed)`. Recorrido por filas,
de izquierda a derecha y de arriba abajo. Para **cada** célula, incluso con
densidad 0 o 10000:

1. Obtener una salida de 32 bits de mt19937.
2. Rechazar valores mayores o iguales a 4294960000 y repetir la extracción.
3. Calcular el resto módulo 10000 del valor aceptado.
4. Escribir 1 si el resto es menor que la densidad solicitada; en otro caso 0.

El intervalo aceptado contiene exactamente 429496 múltiplos completos de
10000, evitando sesgo modular. No se usan distribuciones de la biblioteca
estándar cuya transformación pueda variar entre implementaciones. El motor,
la inicialización y estas operaciones enteras fijan la secuencia entre plataformas.
Cambiar cualquiera de estas reglas requiere otra versión y otros casos dorados.

La densidad solicitada es un entero de 0 a 10000 (3000 significa probabilidad
30 %, no una cantidad exacta de células vivas). La densidad observada se obtiene
del conteo real; su representación `double` es informativa y no interviene en
la generación determinista del tablero. No se garantiza que semillas distintas
produzcan tableros distintos.

## Validación y presupuesto de memoria

Todas las comprobaciones preceden a la reserva del vector:

- Dimensiones positivas y densidad en rango (`std::invalid_argument`).
- Productos y sumas sin desbordamiento de `size_t`, incluyendo halo de una
  célula: `P = (ancho + 2) * (alto + 2)`.
- Alto estrictamente menor que `PTRDIFF_MAX`, para admitir el incremento
  final del bucle de filas firmado de OpenMP.
- Tanto `N = ancho * alto` como `P` dentro de `vector<uint8_t>::max_size()`.
- Presupuesto comprobado mediante `N + 2*P` bytes: el tablero de entrada y
  los dos buffers de un Game, cuyos elementos son bytes.

Los tamaños inseguros o que superan el presupuesto producen `std::length_error`.
El presupuesto predeterminado es **256 MiB**, una política conservadora para
rechazar dimensiones accidentales antes de reservar memoria. Permite el caso
experimental 2000×2000 (12 016 008 bytes de payload) con margen, sin depender
de detección del host. Se puede ajustar explícitamente pasando
`GenerationLimits{presupuesto_en_bytes}`; no es un límite del algoritmo.

El presupuesto no estima RSS, sobrecarga del asignador, pilas OpenMP ni varios
Game simultáneos. El futuro ejecutor experimental debe presupuestar estos
costes y la concurrencia. La disponibilidad real de memoria puede cambiar:
`std::bad_alloc` se propaga si la reserva falla después de validar. No se
reservan buffers Game en este módulo ni se duplica el vector generado.

Esta validación protege exclusivamente la API nueva. No cambia las limitaciones
conocidas del cargador `.txt` ni el contrato actual de construcción de Game.

## Verificación

`generated_input` en CTest cubre tableros dorados (incluido rechazo), semilla
cero, repetibilidad, dimensiones mínimas, extremos de densidad, valores binarios,
metadatos y un tablero 2000×2000. Comprueba parámetros inválidos, desbordamientos,
índices, capacidad y límites de presupuesto; compara Game secuencial y OpenMP
con solicitudes de 1, 2, 4 y 8 hilos durante diez generaciones. Las pruebas no
imponen umbrales de rendimiento ni suponen que los hilos efectivos coinciden
con los solicitados.
