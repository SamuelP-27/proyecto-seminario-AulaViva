// T2.1.1 - diseñar un archivo de configuración (JSON) de la sala

// Archivo: sala_config.h / sala_config.cpp

// El JSON se genera manualmente (sin librerías externas convenientemente) para no añadir dependencias al proyecto: 

bool guardarConfigSala(const std::string& ruta, const ConfigSala& config)
{
    std::ofstream f(ruta);
    if (!f.is_open()) return false;

    f << "{\n";
    f << "  \"ancho_frame\": " << config.anchoFrame << ",\n";
    f << "  \"alto_frame\": "  << config.altoFrame  << ",\n";
    f << "  \"pizarra\": {\n";
    ...
    f << "  \"puestos\": [\n";
    ...
    return f.good();
}
