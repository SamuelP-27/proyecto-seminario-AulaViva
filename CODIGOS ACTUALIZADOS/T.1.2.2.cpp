// T.1.2.2 - detectar el rostro y generar un cuadro delimitador

// Archivo: procesador_video.cpp (función detectarRostrosEnFrame)

detector->setInputSize(entrada.size());

cv::Mat resultados;  // Nx15: x,y,w,h, 5 landmarks(x,y), score
detector->detect(entrada, resultados);

...
for (int i = 0; i < resultados.rows; ++i)
{
    const float x      = resultados.at<float>(i, 0);
    const float y      = resultados.at<float>(i, 1);
    const float ancho  = resultados.at<float>(i, 2);
    const float alto   = resultados.at<float>(i, 3);
    const float score  = resultados.at<float>(i, 14);

    cv::Rect r(
        static_cast<int>(std::lround(x     * escalaX)),
        static_cast<int>(std::lround(y     * escalaY)),
        static_cast<int>(std::lround(ancho * escalaX)),
        static_cast<int>(std::lround(alto  * escalaY)));
    r &= limites;
    ...
}

// El cuadro delimitador (bounding box) se materializa en la estructura RostroDetectado declarada en procesador_video.h:

struct RostroDetectado
{
    int    x        = 0;
    int    y        = 0;
    int    ancho    = 0;
    int    alto     = 0;
    double confianza = 0.0;
};