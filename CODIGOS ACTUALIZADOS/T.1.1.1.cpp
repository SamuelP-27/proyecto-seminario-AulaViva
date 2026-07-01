// T.1.1.1 - seleccionar el archivo MP4 grabado en el aula

// Archivo: main.cpp

// 1. Selección del video de clase
const QString rutaVideo = QFileDialog::getOpenFileName(
    nullptr,
    "Selecciona el video de la clase",
    QString(),
    "Videos (*.mp4 *.avi *.mkv *.mov);;Todos los archivos (*.*)");
if (rutaVideo.isEmpty()) {
    // El usuario cerró el diálogo sin elegir nada: terminamos sin error.
    std::cout << "Operación cancelada: no se seleccionó ningún archivo.\n";
    return 0;
}

// Existe además una versión equivalente y reutilizable en procesador_video.cpp:

std::string seleccionarArchivo()
{
    QString ruta = QFileDialog::getOpenFileName(
        nullptr, "Selecciona el video MP4", QString(),
        "Archivos MP4 (*.mp4);;Todos los archivos (*.*)");
    return ruta.isEmpty() ? "" : ruta.toStdString();
}

