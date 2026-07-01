// T1.3.1 - detectar los landmarks faciales para determinar la orientación de la cabeza

// Archivo: estimador.h

// Los 5 puntos faciales que entrega YuNet (orden: ojoIzq, ojoDer, nariz, bocaIzq, bocaDer)
struct LandmarksYuNet
{
    cv::Point2f ojoIzq;
    cv::Point2f ojoDer;
    cv::Point2f nariz;
    cv::Point2f bocaIzq;
    cv::Point2f bocaDer;
};

// estos 5 puntos se extraen directamente de la salida del detector en procesador_video.cpp:

// Columnas 4-13: 5 landmarks (x,y) × 5 puntos
LandmarksYuNet lm;
lm.ojoIzq  = { resultados.at<float>(i,  4) * escalaX, resultados.at<float>(i,  5) * escalaY };
lm.ojoDer  = { resultados.at<float>(i,  6) * escalaX, resultados.at<float>(i,  7) * escalaY };
lm.nariz   = { resultados.at<float>(i,  8) * escalaX, resultados.at<float>(i,  9) * escalaY };
lm.bocaIzq = { resultados.at<float>(i, 10) * escalaX, resultados.at<float>(i, 11) * escalaY };
lm.bocaDer = { resultados.at<float>(i, 12) * escalaX, resultados.at<float>(i, 13) * escalaY };