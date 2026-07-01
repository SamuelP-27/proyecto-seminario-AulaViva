#ifndef SALA_CONFIG_H
#define SALA_CONFIG_H

// sala_config.h
//
// Define las estructuras de datos centrales que describen la distribución
// física de la sala: la pizarra y los puestos de cada estudiante.
// También declara las funciones de serialización (JSON manual, sin
// dependencias externas), de asociación rostro→puesto en tiempo real
// y de dibujo de la configuración sobre un frame de OpenCV.

#include <string>
#include <vector>
#include <opencv2/core.hpp>

// Estructuras de datos

// Un puesto representa el área de la imagen donde se sienta un alumno.
struct PuestoEstudiante
{
    int         id          = 0;    // identificador único del puesto
    std::string nombre      = "";   // etiqueta de posición, ej. "Fila1-Col2"
    std::string estudiante  = "";   // nombre del alumno asignado, ej. "Juan Pérez"
    cv::Rect    rect        = {};   // bounding box del asiento en coords del frame
};

// La pizarra se modela como un rectángulo centrado, opcionalmente rotado.
// Esto permite representar pizarras que no están alineadas con los bordes
// del frame (ej. pizarras en perspectiva o pantallas laterales).
struct ConfigPizarra
{
    cv::Point2f centro = {};  // centro del rectángulo en coords del frame
    float       ancho  = 0.f;  // ancho en píxeles (antes de rotar)
    float       alto   = 0.f;  // alto en píxeles (antes de rotar)
    float       angulo = 0.f;  // rotación en grados, sentido horario

    // devuelve los 4 vértices del rectángulo rotado en coords del frame
    std::vector<cv::Point2f> vertices() const;

    // devuelve true si el punto p cae dentro del rectángulo rotado
    bool contiene(cv::Point2f p) const;
};

// Agrupa toda la información de la sala: resolución de referencia,
// la pizarra y la lista de puestos.
struct ConfigSala
{
    int anchoFrame = 0;     // ancho del frame de referencia (px)
    int altoFrame  = 0;     // alto del frame de referencia (px)

    ConfigPizarra                 pizarra;
    std::vector<PuestoEstudiante> puestos;

    // devuelve true si la configuración tiene los campos mínimos necesarios
    bool valida() const;
};

// guarda config en formato JSON en la ruta indicada
bool guardarConfigSala(const std::string& ruta, const ConfigSala& config);

// carga una ConfigSala desde un archivo JSON; devuelve true si es válida
bool cargarConfigSala (const std::string& ruta, ConfigSala& config);

// Asociación en runtime
// dado el bounding box de un rostro detectado, devuelve el id del puesto
// cuyo centro está más cerca del centro del rostro.
// devuelve -1 si ningún puesto supera el umbral de proximidad.
int asociarRostroAPuesto(const cv::Rect& rostro, const ConfigSala& config);

// Dibujo en frame
// dibuja la pizarra (rectángulo rotado rojo) y todos los puestos (cyan)
// sobre el frame, útil para verificar la configuración en tiempo real (para confiabilidad).
void dibujarSobreFrame(cv::Mat& frame, const ConfigSala& config);

#endif // SALA_CONFIG_H
