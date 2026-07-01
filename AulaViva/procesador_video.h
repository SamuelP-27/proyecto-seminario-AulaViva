#ifndef PROCESADOR_VIDEO_H
#define PROCESADOR_VIDEO_H

// procesador_video.h
//
// Declara las estructuras y funciones encargadas de recorrer el video frame
// a frame, detectar rostros con el modelo YuNet de OpenCV, asociar cada
// rostro detectado a un puesto de la sala y dejar todo registrado en CSV
// (y opcionalmente en frames anotados, para revisión visual).

#include <opencv2/core.hpp>
#include <opencv2/objdetect.hpp>   // cv::FaceDetectorYN
#include <string>
#include <vector>
#include "sala_config.h"

// Estructuras públicas
// Representa un rostro detectado en un frame, en coordenadas
// de pixel dentro de ese frame.
struct RostroDetectado
{
    int    x        = 0;   // esquina superior izquierda, eje X
    int    y        = 0;   // esquina superior izquierda, eje Y
    int    ancho    = 0;   // ancho del rectángulo del rostro
    int    alto     = 0;   // alto del rectángulo del rostro
    double confianza = 0.0; // puntaje de confianza entregado por el detector (0-1)
};

// Resultado de analizar un único frame del video: si se encontró rostro,
// cuántos candidatos se evaluaron y si la detección fue directa o, por
// ejemplo, heredada/interpolada de un frame cercano.
struct ResultadoDeteccion
{
    int  numeroFrame          = 0;     // índice del frame dentro del video
    bool rostroEncontrado     = false; // true si se asignó un rostro válido
    int  candidatosDetectados = 0;     // cantidad de rostros candidatos vistos
    bool deteccionDirecta     = true;  // false si el resultado viene de medios auxiliares
    RostroDetectado rostro;            // datos del rostro elegido (si lo hay)
};

// API pública
// Muestra el diálogo de selección de archivo y retorna la ruta elegida.
// (Mantenida por compatibilidad; main.cpp usa QFileDialog directamente.)
std::string seleccionarArchivo();

// Procesa el video frame a frame: detecta rostros, estima pose, asocia cada
// rostro a su puesto según configSala, y guarda los resultados en CSV.
//
//   rutaVideo            : ruta completa al archivo de video
//   carpetaSalida        : carpeta donde se vuelcan frames e informes
//   guardarVisualizacion : true → guarda frames anotados en /anotados/
//   intervaloDeteccion   : analizar 1 de cada N frames (1 = todos)
//   calidadJPEG          : 0-100 para imwrite
//   configSala           : distribución de la sala (pizarra + puestos)
void procesarVideo(const std::string& rutaVideo,
                   const std::string& carpetaSalida        = "frames_extraidos",
                   bool               guardarVisualizacion = true,
                   int                intervaloDeteccion    = 1,
                   int                calidadJPEG           = 90,
                   const ConfigSala&  configSala            = ConfigSala{});

// Parte de compatibilidad (sin sala)
bool guardarCoordenadasCSV(const std::string& rutaCSV,
                           const std::vector<ResultadoDeteccion>& resultados);

#endif // PROCESADOR_VIDEO_H
