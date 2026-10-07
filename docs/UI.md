# Módulo de Interfaz de Usuario (UI) y Layout Base

Este documento describe la arquitectura, componentes y forma de uso del módulo de interfaz de terminal para `conway-parallel`.

---

## 1. Arquitectura y Principios de Diseño

El módulo UI sigue estrictamente los principios definidos en `docs/ARCHITECTURE.md`:
- **Renderizado Puro y Desacoplado:** Las funciones de UI son funciones puras e inmutables que reciben el estado actual (índice seleccionado, información de la entrada activa) y renderizan la interfaz en terminal. **No contienen loops de navegación ni lecturas de teclado.**
- **Sin Lógica de Simulación:** La UI no calcula estados del Juego de la Vida ni modifica buffers de células.
- **Layout Base Reutilizable:** Proporciona un marco con bordes de caja, título centrado entre separadores, línea de estado para la entrada activa, zona de opciones destacadas y footer de ayuda de navegación fijo.

---

## 2. Componentes Principales

- `conway::ui::ActiveInputInfo`: Estructura para consultar el estado de la entrada activa (nombre de archivo, dimensiones, células vivas).
- `conway::ui::MenuItem`: Estructura para representar opciones del menú (`key`, `label`).
- `conway::ui::renderLayout(...)`: Función principal que renderiza el marco reutilizable y las opciones.
- `conway::ui::renderMainMenu(...)`: Función específica para renderizar la vista del **Menú Principal v1** (`[1] Benchmark`, `[2] Visualización`, `[0] Salir`).

---

## 3. Prerrequisitos del Sistema

Se requiere un entorno Linux o compatible con C++20, CMake y Open MPI.

```bash
sudo apt update
sudo apt install -y libopenmpi-dev openmpi-bin cmake g++
```

> **Nota de compatibilidad visual:** La interfaz utiliza secuencias de escape ANSI para colores (Cian, Verde, Gris, Blanco) y caracteres de dibujo de caja UTF-8 (`┌`, `─`, `┐`, `│`, `└`, `┘`). Se requiere una terminal con soporte para secuencias ANSI y UTF-8 (ej. WSL, Ubuntu/Linux terminal, macOS Terminal o Windows Terminal moderno).

---

## 4. Compilación y Ejecución con CMake

Para compilar y ejecutar el target de demostración del Menú Principal y Layout Base (`conway_ui_demo`):

```bash
# 1. Configurar el proyecto
cmake -B build -S .

# 2. Compilar los ejecutables
cmake --build build

# 3. Ejecutar la demostración de la UI
./build/conway_ui_demo
```

---

## 5. Integración para Integrantes del Equipo

Los compañeros encargados de la navegación (ej. Manuel) o de pantallas futuras (Benchmark, Visualización) pueden reutilizar el layout base importando `include/ui/layout.hpp` o `include/ui/menu.hpp`:

```cpp
#include "ui/menu.hpp"

// Renderizar Menú Principal con opción 0 (Benchmark) seleccionada
conway::ui::ActiveInputInfo inputInfo;
inputInfo.hasInput = true;
inputInfo.filename = "tablero.txt";
inputInfo.width = 1000;
inputInfo.height = 1000;

conway::ui::renderMainMenu(0, inputInfo);
```
