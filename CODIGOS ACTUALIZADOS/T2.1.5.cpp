// T2.1.5 - validar los datos cargados antes de realizar cálculos

// Archivo: sala_config.cpp

bool ConfigSala::valida() const
{
    if (anchoFrame <= 0 || altoFrame <= 0)  return false;
    if (pizarra.ancho <= 0 || pizarra.alto <= 0) return false;
    if (puestos.empty()) return false;
    for (const auto& p : puestos)
        if (p.rect.width <= 0 || p.rect.height <= 0) return false;
    return true;
}

// esta validación se exige explícitamente antes de iniciar el análisis (main.cpp):

if (!configSala.valida()) {
    QMessageBox::critical(nullptr, "Error de configuración",
                          "La configuración de sala cargada no es válida.\n"
                          "Elimina el archivo JSON y vuelve a configurar.");
    return 1;
}
