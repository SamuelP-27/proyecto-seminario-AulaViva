<div align="center">

# AulaViva

### Sistema de Análisis de Atención y Pose Facial en Tiempo Real

**Convierte el video de una clase en métricas pedagógicas para los profesores**, mapeando la geometría física del aula y estimando hacia dónde mira cada alumno, frame a frame.

![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square&logo=c%2B%2B&logoColor=white)
![Qt](https://img.shields.io/badge/Qt-6.11.1-41CD52?style=flat-square&logo=qt&logoColor=white)
![OpenCV](https://img.shields.io/badge/OpenCV-5.0.0-5C3EE8?style=flat-square&logo=opencv&logoColor=white)
![YuNet](https://img.shields.io/badge/YuNet-2026-orange?style=flat-square)
![Platform](https://img.shields.io/badge/platform-Windows%20(MSVC2022)-lightgrey?style=flat-square)

</div>

---

## 📖 Descripción

**AulaViva** es una aplicación de escritorio (C++/Qt) que analiza videos de clases presenciales para medir el nivel de **atención colectiva e individual** de los estudiantes, sin depender de hardware especializado ni sensores adicionales.

El sistema no se limita a "detectar caras": construye un **modelo geométrico del aula** (dónde está la pizarra, dónde se sienta cada alumno) y lo cruza con la **pose de cabeza y el vector de mirada (gaze)** estimados a partir de los landmarks faciales que entrega el detector **YuNet**. El resultado es un veredicto por alumno: *atento* / *distraído* / *sin detección* con evidencia visual y temporal concreta, pensado para que un docente pueda **evaluar su propia clase** sin revisar el video completo manualmente.

> 💡 **Caso de uso central:** un profesor sube el video de su clase, dibuja una vez la posición de la pizarra y los puestos, y el sistema entrega automáticamente un reporte con el % de atención por alumno, los momentos de mayor dispersión colectiva y una tarjeta de desempeño individual con recomendaciones concretas ("revisa lo cubierto entre el minuto 12:30 y 14:10").

---

## ✨ Características Principales

- 🧑‍🤝‍🧑 **Detección facial multi-rostro con YuNet 2026** — Usa la API DNN de OpenCV 5.0 (`cv::FaceDetectorYN`) con ajuste dinámico de `setInputSize()` por frame y **downscale controlado** (`FACTOR_ESCALA_DETECCION`) para mejorar la confianza en alumnos ubicados al fondo del aula.

- 🪑 **Mapeo geométrico del aula, no solo caras** — `ConfigSala` modela la pizarra como un rectángulo **rotable** (`ConfigPizarra::vertices()`, `contiene()`) y cada asiento como un `PuestoEstudiante`. Toda la sala se serializa a JSON mediante un **parser propio, sin dependencias externas** (`leerDouble`, `leerInt`, `leerStr` en `sala_config.cpp`).

- 🖥️ **Asistente de calibración interactivo (Qt)** — `configurador_sala.cpp` permite dibujar la pizarra (con rotación por scroll del mouse) y cada puesto directamente sobre el primer frame del video. La configuración se **reutiliza automáticamente** en próximas ejecuciones del mismo video.

- 👁️ **Estimación real de gaze 2D, no solo yaw/pitch aproximado** — `estimador.cpp` calcula un vector de mirada a partir de la geometría del triángulo ojos–nariz de los 5 landmarks de YuNet (centro interocular, ejes locales de cara, desplazamiento normalizado de la nariz), incluyendo **corrección de inversión del eje interocular** en perfiles extremos.

- 🎯 **Test de intersección rayo–pizarra** — `gazeApuntaAPizarra()` proyecta el vector de gaze desde el centro interocular y verifica si intersecta el **bounding box expandido** y luego el **polígono rotado exacto** de la pizarra; si el gaze no es confiable (cara muy de perfil), cae a un *fallback* por rango de yaw.

- 🧠 **Clasificación con suavizado temporal anti-falsos-positivos** — `EvaluadorAtencion` usa un **buffer circular por puesto** (`actualizarVentana`) que solo confirma un evento de distracción cuando se sostiene durante `N` frames consecutivos, evitando que un giro momentáneo de cabeza dispare una alerta.

- 🎚️ **Filtro de ventana deslizante por eje (yaw/pitch/roll)** — `FiltroPose` mantiene un `std::deque` independiente por ángulo y promedia las últimas *k* muestras, desacoplado del resto del pipeline para poder aplicarse selectivamente por persona rastreada.

- 🆔 **Tracking de identidad entre frames** — Continuidad espacial mediante `cv::norm()` entre centroides, con tolerancia a oclusión de hasta `MAX_FRAMES_SIN_DETECCION` frames antes de descartar a una persona, y asociación rostro→puesto por **distancia mínima al centroide** (`asociarRostroAPuesto`).

- ⚡ **Persistencia de frames sin bloquear el pipeline** — Uso de `std::async` / `std::future` junto a `cv::getNumCPUs()` para paralelizar de forma portable el guardado en disco de los frames anotados mientras continúa el análisis.

- 🚨 **Detección automática de "momentos críticos"** — `identificarMomentosCriticos()` recorre los eventos de distracción con una **ventana deslizante configurable** y reporta el top-N de tramos con mayor densidad de distracciones por segundo.

- 📊 **Reportería exhaustiva y trazable** — Exportación a CSV (frames, eventos, métricas, coordenadas, momentos críticos) y JSON (reporte agregado, rangos de calibración), más una **conclusión pedagógica generada automáticamente** por alumno que referencia el tramo exacto (MM:SS) de su peor episodio de distracción.

- 🖼️ **Visualización lista para el docente** — Timeline de atención por alumno (verde/rojo/gris frame a frame), panel de "Atención General de la Sala" en tiempo real con barras apiladas, y **tarjetas de desempeño individuales (PNG)** con foto real del puesto, barra de progreso y tipos de distracción más frecuentes.

---

## 🛠️ Stack Tecnológico

| Tecnología | Versión / Detalle | Propósito en el proyecto |
|---|---|---|
| **C++** | C++17 | Lenguaje base — `std::filesystem`, `std::async`, `std::deque`, RAII |
| **Qt** | 6.11.1 · módulo `widgets` | Diálogos de selección de archivo, asistente de calibración de sala, visor de reportes y tarjetas |
| **OpenCV** | 5.0.0 (`opencv_world500`) | Captura de video, primitivas gráficas (`cv::Mat`, `cv::Rect`, dibujo de overlays) y motor DNN |
| **YuNet** | `face_detection_yunet_2026may.onnx` | Modelo de detección facial (bbox + 5 landmarks), ejecutado vía `cv::FaceDetectorYN` |
| **std::async / std::future** | STL (C++17) | Paralelismo portable para I/O de frames sin bloquear el bucle principal |
| **Parser JSON propio** | Sin dependencias externas | Serialización de `ConfigSala` y de los reportes de atención |
| **MSVC 2022 (64-bit)** | Toolchain | Compilador objetivo del proyecto (Windows) |
| **qmake** | `AulaViva.pro` | Sistema de build del proyecto |

---

## 📈 Arquitectura y Flujo de Datos

El flujo va desde la **selección del video** hasta los **reportes finales**, pasando por una etapa de calibración espacial que solo se ejecuta una vez por aula:

```text
┌───────────────────┐
│ 1. Selección de   │  QFileDialog (main.cpp)
│    video (.mp4)   │
└─────────┬─────────┘
          ▼
┌────────────────────────────────────┐
│ 2. ¿Existe                         │  cargarConfigSala()  →  ¿JSON válido?
│    sala_config.json?               │
└─────────┬───────────────────┬──────┘
     No / inválido    Sí (usar existente)
          ▼                   │
┌───────────────────────────┐ │
│ 3. Asistente Qt de        │ │   configurador_sala.cpp
│    calibración de sala    │ │   → dibuja pizarra (rotable) y puestos
│    (guardarConfigSala)    │ │
└─────────┬─────────────────┘ │
          ▼◄──────────────────┘
┌─────────────────────────────────────────────────┐
│ 4. procesarVideo()  —  bucle frame a frame      │
│    a. Detección YuNet (cv::FaceDetectorYN)      │
│    b. Tracking por continuidad espacial         │
│    c. asociarRostroAPuesto() → id de puesto     │
│    d. estimador::calcularpose() → yaw/pitch/    │
│       roll + gazeDir                            │
│    e. FiltroPose → suavizado por eje            │
│    f. EvaluadorAtencion::evaluarFrame() →       │
│       gazeApuntaAPizarra() + ventana temporal   │
│    g. Overlay + panel de atención en vivo       │
│    h. Persistencia async de frames anotados     │
└──────────────────────┬──────────────────────────┘
                       ▼
┌────────────────────────────────────────────────┐
│ 5. calcularReporte() + identificarMomentos-    │
│    Criticos()  (reporte_atencion.cpp)          │
│    → timeline visual, tarjetas de desempeño,   │
│      conclusiones pedagógicas por alumno       │
└──────────────────────┬─────────────────────────┘
                       ▼
┌────────────────────────────────────────────────┐
│ 6. Exportación final                           │
│    CSV: frames / eventos / métricas /          │
│         coordenadas / momentos críticos        │
│    JSON: reporte_atencion.json,                │
│          rangos_atencion.json                  │
│    PNG: frames anotados, tarjetas de desempeño │
└────────────────────────────────────────────────┘
```

### 📤 Artefactos generados por ejecución

| Archivo / carpeta | Contenido |
|---|---|
| `frames_extraidos/anotados/` | Frames del video con overlays de detección y estado de atención |
| `tarjetas_desempeño/` | Tarjeta PNG individual por alumno (progreso, métricas, conclusión) |
| `atencion_metricas.csv` | % de atención y métricas consolidadas por alumno |
| `poses.csv` | Ángulos yaw / pitch / roll por frame y por alumno |
| `coordenadas_rostros.csv` | Bounding boxes y landmarks detectados por YuNet |
| `atencion_eventos.csv` | Transiciones de estado (inicio/fin de cada episodio de distracción) |
| `atencion_frames.csv` | Registro frame a frame del estado de atención |
| `momentos_criticos.csv` | Ventanas de tiempo con mayor densidad de distracciones simultáneas |
| `rangos_atencion.json` | Parámetros geométricos de calibración por puesto |
| `reporte_atencion.json` | Reporte agregado final de la sesión |

---

## 📁 Estructura del Proyecto

```text
AulaViva/
├── AulaViva.pro                 # Proyecto qmake: fuentes, headers y flags de OpenCV
├── main.cpp                     # Orquestación del flujo: selección de video → sala → análisis
│
├── configurador_sala.h / .cpp   # Asistente Qt interactivo de calibración de aula
├── sala_config.h / .cpp         # Geometría de sala (pizarra + puestos), JSON I/O, dibujo
│
├── estimador.h / .cpp           # Pose 3DoF y vector de gaze 2D a partir de landmarks YuNet
├── filtro_pose.h / .cpp         # Filtro de ventana deslizante (yaw/pitch/roll)
│
├── analizador_atencion.h / .cpp # Núcleo de clasificación: EvaluadorAtencion, test rayo-pizarra
├── procesador_video.h / .cpp    # Pipeline principal: captura, YuNet, tracking, overlays, CSV
├── reporte_atencion.h / .cpp    # Reporte final: momentos críticos, tarjetas, export JSON/CSV
│
├── modelos/                     # (No versionado) face_detection_yunet_2026may.onnx
├── docs/
│   ├── Manual-de-Instalacion.word
│   └── Manual-de-Usuario.word
│
└── .../debug/frames_extraidos/            # (Generado en runtime) salidas de cada análisis
```

> ⚠️ El modelo `face_detection_yunet_2026may.onnx` y la DLL `opencv_world500d.dll` **no se versionan** en el repositorio (peso/licencia). Deben copiarse manualmente junto al binario compilado (ver sección de instalación).

---

## 🚀 Instalación y Requisitos

### Requisitos del sistema

| Herramienta | Versión mínima | Notas |
|---|---|---|
| **Qt Creator** | 6.11.1 (kit MSVC2022 64-bit) | El kit **MinGW debe desactivarse**; usar exclusivamente MSVC |
| **MSVC Build Tools** | Visual Studio 2022 | Workload **"Desarrollo para el escritorio con C++"** |
| **OpenCV** | 5.0.0 (build oficial Windows) | [Descarga oficial](https://github.com/opencv/opencv/releases/download/5.0.0/opencv-5.0.0-windows.exe) |
| **Modelo YuNet** | `face_detection_yunet_2026may.onnx` | Debe copiarse junto al ejecutable compilado |
| **Sistema operativo** | Windows 10/11 (64-bit) | Proyecto validado sobre toolchain MSVC |

### 1. Clonar el repositorio

```bash
git clone https://github.com/<tu-usuario>/AulaViva.git
cd AulaViva
```

### 2. Configurar OpenCV en el archivo `.pro`

Edita `AulaViva.pro` y apunta `OPENCV_DIR` a tu ruta local de extracción de OpenCV 5.0.0:

```qmake
OPENCV_DIR = C:/<usuario>/Programas/OpenCV-5.0.0/build
```

### 3. Abrir el proyecto en Qt Creator

1. Abre `AulaViva.pro`.
2. En el asistente de *Kit Selection*, **desactiva** `Desktop Qt 6.11.1 MinGW 64-bit`.
3. Activa exclusivamente `Desktop Qt 6.11.1 MSVC2022 64-bit` (variante *Debug*).

### 4. Configurar el `PATH` de ejecución

En **Projects → Run → Environment**, añade al inicio del `PATH`:

```text
C:\<usuario>\Programas\Qt\6.11.1\msvc2022_64\bin;C:\<usuario>\Programas\OpenCV-5.0.0\build\x64\vc16\bin;%PATH%
```

### 5. Obtener y copiar las dependencias binarias

#### 📦 `opencv_world500d.dll`

Esta es la DLL de **depuración (Debug)** de OpenCV 5.0.0, generada automáticamente al instalar el build oficial de Windows — **no hace falta compilarla ni descargarla por separado**.

1. Descarga el instalador oficial de OpenCV 5.0.0:
   👉 [`opencv-5.0.0-windows.exe`](https://github.com/opencv/opencv/releases/download/5.0.0/opencv-5.0.0-windows.exe)
2. Ejecútalo y extrae el contenido en una ruta **sin espacios** (ej. `C:/<usuario>/Programas/OpenCV-5.0.0`). Es la misma extracción que ya usaste en el paso 2 para configurar `OPENCV_DIR`.
3. Dentro de la carpeta extraída, localiza la DLL en:

   ```text
   OpenCV-5.0.0/build/x64/vc16/bin/opencv_world500d.dll   ← build Debug (la que usa este proyecto)
   OpenCV-5.0.0/build/x64/vc16/bin/opencv_world500.dll    ← build Release (sin el sufijo "d")
   ```

4. Copia **`opencv_world500d.dll`** al directorio donde se genera tu ejecutable compilado, típicamente:

   ```text
   AulaViva/build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug/debug/
   ```

   (debe quedar **al lado de `AulaViva.exe`**, no en una subcarpeta).

> 💡 Si compilas en modo *Release*, usa `opencv_world500.dll` (sin la "d") y cópiala en la carpeta `release/` correspondiente. El archivo `.pro` ya selecciona la librería correcta según el modo de compilación (`CONFIG(debug, debug|release)`), pero **la DLL debes copiarla tú manualmente** — qmake no lo hace de forma automática.

#### 🧠 `face_detection_yunet_2026may.onnx`

El modelo de detección facial esta incluido en la carpeta /modelos.

Debe quedar **al lado de `AulaViva.exe`** como el archivo anterior.

#### ✅ Checklist final antes de ejecutar

```text
build/.../debug/
├── AulaViva.exe
├── opencv_world500d.dll                       ← copiado manualmente
└── face_detection_yunet_2026may.onnx           ← copiado manualmente
```

### 6. Compilar y ejecutar

```bash
# Desde Qt Creator: Ctrl + R
# o vía qmake + make en una consola de desarrollador de MSVC:
qmake AulaViva.pro
nmake        # o jom, según tu configuración
```

---

## 🧭 Uso rápido

1. Ejecuta `AulaViva.exe` y selecciona el video de la clase (`.mp4`, `.avi`, `.mkv`).
2. Si es la primera vez con ese video, dibuja la pizarra (arrastra + rueda del mouse para rotar) y un rectángulo por cada puesto de alumno.
3. Guarda la configuración — el análisis del video comienza automáticamente.
4. Al finalizar, revisa el timeline de atención, las tarjetas de desempeño y los CSV/JSON generados en la carpeta de salida.

---

## 🧩 Notas técnicas de diseño

- **Corrección de perfil extremo:** cuando el eje interocular se invierte en proyección (`ojoDer.x < ojoIzq.x`), el sistema detecta la anomalía y cae a un *fallback* de yaw en vez de propagar un `gazeDir` espejado.
- **`OPENCV_FORCE_DNN_ENGINE=4`:** se fija por entorno antes de inicializar Qt, requerido para que YuNet 2026 resuelva correctamente *shapes* dinámicos en OpenCV 5.x.
- **Codificación de rutas:** en Windows, la carpeta `tarjetas_desempeño/` puede aparecer como `tarjetas_desempeÃ±o` por el manejo de UTF-8 del sistema de archivos — no afecta la integridad de los datos.

---

## 🔎 Proyectos de referencia / Inspiración

Durante el diseño de AulaViva se investigaron proyectos open-source relacionados con detección facial en tiempo real, estimación de pose de cabeza y monitoreo de atención/engagement. Estos repositorios sirvieron como referencia conceptual y punto de partida para varias decisiones de arquitectura:

| Repositorio | Qué aporta | Relación con AulaViva |
|---|---|---|
| [`opencv/opencv_zoo`](https://github.com/opencv/opencv_zoo) | Modelo oficial **YuNet** (`.onnx`) y ejemplo de uso con `cv::FaceDetectorYN` | Fuente del modelo de detección facial usado en `procesador_video.cpp` |
| [`ShiqiYu/libfacedetection`](https://github.com/ShiqiYu/libfacedetection) | Implementación original de **YuNet**, el detector facial ligero en el que se basa el módulo de OpenCV | Referencia del paper/arquitectura detrás del detector integrado |
| [`ShiqiYu/libfacedetection.train`](https://github.com/ShiqiYu/libfacedetection.train) | Pipeline de entrenamiento de YuNet | Contexto sobre las limitaciones y el rango de tamaños de rostro que el modelo puede detectar |
| [`yinguobing/head-pose-estimation`](https://github.com/yinguobing/head-pose-estimation) | Estimación de pose de cabeza en tiempo real con landmarks + OpenCV | Referencia para el cálculo de yaw/pitch/roll a partir de landmarks faciales en `estimador.cpp` |
| [`mpatacchiola/deepgaze`](https://github.com/mpatacchiola/deepgaze) | Librería de estimación de **pose de cabeza y dirección de mirada (gaze)** para interacción humano-computador | Inspiración conceptual para separar el "foco de atención" (head pose) de la mirada real (gaze) |
| [`Johann-Pinto/Predicting-Student-Attentiveness-using-OpenCV`](https://github.com/Johann-Pinto/Predicting-Student-Attentiveness-using-OpenCV) | Predicción de atención de un alumno individual vía pose de cabeza + detección de somnolencia | Referencia directa de dominio: traducir ángulos de pose a un estado "atento / no atento" |
| [`anupampatil44/Computer-Vision-System-for-Gauging-Student-Attentiveness-in-Online-Classes`](https://github.com/anupampatil44/Computer-Vision-System-for-Gauging-Student-Attentiveness-in-Online-Classes) | Sistema de atención estudiantil combinando pose de cabeza, expresión facial y eye-tracking | Referencia de dominio para clasificar tipos de distracción (`TipoDistraccion` en AulaViva) |
| [`yptheangel/attention-monitor`](https://github.com/yptheangel/attention-monitor) | Traduce estadísticas de comportamiento facial en métricas de *engagement* para un docente en clases online | Inspiración para el enfoque de **reporte orientado al docente** (dashboards y métricas post-sesión) |
| [`Bill2015/Attention-Detect-OpenCV`](https://github.com/Bill2015/Attention-Detect-OpenCV) | Detección de distracción mediante seguimiento de rotación de cabeza | Referencia conceptual para el umbral de rotación (roll) usado en `RangosAtencion` |

> 📚 AulaViva **no reutiliza código directamente** de estos repositorios (salvo el modelo `.onnx` de `opencv_zoo`, usado tal cual vía la API `cv::FaceDetectorYN`); se listan como referencias de diseño y de dominio consultadas durante el desarrollo. Todo el pipeline de geometría de aula, gaze-ray contra la pizarra, tracking por continuidad espacial y reportería fue implementado desde cero para este proyecto.

## 📄 Licencia

Este proyecto no se distribuye bajo niguna la licencia, es de libre uso.

## 👤 Autor

Desarrollado por estudiantes como proyecto de análisis de visión artificial aplicado a educación :).
