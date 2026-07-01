// estimador.cpp
//
// MODELO DE GAZE 2-D CON LANDMARKS YUNET
// YuNet entrega 5 landmarks por cara: ojo_izq, ojo_der, nariz, boca_izq, boca_der.
//
// Para estimar hacia dónde miran los ojos usamos la geometría del triángulo
// nariz-ojos:
// 1. Centro interocular   M  = (ojoIzq + ojoDer) / 2
// 2. Eje X local de cara  eX = (ojoDer − ojoIzq) normalizado
// 3. Eje Y local de cara  eY = perpendicular a eX (apunta hacia abajo en imagen)
// 4. Desplazamiento nariz desde M:  d = nariz − M
// dx = dot(d, eX)  → lateral (yaw proxy)
// dy = dot(d, eY)  → vertical (pitch proxy)
// 5. Para una cara frontal, dx ≈ 0 y dy > 0 (la nariz siempre queda por
// debajo del centro interocular). Cuando la cabeza gira a la derecha,
// dx > 0.
//
// El VECTOR DE GAZE 2-D se construye como:
// gazeDir = normalize( kLat * eX + kVert * eY )
// donde kLat es dx normalizado por el semiancho interocular y kVert
// es el desplazamiento vertical corregido por un bias de cara frontal.

#include "estimador.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Helpers internos

// producto escalar de dos vectores 2-D
static float dot2(cv::Point2f a, cv::Point2f b) { return a.x*b.x + a.y*b.y; }

// normaliza un vector 2-D; devuelve (0,0) si su longitud es casi cero
static cv::Point2f norm2(cv::Point2f v)
{
    float len = std::sqrt(v.x*v.x + v.y*v.y);
    return (len > 1e-6f) ? cv::Point2f(v.x/len, v.y/len) : cv::Point2f(0.f, 0.f);
}

// calcularpose (versión con landmarks)

DatosPose estimador::calcularpose(const cv::Rect& rostro,
                                  const LandmarksYuNet& lm) const
{
    DatosPose resultado;

    if (rostro.width <= 0 || rostro.height <= 0)
        return resultado;

    // centro interocular M y ancho entre ojos
    const cv::Point2f M        = (lm.ojoIzq + lm.ojoDer) * 0.5f;
    const float       anchOjos = std::abs(lm.ojoDer.x - lm.ojoIzq.x);

    if (anchOjos < 4.0f)    // detección dudosa: cara demasiado pequeña
        return resultado;

    // Detección de inversión del eje interocular
    // En perfiles extremos, los landmarks se cruzan en proyección y ojoDer.x
    // puede quedar a la izquierda de ojoIzq.x. Esto invierte eX y produce
    // yaw y gazeDir con signo opuesto al real. (esto tomo su tiempo para detectar y corregir).
    const bool ejeInvertido = (lm.ojoDer.x < lm.ojoIzq.x);

    // ejes locales de la cara en coordenadas imagen
    const cv::Point2f eX = norm2(lm.ojoDer - lm.ojoIzq); // apunta de izq a der
    const cv::Point2f eY = cv::Point2f(-eX.y, eX.x);    // perpendicular, apunta abajo

    // desplazamiento de la nariz desde el centro interocular
    const cv::Point2f d     = lm.nariz - M;
    const float       dxLoc = dot2(d, eX);   // componente lateral: + = nariz a la derecha
    const float       dyLoc = dot2(d, eY);   // componente vertical: + = nariz abajo (normal)

    resultado.validar = true;

    // yaw proxy: desplazamiento lateral de la nariz / anchura interocular × 90°
    // si el eje está invertido, el signo es opuesto al real; lo corregimos
    resultado.yaw = static_cast<double>(dxLoc / anchOjos) * 90.0;
    if (ejeInvertido)
        resultado.yaw = -resultado.yaw;

    // pitch proxy: desplazamiento vertical de la nariz / alto del bbox × 90°
    resultado.pitch = static_cast<double>((lm.nariz.y - M.y) / rostro.height) * 90.0;

    // roll: ángulo de la línea interocular respecto a la horizontal
    resultado.roll = static_cast<double>(
                         std::atan2(lm.ojoDer.y - lm.ojoIzq.y,
                                    lm.ojoDer.x - lm.ojoIzq.x))
                     * 180.0 / M_PI;

    // Gaze vector 2-D
    //
    // Si el eje está invertido, gazeDir quedaría espejado horizontalmente
    // y haría que el test rayo-pizarra devuelva falsos positivos.
    // Marcamos gazeValido=false y usamos el yaw corregido como fallback.
    if (ejeInvertido) {
        resultado.gazeValido = false;
        std::cerr << "[Estimador] eje interocular invertido (perfil extremo): "
                     "gazeValido=false, yaw corregido a "
                  << resultado.yaw << "°\n";
        return resultado;
    }

    // Gate de perfil extremo
    // Si anchOjos < 35% del ancho del bbox, la cara está tan de perfil que
    // kVert ≈ 0 y gazeDir solo tiene componente X, lo que no discrimina bien
    // la dirección de la mirada. El yaw fallback es más confiable en este caso.
    if (anchOjos < rostro.width * 0.35f) {
        resultado.gazeValido = false;
        std::cerr << "[Estimador] perfil extremo (anchOjos=" << anchOjos
                  << " < " << rostro.width * 0.35f
                  << "): gazeValido=false, usar yaw fallback\n";
        return resultado;
    }

    // factor lateral: dx normalizado por el semiancho interocular,
    // limitado a [-1.5, 1.5] para evitar vectores inestables en perfiles
    const float kLat = std::clamp(dxLoc / (anchOjos * 0.5f), -1.5f, 1.5f);

    // factor vertical: en una cara frontal, la nariz queda ~25% del bbox
    // por debajo del centro interocular (dyNorm ≈ 0.25). Compensamos ese
    // bias para que una mirada frontal produzca gazeDir.y ≈ 0
    const float dyNorm = dyLoc / static_cast<float>(rostro.height);
    const float kVert  = std::clamp((dyNorm - 0.25f) * 4.0f, -1.5f, 1.5f);

    // construye el vector de gaze en coordenadas imagen y normaliza
    const cv::Point2f gazeRaw(kLat * eX.x + kVert * eY.x,
                              kLat * eX.y + kVert * eY.y);
    resultado.gazeDir    = norm2(gazeRaw);
    resultado.gazeValido = (resultado.gazeDir.x != 0.f || resultado.gazeDir.y != 0.f);

    return resultado;
}

