// analizador_atencion.cpp
// Funcionalidades:
// 1. Se calcula gazeDir (estimador.cpp): vector unitario en coordenadas imagen
// que apunta hacia donde miran los ojos, construido con los landmarks.
// 2. Se lanza un rayo desde el centro interocular M en dirección gazeDir
// a una distancia distProyeccionGaze (proporcional a la distancia real
// puesto→pizarra en el frame).
// 3. Si el punto proyectado cae dentro del bbox EXPANDIDO de la pizarra
// → "mira a la pizarra".
// 4. Si gazeValido==false (cara de perfil extremo, oclusión) → fallback yaw.
//
// CRITERIO FINAL (AND lógico):
// Atento = gazeEnPizarra  AND  pitch ∈ [pitchMin, pitchMax]  AND  |roll| < rollTol
//
// Si gazeValido==false:
// Atento = yaw ∈ [yawMin, yawMax]  AND  pitch ∈ rango  AND  |roll| < rollTol

#include "analizador_atencion.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <iostream>
#include <cmath>
#include <limits>
#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

// Constantes globales de calibración

// Fallback yaw (usado cuando gazeValido==false)
static constexpr double K_YAW_UMBRAL     = 0.55;
static constexpr double YAW_LIMITE       = 110.0;
static constexpr double MARGEN_MINIMO_YAW = 5.0;

// Pitch fijo calibrado
static constexpr double PITCH_MIN_DEF   = -25.0;
static constexpr double PITCH_MAX_DEF   =  35.0;

// Helpers geométricos para gazeApuntaAPizarra

// Intersección segmento [p0,p1] vs rectángulo axis-aligned r usando Liang-Barsky (algoritmo de recorte de líneas).
// Recorta el segmento contra los cuatro planos del rectángulo actualizando
// tMin/tMax; si al final tMin > tMax, el segmento no lo toca.
static bool segmentoIntersectaRect(cv::Point2f p0, cv::Point2f p1, const cv::Rect2f& r)
{
    float tMin = 0.f, tMax = 1.f;
    const float dx = p1.x - p0.x;
    const float dy = p1.y - p0.y;

    auto clip = [&](float p, float q) -> bool {
        if (std::abs(p) < 1e-6f) return q >= 0.f;   // paralelo: dentro si q>=0
        const float t = q / p;
        if (p < 0.f) {
            if (t > tMax) return false;
            if (t > tMin) tMin = t;
        } else {
            if (t < tMin) return false;
            if (t < tMax) tMax = t;
        }
        return true;
    };

    if (!clip(-dx, p0.x - r.x))                 return false;
    if (!clip( dx, r.x + r.width  - p0.x))      return false;
    if (!clip(-dy, p0.y - r.y))                 return false;
    if (!clip( dy, r.y + r.height - p0.y))      return false;
    return tMin <= tMax;
}

// Intersección segmento [p0,p1] vs el rectángulo ROTADO de la pizarra.
// Transforma ambos extremos al espacio local de la pizarra (donde el
// rectángulo queda axis-aligned) y reutiliza segmentoIntersectaRect.
static bool segmentoIntersectaPizarra(cv::Point2f p0, cv::Point2f p1,
                                      const ConfigPizarra& pizarra)
{
    const float rad  = pizarra.angulo * static_cast<float>(M_PI) / 180.f;
    const float cosA = std::cos(-rad);
    const float sinA = std::sin(-rad);

    auto aLocal = [&](cv::Point2f p) -> cv::Point2f {
        const float dx = p.x - pizarra.centro.x;
        const float dy = p.y - pizarra.centro.y;
        return cv::Point2f(dx * cosA - dy * sinA, dx * sinA + dy * cosA);
    };

    const cv::Point2f l0 = aLocal(p0);
    const cv::Point2f l1 = aLocal(p1);
    const cv::Rect2f  rectLocal(-pizarra.ancho / 2.f, -pizarra.alto / 2.f,
                               pizarra.ancho, pizarra.alto);
    return segmentoIntersectaRect(l0, l1, rectLocal);
}

// gazeApuntaAPizarra  — TEST PRINCIPAL

