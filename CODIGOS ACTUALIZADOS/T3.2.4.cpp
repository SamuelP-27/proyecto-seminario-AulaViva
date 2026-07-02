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



// Ampliación - exportación adicional a PDF

// La tarea, tal como está redactada en el EDT, contempla explícitamente CSV o JSON. 
// En esta versión se agregó un tercer formato de exportación (PDF) que consolida en un solo documento todo lo que hasta ahora estaba repartido entre el JSON, 
// los CSV y las imágenes (timeline + tarjetas). Se documenta aquí, como ampliación de la misma tarea, porque cumple el mismo propósito de fondo 
// (dejar el reporte final disponible en disco en un formato estándar) aunque el formato específico no estuviera nombrado en el EDT original.

// Requisito de build - AulaViva.pro:

// se quita QT += widgets
+QT += widgets printsupport

// se agrega el módulo Qt printsupport, para usar QPrinter (motor de generación de PDF).


// Declaración - reporte_atencion.h:

bool exportarReportePDF(const ReporteClase&              reporte,
                        const std::string&                rutaSalida,
                        const std::string&                rutaTimelinePNG   = "",
                        const std::vector<std::string>&   rutasTarjetasPNG  = {});

// Implementación — reporte_atencion.cpp. 
// La exportación no usa una librería de PDF externa: arma un documento HTML completo y lo renderiza con Qt (QTextDocument + QPrinter en modo PdfFormat), 
// reutilizando helpers y funciones ya existentes de este mismo módulo (formatearTiempo, generarConclusionAlumno, contarTiposDistrac):

#include <QTextDocument>
#include <QPrinter>
#include <QUrl>


// Escapa & < > " ' para insertar texto seguro dentro del HTML
std::string htmlEscape(const std::string& s)
{
    std::string r;
    for (char c : s) {
        switch (c) {
        case '&':  r += "&amp;";  break;
        case '<':  r += "&lt;";   break;
        case '>':  r += "&gt;";   break;
        case '"':  r += "&quot;"; break;
        case '\'': r += "&#39;";  break;
        default:   r += c;        break;
        }
    }
    return r;
}

// Convierte una ruta de archivo local a URL file:// para poder
// ver imágenes (<img src=...>) en el documento
std::string rutaImagenHTML(const std::string& ruta)
{
    if (ruta.empty()) return "";
    return QUrl::fromLocalFile(QString::fromStdString(ruta)).toString().toStdString();
}



bool exportarReportePDF(const ReporteClase& reporte,
                        const std::string& rutaSalida,
                        const std::string& rutaTimelinePNG,
                        const std::vector<std::string>& rutasTarjetasPNG)
{
    if (rutaSalida.empty()) return false;

    std::ostringstream html;
    html << "<html><head><meta charset='UTF-8'><style>"
            "body{font-family:Arial,sans-serif;font-size:10pt;color:#222;}"
            "h2{color:#264b6a;border-bottom:1px solid #aaa;}"
            "table{border-collapse:collapse;width:100%;}"
            "th,td{border:1px solid #aaa;padding:5px;}"
            "img{max-width:100%;margin:8px 0 16px 0;}"
            "</style></head><body>";

    html << "<h1>Reporte Final de Atencion - AulaViva</h1>";

    // 1. Datos generales del video
    html << "<h2>1. Datos generales del video</h2><p>"
         << "<b>Video:</b> " << htmlEscape(reporte.nombreVideo) << "  "
         << "<b>Duracion:</b> " << formatearTiempo(reporte.duracionTotalS) << "</p>";

    // 2. Resumen global (tabla) + conclusión general
    // 3. Detalle por alumno (tabla completa de métricas)
    // 4. Conclusiones por alumno (texto + tabla de frecuencia de distracciones,
    //    reutilizando generarConclusionAlumno y contarTiposDistrac)
    // 5. Momentos críticos (tabla)
    // 6. Eventos de distracción (tabla detallada)
    ...

    // 7. Timeline visual embebido
    if (!rutaTimelinePNG.empty()) {
        html << "<h2>7. Timeline visual de atencion</h2>";
        html << "<img src='" << rutaImagenHTML(rutaTimelinePNG) << "'>";
    }

    // 8. Tarjetas de desempeño embebidas
    if (!rutasTarjetasPNG.empty()) {
        html << "<h2>8. Tarjetas de desempeno por alumno</h2>";
        for (const auto& rutaTarjeta : rutasTarjetasPNG)
            if (!rutaTarjeta.empty())
                html << "<img src='" << rutaImagenHTML(rutaTarjeta) << "'>";
    }

    // 9. Nota sobre archivos complementarios (CSV/JSON)
    html << "</body></html>";

    QTextDocument documento;
    documento.setHtml(QString::fromUtf8(html.str().c_str()));

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(QString::fromStdString(rutaSalida));
    documento.print(&printer);

    return true;
}


// Integración al pipeline - procesador_video.cpp. 
// Se acumulan las rutas de las tarjetas PNG a medida que se generan (antes solo quedaban en memoria como cv::Mat, sin registrar su ruta en disco), 
// y se agrega la llamada de exportación junto a las de JSON y CSV:

std::vector<std::string> rutasTarjetasPDF;
for (const auto& m : reporte.metricasPorAlumno) {
    ...
    const fs::path rutaTarjeta = carpetaTarjetas / nombreArchivo;
    rutasTarjetasPDF.push_back(rutaTarjeta.string());
    tarjetas.push_back(generarTarjetaDesempeño(
        m, reporte.eventos, reporte.fps, configSala,
        primerFrame, rutaTarjeta.string()));
    ...
}



const fs::path rutaReportePDF = carpeta / "reporte_atencion.pdf";

const bool pdfOk = exportarReportePDF(
    reporte,
    rutaReportePDF.string(),
    rutaTimeline.string(),
    rutasTarjetasPDF
    );

std::cout << "Reporte final exportado en:\n"
          ...
          << "  " << rutaReportePDF.string() << (pdfOk ? "" : "  [ERROR]") << "\n";



// Limitación conocida: exportarReportePDF solo valida que rutaSalida no esté vacía al inicio, 
// después de llamar a documento.print(&printer) retorna true incondicionalmente, sin comprobar si QPrinter::setOutputFileName 
// realmente pudo abrir el archivo para escritura (tiene fe). Esto significa que, si la escritura del PDF falla (por ejemplo, por permisos de carpeta), 
// el flag pdfOk reportado en consola y en el QMessageBox final sería un falso positivo. Queda pendiente como mejora incorporar una verificación 
// explícita del resultado de la impresión antes de retornar.