// calcularpose (versión legacy sin landmarks)
//
// Estima yaw y pitch únicamente a partir de la posición del centro del
// rostro relativa al centro del frame. No produce gazeDir fiable.
// Se usa como último recurso cuando YuNet no entrega landmarks.

DatosPose estimador::calcularpose(const cv::Mat& frame,
                                  const cv::Rect& rostro) const
{
    DatosPose resultado;
    if (!validarRostro(frame, rostro)) return resultado;
    resultado.validar = true;

    const double centroRostroX = rostro.x + rostro.width  / 2.0;
    const double centroRostroY = rostro.y + rostro.height / 2.0;

    // yaw positivo si el rostro está a la izquierda del centro del frame
    resultado.yaw   = (frame.cols / 2.0 - centroRostroX) / (frame.cols / 2.0) * 90.0;
    resultado.pitch = (frame.rows / 2.0 - centroRostroY) / (frame.rows / 2.0) * 90.0;
    resultado.roll  = 0.0;

    resultado.gazeValido = false;   // sin landmarks no podemos calcular gazeDir
    return resultado;
}

// proyectarGaze
//
// proyecta centroRostro + gazeDir * distProyeccion para obtener el punto
// del frame hacia el que apunta la mirada, útil para visualización y tests.

cv::Point2f estimador::proyectarGaze(cv::Point2f centroRostro,
                                     cv::Point2f gazeDir,
                                     float distProyeccion)
{
    return cv::Point2f(centroRostro.x + gazeDir.x * distProyeccion,
                       centroRostro.y + gazeDir.y * distProyeccion);
}

// validarRostro
//
// comprueba que el frame no está vacío y que el rect del rostro cae
// completamente dentro de los límites del frame

bool estimador::validarRostro(const cv::Mat& frame,
                              const cv::Rect& rostro) const
{
    if (frame.empty())                               return false;
    if (rostro.width <= 0 || rostro.height <= 0)    return false;
    if (rostro.x < 0 || rostro.y < 0)               return false;
    if (rostro.x + rostro.width  > frame.cols)      return false;
    if (rostro.y + rostro.height > frame.rows)      return false;
    return true;
}