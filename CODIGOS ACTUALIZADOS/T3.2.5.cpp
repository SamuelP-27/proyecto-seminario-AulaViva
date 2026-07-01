// T3.2.5 - generar un resumen final con conclusiones pedagógicas claras

// Archivo: reporte_atencion.cpp (función generarConclusionAlumno)

std::string generarConclusionAlumno(const MetricasPuesto& m,
                                    const std::vector<EventoDistraccion>& eventos,
                                    double fps)
{
    std::ostringstream out;
    const double pct = m.porcentajeAtencion;
    out << std::fixed << std::setprecision(1) << pct << "% de atencion lograda";

    // Busca el episodio de distracción más largo para citarlo con timestamps concretos
    const EventoDistraccion* peor = nullptr;
    for (const auto& ev : eventos) {
        if (ev.idPuesto != m.idPuesto) continue;
        if (!peor || (ev.tiempoFinS - ev.tiempoInicioS) > (peor->tiempoFinS - peor->tiempoInicioS))
            peor = &ev;
    }

    if (pct >= 75.0) {
        out << ". Buen nivel de atencion, seguimiento rutinario.";
    } else if (pct >= 50.0) {
        out << ". Puede mejorar: atencion moderada.";
        if (peor) out << " Enfocate en repasar lo cubierto entre "
                       << formatearTiempo(peor->tiempoInicioS) << " y "
                       << formatearTiempo(peor->tiempoFinS) << "...";
    } else {
        out << ". Puede mejorar: atencion baja.";
        if (peor) out << " Intervencion pedagogica sugerida...";
    }
    return out.str();
}

// conclusión agregada a nivel de toda la clase (calcularReporte, reporte_atencion.cpp):

std::ostringstream c;
c << rep.porcentajeAtencionGlobal << "% de atencion promedio lograda";
if (rep.porcentajeAtencionGlobal >= 75.0) {
    c << ". Buen nivel general, seguimiento rutinario.";
} else {
    c << ", puede mejorar! Enfocate en repasar lo cubierto entre "
      << formatearTiempo(peor.tiempoInicioS) << " y " << formatearTiempo(peor.tiempoFinS)
      << ", el momento con mayor concentracion de distracciones.";
}
rep.conclusionGeneral = c.str();