bool gazeApuntaAPizarra(const DatosPose&     pose,
                        const LandmarksYuNet& lm,
                        const cv::Rect&       bboxRostro,
                        const RangosAtencion& rangos,
                        const ConfigPizarra&  pizarra)
{
    // Gate de pitch: si mira claramente hacia abajo, nunca apunta a la pizarra
    static constexpr double PITCH_GATE_ABAJO = 32.0;
    if (pose.pitch > PITCH_GATE_ABAJO) return false;

    if (!pose.gazeValido) {
        // Fallback: usar yaw clásico cuando no hay landmarks fiables
        return (pose.yaw >= rangos.yawMin && pose.yaw <= rangos.yawMax);
    }

    // Centro interocular como origen del rayo
    const cv::Point2f M = (lm.ojoIzq + lm.ojoDer) * 0.5f;

    // Punto proyectado por el rayo de gaze
    const cv::Point2f P = estimador::proyectarGaze(M, pose.gazeDir,
                                                   rangos.distProyeccionGaze);

    // Test 1: se verifica si P cae dentro del bbox expandido de la pizarra
    if (rangos.bboxPizarraExp.contains(P))
        return true;

    // Test 2: se verifica si P cae dentro del polígono rotado de la pizarra
    if (pizarra.contiene(P))
        return true;

    // Test 3: intersección geométrica del segmento [M,P] contra
    // el bbox expandido y contra el polígono rotado de la pizarra. Detecta
    // cualquier cruce del rayo con la zona objetivo, sin depender de cuántos
    // puntos se muestreen ni de la distancia de proyección.
    if (segmentoIntersectaRect(M, P, rangos.bboxPizarraExp))
        return true;

    if (segmentoIntersectaPizarra(M, P, pizarra))
        return true;

    return false;
}

// calcularRangosParaPuesto

// Conservadas por compatibilidad (usadas por calcularRangosParaPuesto y por
// el JSON de rangos).
void calcularRangosYaw(cv::Point2f centroPuesto,
                       cv::Point2f extremoIzqPiz,
                       cv::Point2f extremoDerPiz,
                       double      toleranciaExtra,
                       double&     yawMinOut,
                       double&     yawMaxOut)
{
    const cv::Point2f centroPiz = (extremoIzqPiz + extremoDerPiz) * 0.5f;
    const double dist = cv::norm(centroPuesto - centroPiz);
    if (dist < 1.0) {
        yawMinOut = -MARGEN_MINIMO_YAW;
        yawMaxOut =  MARGEN_MINIMO_YAW;
        return;
    }
    const double dxCentro  = static_cast<double>(centroPiz.x - centroPuesto.x);
    const double theta_deg = std::atan2(dxCentro, dist) * 180.0 / M_PI;
    const double umbral    = theta_deg * K_YAW_UMBRAL;

    if (theta_deg < 0.0) {
        yawMaxOut = umbral + toleranciaExtra;
        yawMinOut = -YAW_LIMITE;
    } else if (theta_deg > 0.0) {
        yawMinOut = umbral - toleranciaExtra;
        yawMaxOut = +YAW_LIMITE;
    } else {
        yawMinOut = -MARGEN_MINIMO_YAW - toleranciaExtra;
        yawMaxOut = +MARGEN_MINIMO_YAW + toleranciaExtra;
    }
}

void calcularRangosPitch(cv::Point2f centroPuesto,
                         cv::Point2f centroPizarra,
                         float       altoPizarra,
                         int         altoFrame,
                         double      toleranciaExtra,
                         double&     pitchMinOut,
                         double&     pitchMaxOut)
{
    pitchMinOut = PITCH_MIN_DEF - toleranciaExtra;
    pitchMaxOut = PITCH_MAX_DEF + toleranciaExtra;

    if (altoFrame > 0 && altoPizarra > 0) {
        const double dy = static_cast<double>(centroPizarra.y - centroPuesto.y);
        if (dy / altoFrame < -0.10)
            pitchMinOut = std::min(pitchMinOut, PITCH_MIN_DEF - 10.0 - toleranciaExtra);
    }

    pitchMinOut = std::max(pitchMinOut, -60.0);
    pitchMaxOut = std::min(pitchMaxOut,  55.0);
}

