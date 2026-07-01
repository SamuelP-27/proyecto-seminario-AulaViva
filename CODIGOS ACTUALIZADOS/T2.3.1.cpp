// T2.3.1 - comparar los ángulos reales con los rangos permitidos según la posición

// Archivo: analizador_atencion.cpp (función clasificarPose)

EstadoAtencion EvaluadorAtencion::clasificarPose(
    const DatosPose& pose, const LandmarksYuNet& lm, const cv::Rect& bbox,
    const EstadoPuesto& ep, TipoDistraccion& tipoOut) const
{
    if (!pose.validar) {
        tipoOut = TipoDistraccion::SinRostro;
        return EstadoAtencion::SinDeteccion;
    }

    const bool gazeOk = gazeApuntaAPizarra(pose, lm, bbox, ep.rangos, ep.pizarra);

    const bool pitchAbajo  = (pose.pitch >  ep.rangos.pitchMax);
    const bool pitchArriba = (pose.pitch <  ep.rangos.pitchMin);
    const bool rollFuera   = (std::abs(pose.roll) > ep.rangos.rollTolerancia);
    ...
}