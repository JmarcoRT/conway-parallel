# Guía de contribución

Este documento define las reglas de trabajo para colaborar en `conway-parallel`.

El objetivo es que los integrantes puedan desarrollar módulos en paralelo manteniendo un código integrable, trazable y consistente.

Antes de comenzar a programar, cada integrante debe leer también `docs/ARCHITECTURE.md`.

---

## 1. Ramas principales

El repositorio utiliza las siguientes ramas principales:

- `main`: versión estable del proyecto.
- `develop`: rama de integración del trabajo en desarrollo.

No se debe desarrollar directamente sobre `main`.

El trabajo de cada funcionalidad se realizará en ramas creadas a partir de `develop`.

---

## 2. Ramas de trabajo

Formato general:

```text
tipo/nombre-breve
```

Tipos principales:

- `feature/`: nueva funcionalidad.
- `fix/`: corrección de errores.
- `test/`: trabajo centrado en pruebas.
- `docs/`: documentación.
- `refactor/`: reorganización interna sin cambiar el comportamiento.

Ejemplos:

```text
feature/conway-core
feature/patterns
feature/openmp
feature/mpi
feature/ui
feature/visual-age
feature/parallel-view
feature/benchmark
test/core-validation
fix/neighbor-count
```

Una rama debe tener un objetivo claro y evitar mezclar funcionalidades independientes.

---

## 3. Flujo de trabajo

El flujo normal será:

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

Cuando `develop` alcance una versión estable y validada, se integrará en `main`.

Pasos generales:

1. actualizar `develop`;
2. crear una rama de trabajo desde `develop`;
3. realizar cambios pequeños y coherentes;
4. probar localmente;
5. subir la rama a GitHub;
6. crear un Pull Request hacia `develop`;
7. solicitar revisión;
8. corregir observaciones si existen;
9. integrar únicamente cuando el cambio sea compatible con la arquitectura.

---

## 4. Convención de commits

Los commits seguirán el formato:

```text
tipo(área): descripción breve
```

El área puede omitirse cuando no aporte información.

### Tipos permitidos

| Tipo | Uso |
|---|---|
| `feat` | Nueva funcionalidad |
| `fix` | Corrección de un error |
| `test` | Creación o modificación de pruebas |
| `docs` | Documentación |
| `refactor` | Reorganización sin cambiar comportamiento |
| `perf` | Mejora de rendimiento |
| `build` | CMake, compilación o dependencias |
| `chore` | Mantenimiento general |

### Áreas habituales

Entre otras:

```text
core
mpi
openmp
hybrid
ui
patterns
controls
benchmark
tests
```

### Ejemplos correctos

```text
feat(core): implementar evolución secuencial del tablero
feat(mpi): agregar partición horizontal del tablero
feat(openmp): paralelizar cálculo de generaciones
feat(ui): agregar visualización por edad
fix(core): corregir conteo de vecinos en los bordes
test(core): agregar prueba del patrón blinker
perf(openmp): reducir trabajo redundante por generación
docs: documentar arquitectura del proyecto
build: configurar CMake para OpenMP y MPI
```

### Evitar

```text
cambios
avance
update
final
ahora si
prueba
codigo nuevo
avance semana 4
```

La descripción debe indicar claramente qué cambio introduce el commit.

Se recomienda utilizar verbos en infinitivo:

```text
agregar
implementar
corregir
actualizar
eliminar
refactorizar
documentar
```

Un commit debe representar un cambio coherente. No se deben agrupar cambios independientes únicamente para reducir la cantidad de commits.

---

## 5. Pull Requests

Todo trabajo que vaya a integrarse en `develop` debe pasar por un Pull Request.

El Pull Request debe indicar como mínimo:

- qué se implementó o modificó;
- cómo se probó;
- si afecta a otros módulos;
- cualquier decisión técnica relevante.

Antes de solicitar integración, el autor debe comprobar que:

- el proyecto compila;
- las pruebas relacionadas pasan;
- no se incluyeron archivos generados innecesarios;
- el cambio respeta `ARCHITECTURE.md`;
- no se modificaron interfaces compartidas sin coordinación.