RangosAtencion calcularRangosParaPuesto(
    const PuestoEstudiante& puesto,
    const ConfigPizarra&    pizarra,
    int anchoFrame,
    int altoFrame,
    double toleranciaExtra)
{
    RangosAtencion rangos;
    rangos.toleranciaExtra = toleranciaExtra;

    const cv::Point2f centroPuesto(
        puesto.rect.x + puesto.rect.width  / 2.f,
        puesto.rect.y + puesto.rect.height / 2.f);

    const auto verts = pizarra.vertices();

    // Extremos horizontales para fallback yaw
    cv::Point2f extremoIzq = verts[0], extremoDer = verts[0];
    for (const auto& v : verts) {
        if (v.x < extremoIzq.x) extremoIzq = v;
        if (v.x > extremoDer.x) extremoDer = v;
    }

    // El yaw proxy del estimador tiene escala distinta al yaw geométrico real,
    // así que se necesita margen extra para que el rango [yawMin, yawMax]
    // capture las lecturas reales cuando el alumno mira lateralmente a la pizarra.
    const double toleranciaExtraYaw = std::max(toleranciaExtra, 12.0);
    calcularRangosYaw(centroPuesto, extremoIzq, extremoDer,
                      toleranciaExtraYaw, rangos.yawMin, rangos.yawMax);

    calcularRangosPitch(centroPuesto, pizarra.centro,
                        pizarra.alto, altoFrame,
                        toleranciaExtra, rangos.pitchMin, rangos.pitchMax);

    rangos.rollTolerancia = 22.0;

    // Guardamos el centro del puesto para que gazeApuntaAPizarra pueda
    // calcular el ángulo puesto→pizarra sin parámetros extra.
    rangos.centroPuesto = centroPuesto;

    // Bbox expandida de la pizarra para el test de gaze
    // Calculamos el bounding box del polígono rotado y lo expandimos con un
    // margen en píxeles acotado por la distancia puesto→pizarra.
    float xMin = verts[0].x, xMax = verts[0].x;
    float yMin = verts[0].y, yMax = verts[0].y;
    for (const auto& v : verts) {
        xMin = std::min(xMin, v.x); xMax = std::max(xMax, v.x);
        yMin = std::min(yMin, v.y); yMax = std::max(yMax, v.y);
    }
    const float anchoP = xMax - xMin;
    const float altoP  = yMax - yMin;

    // Distancia puesto→pizarra (calculada antes del bloque de distProyeccionGaze)
    const float distParaMargen = static_cast<float>(cv::norm(centroPuesto - pizarra.centro));

    // Margen máximo en px = 25% de la distancia.
    const float margenMaxPx = std::max(distParaMargen * 0.25f, 20.f);
    const float mx = std::min(anchoP * static_cast<float>(rangos.margenRelativoPizarra), margenMaxPx);
    const float my = std::min(altoP  * static_cast<float>(rangos.margenRelativoPizarra), margenMaxPx);
    rangos.bboxPizarraExp = cv::Rect2f(xMin - mx, yMin - my,
                                       anchoP + 2*mx, altoP + 2*my);

    // Distancia de proyección del rayo
    rangos.distProyeccionGaze = std::max(distParaMargen * 2.0f, 300.f);

    // Margen angular (guardado en struct por compatibilidad con JSON)
    // Se conserva en la struct y en el JSON para diagnóstico, pero gazeApuntaAPizarra
    // usa exclusivamente el test de intersección rayo-pizarra en píxeles.
    static constexpr double MARGEN_ANGULAR_TOLERANCIA = 12.0;
    static constexpr double MARGEN_ANGULAR_MIN        = 10.0;
    static constexpr double MARGEN_ANGULAR_MAX        = 40.0;
    const float distSeguro = std::max(distParaMargen, 50.f);
    const double semiAnguloPizarra =
        std::atan2(static_cast<double>(pizarra.ancho) / 2.0,
                   static_cast<double>(distSeguro)) * 180.0 / M_PI;
    rangos.margenAngularPizarraDeg = std::clamp(
        semiAnguloPizarra + MARGEN_ANGULAR_TOLERANCIA,
        MARGEN_ANGULAR_MIN, MARGEN_ANGULAR_MAX);

    std::cerr << "[Rangos puesto #" << puesto.id
              << " '" << puesto.estudiante << "']"
              << " yaw_fb=[" << std::fixed << std::setprecision(1)
              << rangos.yawMin << "," << rangos.yawMax << "]"
              << " pitch=[" << rangos.pitchMin << "," << rangos.pitchMax << "]"
              << " distGaze=" << rangos.distProyeccionGaze << "px"
              << " margenAngular=" << rangos.margenAngularPizarraDeg << "deg"
              << " bboxPiz=(" << rangos.bboxPizarraExp.x << ","
              << rangos.bboxPizarraExp.y << ","
              << rangos.bboxPizarraExp.width << ","
              << rangos.bboxPizarraExp.height << ")\n";

    return rangos;
}

// EvaluadorAtencion

