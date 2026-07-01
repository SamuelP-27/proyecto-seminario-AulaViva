// T2.4.3 - calcular la duración de cada evento de distracción

// Archivo: analizador_atencion.cpp (dentro de metricasPorPuesto)

double durMax = 0.0, durTotal = 0.0;
int nEvt = 0;
for (const auto& ev : eventos_) {
    if (ev.idPuesto != ep.idPuesto) continue;

    const double dur = ev.tiempoFinS - ev.tiempoInicioS;
    durTotal += dur;
    durMax    = std::max(durMax, dur);
    ++nEvt;
}
