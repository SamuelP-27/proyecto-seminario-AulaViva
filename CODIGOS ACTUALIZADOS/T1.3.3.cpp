// T1.3.3 - validar que los ángulos calculados sean lógicos y físicamente posibles

// Archivo: estimador.cpp

if (anchOjos < 4.0f)    // detección dudosa: cara demasiado pequeña
    return resultado;

// detección de inversión del eje interocular (perfiles extremos)
const bool ejeInvertido = (lm.ojoDer.x < lm.ojoIzq.x);
...
if (ejeInvertido) {
    resultado.gazeValido = false;
    std::cerr << "[Estimador] eje interocular invertido (perfil extremo): "
                 "gazeValido=false, yaw corregido a " << resultado.yaw << "°\n";
    return resultado;
}

// gate de perfil extremo: con anchOjos < 35% del ancho del bbox, el gaze ya no es confiable
if (anchOjos < rostro.width * 0.35f) {
    resultado.gazeValido = false;
    ...
}

// factor lateral acotado a un rango físicamente posible
const float kLat = std::clamp(dxLoc / (anchOjos * 0.5f), -1.5f, 1.5f);

// también existe una validación de que el rostro caiga dentro de los límites del frame:

bool estimador::validarRostro(const cv::Mat& frame, const cv::Rect& rostro) const
{
    if (frame.empty())                               return false;
    if (rostro.width <= 0 || rostro.height <= 0)    return false;
    if (rostro.x < 0 || rostro.y < 0)               return false;
    if (rostro.x + rostro.width  > frame.cols)      return false;
    if (rostro.y + rostro.height > frame.rows)      return false;
    return true;
}