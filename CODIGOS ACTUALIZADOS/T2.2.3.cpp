// T.2.2.3 - establecer una tolerancia razonable para el ángulo de roll

// Archivo: analizador_atencion.cpp

rangos.rollTolerancia = 22.0;  // permite movimientos naturales de cabeza sin falsos positivos

// uso de esta tolerancia en la clasificación (analizador_atencion.cpp):

const bool rollFuera = (std::abs(pose.roll) > ep.rangos.rollTolerancia);