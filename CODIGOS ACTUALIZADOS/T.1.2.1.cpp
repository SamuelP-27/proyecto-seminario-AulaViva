// T.1.2.1 - integrar una librería de detección facial (YuNet / OpenCV DNN)

// Archivo: procesador_video.cpp

cv::Ptr<cv::FaceDetectorYN> cargarYuNet(const std::vector<std::string>& rutas)
{
    for (const auto& ruta : rutas)
    {
        if (!fs::exists(ruta))
            continue;

        try
        {
            auto det = cv::FaceDetectorYN::create(
                ruta, "", cv::Size(320, 320),
                YUNET_UMBRAL_SCORE, YUNET_UMBRAL_NMS, YUNET_TOP_K);

            std::cout << "YuNet cargado desde: " << ruta << "\n";
            return det;
        }
        catch (const cv::Exception& e)
        {
            std::cerr << "Fallo al cargar " << ruta << ": " << e.what() << "\n";
            continue;
        }
    }
    return nullptr;
}

// modelo declarado en las rutas de búsqueda:

const std::vector<std::string> RUTAS_YUNET = {
    "face_detection_yunet_2026may.onnx",
    "modelos/face_detection_yunet_2026may.onnx",
};