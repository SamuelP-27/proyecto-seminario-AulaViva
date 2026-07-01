// T3.1.1 - calcular el tiempo total que el estudiante estuvo atento y distraído

// Archivo: reporte_atencion.cpp (imprimirResumenConsola)

const double tiempoAtentoS = m.framesAtento    / rep.fps;
const double tiempoDistS   = m.framesDistraido / rep.fps;

std::cout << "    Tiempo atento     : " << formatearTiempo(tiempoAtentoS)
          << "  (" << std::fixed << std::setprecision(1)
          << m.porcentajeAtencion << " %)\n";
std::cout << "    Tiempo distraído  : " << formatearTiempo(tiempoDistS) << "\n";

// conteo de frames por estado (analizador_atencion.cpp, metricasPorPuesto):

switch (r.estado) {
case EstadoAtencion::Atento:       m.framesAtento++;       break;
case EstadoAtencion::Distraido:    m.framesDistraido++;    break;
case EstadoAtencion::SinDeteccion: m.framesSinDeteccion++; break;
}

