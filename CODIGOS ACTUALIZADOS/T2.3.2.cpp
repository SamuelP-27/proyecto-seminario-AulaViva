// T2.3.2 - definir reglas claras para determinar atención/distracción según la mirada

// Archivo: analizador_atencion.cpp (test de intersección rayo-pizarra, gazeApuntaAPizarra)

bool gazeApuntaAPizarra(const DatosPose& pose, const LandmarksYuNet& lm,
                        const cv::Rect& bboxRostro, const RangosAtencion& rangos,
                        const ConfigPizarra& pizarra)
{
    if (pose.pitch > PITCH_GATE_ABAJO) return false;

    if (!pose.gazeValido) {
        // Fallback: usar yaw clásico cuando no hay landmarks fiables
        return (pose.yaw >= rangos.yawMin && pose.yaw <= rangos.yawMax);
    }

    const cv::Point2f M = (lm.ojoIzq + lm.ojoDer) * 0.5f;
    const cv::Point2f P = estimador::proyectarGaze(M, pose.gazeDir, rangos.distProyeccionGaze);

    if (rangos.bboxPizarraExp.contains(P)) return true;
    if (pizarra.contiene(P))               return true;
    if (segmentoIntersectaRect(M, P, rangos.bboxPizarraExp))   return true;
    if (segmentoIntersectaPizarra(M, P, pizarra))              return true;

    return false;
}

// regla final combinada (AND lógico, documentada al inicio del archivo):

Atento = gazeEnPizarra  AND  pitch ∈ [pitchMin, pitchMax]  AND  |roll| < rollTol