// T.2.2.4 - guardar los rangos de atención personalizados según la ubicación de cada puesto

// Archivo: analizador_atencion.cpp

bool guardarRangosAtencion(const std::string& ruta,
                           const ConfigSala&  config,
                           double toleranciaExtra)
{
    std::ofstream f(ruta);
    if (!f.is_open()) return false;
    f << "{\n  \"rangos_puestos\": [\n";
    for (size_t i = 0; i < config.puestos.size(); ++i) {
        const auto& p = config.puestos[i];
        const RangosAtencion r = calcularRangosParaPuesto(
            p, config.pizarra, config.anchoFrame, config.altoFrame, toleranciaExtra);
        f << "    {\n";
        f << "      \"id_puesto\": "        << p.id             << ",\n";
        f << "      \"yaw_min\": "          << r.yawMin         << ",\n";
        f << "      \"yaw_max\": "          << r.yawMax         << ",\n";
        f << "      \"pitch_min\": "        << r.pitchMin       << ",\n";
        f << "      \"pitch_max\": "        << r.pitchMax       << ",\n";
        f << "      \"roll_tolerancia\": "  << r.rollTolerancia << ",\n";
        ...
        f << "    }" << (i + 1 < config.puestos.size() ? "," : "") << "\n";
    }
    f << "  ]\n}\n";
    return f.good();
}

// esta función se invoca automáticamente al iniciar el procesamiento de cada video (procesador_video.cpp):

if (configSala.valida()) {
    const fs::path rutaRangos = carpeta / "rangos_atencion.json";
    guardarRangosAtencion(rutaRangos.string(), configSala);
}