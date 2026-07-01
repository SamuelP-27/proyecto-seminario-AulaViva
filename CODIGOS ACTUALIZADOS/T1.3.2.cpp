// T1.3.2 - calcular los ángulos yaw, pitch y roll

// Archivo: estimador.cpp (función calcularpose)

// yaw proxy: desplazamiento lateral de la nariz / anchura interocular × 90°
resultado.yaw = static_cast<double>(dxLoc / anchOjos) * 90.0;
if (ejeInvertido)
    resultado.yaw = -resultado.yaw;

// pitch proxy: desplazamiento vertical de la nariz / alto del bbox × 90°
resultado.pitch = static_cast<double>((lm.nariz.y - M.y) / rostro.height) * 90.0;

// roll: ángulo de la línea interocular respecto a la horizontal
resultado.roll = static_cast<double>(
                     std::atan2(lm.ojoDer.y - lm.ojoIzq.y,
                                lm.ojoDer.x - lm.ojoIzq.x))
                 * 180.0 / M_PI;