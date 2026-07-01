// T.1.1.4 - extraer los frames del video de forma secuencial

// Archivo: procesador_video.cpp (bucle principal de procesarVideo)

while (true)
{
    ...
    bool ret = cap.read(frame);
    if (!ret || frame.empty()) break;

    // Guarda frame (escritura asíncrona para no bloquear el bucle principal)
    const std::string nombreArch = nombreFrame(carpeta, frameActual);
    cv::Mat frameParaGuardar = frame.clone();
    tareasEscritura.push_back(
        std::async(std::launch::async,
                   [nombreArch, frameParaGuardar, parametrosJPEG]() {
                       cv::imwrite(nombreArch, frameParaGuardar,
                                   parametrosJPEG);
                   }));
    ++framesGuardados;
    ...
    ++frameActual;
}

// función auxiliar que arma el nombre secuencial de cada imagen:

static std::string nombreFrame(const fs::path& carpeta, int numero)