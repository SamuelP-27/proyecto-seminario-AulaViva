// T2.2.1 - calcular los ángulos límite hacia los extremos de la pizarra

// Archivo: analizador_atencion.cpp

const auto verts = pizarra.vertices();

// extremos horizontales para fallback yaw
cv::Point2f extremoIzq = verts[0], extremoDer = verts[0];
for (const auto& v : verts) {
    if (v.x < extremoIzq.x) extremoIzq = v;
    if (v.x > extremoDer.x) extremoDer = v;
}

calcularRangosYaw(centroPuesto, extremoIzq, extremoDer,
                  toleranciaExtraYaw, rangos.yawMin, rangos.yawMax);




                  void calcularRangosYaw(cv::Point2f centroPuesto,
                       cv::Point2f extremoIzqPiz,
                       cv::Point2f extremoDerPiz,
                       double      toleranciaExtra,
                       double&     yawMinOut,
                       double&     yawMaxOut)
{
    const cv::Point2f centroPiz = (extremoIzqPiz + extremoDerPiz) * 0.5f;
    const double dist = cv::norm(centroPuesto - centroPiz);
    ...
    const double dxCentro  = static_cast<double>(centroPiz.x - centroPuesto.x);
    const double theta_deg = std::atan2(dxCentro, dist) * 180.0 / M_PI;
    ...
}

