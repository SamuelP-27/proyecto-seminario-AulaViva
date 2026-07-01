// T3.1.4 - identificar los momentos críticos con mayor concentración de distracciones

// Archivo: reporte_atencion.cpp (función identificarMomentosCriticos, ventana deslizante)

std::vector<int> actividad(static_cast<size_t>(totalFrames), 0);
for (const auto& ev : eventos) {
    const int fi = std::max(0,               ev.frameInicio);
    const int ff = std::min(totalFrames - 1, ev.frameFin);
    for (int f = fi; f <= ff; ++f)
        actividad[static_cast<size_t>(f)] = 1;
}

// suma de tamaño W para detectar tramos densos en distracciones
const int W = std::max(1, ventanaFrames);
...
for (int i = 0; i < totalFrames; ++i) {
    if (suma[i] <= 0) continue;
    // ...comprueba que sea un máximo local dentro de un entorno minSep...
    MomentoCritico mc;
    mc.frameInicio      = i;
    mc.frameFin         = std::min(totalFrames - 1, i + W - 1);
    mc.numDistracciones = suma[i];
    mc.densidad         = mc.numDistracciones / ((mc.frameFin - mc.frameInicio + 1) / fps);
    candidatos.push_back(mc);
}

std::sort(candidatos.begin(), candidatos.end(),
          [](const MomentoCritico& a, const MomentoCritico& b) {
              return a.densidad > b.densidad;
          });
if (static_cast<int>(candidatos.size()) > topN)
    candidatos.resize(static_cast<size_t>(topN));