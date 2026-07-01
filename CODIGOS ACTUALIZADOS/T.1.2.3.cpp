// T.1.2.3 - validar la detección del rostro con filtros de confianza

// Archivo: procesador_video.cpp

constexpr double UMBRAL_CONFIANZA   = 0.45;   // score mínimo para aceptar un candidato
constexpr int    YUNET_MIN_FACE_PX  = 15;     // descarta detecciones muy pequeñas (ruido/reflejos)
constexpr float  YUNET_UMBRAL_SCORE = 0.55f;  // score mínimo del propio detector
constexpr float  YUNET_UMBRAL_NMS   = 0.3f;   // elimina detecciones duplicadas de un mismo rostro



const double conf = std::clamp(static_cast<double>(score), 0.0, 1.0);
if (conf >= UMBRAL_CONFIANZA
    && r.width  >= YUNET_MIN_FACE_PX
    && r.height >= YUNET_MIN_FACE_PX)
{
    // sólo aquí se acepta el candidato como rostro válido
    ...
    candidatos.push_back({r, conf, lm});
}
