#ifndef ESTIMADOR_H
#define ESTIMADOR_H

// estimador.h
//
// Declara la clase estimador y las estructuras de datos que transportan
// la pose de cabeza y el vector de gaze calculados a partir de los
// landmarks que entrega YuNet.
// La pose se expresa como tres ángulos proxy (yaw, pitch, roll) y
// opcionalmente como un vector de gaze 2-D normalizado en el plano
// de imagen, que permite hacer el test de intersección rayo-pizarra
// en analizador_atencion.cpp sin convertir a ángulos reales.

#include <opencv2/opencv.hpp>

// DatosPose
// Resultado de la estimación de pose para un rostro detectado.
// Todos los campos son válidos solo si validar == true.
struct DatosPose
{
    bool   validar = false; // false si el rostro no era usable

    // ángulos de pose (en grados, valores proxy basados en landmarks):
    double yaw   = 0.0;   // + = mira derecha, - = mira izquierda
    double pitch = 0.0;   // + = mira abajo,   - = mira arriba
    double roll  = 0.0;   // + = inclina a la derecha

    // Vector de gaze 2-D proyectado en el plano de imagen
    //
    // Vector que indica hacia dónde apuntan los ojos.
    // Se construye con la geometría de los landmarks de YuNet:
    //   • la línea interocular define el eje X local de la cara
    //   • el desplazamiento lateral de la nariz da el yaw real (kLat)
    //   • el desplazamiento vertical de la nariz da el pitch real (kVert)
    // Convenciones de signo (coordenadas imagen):
    //   gazeDir.x > 0  → apunta hacia la derecha del frame
    //   gazeDir.x < 0  → apunta hacia la izquierda del frame
    //   gazeDir.y > 0  → apunta hacia abajo del frame
    //   gazeDir.y < 0  → apunta hacia arriba del frame
    cv::Point2f gazeDir    = {0.f, 0.f}; // vector unitario
    bool        gazeValido = false;     // false si el cálculo no es confiable
    // (cara muy de perfil u oclusión)
};

// LandmarksYuNet

// Los 5 puntos faciales que entrega YuNet en columnas 4-13 de la matriz
// de detección (orden: ojoIzq, ojoDer, nariz, bocaIzq, bocaDer).
// Nota: "izquierda/derecha" es desde el punto de vista
// del detector (imagen), no del sujeto.
struct LandmarksYuNet
{
    cv::Point2f ojoIzq;   // ojo a la izquierda en imagen (derecho del sujeto)
    cv::Point2f ojoDer;   // ojo a la derecha en imagen (izquierdo del sujeto)
    cv::Point2f nariz;
    cv::Point2f bocaIzq;
    cv::Point2f bocaDer;
};

// estimador

class estimador
{
public:
    estimador() = default;

    // versión principal: usa los landmarks de YuNet para calcular yaw, pitch,
    // roll y el vector de gaze 2-D con geometría facial real.
    DatosPose calcularpose(const cv::Rect& rostro,
                           const LandmarksYuNet& lm) const;

    // versión de respaldo sin landmarks: estima yaw y pitch a partir del
    // desplazamiento del centro del rostro respecto al centro del frame.
    // gazeValido siempre es false en este modo.
    DatosPose calcularpose(const cv::Mat& frame,
                           const cv::Rect& rostro) const;

    // proyecta el rayo de gaze desde centroRostro en dirección gazeDir
    // a una distancia distProyeccion y devuelve el punto resultante.
    // útil para visualización y para el test de intersección con la pizarra.
    static cv::Point2f proyectarGaze(cv::Point2f centroRostro,
                                     cv::Point2f gazeDir,
                                     float distProyeccion);

private:
    // comprueba que el rect del rostro cae dentro del frame y tiene tamaño > 0
    bool validarRostro(const cv::Mat& frame, const cv::Rect& rostro) const;
};

#endif // ESTIMADOR_H
