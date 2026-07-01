// T3.2.2 - crear una visualización simple que resuma el desempeño del alumno

// Archivo: reporte_atencion.cpp (función generarTarjetaDesempeño)

cv::Mat generarTarjetaDesempeño(
    const MetricasPuesto& m, const std::vector<EventoDistraccion>& eventos,
    double fps, const ConfigSala& configSala,
    const cv::Mat& frameReferencia, const std::string& rutaSalida)
{
    cv::Mat card(ALTO, ANCHO, CV_8UC3, cv::Scalar(25, 25, 35));

    // Barra de progreso de atención
    const double pct  = std::clamp(m.porcentajeAtencion, 0.0, 100.0);
    const int barFill = static_cast<int>(barAncho * pct / 100.0);
    const cv::Scalar colorBar =
        (pct >= 75.0) ? cv::Scalar(0, 190, 55) :
            (pct >= 50.0) ? cv::Scalar(0, 165, 220) :
            cv::Scalar(40, 40, 200);
    ...
    // KPIs (nº distracciones, duración media/máx), foto del puesto y conclusión
    ...
    if (!rutaSalida.empty()) cv::imwrite(rutaSalida, card);
    return card;
}

// Los KPIs fueron inspirados y modificados de:
// https://github.com/AjNavneet/Brand-KPI-Video-Analysis-OpenCV-TensorFlow
// https://github.com/AmirhosseinHonardoust/Content-KPI-Monitor