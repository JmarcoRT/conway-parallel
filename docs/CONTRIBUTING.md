# Guía de contribución

Este documento resume la forma de trabajo del repositorio `conway-parallel`.

La idea es que cada integrante pueda avanzar en su módulo sin romper el trabajo de los demás. Antes de programar, conviene revisar también `docs/ARCHITECTURE.md`.

---

## 1. Ramas principales

El repositorio usa dos ramas principales:

- `main`: versión estable.
- `develop`: rama de integración del trabajo en curso.

No se debe desarrollar directamente sobre `main`.

Las ramas de trabajo deben crearse desde `develop`.

---

## 2. Ramas de trabajo

Formato general:

```text
tipo/nombre-breve
```

Tipos habituales:

- `feature/`: nueva funcionalidad.
- `fix/`: corrección de errores.
- `test/`: pruebas.
- `docs/`: documentación.
- `refactor/`: reorganización interna sin cambiar el comportamiento.

Ejemplos:

```text
feature/conway-core
feature/input
feature/patterns
feature/openmp
feature/pthreads
feature/mpi
feature/ui
feature/visualization
feature/benchmark
test/core-validation
fix/neighbor-count
```

Cada rama debe tener un objetivo claro y evitar mezclar tareas que no estén relacionadas.

---

## 3. Flujo de trabajo

El flujo normal es:

```text
develop
   |
   +-- feature/modulo
           |
           +-- commits
           |
           +-- Pull Request
                   |
                   v
                develop
```

Cuando `develop` alcance un estado estable, se integrará en `main`.

Pasos habituales:

1. actualizar `develop`;
2. crear una rama desde `develop`;
3. realizar los cambios;
4. probar localmente;
5. subir la rama a GitHub;
6. crear un Pull Request hacia `develop`;
7. revisar y corregir observaciones;
8. integrar cuando el cambio esté listo.

---

## 4. Commits

Formato recomendado:

```text
tipo(área): descripción breve
```

Tipos principales:

| Tipo | Uso |
|---|---|
| `feat` | Nueva funcionalidad |
| `fix` | Corrección de errores |
| `test` | Pruebas |
| `docs` | Documentación |
| `refactor` | Reorganización interna |
| `perf` | Mejora de rendimiento |
| `build` | Compilación, CMake o dependencias |
| `chore` | Mantenimiento general |

Áreas frecuentes:

```text
core
input
openmp
pthreads
mpi
ui
patterns
controls
benchmark
tests
```

Ejemplos:

```text
feat(core): implementar evolución secuencial
feat(input): agregar carga de tablero desde archivo
feat(openmp): paralelizar cálculo de generaciones
feat(pthreads): repartir filas entre hilos
feat(mpi): agregar partición horizontal
feat(ui): agregar visualización de construcción de generaciones
fix(core): corregir conteo de vecinos
test(core): agregar prueba del patrón blinker
perf(openmp): reducir trabajo redundante
docs: actualizar arquitectura
build: configurar CMake para OpenMP y MPI
```

Evitar mensajes demasiado generales como:

```text
cambios
avance
update
final
prueba
codigo nuevo
```

Un commit debe representar un cambio coherente.

---

## 5. Pull Requests

Todo cambio que vaya a integrarse en `develop` debe pasar por un Pull Request.

La descripción debe ser breve, pero permitir entender qué cambió y cómo se comprobó.

Conviene incluir:

- **Cambios principales:** qué se implementó o modificó.
- **Pruebas realizadas:** cómo se comprobó que funciona.
- **Impacto:** si afecta a otros módulos o interfaces.
- **Decisiones técnicas:** solo cuando haya algo importante que dejar registrado.

Formato sugerido:

```text
## Cambios principales

- ...
- ...

## Pruebas realizadas

- ...
- ...

## Impacto

- ...

## Decisiones técnicas

- ... (opcional)
```

No hace falta describir cada archivo ni cada línea. La idea es dejar suficiente contexto para que otra persona pueda revisar el PR sin reconstruir todo el cambio desde cero.

Antes de solicitar revisión, comprobar que:

- el proyecto compila;
- las pruebas relacionadas pasan;
- no se subieron archivos generados innecesarios;
- el cambio respeta `ARCHITECTURE.md`;
- no se modificaron interfaces compartidas sin coordinación.

---

## 6. Integración

Los cambios se integran primero en `develop`.

`main` se reserva para versiones estables o hitos del proyecto.

No se deben hacer cambios directos en `main` para saltarse el flujo normal.

Un Pull Request puede requerir cambios si:

- rompe la compilación;
- duplica funcionalidad existente;
- cambia interfaces sin coordinación;
- mezcla responsabilidades entre módulos;
- introduce dependencias no acordadas;
- produce resultados distintos del modo secuencial sin justificación.

---

## 7. Arquitectura

Todos los integrantes deben respetar `docs/ARCHITECTURE.md`.

En particular:

- no crear versiones alternativas del `core`;
- no agregar dependencias MPI, OpenMP o Pthreads dentro del `core`;
- no colocar lógica de Conway en la interfaz;
- no colocar renderizado dentro de los motores de ejecución;
- no cambiar interfaces compartidas sin coordinación;
- reutilizar las interfaces comunes del proyecto.

Si una tarea requiere cambiar la arquitectura, debe discutirse antes de implementarla.

---

## 8. Pruebas

Cada funcionalidad debe tener una forma reproducible de comprobar que funciona.

Casos de referencia:

- Block permanece estable.
- Blinker vuelve a su estado inicial después de dos generaciones.
- OpenMP produce el mismo tablero que el modo secuencial.
- Pthreads produce el mismo tablero que el modo secuencial.
- MPI produce el mismo tablero que el modo secuencial.

Las optimizaciones de rendimiento no deben cambiar el resultado de la simulación.

---

## 9. Entorno y dependencias

Entorno de referencia:

```text
Ubuntu 24.04 LTS
C++20
GCC/G++ 14.2
CMake 3.20 o superior
OpenMP mediante GCC (libgomp)
Open MPI 5.0.10 compilado con GCC 14
```

Open MPI se encuentra en:

```text
/opt/openmpi-5.0.10-gcc14
```

No se deben agregar nuevas dependencias sin coordinación.

---

## 10. Archivos que no deben subirse

No versionar:

- ejecutables compilados;
- archivos objeto;
- carpetas de compilación;
- archivos temporales del editor;
- resultados generados que no formen parte de una entrega;
- credenciales, claves, tokens o contraseñas.

Cuando corresponda, estos archivos deben excluirse mediante `.gitignore`.

---

## 11. Uso de herramientas de IA

Se pueden usar herramientas de IA como apoyo, pero cada integrante sigue siendo responsable del código que entrega.

Antes de integrar código generado o asistido por IA, se debe comprobar que:

- respeta `ARCHITECTURE.md`;
- usa las interfaces existentes;
- no agrega dependencias innecesarias;
- compila en el entorno del proyecto;
- el integrante entiende lo que hace;
- puede explicar las decisiones implementadas;
- cuenta con pruebas suficientes.

Al pedir ayuda sobre el proyecto, es recomendable proporcionar `ARCHITECTURE.md` y el contexto del módulo correspondiente.

---

## 12. Cuándo se considera terminada una tarea

Una tarea no se considera terminada solo porque exista código.

Cuando corresponda, debe incluir:

1. cambios en su rama;
2. commits claros;
3. una prueba reproducible;
4. evidencia de funcionamiento;
5. compatibilidad con el resto del proyecto;
6. comprensión del código por parte del responsable.

El objetivo es que cada contribución pueda integrarse sin comprometer la estabilidad del proyecto.
