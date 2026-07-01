// T3.2.4 - exportar los datos y métricas a archivos CSV o JSON

// Archivo: reporte_atencion.cpp y analizador_atencion.cpp

bool exportarReporteJSON(const ReporteClase& rep, const std::string& rutaSalida)
{
    std::ofstream f(rutaSalida);
    if (!f.is_open()) return false;
    f << "{\n";
    f << "  \"video\": "              << jsonStr(rep.nombreVideo) << ",\n";
    f << "  \"atencion_global_pct\": " << rep.porcentajeAtencionGlobal << ",\n";
    ...
    f << "  \"alumnos\": [ ... ],\n";
    f << "  \"eventos_distraccion\": [ ... ],\n";
    f << "  \"momentos_criticos\": [ ... ]\n";
    f << "}\n";
    return f.good();
}

bool exportarMomentosCriticosCSV(const ReporteClase& rep, const std::string& rutaSalida)
{
    std::ofstream f(rutaSalida);
    f << "rank,frame_inicio,frame_fin,tiempo_inicio_s,tiempo_fin_s,"
         "num_frames_distraccion,densidad_distrac_por_s\n";
    ...
}

// exportación adicional desde EvaluadorAtencion (analizador_atencion.cpp):

bool EvaluadorAtencion::guardarCSVFrames  (const std::string& ruta) const { ... }
bool EvaluadorAtencion::guardarCSVEventos (const std::string& ruta) const { ... }
bool EvaluadorAtencion::guardarCSVMetricas(const std::string& ruta) const { ... }

// llamadas en conjunto al cerrar el procesamiento (procesador_video.cpp):

evaluador.guardarCSVFrames  (rutaCSVFrames.string());
evaluador.guardarCSVEventos (rutaCSVEventos.string());
evaluador.guardarCSVMetricas(rutaCSVMetricas.string());
...
const bool jsonOk     = exportarReporteJSON(reporte, rutaReporteJSON.string());
const bool momentosOk = exportarMomentosCriticosCSV(reporte, rutaMomentosCSV.string());

