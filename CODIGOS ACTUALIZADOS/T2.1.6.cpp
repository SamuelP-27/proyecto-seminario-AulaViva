// T2.1.6 - asociar al estudiante del video con su puesto correspondiente

// Archivo: sala_config.cpp (función asociarRostroAPuesto, ver T1.2.5) y procesador_video.cpp:

const int idPuesto = configSala.valida()
                         ? asociarRostroAPuesto(p.rect, configSala)
                         : -1;

std::string nomEstudiante;
if (idPuesto >= 0 && configSala.valida()) {
    for (const auto& pu : configSala.puestos)
        if (pu.id == idPuesto) { nomEstudiante = pu.estudiante; break; }
}