EvaluadorAtencion::EvaluadorAtencion(const ConfigSala& configSala,
                                     int ventanaTemporalFrames,
                                     double fps)
    : ventana_(std::max(1, ventanaTemporalFrames))
    , fps_(fps > 0.0 ? fps : 25.0)
{
    for (const auto& p : configSala.puestos) {
        EstadoPuesto ep;
        ep.idPuesto     = p.id;
        ep.nombreAlumno = p.estudiante;
        ep.pizarra      = configSala.pizarra;
        ep.rangos       = calcularRangosParaPuesto(
            p, configSala.pizarra,
            configSala.anchoFrame, configSala.altoFrame);
        ep.bufferEstados.assign(ventana_, EstadoAtencion::SinDeteccion);
        // NOTA: countFueraRango es un contador diferencial sobre el buffer
        // circular (resta lo que sale, suma lo que entra). El buffer arranca
        // lleno de SinDeteccion, que cuenta como "no-atento", así que el
        // contador debe arrancar en ventana_ para ser consistente con el
        // contenido real del buffer. Si se deja en 0 (valor por defecto),
        // cada frame distraído que entra solo cancela al "no-atento" inicial
        // que sale, y countFueraRango nunca llega a >= ventana_: el sistema
        // jamás confirma un estado Distraido y la atención queda en 100%
        // sin importar lo que detecte clasificarPose.
        ep.countFueraRango = ventana_;
        puestos_.push_back(std::move(ep));
    }
}

// clasificarPose

EstadoAtencion EvaluadorAtencion::clasificarPose(
    const DatosPose&    pose,
    const LandmarksYuNet& lm,
    const cv::Rect&     bbox,
    const EstadoPuesto& ep,
    TipoDistraccion&    tipoOut) const
{
    if (!pose.validar) {
        tipoOut = TipoDistraccion::SinRostro;
        return EstadoAtencion::SinDeteccion;
    }

    // 1. Test de gaze (criterio principal)
    const bool gazeOk = gazeApuntaAPizarra(pose, lm, bbox, ep.rangos, ep.pizarra);

    // 2. Pitch y Roll (independientes del gaze)
    const bool pitchAbajo = (pose.pitch >  ep.rangos.pitchMax);
    const bool pitchArriba= (pose.pitch <  ep.rangos.pitchMin);
    const bool rollFuera  = (std::abs(pose.roll) > ep.rangos.rollTolerancia);

    // El alumno está Atento solo si:
    // - El gaze apunta a la pizarra (o el yaw fallback lo indica)
    // - El pitch es razonable (no mira la mesa ni el techo)
    // - El roll no es extremo
    const int nFuera = (!gazeOk ? 1 : 0)
                       + (pitchAbajo || pitchArriba ? 1 : 0)
                       + (rollFuera  ? 1 : 0);

    if (nFuera == 0) {
        tipoOut = TipoDistraccion::Ninguna;
        return EstadoAtencion::Atento;
    }
    if (nFuera >= 2) {
        tipoOut = TipoDistraccion::Combinada;
    } else if (!gazeOk) {
        tipoOut = TipoDistraccion::MiradaLateral;
    } else if (pitchAbajo) {
        tipoOut = TipoDistraccion::MiradaAbajo;
    } else if (pitchArriba) {
        tipoOut = TipoDistraccion::MiradaArriba;
    } else {
        tipoOut = TipoDistraccion::CabeceoBrusco;
    }

    return EstadoAtencion::Distraido;
}

// actualizarVentana
// Implementa un buffer circular de tamaño ventana_ que suaviza las clasificaciones
// frame a frame. Un estado Distraido solo se confirma si la MAYORÍA de los
// frames dentro de la ventana son no-Atento (countFueraRango >= ventana_),
// lo que evita parpadeos por detecciones ruidosas en frames aislados.

EstadoAtencion EvaluadorAtencion::actualizarVentana(
    EstadoPuesto&    ep,
    EstadoAtencion   estadoBruto,
    TipoDistraccion& tipoInOut) const
{
    // Saca el estado más antiguo del buffer y mete el estado nuevo
    EstadoAtencion saliente = ep.bufferEstados[ep.indiceBuffer];
    ep.bufferEstados[ep.indiceBuffer] = estadoBruto;
    ep.indiceBuffer = (ep.indiceBuffer + 1) % ventana_;

    // Actualiza el contador diferencial: resta el que salió, suma el que entró
    if (saliente   != EstadoAtencion::Atento) ep.countFueraRango--;
    if (estadoBruto != EstadoAtencion::Atento) ep.countFueraRango++;

    // Si toda la ventana está fuera de rango, confirma el estado bruto
    if (ep.countFueraRango >= ventana_)
        return estadoBruto;

    // Mientras la ventana no esté saturada de distracciones, reporta Atento
    tipoInOut = TipoDistraccion::Ninguna;
    return EstadoAtencion::Atento;
}

// Gestión de eventos
// callback: marca el inicio de un episodio de distracción (un evento) para el puesto.
// Guarda el frame y tiempo de inicio, no hace nada si ya hay uno abierto.
void EvaluadorAtencion::abrirEvento(EstadoPuesto& ep, int frame, TipoDistraccion tipo)
{
    if (ep.enDistraccion) return;
    ep.enDistraccion                = true;
    ep.eventoActivo.frameInicio     = frame;
    ep.eventoActivo.tiempoInicioS   = frame / fps_;
    ep.eventoActivo.tipo            = tipo;
    ep.eventoActivo.idPuesto        = ep.idPuesto;
    ep.eventoActivo.nombreAlumno    = ep.nombreAlumno;
}

