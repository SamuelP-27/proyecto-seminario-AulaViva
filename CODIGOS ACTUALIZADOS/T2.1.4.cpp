// T.2.1.4 - implementar la lectura del archivo de configuración desde C++

// Archivo: sala_config.cpp

bool cargarConfigSala(const std::string& ruta, ConfigSala& config)
{
    std::ifstream f(ruta);
    if (!f.is_open()) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string json = ss.str();

    leerInt(json, "ancho_frame", config.anchoFrame);
    leerInt(json, "alto_frame",  config.altoFrame);

    // Bloque pizarra
    auto blkPos = json.find("\"pizarra\":");
    ...
    // Array de puestos (parser propio basado en conteo de llaves balanceadas)
    auto arrPos = json.find("\"puestos\":");
    ...
    return config.valida();
}

// esta misma función es invocada al iniciar el programa en main.cpp:

if (fs::exists(rutaConfig.toStdString())) {
    if (cargarConfigSala(rutaConfig.toStdString(), configSala)) {
        configCargada = true;
        std::cout << "Configuración de sala cargada desde:\n  "
                  << rutaConfig.toStdString() << "\n";
        ...
    }
}