// T.1.1.2 - validar la existencia del archivo en el disco

// Archivo: procesador_video.cpp (función procesarVideo)

if (!fs::exists(rutaVideo))
{
    QMessageBox::critical(nullptr, "Error",
                          QString("El archivo no existe:\n%1")
                              .arg(QString::fromStdString(rutaVideo)));
    return;
}

// Adicionalmente, al abrir el flujo de video se valida que cv::VideoCapture realmente pudo abrirlo:

cap.open(rutaVideo);
if (!cap.isOpened())
{
    QMessageBox::critical(nullptr, "Error",
                          "No se pudo abrir el flujo de video.");
    return;
}