// callback: cierra el episodio de distracción activo, registra el frame final
// y lo empuja a la lista global de eventos. No hace nada si no hay evento abierto.
void EvaluadorAtencion::cerrarEvento(EstadoPuesto& ep, int frame)
{
    if (!ep.enDistraccion) return;
    ep.eventoActivo.frameFin   = frame;
    ep.eventoActivo.tiempoFinS = frame / fps_;
    eventos_.push_back(ep.eventoActivo);
    ep.enDistraccion = false;
}

// Búsqueda de puesto

EvaluadorAtencion::EstadoPuesto*
EvaluadorAtencion::buscarPuesto(int idPuesto)
{
    for (auto& ep : puestos_)
        if (ep.idPuesto == idPuesto) return &ep;
    return nullptr;
}

const EvaluadorAtencion::EstadoPuesto*
EvaluadorAtencion::buscarPuesto(int idPuesto) const
{
    for (const auto& ep : puestos_)
        if (ep.idPuesto == idPuesto) return &ep;
    return nullptr;
}

// evaluarFrame (con Landmarks) (se entiende su función)

ResultadoFrameAtencion EvaluadorAtencion::evaluarFrame(
    const DatosPose&      pose,
    const LandmarksYuNet& lm,
    const cv::Rect&       bboxRostro,
    int                   idPuesto,
    int                   frame)
{
    ResultadoFrameAtencion res;
    res.frame      = frame;
    res.idPuesto   = idPuesto;
    res.yaw        = pose.yaw;
    res.pitch      = pose.pitch;
    res.roll       = pose.roll;
    res.poseValida = pose.validar;
    res.gazeEnPizarra = pose.gazeValido
                            ? false   // se actualiza abajo
                            : false;

    EstadoPuesto* ep = buscarPuesto(idPuesto);
    if (!ep) {
        res.estado      = pose.validar ? EstadoAtencion::Atento : EstadoAtencion::SinDeteccion;
        res.tipoDistrac = pose.validar ? TipoDistraccion::Ninguna : TipoDistraccion::SinRostro;
        historial_.push_back(res);
        return res;
    }
    res.nombreAlumno = ep->nombreAlumno;

    // Pre-calcular gazeEnPizarra para el log
    if (pose.validar && pose.gazeValido)
        res.gazeEnPizarra = gazeApuntaAPizarra(pose, lm, bboxRostro,
                                               ep->rangos, ep->pizarra);

    // Clasificación en dos pasos:
    // 1. clasificarPose → estado bruto frame a frame (puede ser ruidoso)
    // 2. actualizarVentana → suaviza con buffer circular y produce el estado confirmado
    TipoDistraccion tipoBruto = TipoDistraccion::Ninguna;
    EstadoAtencion  estadoBruto = clasificarPose(pose, lm, bboxRostro, *ep, tipoBruto);

    TipoDistraccion tipoConfirmado = tipoBruto;
    EstadoAtencion  estadoConfirmado = actualizarVentana(*ep, estadoBruto, tipoConfirmado);

    const EstadoAtencion estadoAnterior = ep->estadoActual;
    ep->estadoActual = estadoConfirmado;

    // Detecta transiciones de estado para abrir/cerrar eventos de distracción.
    // Si el tipo cambia dentro de un evento ya abierto, actualiza el tipo en curso.
    if (estadoConfirmado != EstadoAtencion::Atento && estadoAnterior == EstadoAtencion::Atento)
        abrirEvento(*ep, frame, tipoConfirmado);
    else if (estadoConfirmado == EstadoAtencion::Atento && estadoAnterior != EstadoAtencion::Atento)
        cerrarEvento(*ep, frame);
    else if (ep->enDistraccion && tipoConfirmado != TipoDistraccion::Ninguna)
        ep->eventoActivo.tipo = tipoConfirmado;

    res.estado      = estadoConfirmado;
    res.tipoDistrac = tipoConfirmado;

    historial_.push_back(res);
    return res;
}

// evaluarFrame (compatibilidad sin Landmarks)

ResultadoFrameAtencion EvaluadorAtencion::evaluarFrame(
    const DatosPose& pose,
    int idPuesto,
    int frame)
{
    // Sin landmarks no podemos calcular gaze → usamos landmarks nulos
    LandmarksYuNet lmVacio{};
    cv::Rect       bboxVacio{};
    return evaluarFrame(pose, lmVacio, bboxVacio, idPuesto, frame);
}