Los Pull Requests deben ser suficientemente pequeños para poder revisarse de manera razonable.

---

## 6. Integración

La integración a `develop` debe realizarse después de una revisión.

La integración a `main` se reserva para versiones estables o hitos del proyecto.

No se deben realizar cambios directos en `main` para solucionar rápidamente problemas que deberían pasar primero por `develop`.

El integrador podrá solicitar cambios cuando una contribución:

- rompa la compilación;
- duplique funcionalidad existente;
- cambie interfaces sin coordinación;
- mezcle responsabilidades entre módulos;
- introduzca dependencias no acordadas;
- produzca resultados distintos de la implementación de referencia sin justificación.

---

## 7. Reglas de arquitectura

Todos los integrantes deben respetar `docs/ARCHITECTURE.md`.

En particular:

- no implementar versiones alternativas del `core`;
- no agregar dependencias MPI a `core`;
- no colocar lógica de Conway en la interfaz;
- no colocar renderizado dentro de los motores de ejecución;
- no cambiar interfaces públicas compartidas sin coordinación;
- reutilizar las interfaces comunes en lugar de crear otras incompatibles.

Si una tarea requiere cambiar la arquitectura, debe discutirse antes de implementar el cambio.

---

## 8. Pruebas

Cada funcionalidad debe incluir una forma reproducible de comprobar que funciona.

Cuando corresponda, las implementaciones paralelas deberán compararse con la implementación secuencial de referencia.

Algunos casos de validación previstos son:

- Block permanece estable.
- Blinker vuelve a su estado inicial después de dos generaciones.
- OpenMP produce el mismo tablero que el modo secuencial.
- MPI produce el mismo tablero que el modo secuencial.
- El modo híbrido produce el mismo tablero que el modo secuencial.

Las optimizaciones de rendimiento no deben sacrificar la corrección.

---

## 9. Dependencias

No se deben agregar bibliotecas o dependencias nuevas sin coordinación.

Entorno de referencia:

``` text
Ubuntu 24.04 LTS
C++20
GCC/G++ 14.2
CMake 3.20 o superior
OpenMP mediante GCC (libgomp)
Open MPI 5.0.10 compilado con GCC 14
```

La instalación de referencia de Open MPI se encuentra en:

`/opt/openmpi-5.0.10-gcc14`

La interfaz de terminal y otras dependencias todavía pueden definirse
posteriormente.

---

## 10. Archivos que no deben subirse

No se deben versionar:

- ejecutables compilados;
- archivos objeto;
- carpetas de compilación;
- archivos temporales del editor;
- resultados generados que no formen parte de una entrega acordada;
- credenciales, claves SSH, tokens o contraseñas.

Estos archivos deberán cubrirse mediante `.gitignore` cuando corresponda.

---

## 11. Uso de herramientas de IA

Se permite utilizar herramientas de IA como apoyo al desarrollo, pero el integrante sigue siendo responsable del código que entrega.

Antes de aceptar código generado mediante IA se debe comprobar que:

- respeta `ARCHITECTURE.md`;
- utiliza las interfaces existentes;
- no introduce dependencias no acordadas;
- compila en el entorno de referencia;
- el integrante comprende su funcionamiento;
- puede explicar y defender las decisiones implementadas;
- cuenta con pruebas suficientes.

Al solicitar ayuda a una IA sobre el proyecto, se recomienda proporcionar `ARCHITECTURE.md` y el contexto del módulo correspondiente para evitar soluciones incompatibles con el resto del sistema.

---

## 12. Criterio de entrega válida

Una tarea no se considera terminada únicamente porque exista código.

Cuando corresponda, una entrega debe incluir:

1. código en su rama;
2. commits siguiendo la convención;
3. prueba reproducible;
4. evidencia de funcionamiento;
5. compatibilidad con el resto del proyecto;
6. comprensión del código por parte del responsable.

El objetivo es que cada contribución pueda integrarse sin comprometer la estabilidad del proyecto.