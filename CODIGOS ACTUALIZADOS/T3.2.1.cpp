// T3.2.1 - generar un timeline visual de la atención del estudiante

// Archivo: reporte_atencion.cpp (función generarTimelineVisual)

cv::Mat generarTimelineVisual(
    const EvaluadorAtencion& evaluador, const ReporteClase& rep,
    const ConfigSala& configSala, double fps, int totalFrames,
    const std::string& rutaSalida)
{
    ...
    cv::Mat img(ALTO_TOTAL, ANCHO_TOTAL, CV_8UC3, cv::Scalar(30, 30, 30));

    // Eje de tiempo (marcas cada N segundos)
    for (double t = 0.0; t <= durS; t += tickInterval) {
        const int x = MARGEN_IZQ + static_cast<int>(t * segsXPixel);
        cv::line(img, cv::Point(x, MARGEN_SUP - 8),
                 cv::Point(x, ALTO_TOTAL - MARGEN_INF + 4),
                 cv::Scalar(70, 70, 70), 1, cv::LINE_AA);
    }

    // Una fila por alumno: verde=atento, rojo=distraído, gris=sin detección
    for (int row = 0; row < numAlumnos; ++row) {
        ...
    }
    if (!rutaSalida.empty()) cv::imwrite(rutaSalida, img);
    return img;
}