// ════════════════════════════════════════════════════════════════════════════
// MÓDULO: Métricas de Atención por Puesto
// ════════════════════════════════════════════════════════════════════════════

// Calcula y consolida las estadísticas de atención individuales para cada puesto.
// Recorre el historial de frames y eventos acumulados para estructurar las métricas finales.
std::vector<MetricasPuesto> EvaluadorAtencion::metricasPorPuesto() const
{
    std::vector<MetricasPuesto> metricas;

    // Iteramos por cada puesto de estudiante registrado en la sala
    for (const auto& ep : puestos_) {
        MetricasPuesto m;
        m.idPuesto     = ep.idPuesto;
        m.nombreAlumno = ep.nombreAlumno;

        // 1. Procesamiento del Historial de Frames
        // Filtramos los registros del historial global que corresponden a este puesto específico
        for (const auto& r : historial_) {
            if (r.idPuesto != ep.idPuesto) continue; // Si no es el alumno actual, saltamos

            m.framesAnalizados++;

            // Clasificamos el estado del frame según los enumeradores de atención
            switch (r.estado) {
            case EstadoAtencion::Atento:       m.framesAtento++;       break;
            case EstadoAtencion::Distraido:    m.framesDistraido++;    break;
            case EstadoAtencion::SinDeteccion: m.framesSinDeteccion++; break;
            }
        }

        // 2. Cálculo del Porcentaje de Atención
        // Solo consideramos los frames donde el algoritmo logró una detección válida (Atento + Distraído)
        const int framesConDet = m.framesAtento + m.framesDistraido;
        m.porcentajeAtencion = (framesConDet > 0)
                                   ? (100.0 * m.framesAtento / framesConDet) : 0.0;

        // 3. Procesamiento de Eventos de Distracción Crónica
        // Los eventos representan lapsos continuos de tiempo en los que el alumno estuvo distraído
        double durMax = 0.0, durTotal = 0.0;
        int nEvt = 0;
        for (const auto& ev : eventos_) {
            if (ev.idPuesto != ep.idPuesto) continue; // Filtrado por puesto

            // Duración del evento en segundos (Fin - Inicio)
            const double dur = ev.tiempoFinS - ev.tiempoInicioS;
            durTotal += dur;
            durMax    = std::max(durMax, dur); // Mantiene el registro de la distracción más larga
            ++nEvt; // Contador de eventos de distracción
        }

        // Guardamos los datos temporales consolidados en la estructura del puesto
        m.numEventosDistrac       = nEvt;
        m.duracionMaxDistracS     = durMax;
        m.duracionMediaDistracS   = (nEvt > 0) ? (durTotal / nEvt) : 0.0;

        metricas.push_back(m);
    }
    return metricas;
}

// ════════════════════════════════════════════════════════════════════════════
// MÓDULO: Consulta de Rangos
// ════════════════════════════════════════════════════════════════════════════

// Devuelve los límites angulares (Yaw, Pitch, Roll) permitidos para un puesto específico.
const RangosAtencion* EvaluadorAtencion::rangosParaPuesto(int idPuesto) const
{
    const EstadoPuesto* ep = buscarPuesto(idPuesto);
    return ep ? &ep->rangos : nullptr; // Si el puesto existe, retorna el puntero a sus rangos, si no, nullptr
}

// ════════════════════════════════════════════════════════════════════════════
// MÓDULO: Exportación de Reportes en Formato CSV
// ════════════════════════════════════════════════════════════════════════════

// Exporta el desglose completo frame a frame de cada puesto analizado en la sesión.
bool EvaluadorAtencion::guardarCSVFrames(const std::string& ruta) const
{
    std::ofstream f(ruta);
    if (!f.is_open()) return false; // Error de apertura o permisos de escritura

    // Cabecera del archivo CSV (Telemetría cruda y orientación de cabeza)
    f << "frame,id_puesto,alumno,estado,tipo_distraccion,"
         "yaw,pitch,roll,pose_valida,gaze_en_pizarra\n";

    for (const auto& r : historial_) {
        f << r.frame << ',' << r.idPuesto << ','
          << r.nombreAlumno << ','
          << nombreEstado(r.estado) << ','                     // Convierte enum a string legible
          << nombreTipoDistraccion(r.tipoDistrac) << ','         // Convierte enum de distracción a string
          << std::fixed << std::setprecision(2)                  // Formatea los ángulos flotantes a 2 decimales
          << r.yaw << ',' << r.pitch << ',' << r.roll << ','
          << (r.poseValida ? 1 : 0) << ','                      // Booleano numérico (0 o 1)
          << (r.gazeEnPizarra ? 1 : 0) << '\n';
    }
    return f.good(); // Retorna true si la persistencia en disco se completó correctamente
}

