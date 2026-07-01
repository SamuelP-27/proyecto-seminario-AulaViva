// T.1.1.3 - leer los metadatos básicos del video (FPS, duración, resolución)

// Archivo: procesador_video.cpp

const double fps         = cap.get(cv::CAP_PROP_FPS);
const int    totalFrames = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));
const int    ancho       = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
const int    alto        = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));

QMessageBox::information(nullptr, "Metadatos del Video",
                         QString("Resolucion: %1 x %2\nFPS: %3\nFrames Totales: %4")
                             .arg(ancho).arg(alto).arg(fps).arg(totalFrames));