// Exporta la lista de eventos o intervalos de tiempo donde se detectaron distracciones consecutivas.
bool EvaluadorAtencion::guardarCSVEventos(const std::string& ruta) const
{
    std::ofstream f(ruta);
    if (!f.is_open()) return false;

    // Cabecera del archivo CSV de eventos temporales
    f << "id_puesto,alumno,frame_inicio,frame_fin,"
         "tiempo_inicio_s,tiempo_fin_s,duracion_s,tipo_distraccion\n";

    for (const auto& ev : eventos_) {
        const double dur = ev.tiempoFinS - ev.tiempoInicioS;
        f << ev.idPuesto << ',' << ev.nombreAlumno << ','
          << ev.frameInicio << ',' << ev.frameFin << ','
          << std::fixed << std::setprecision(3)                  // Precisión de 3 decimales para marcas de tiempo (ms)
          << ev.tiempoInicioS << ',' << ev.tiempoFinS << ','
          << dur << ',' << nombreTipoDistraccion(ev.tipo) << '\n';
    }
    return f.good();
}

// Exporta el resumen estadístico final de rendimiento de atención por cada alumno de la sesión.
bool EvaluadorAtencion::guardarCSVMetricas(const std::string& ruta) const
{
    std::ofstream f(ruta);
    if (!f.is_open()) return false;

    // Cabecera del archivo CSV consolidado
    f << "id_puesto,alumno,frames_analizados,frames_atento,"
         "frames_distraido,frames_sin_deteccion,porcentaje_atencion,"
         "num_eventos_distraccion,dur_media_distrac_s,dur_max_distrac_s\n";

    // Hace uso de la función interna metricasPorPuesto() para volcar los objetos procesados
    for (const auto& m : metricasPorPuesto()) {
        f << m.idPuesto << ',' << m.nombreAlumno << ','
          << m.framesAnalizados << ',' << m.framesAtento << ','
          << m.framesDistraido << ',' << m.framesSinDeteccion << ','
          << std::fixed << std::setprecision(1) << m.porcentajeAtencion << ',' // 1 decimal para el porcentaje (ej: 92.5%)
          << m.numEventosDistrac << ','
          << std::fixed << std::setprecision(2)
          << m.duracionMediaDistracS << ',' << m.duracionMaxDistracS << '\n';
    }
    return f.good();
}

// Entrada/Salida JSON (Rangos de Atención)

// Genera un archivo JSON plano que contiene las calibraciones calculadas para cada puesto
// respecto a la pizarra de la sala actual.
bool guardarRangosAtencion(const std::string& ruta,
                           const ConfigSala&  config,
                           double toleranciaExtra)
{
    std::ofstream f(ruta);
    if (!f.is_open()) return false;
    f << "{\n";
    f << "  \"tolerancia_extra\": " << toleranciaExtra << ",\n";
    f << "  \"rangos_puestos\": [\n";
    // Recorremos los puestos configurados
    for (size_t i = 0; i < config.puestos.size(); ++i) {
        const auto& p = config.puestos[i];
        // Aca se proyecta los vectores desde el puesto del alumno hacia los bordes de la pizarra
        const RangosAtencion r = calcularRangosParaPuesto(
            p, config.pizarra,
            config.anchoFrame, config.altoFrame, toleranciaExtra);
        // Volcado de propiedades del JSON representando el puesto
        f << "    {\n";
        f << "      \"id_puesto\": "        << p.id             << ",\n";
        f << "      \"nombre_alumno\": \""  << p.estudiante     << "\",\n";
        f << "      \"yaw_min\": "          << r.yawMin         << ",\n";
        f << "      \"yaw_max\": "          << r.yawMax         << ",\n";
        f << "      \"pitch_min\": "        << r.pitchMin       << ",\n";
        f << "      \"pitch_max\": "        << r.pitchMax       << ",\n";
        f << "      \"roll_tolerancia\": "  << r.rollTolerancia << ",\n";
        f << "      \"dist_gaze\": "        << r.distProyeccionGaze << ",\n";
        f << "      \"centro_puesto_x\": "  << r.centroPuesto.x << ",\n";
        f << "      \"centro_puesto_y\": "  << r.centroPuesto.y << ",\n";
        f << "      \"margen_angular_piz\": " << r.margenAngularPizarraDeg << "\n";
        // Manejo de la coma
        f << "    }" << (i + 1 < config.puestos.size() ? "," : "") << "\n";
    }
    f << "  ]\n}\n";
    return f.good();
}

// Alternativa para no depender de librerías externas

// Busca una clave numérica de tipo flotante dentro de una cadena de texto JSON y extrae su valor.
static bool leerDoubleLocal(const std::string& json,
                            const std::string& clave, double& val)
{
    const std::string buscar = "\"" + clave + "\":";
    auto pos = json.find(buscar);
    if (pos == std::string::npos) return false; // Clave inexistente en el substring

    pos += buscar.size();

    // Saltamos espacios en blanco o saltos de línea hasta alcanzar el valor numérico
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n')) ++pos;

    try {
        // Convierte los caracteres numéricos a tipo double (std::stod detiene la lectura al toparse con comas o llaves)
        val = std::stod(json.substr(pos));
        return true;
    }
    catch (...) {
        return false; // Error (nooooooo)
    }
}

// Reutiliza el lector flotante local convirtiendo de forma segura el resultado a entero.
static bool leerIntLocal(const std::string& json,
                         const std::string& clave, int& val)
{
    double d = 0;
    if (!leerDoubleLocal(json, clave, d)) return false;
    val = static_cast<int>(d); // Truncamiento
    return true;
}

// Carga e interpreta el archivo de rangos guardado en disco para reconstruir el vector en memoria.
bool cargarRangosAtencion(const std::string& ruta,
                          const ConfigSala&  config,
                          std::vector<std::pair<int, RangosAtencion>>& rangosOut)
{
    std::ifstream f(ruta);
    if (!f.is_open()) return false;

    // Leemos todo el contenido del archivo JSON directamente a un buffer en string
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string json = ss.str();
    rangosOut.clear();
    // Localizamos el inicio del array que contiene los puestos
    auto arrPos = json.find("\"rangos_puestos\":");
    if (arrPos == std::string::npos) return false;

    auto arrIni = json.find('[', arrPos);
    auto arrFin = json.rfind(']');
    if (arrIni == std::string::npos || arrFin == std::string::npos) return false;

    // Tokenizador de objetos encerrados en llaves {} dentro del arreglo []
    size_t cur = arrIni + 1;
    while (cur < arrFin) {
        auto objIni = json.find('{', cur);
        if (objIni == std::string::npos || objIni >= arrFin) break; // Fin de elementos válidos

        // Control de balance de llaves anidadas para recortar exactamente el bloque del puesto actual
        int depth = 1;
        size_t objFin = objIni + 1;
        while (objFin < json.size() && depth > 0) {
            if      (json[objFin] == '{') ++depth;
            else if (json[objFin] == '}') --depth;
            ++objFin;
        }

        // Subcadena aislada que contiene las propiedades exclusivas de un solo alumno/puesto
        const std::string obj = json.substr(objIni, objFin - objIni);

        int id = -1;
        leerIntLocal(obj, "id_puesto", id);

        RangosAtencion r;
        double v = 0;

        // Extracción uno a uno de los campos de calibración geométrica y tolerancia
        if (leerDoubleLocal(obj, "yaw_min",        v)) r.yawMin         = v;
        if (leerDoubleLocal(obj, "yaw_max",        v)) r.yawMax         = v;
        if (leerDoubleLocal(obj, "pitch_min",      v)) r.pitchMin       = v;
        if (leerDoubleLocal(obj, "pitch_max",      v)) r.pitchMax       = v;
        if (leerDoubleLocal(obj, "roll_tolerancia",v)) r.rollTolerancia = v;
        if (leerDoubleLocal(obj, "dist_gaze",      v)) r.distProyeccionGaze = static_cast<float>(v);
        if (leerDoubleLocal(obj, "centro_puesto_x", v)) r.centroPuesto.x = static_cast<float>(v);
        if (leerDoubleLocal(obj, "centro_puesto_y", v)) r.centroPuesto.y = static_cast<float>(v);
        if (leerDoubleLocal(obj, "margen_angular_piz", v)) r.margenAngularPizarraDeg = v;

        // Si se leyó un ID coherente, se genera al mapeo de salida
        if (id >= 0) rangosOut.emplace_back(id, r);
        cur = objFin; // Avanzamos el cursor al final de este objeto procesado
    }

    // Validación de Integridad:
    // Comprobamos que todos los puestos declarados en la configuración física actual de la sala (config.puestos)
    // posean su contraparte de rangos cargada exitosamente desde el JSON.
    for (const auto& p : config.puestos) {
        bool encontrado = false;
        for (const auto& par : rangosOut)
            if (par.first == p.id) { encontrado = true; break; }
        if (!encontrado) return false; // Si falta un solo alumno por mapear, la carga se declara corrupta/inválida
    }

    return !rangosOut.empty();
}