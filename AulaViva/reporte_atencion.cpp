// reporte_atencion.cpp
//
// Implementación del módulo de reportabilidad y visualización final de atención.
//
// Características:
// - calcularReporte(): agrega métricas del EvaluadorAtencion en un ReporteClase.
// - identificarMomentosCriticos(): ventana deslizante sobre la lista de eventos
// para encontrar los intervalos con mayor densidad de distracciones.
// - imprimirResumenConsola(): salida formateada para el tecnico (o profesor) al cierre.
// - generarTimelineVisual(): imagen PNG con una fila por alumno (verde/rojo/gris)
// y marcadores de momentos críticos.
// - generarTarjetaDesempeño(): "carnet" por alumno con barra de progreso y KPIs.
// - exportarReporteJSON(): volcado completo en JSON.
// - exportarMomentosCriticosCSV(): CSV de los tramos más problemáticos.

#include "reporte_atencion.h"
#include <opencv2/opencv.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <sstream>
#include <QTextDocument>
#include <QPrinter>
#include <QByteArray>
#include <QPageSize>
#include <QPageLayout>
#include <QMarginsF>
#include <chrono>

// Helpers internos

namespace {

// Formatea segundos como "MM:SS" (o "HH:MM:SS" si ≥ 1 h)
std::string formatearTiempo(double segundos)
{
    const int total = static_cast<int>(std::round(segundos));
    const int h     = total / 3600;
    const int m     = (total % 3600) / 60;
    const int s     = total % 60;
    std::ostringstream ss;
    if (h > 0)
        ss << std::setw(2) << std::setfill('0') << h << ":"
           << std::setw(2) << std::setfill('0') << m << ":"
           << std::setw(2) << std::setfill('0') << s;
    else
        ss << std::setw(2) << std::setfill('0') << m << ":"
           << std::setw(2) << std::setfill('0') << s;
    return ss.str();
}

// Genera una cadena para JSON
std::string jsonStr(const std::string& s)
{
    std::string r;
    r.reserve(s.size() + 2);
    r += '"';
    for (char c : s) {
        if (c == '"' || c == '\\') r += '\\';
        r += c;
    }
    r += '"';
    return r;
}

std::string htmlEscape(const std::string& s)
{
    std::string r;
    r.reserve(s.size());
    for (char c : s)
    {
        switch (c)
        {
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

// Ancho máximo (px) por defecto al que se reescalan las imágenes vistas
// en el PDF, usado solo como respaldo si el caller no calcula uno propio.
// exportarReportePDF() calcula y pasa el ancho real disponible en la
// página (ver anchoUtilImagenesPx más abajo), así que en la práctica este
// valor casi no se usa; se deja como límite de seguridad razonable.
constexpr int kAnchoMaxImagenPDFPorDefecto = 700;
constexpr int kCalidadJPEG                 = 85;

// Carga una imagen desde disco, la reescala para que su ancho no supere
// anchoMaximoPx y devuelve la etiqueta <img> ya lista para insertar en el
// HTML, con el data-URI en base64 (JPEG) y los atributos width/height
// FIJADOS EXPLÍCITAMENTE con el tamaño real ya redimensionado.
//
// Por qué se fija el ancho como atributo HTML (width="...") y no solo con
// CSS (max-width:100%): el motor de render HTML de Qt (QTextDocument, el
// que convierte este HTML a PDF) interpreta el tamaño en píxeles de una
// imagen con su propia métrica interna de "píxel de documento", que NO
// coincide automáticamente con la resolución configurada en QPrinter. Si
// no se sincronizan (ver documento.setPageSize() en exportarReportePDF),
// una imagen puede terminar ocupando más ancho físico que el área
// imprimible de la página y quedar cortada en el margen derecho —
// exactamente el problema reportado. Fijar aquí el ancho ya calculado
// para caber en la página, tanto en el redimensionado real del buffer
// JPEG como en el atributo width= del <img>, hace que el resultado sea
// determinístico y ya no dependa de esa métrica interna ni del soporte
// (limitado) de Qt para porcentajes en CSS.
//
// Si la imagen no se puede leer, devuelve "" y el caller simplemente omite
// toda la sección (título incluido).
std::string imagenADataURI(const std::string& ruta,
                           int anchoMaximoPx = kAnchoMaxImagenPDFPorDefecto)
{
    if (ruta.empty())
        return "";

    cv::Mat img = cv::imread(ruta, cv::IMREAD_COLOR);
    if (img.empty())
        return "";

    if (anchoMaximoPx > 0 && img.cols > anchoMaximoPx)
    {
        const double escala = static_cast<double>(anchoMaximoPx) / img.cols;
        cv::resize(img, img, cv::Size(), escala, escala, cv::INTER_AREA);
    }

    std::vector<uchar> buffer;
    std::vector<int> params = { cv::IMWRITE_JPEG_QUALITY, kCalidadJPEG };
    if (!cv::imencode(".jpg", img, buffer, params))
        return "";

    const QByteArray bytes(reinterpret_cast<const char*>(buffer.data()),
                           static_cast<int>(buffer.size()));
    const QByteArray b64 = bytes.toBase64();

    std::ostringstream tag;
    tag << "<img src='data:image/jpeg;base64," << b64.toStdString() << "' "
        << "width='" << img.cols << "' height='" << img.rows << "'>";
    return tag.str();
}

// Frecuencia de cada TipoDistraccion en un vector de eventos de un puesto
std::map<TipoDistraccion, int> contarTiposDistrac(
    const std::vector<EventoDistraccion>& eventos, int idPuesto)
{
    std::map<TipoDistraccion, int> freq;
    for (const auto& ev : eventos)
        if (ev.idPuesto == idPuesto)
            freq[ev.tipo]++;
    return freq;
}

} // namespace

// calcularReporte

ReporteClase calcularReporte(
    const EvaluadorAtencion& evaluador,
    const ConfigSala&        configSala,
    const std::string&       nombreVideo,
    double                   fps,
    int                      totalFrames)
{
    ReporteClase rep;
    rep.nombreVideo   = nombreVideo;
    rep.fps           = fps > 0.0 ? fps : 25.0;
    rep.totalFrames   = totalFrames;
    rep.duracionTotalS = totalFrames / rep.fps;

    // Copia las métricas y eventos desde el evaluador para poder procesarlos aquí
    rep.metricasPorAlumno = evaluador.metricasPorPuesto();
    rep.eventos           = evaluador.eventosDistraccion();

    // Agrega métricas globales de la clase recorriendo todos los puestos.
    // sumaPct/sumaDur acumulan valores ponderados para promediar al final.
    double sumaPct   = 0.0;
    double sumaDur   = 0.0;
    double maxDur    = 0.0;
    int    cntPuesto = 0;

    for (const auto& m : rep.metricasPorAlumno) {
        sumaPct += m.porcentajeAtencion;
        ++cntPuesto;
        rep.totalEventosDistraccion += m.numEventosDistrac;
        if (m.duracionMaxDistracS > maxDur) maxDur = m.duracionMaxDistracS;
        // Pondera la duración media por cantidad de eventos (para el promedio global correcto)
        sumaDur += m.duracionMediaDistracS * m.numEventosDistrac;
    }

    // Promedio simple de atención entre puestos; 0 si no hay ninguno
    rep.porcentajeAtencionGlobal =
        (cntPuesto > 0) ? sumaPct / cntPuesto : 0.0;
    rep.duracionMaxDistracS =
        maxDur;
    // Duración media global ponderada por eventos; 0 si no hubo ninguno
    rep.duracionMediaDistracS =
        (rep.totalEventosDistraccion > 0)
            ? sumaDur / rep.totalEventosDistraccion
            : 0.0;

    // Momentos críticos
    rep.momentosCriticos = identificarMomentosCriticos(
        rep.eventos, rep.fps, totalFrames);

    // Conclusión pedagógica agregada de la clase
    {
        std::ostringstream c;
        c << std::fixed << std::setprecision(1)
          << rep.porcentajeAtencionGlobal << "% de atencion promedio lograda";
        if (rep.porcentajeAtencionGlobal >= 75.0) {
            c << ". Buen nivel general, seguimiento rutinario.";
        } else {
            c << ", puede mejorar!";
            if (!rep.momentosCriticos.empty()) {
                const auto& peor = *std::max_element(
                    rep.momentosCriticos.begin(), rep.momentosCriticos.end(),
                    [](const MomentoCritico& a, const MomentoCritico& b) {
                        return a.densidad < b.densidad;
                    });
                c << " Enfocate en repasar lo cubierto entre "
                  << formatearTiempo(peor.tiempoInicioS) << " y "
                  << formatearTiempo(peor.tiempoFinS)
                  << ", el momento con mayor concentracion de distracciones ("
                  << peor.numDistracciones << " frames distraidos).";
            }
        }
        rep.conclusionGeneral = c.str();
    }

    return rep;
}

// identificarMomentosCriticos

std::vector<MomentoCritico> identificarMomentosCriticos(
    const std::vector<EventoDistraccion>& eventos,
    double fps,
    int    totalFrames,
    int    ventanaFrames,
    int    topN)
{
    if (eventos.empty() || totalFrames <= 0)
        return {};

    // Construye un arreglo booleano frame a frame:
    // actividad[f] = 1 si en ese frame al menos un alumno estaba distraído,
    // 0 si no hubo distracción. Los eventos se extienden sobre el rango [frameInicio, frameFin].
    std::vector<int> actividad(static_cast<size_t>(totalFrames), 0);
    for (const auto& ev : eventos) {
        const int fi = std::max(0,                   ev.frameInicio);
        const int ff = std::min(totalFrames - 1,     ev.frameFin);
        for (int f = fi; f <= ff; ++f)
            actividad[static_cast<size_t>(f)] = 1;
    }

    // Aplica una suma de tamaño W para suavizar y detectar tramos densos.
    // suma[i] = cantidad de frames con distracción dentro de la ventana que empieza en i.
    const int W = std::max(1, ventanaFrames);
    std::vector<int> suma(static_cast<size_t>(totalFrames), 0);
    {
        int acum = 0;
        // Carga inicial de la primera ventana
        for (int i = 0; i < std::min(W, totalFrames); ++i)
            acum += actividad[static_cast<size_t>(i)];
        suma[0] = acum;
        // Avance incremental: suma el elemento que entra y resta el que sale
        for (int i = 1; i < totalFrames; ++i) {
            if (i + W - 1 < totalFrames)
                acum += actividad[static_cast<size_t>(i + W - 1)];
            if (i - 1 >= 0)
                acum -= actividad[static_cast<size_t>(i - 1)];
            suma[static_cast<size_t>(i)] = acum;
        }
    }

    // Recorre el arreglo de sumas buscando máximos locales.
    // Un pico es válido si es el valor más alto dentro de un entorno de minSep frames
    // a cada lado, lo que evita seleccionar dos momentos críticos solapados.
    std::vector<MomentoCritico> candidatos;
    const int minSep = W / 2;   // separación mínima entre picos

    for (int i = 0; i < totalFrames; ++i) {
        if (suma[static_cast<size_t>(i)] <= 0) continue;

        // Comprueba que es máximo local en un entorno de minSep
        bool esPico = true;
        for (int j = std::max(0, i - minSep);
             j <= std::min(totalFrames - 1, i + minSep); ++j)
        {
            if (j != i && suma[static_cast<size_t>(j)] > suma[static_cast<size_t>(i)]) {
                esPico = false;
                break;
            }
        }
        if (!esPico) continue;

        MomentoCritico mc;
        mc.frameInicio    = std::max(0, i);
        mc.frameFin       = std::min(totalFrames - 1, i + W - 1);
        mc.tiempoInicioS  = mc.frameInicio / fps;
        mc.tiempoFinS     = mc.frameFin    / fps;
        mc.numDistracciones = suma[static_cast<size_t>(i)];
        const double durS = (mc.frameFin - mc.frameInicio + 1) / fps;
        mc.densidad = (durS > 0.0) ? mc.numDistracciones / durS : 0.0;
        candidatos.push_back(mc);
    }

    // Ordena por densidad descendente y toma los topN
    std::sort(candidatos.begin(), candidatos.end(),
              [](const MomentoCritico& a, const MomentoCritico& b) {
                  return a.densidad > b.densidad;
              });

    if (static_cast<int>(candidatos.size()) > topN)
        candidatos.resize(static_cast<size_t>(topN));

    // Re-ordena por tiempo para la presentación
    std::sort(candidatos.begin(), candidatos.end(),
              [](const MomentoCritico& a, const MomentoCritico& b) {
                  return a.tiempoInicioS < b.tiempoInicioS;
              });

    return candidatos;
}

// imprimirResumenConsola (no es necesario pero no queriamos eliminar la función)

void imprimirResumenConsola(const ReporteClase& rep)
{
    const std::string SEP(72, '=');
    const std::string sep(72, '-');

    std::cout << "\n" << SEP << "\n";
    std::cout << "  REPORTE FINAL DE ATENCIÓN — AulaViva\n";
    std::cout << SEP << "\n";

    std::cout << "  Video     : " << rep.nombreVideo << "\n";
    std::cout << "  Duración  : " << formatearTiempo(rep.duracionTotalS)
              << "  (" << rep.totalFrames << " frames @ "
              << std::fixed << std::setprecision(1) << rep.fps << " fps)\n";
    std::cout << "  Alumnos   : " << rep.metricasPorAlumno.size() << "\n";

    // Métricas globales
    std::cout << "\n" << sep << "\n";
    std::cout << "  MÉTRICAS GLOBALES DE LA CLASE\n";
    std::cout << sep << "\n";
    std::cout << "  Atención promedio de la clase : "
              << std::fixed << std::setprecision(1)
              << rep.porcentajeAtencionGlobal << " %\n";
    std::cout << "  Total de eventos de distracción: "
              << rep.totalEventosDistraccion << "\n";
    std::cout << "  Duración media de distracción  : "
              << std::fixed << std::setprecision(2)
              << rep.duracionMediaDistracS << " s\n";
    std::cout << "  Duración máxima de distracción : "
              << std::fixed << std::setprecision(2)
              << rep.duracionMaxDistracS << " s\n";

    // Por alumno
    std::cout << "\n" << sep << "\n";
    std::cout << "  DETALLE POR ALUMNO\n";
    std::cout << sep << "\n";

    for (const auto& m : rep.metricasPorAlumno) {
        const std::string nombre =
            m.nombreAlumno.empty()
                ? ("Puesto #" + std::to_string(m.idPuesto))
                : m.nombreAlumno;

        const double tiempoAtentoS =
            m.framesAtento  / rep.fps;
        const double tiempoDistS   =
            m.framesDistraido / rep.fps;

        std::cout << "\n  Alumno : " << nombre << "\n";
        std::cout << "    Frames analizados : " << m.framesAnalizados << "\n";
        std::cout << "    Tiempo atento     : " << formatearTiempo(tiempoAtentoS)
                  << "  (" << std::fixed << std::setprecision(1)
                  << m.porcentajeAtencion << " %)\n";
        std::cout << "    Tiempo distraído  : " << formatearTiempo(tiempoDistS) << "\n";
        std::cout << "    Nº distracciones  : " << m.numEventosDistrac << "\n";

        if (m.numEventosDistrac > 0) {
            std::cout << "    Duración media    : "
                      << std::fixed << std::setprecision(2)
                      << m.duracionMediaDistracS << " s\n";
            std::cout << "    Duración máxima   : "
                      << std::fixed << std::setprecision(2)
                      << m.duracionMaxDistracS  << " s\n";
        }

        // Tipos de distracción más frecuentes
        // (construido directamente desde rep.eventos para este alumno)
        auto freq = contarTiposDistrac(rep.eventos, m.idPuesto);
        if (!freq.empty()) {
            // Ordena por frecuencia
            std::vector<std::pair<int, TipoDistraccion>> ordenado;
            for (const auto& kv : freq)
                ordenado.emplace_back(kv.second, kv.first);
            std::sort(ordenado.begin(), ordenado.end(),
                      [](const auto& a, const auto& b){ return a.first > b.first; });

            std::cout << "    Tipos de distracción:\n";
            for (const auto& kv : ordenado)
                std::cout << "      • " << nombreTipoDistraccion(kv.second)
                          << " (" << kv.first << " veces)\n";
        }

        // Conclusión pedagógica concreta (con referencia a tiempos)
        std::cout << "    -- Conclusion --------------------------------\n";
        std::cout << "    " << generarConclusionAlumno(m, rep.eventos, rep.fps) << "\n";
    }

    // Momentos críticos
    if (!rep.momentosCriticos.empty()) {
        std::cout << "\n" << sep << "\n";
        std::cout << "  MOMENTOS CRÍTICOS (mayor concentración de distracciones)\n";
        std::cout << sep << "\n";
        for (size_t i = 0; i < rep.momentosCriticos.size(); ++i) {
            const auto& mc = rep.momentosCriticos[i];
            std::cout << "  [" << (i + 1) << "] "
                      << formatearTiempo(mc.tiempoInicioS) << " – "
                      << formatearTiempo(mc.tiempoFinS)
                      << "  |  " << mc.numDistracciones << " frames distraídos"
                      << "  |  densidad: " << std::fixed << std::setprecision(2)
                      << mc.densidad << " distrac/s\n";
        }
    }

    // Conclusión general de la clase
    std::cout << "\n" << sep << "\n";
    std::cout << "  CONCLUSIÓN GENERAL PARA EL DOCENTE\n";
    std::cout << sep << "\n";
    std::cout << "  " << rep.conclusionGeneral << "\n";

    std::cout << "\n" << SEP << "\n\n";
}

// generarTimelineVisual

cv::Mat generarTimelineVisual(
    const EvaluadorAtencion& evaluador,
    const ReporteClase&      rep,
    const ConfigSala&        configSala,
    double                   fps,
    int                      totalFrames,
    const std::string&       rutaSalida)
{
    const auto& historial = evaluador.historial();
    const auto& puestos   = configSala.puestos;

    const int numAlumnos = static_cast<int>(puestos.size());
    if (numAlumnos == 0 || totalFrames <= 0) return cv::Mat();

    // Dimensiones del timeline
    const int MARGEN_IZQ  = 160;  // espacio para el nombre
    const int MARGEN_DER  = 20;
    const int ALTO_FILA   = 28;
    const int MARGEN_SUP  = 50;   // eje de tiempo superior
    const int MARGEN_INF  = 40;
    const int ANCHO_GRAF  = std::max(800, std::min(totalFrames, 1400));
    const int ANCHO_TOTAL = MARGEN_IZQ + ANCHO_GRAF + MARGEN_DER;
    const int ALTO_TOTAL  = MARGEN_SUP + numAlumnos * (ALTO_FILA + 4) + MARGEN_INF;

    cv::Mat img(ALTO_TOTAL, ANCHO_TOTAL, CV_8UC3, cv::Scalar(30, 30, 30));

    // Título
    cv::putText(img, "Timeline de Atencion — AulaViva",
                cv::Point(MARGEN_IZQ, 22),
                cv::FONT_HERSHEY_SIMPLEX, 0.6,
                cv::Scalar(220, 220, 220), 1, cv::LINE_AA);

    // Eje de tiempo (marcas cada 30 s)
    const double durS        = totalFrames / fps;
    const double segsXPixel  = ANCHO_GRAF / durS;
    const double tickInterval = (durS > 300.0) ? 60.0 :
                                    (durS > 60.0)  ? 30.0 : 10.0;

    for (double t = 0.0; t <= durS; t += tickInterval) {
        const int x = MARGEN_IZQ + static_cast<int>(t * segsXPixel);
        cv::line(img, cv::Point(x, MARGEN_SUP - 8),
                 cv::Point(x, ALTO_TOTAL - MARGEN_INF + 4),
                 cv::Scalar(70, 70, 70), 1, cv::LINE_AA);
        cv::putText(img, formatearTiempo(t),
                    cv::Point(x - 12, MARGEN_SUP - 12),
                    cv::FONT_HERSHEY_SIMPLEX, 0.28,
                    cv::Scalar(160, 160, 160), 1, cv::LINE_AA);
    }

    // Leyenda
    const int legendX = MARGEN_IZQ;
    const int legendY = ALTO_TOTAL - MARGEN_INF + 14;
    cv::rectangle(img, cv::Point(legendX, legendY - 8),
                  cv::Point(legendX + 14, legendY + 2),
                  cv::Scalar(0, 200, 60), -1);
    cv::putText(img, "Atento", cv::Point(legendX + 18, legendY),
                cv::FONT_HERSHEY_SIMPLEX, 0.3, cv::Scalar(200, 200, 200), 1);
    cv::rectangle(img, cv::Point(legendX + 90, legendY - 8),
                  cv::Point(legendX + 104, legendY + 2),
                  cv::Scalar(30, 30, 200), -1);
    cv::putText(img, "Distraido", cv::Point(legendX + 108, legendY),
                cv::FONT_HERSHEY_SIMPLEX, 0.3, cv::Scalar(200, 200, 200), 1);
    cv::rectangle(img, cv::Point(legendX + 195, legendY - 8),
                  cv::Point(legendX + 209, legendY + 2),
                  cv::Scalar(90, 90, 90), -1);
    cv::putText(img, "Sin det.", cv::Point(legendX + 213, legendY),
                cv::FONT_HERSHEY_SIMPLEX, 0.3, cv::Scalar(200, 200, 200), 1);

    // Construye un mapa bidimensional para consultar el estado de cualquier puesto
    // en cualquier frame en O(1), sin tener que recorrer el historial completo por cada píxel.
    // historial puede contener varias entradas por frame (una por cada puesto activo).
    using MapaEstados = std::map<int, EstadoAtencion>;  // frame → estado
    std::map<int, MapaEstados> estadosPorPuesto;       // idPuesto → mapa

    for (const auto& r : historial)
        estadosPorPuesto[r.idPuesto][r.frame] = r.estado;

    // Dibuja cada fila
    for (int row = 0; row < numAlumnos; ++row) {
        const auto& puesto = puestos[static_cast<size_t>(row)];
        const int filaY    = MARGEN_SUP + row * (ALTO_FILA + 4);

        // Nombre del alumno
        const std::string nombre =
            puesto.estudiante.empty()
                ? ("Puesto #" + std::to_string(puesto.id))
                : puesto.estudiante;
        cv::putText(img, nombre,
                    cv::Point(4, filaY + ALTO_FILA / 2 + 4),
                    cv::FONT_HERSHEY_SIMPLEX, 0.32,
                    cv::Scalar(200, 200, 200), 1, cv::LINE_AA);

        // Fondo de fila
        cv::rectangle(img,
                      cv::Point(MARGEN_IZQ, filaY),
                      cv::Point(MARGEN_IZQ + ANCHO_GRAF, filaY + ALTO_FILA),
                      cv::Scalar(50, 50, 50), -1);

        // Colores frame a frame
        const auto& mapaEstado = estadosPorPuesto[puesto.id];
        const float escala     = static_cast<float>(ANCHO_GRAF) / totalFrames;

        for (int f = 0; f < totalFrames; ++f) {
            auto it = mapaEstado.find(f);
            cv::Scalar color;
            if (it == mapaEstado.end() || it->second == EstadoAtencion::SinDeteccion)
                color = cv::Scalar(80, 80, 80);
            else if (it->second == EstadoAtencion::Atento)
                color = cv::Scalar(0, 200, 60);
            else
                color = cv::Scalar(30, 30, 200);

            const int x0 = MARGEN_IZQ + static_cast<int>(f       * escala);
            const int x1 = MARGEN_IZQ + static_cast<int>((f + 1) * escala);
            if (x1 > x0)
                cv::rectangle(img,
                              cv::Point(x0, filaY + 2),
                              cv::Point(x1, filaY + ALTO_FILA - 2),
                              color, -1);
        }

        // Porcentaje al final de la fila
        for (const auto& m : rep.metricasPorAlumno) {
            if (m.idPuesto == puesto.id) {
                std::ostringstream pct;
                pct << std::fixed << std::setprecision(0)
                    << m.porcentajeAtencion << "%";
                cv::putText(img, pct.str(),
                            cv::Point(MARGEN_IZQ + ANCHO_GRAF + 4,
                                      filaY + ALTO_FILA / 2 + 4),
                            cv::FONT_HERSHEY_SIMPLEX, 0.32,
                            (m.porcentajeAtencion >= 75.0)
                                ? cv::Scalar(0, 220, 80)
                                : (m.porcentajeAtencion >= 50.0)
                                      ? cv::Scalar(0, 180, 230)
                                      : cv::Scalar(50, 50, 230),
                            1, cv::LINE_AA);
                break;
            }
        }
    }

    // Dibuja los momentos críticos sobre el timeline con tres elementos visuales:
    // 1. Caja amarilla translúcida sobre TODAS las filas (overlay suave,
    // alpha=0.22) para destacar el tramo sin ocultar los colores de atención.
    // 2. Borde amarillo fino en los extremos de la caja.
    // 3. Triángulo marcador sobre el eje de tiempo señalando el centro del pico.
    //
    // Nota técnica: se pinta primero en un overlay clonado, luego se mezcla con
    // addWeighted para obtener la transparencia sin sobreescribir los píxeles
    // ya dibujados. Los bordes se redibujan encima del blend para que queden opacos.
    if (!rep.momentosCriticos.empty()) {
        // Altura total de la zona de filas
        const int filasTop = MARGEN_SUP;
        const int filasBtm = MARGEN_SUP + numAlumnos * (ALTO_FILA + 4) - 4;

        cv::Mat overlay = img.clone();

        for (const auto& mc : rep.momentosCriticos) {
            const int x0 = MARGEN_IZQ
                           + static_cast<int>(mc.tiempoInicioS * segsXPixel);
            const int x1 = std::min(MARGEN_IZQ + ANCHO_GRAF,
                                    MARGEN_IZQ + static_cast<int>(mc.tiempoFinS * segsXPixel));
            const int xc = (x0 + x1) / 2;

            // 1. Relleno amarillo translúcido sobre el overlay
            cv::rectangle(overlay,
                          cv::Point(x0, filasTop),
                          cv::Point(x1, filasBtm),
                          cv::Scalar(0, 200, 255),   // BGR: amarillo
                          -1);

            // 2. Borde amarillo fino a los lados
            cv::line(img, cv::Point(x0, filasTop - 2), cv::Point(x0, filasBtm + 2),
                     cv::Scalar(0, 210, 255), 1, cv::LINE_AA);
            cv::line(img, cv::Point(x1, filasTop - 2), cv::Point(x1, filasBtm + 2),
                     cv::Scalar(0, 210, 255), 1, cv::LINE_AA);

            // 3. Triángulo marcador encima del eje de tiempo
            std::vector<cv::Point> tri = {
                {xc - 6, MARGEN_SUP - 12},
                {xc + 6, MARGEN_SUP - 12},
                {xc,     MARGEN_SUP - 2}
            };
            cv::fillConvexPoly(img, tri, cv::Scalar(0, 210, 255), cv::LINE_AA);
        }

        // Mezcla overlay con alpha=0.22 para que la caja sea translúcida
        cv::addWeighted(overlay, 0.22, img, 0.78, 0, img);

        // Re-dibuja los bordes ENCIMA del overlay mezclado (para que sean opacos)
        for (const auto& mc : rep.momentosCriticos) {
            const int x0 = MARGEN_IZQ
                           + static_cast<int>(mc.tiempoInicioS * segsXPixel);
            const int x1 = std::min(MARGEN_IZQ + ANCHO_GRAF,
                                    MARGEN_IZQ + static_cast<int>(mc.tiempoFinS * segsXPixel));
            cv::line(img, cv::Point(x0, filasTop - 2), cv::Point(x0, filasBtm + 2),
                     cv::Scalar(0, 210, 255), 1, cv::LINE_AA);
            cv::line(img, cv::Point(x1, filasTop - 2), cv::Point(x1, filasBtm + 2),
                     cv::Scalar(0, 210, 255), 1, cv::LINE_AA);
        }

        // Entrada de leyenda para los momentos críticos
        cv::rectangle(img, cv::Point(legendX + 280, legendY - 8),
                      cv::Point(legendX + 294, legendY + 2),
                      cv::Scalar(0, 200, 255), -1);
        cv::putText(img, "Momento critico", cv::Point(legendX + 298, legendY),
                    cv::FONT_HERSHEY_SIMPLEX, 0.3, cv::Scalar(200, 200, 200), 1);
    }

    if (!rutaSalida.empty())
        cv::imwrite(rutaSalida, img);

    return img;
}

// generarTarjetaDesempeño

cv::Mat generarTarjetaDesempeño(
    const MetricasPuesto&                 m,
    const std::vector<EventoDistraccion>& eventos,
    double                                fps,
    const ConfigSala&                     configSala,
    const cv::Mat&                        frameReferencia,
    const std::string&                    rutaSalida)
{
    // Resolución interna 3× para evitar borrosidad al escalar en pantalla.
    // Todos los valores de píxel se multiplican por S; el QDialog escala la imagen
    // resultante con Qt::SmoothTransformation para que se vea nítida.
    constexpr int S     = 3;    // factor de escala interno: 3× 360×240 base
    const int ANCHO     = 360 * S;   // 1080
    const int ALTO      = 240 * S;   // 720

    cv::Mat card(ALTO, ANCHO, CV_8UC3, cv::Scalar(25, 25, 35));

    // Encabezado
    const int HEADER_H = 52 * S / 2;
    cv::rectangle(card, cv::Point(0, 0), cv::Point(ANCHO, HEADER_H),
                  cv::Scalar(40, 60, 100), -1);

    const std::string titulo =
        m.nombreAlumno.empty()
            ? ("Puesto #" + std::to_string(m.idPuesto))
            : m.nombreAlumno;
    cv::putText(card, "AulaViva \xe2\x80\x94 Tarjeta de Desempeño",
                cv::Point(7 * S, 9 * S),
                cv::FONT_HERSHEY_SIMPLEX, 0.48 * S / 2.0,
                cv::Scalar(180, 200, 255), S / 2, cv::LINE_AA);
    cv::putText(card, titulo,
                cv::Point(7 * S, 22 * S),
                cv::FONT_HERSHEY_SIMPLEX, 0.82 * S / 2.0,
                cv::Scalar(255, 255, 255), S / 2, cv::LINE_AA);

    // Barra de atención
    const int barX     = 7  * S;
    const int barY     = HEADER_H + 4 * S;
    const int barAncho = ANCHO - 14 * S;
    const int barAlto  = 12 * S;
    const double pct   = std::clamp(m.porcentajeAtencion, 0.0, 100.0);
    const int barFill  = static_cast<int>(barAncho * pct / 100.0);

    cv::rectangle(card, cv::Point(barX, barY),
                  cv::Point(barX + barAncho, barY + barAlto),
                  cv::Scalar(55, 55, 75), -1);

    const cv::Scalar colorBar =
        (pct >= 75.0) ? cv::Scalar(0, 190, 55) :
            (pct >= 50.0) ? cv::Scalar(0, 165, 220) :
            cv::Scalar(40, 40, 200);

    if (barFill > 0)
        cv::rectangle(card, cv::Point(barX, barY),
                      cv::Point(barX + barFill, barY + barAlto),
                      colorBar, -1);

    // Porcentaje centrado en la barra
    std::ostringstream pctStr;
    pctStr << std::fixed << std::setprecision(1) << pct << "% ATENCION";
    int baseline = 0;
    const double fontBar = 0.48 * S / 2.0;
    const cv::Size tsz = cv::getTextSize(pctStr.str(),
                                         cv::FONT_HERSHEY_SIMPLEX, fontBar,
                                         S / 2, &baseline);
    cv::putText(card, pctStr.str(),
                cv::Point(barX + (barAncho - tsz.width) / 2,
                          barY + barAlto - 4 * S / 2),
                cv::FONT_HERSHEY_SIMPLEX, fontBar,
                cv::Scalar(255, 255, 255), S / 2, cv::LINE_AA);

    // Layout: zona izquierda (métricas) | zona derecha (foto del puesto)
    const int ZONA_IZQ_W = 210 * S;
    const int FOTO_X     = 216 * S;
    const int FOTO_Y     = barY + barAlto + 5 * S;
    const int FOTO_W     = ANCHO - FOTO_X - 7 * S;
    const int FOTO_H     = 100 * S;

    // KPIs: 3 columnas × 2 filas en la zona izquierda (los kpi son indicadores para medir el desempeño)
    // realmente el uso de KPIs es un poco complicado, se obtuvo y modifico su uso de:
    // https://github.com/AjNavneet/Brand-KPI-Video-Analysis-OpenCV-TensorFlow
    // https://github.com/AmirhosseinHonardoust/Content-KPI-Monitor
    const int kpiY0  = barY + barAlto + 7 * S;
    const int COL_W  = ZONA_IZQ_W / 3;
    const double fLabel = 0.34 * S / 2.0;
    const double fValue = 0.62 * S / 2.0;
    const int    thick  = std::max(1, S / 2);

    // Lambda para dibujar un KPI (etiqueta + valor) en una celda de la grilla.
    // col y fila determinan la posición dentro de la grilla 3×2 de métricas.
    auto putKPI = [&](const std::string& label, const std::string& valor,
                      int col, int fila)
    {
        const int x = 7 * S + col * COL_W;
        const int y = kpiY0 + fila * 28 * S;
        cv::putText(card, label,
                    cv::Point(x, y),
                    cv::FONT_HERSHEY_SIMPLEX, fLabel,
                    cv::Scalar(140, 160, 200), thick, cv::LINE_AA);
        cv::putText(card, valor,
                    cv::Point(x, y + 11 * S),
                    cv::FONT_HERSHEY_SIMPLEX, fValue,
                    cv::Scalar(230, 230, 255), thick, cv::LINE_AA);
    };

    auto fmt1 = [](double v) {
        std::ostringstream s;
        s << std::fixed << std::setprecision(1) << v;
        return s.str();
    };
    auto fmtI = [](int v) { return std::to_string(v); };

    putKPI("Frames analizados", fmtI(m.framesAnalizados),              0, 0);
    putKPI("Frames atento",     fmtI(m.framesAtento),                  1, 0);
    putKPI("Frames distraido",  fmtI(m.framesDistraido),               2, 0);
    putKPI("N distracciones",   fmtI(m.numEventosDistrac),             0, 1);
    putKPI("Dur. media distrac",fmt1(m.duracionMediaDistracS) + " s",  1, 1);
    putKPI("Dur. max distrac",  fmt1(m.duracionMaxDistracS)  + " s",   2, 1);

    // Tipos de distracción (zona izquierda, debajo de KPIs)
    const int tiposY = kpiY0 + 2 * 28 * S + 4 * S;
    auto freq = contarTiposDistrac(eventos, m.idPuesto);
    if (!freq.empty()) {
        std::vector<std::pair<int, TipoDistraccion>> ordenado;
        for (const auto& kv : freq)
            ordenado.emplace_back(kv.second, kv.first);
        std::sort(ordenado.begin(), ordenado.end(),
                  [](const auto& a, const auto& b){ return a.first > b.first; });

        cv::putText(card, "Tipos de distraccion:",
                    cv::Point(7 * S, tiposY),
                    cv::FONT_HERSHEY_SIMPLEX, 0.37 * S / 2.0,
                    cv::Scalar(140, 160, 200), thick, cv::LINE_AA);
        int txY = tiposY + 9 * S;
        for (size_t i = 0; i < std::min(ordenado.size(), size_t(4)); ++i) {
            const std::string t = "  \xe2\x80\xa2 "
                                  + nombreTipoDistraccion(ordenado[i].second)
                                  + "  (" + std::to_string(ordenado[i].first) + " veces)";
            cv::putText(card, t, cv::Point(7 * S, txY),
                        cv::FONT_HERSHEY_SIMPLEX, 0.36 * S / 2.0,
                        cv::Scalar(210, 220, 255), thick, cv::LINE_AA);
            txY += 10 * S;
        }
    }

    // Foto real del puesto (zona derecha)
    {
        cv::rectangle(card,
                      cv::Point(FOTO_X - S, FOTO_Y - S),
                      cv::Point(FOTO_X + FOTO_W + S, FOTO_Y + FOTO_H + S),
                      cv::Scalar(70, 80, 110), thick);

        bool fotoOk = false;

        if (!frameReferencia.empty() && configSala.valida()) {
            cv::Rect rectPuesto;
            bool encontrado = false;
            for (const auto& p : configSala.puestos) {
                if (p.id == m.idPuesto) { rectPuesto = p.rect; encontrado = true; break; }
            }

            // Recorta la región del puesto desde el frame de referencia, añade márgenes
            // para dar contexto visual, y la escala con interpolación Lanczos4 (máxima calidad).
            // cv::INTER_LANCZOS4 es un método de interpolación de la API pública de OpenCV, implementado en modules/imgproc/src/resize.cpp.
            // Generalmente se usa para generar imágenes de presentación.
            // se usa en este caso para el escalado de resolución de las tarjetas (eran muy borrosas)
            // Si no hay frame disponible o no se encuentra el puesto, muestra un placeholder.
            if (encontrado && rectPuesto.area() > 0) {
                const float sx = static_cast<float>(frameReferencia.cols)
                / std::max(1, configSala.anchoFrame);
                const float sy = static_cast<float>(frameReferencia.rows)
                                 / std::max(1, configSala.altoFrame);

                const int margenX = static_cast<int>(rectPuesto.width  * 0.40f * sx);
                const int margenY = static_cast<int>(rectPuesto.height * 0.40f * sy);

                cv::Rect recorte(
                    static_cast<int>(rectPuesto.x * sx) - margenX,
                    static_cast<int>(rectPuesto.y * sy) - margenY,
                    static_cast<int>(rectPuesto.width  * sx) + 2 * margenX,
                    static_cast<int>(rectPuesto.height * sy) + 2 * margenY);

                recorte &= cv::Rect(0, 0, frameReferencia.cols, frameReferencia.rows);

                if (recorte.area() > 0) {
                    cv::Mat roi = frameReferencia(recorte).clone();

                    const double scaleF = std::min(
                        static_cast<double>(FOTO_W) / roi.cols,
                        static_cast<double>(FOTO_H) / roi.rows);
                    cv::Mat escalada;
                    cv::resize(roi, escalada,
                               cv::Size(static_cast<int>(roi.cols * scaleF),
                                        static_cast<int>(roi.rows * scaleF)),
                               0, 0, cv::INTER_LANCZOS4);   // máxima calidad

                    const int offX = FOTO_X + (FOTO_W - escalada.cols) / 2;
                    const int offY = FOTO_Y + (FOTO_H - escalada.rows) / 2;
                    cv::rectangle(card, cv::Point(FOTO_X, FOTO_Y),
                                  cv::Point(FOTO_X + FOTO_W, FOTO_Y + FOTO_H),
                                  cv::Scalar(10, 10, 15), -1);
                    cv::Mat destRoi = card(cv::Rect(offX, offY, escalada.cols, escalada.rows));
                    escalada.copyTo(destRoi);

                    // Recuadro del puesto dentro de la foto
                    {
                        const float sxF = static_cast<float>(escalada.cols) / recorte.width;
                        const float syF = static_cast<float>(escalada.rows) / recorte.height;
                        const int px0 = offX + static_cast<int>(margenX * sx * sxF);
                        const int py0 = offY + static_cast<int>(margenY * sy * syF);
                        const int px1 = px0  + static_cast<int>(rectPuesto.width  * sx * sxF);
                        const int py1 = py0  + static_cast<int>(rectPuesto.height * sy * syF);
                        cv::rectangle(card,
                                      cv::Point(std::max(FOTO_X, px0), std::max(FOTO_Y, py0)),
                                      cv::Point(std::min(FOTO_X + FOTO_W - 1, px1),
                                                std::min(FOTO_Y + FOTO_H - 1, py1)),
                                      cv::Scalar(0, 220, 255), thick + 1, cv::LINE_AA);
                    }
                    fotoOk = true;
                }
            }
        }

        if (!fotoOk) {
            cv::rectangle(card,
                          cv::Point(FOTO_X, FOTO_Y),
                          cv::Point(FOTO_X + FOTO_W, FOTO_Y + FOTO_H),
                          cv::Scalar(40, 40, 50), -1);
            cv::putText(card, "Foto no disponible",
                        cv::Point(FOTO_X + 15 * S, FOTO_Y + FOTO_H / 2),
                        cv::FONT_HERSHEY_SIMPLEX, 0.38 * S / 2.0,
                        cv::Scalar(100, 110, 140), thick, cv::LINE_AA);
        }

        cv::putText(card, "Zona del puesto (referencia)",
                    cv::Point(FOTO_X, FOTO_Y + FOTO_H + 8 * S),
                    cv::FONT_HERSHEY_SIMPLEX, 0.32 * S / 2.0,
                    cv::Scalar(140, 150, 180), thick, cv::LINE_AA);
    }

    // Línea separadora antes de la conclusión
    const int sepY = FOTO_Y + FOTO_H + 14 * S;
    cv::line(card, cv::Point(7 * S, sepY), cv::Point(ANCHO - 7 * S, sepY),
             cv::Scalar(50, 60, 90), thick);

    // Conclusión pedagógica
    const cv::Scalar colorConcl =
        (pct >= 75.0) ? cv::Scalar(0, 220, 80) :
            (pct >= 50.0) ? cv::Scalar(0, 200, 230) :
            cv::Scalar(60, 60, 230);

    cv::putText(card, "Conclusion:", cv::Point(7 * S, sepY + 8 * S),
                cv::FONT_HERSHEY_SIMPLEX, 0.37 * S / 2.0,
                cv::Scalar(140, 160, 200), thick, cv::LINE_AA);

    const std::string concl = generarConclusionAlumno(m, eventos, fps);

    // Word-wrap manual adaptado al ancho real de la tarjeta:
    // parte la conclusión en líneas de máximo ANCHO_LINEA caracteres
    // sin cortar palabras a la mitad.
    const size_t ANCHO_LINEA = static_cast<size_t>(120 * S / 3);
    std::vector<std::string> lineas;
    {
        std::istringstream iss(concl);
        std::string palabra, linea;
        while (iss >> palabra) {
            if (!linea.empty() && linea.size() + 1 + palabra.size() > ANCHO_LINEA) {
                lineas.push_back(linea);
                linea.clear();
            }
            if (!linea.empty()) linea += ' ';
            linea += palabra;
        }
        if (!linea.empty()) lineas.push_back(linea);
    }

    int concY = sepY + 17 * S;
    for (size_t i = 0; i < lineas.size() && concY < ALTO - 3 * S; ++i) {
        cv::putText(card, lineas[i], cv::Point(7 * S, concY),
                    cv::FONT_HERSHEY_SIMPLEX, 0.36 * S / 2.0,
                    colorConcl, thick, cv::LINE_AA);
        concY += 9 * S;
    }

    if (!rutaSalida.empty())
        cv::imwrite(rutaSalida, card);

    return card;
}

// generarConclusionAlumno

std::string generarConclusionAlumno(const MetricasPuesto&                  m,
                                    const std::vector<EventoDistraccion>& eventos,
                                    double                                fps)
{
    std::ostringstream out;
    const double pct = m.porcentajeAtencion;

    out << std::fixed << std::setprecision(1) << pct << "% de atencion lograda";

    // Busca el episodio de distracción más largo de este alumno para poder
    // citarlo con timestamps concretos en el texto de conclusión.
    const EventoDistraccion* peor = nullptr;
    for (const auto& ev : eventos) {
        if (ev.idPuesto != m.idPuesto) continue;
        if (!peor || (ev.tiempoFinS - ev.tiempoInicioS) >
                         (peor->tiempoFinS - peor->tiempoInicioS))
            peor = &ev;
    }

    if (pct >= 75.0) {
        out << ". Buen nivel de atencion, seguimiento rutinario.";
        if (peor) {
            out << " Unico punto a vigilar: entre "
                << formatearTiempo(peor->tiempoInicioS) << " y "
                << formatearTiempo(peor->tiempoFinS)
                << " hubo un episodio de \"" << nombreTipoDistraccion(peor->tipo)
                << "\".";
        }
    } else if (pct >= 50.0) {
        out << ". Puede mejorar: atencion moderada.";
        if (peor) {
            out << " Enfocate en repasar lo cubierto entre "
                << formatearTiempo(peor->tiempoInicioS) << " y "
                << formatearTiempo(peor->tiempoFinS)
                << ", donde se registro tu episodio de distraccion mas largo ("
                << nombreTipoDistraccion(peor->tipo) << ", "
                << std::setprecision(0)
                << (peor->tiempoFinS - peor->tiempoInicioS) << " s).";
        } else {
            out << " Revisa los factores de distraccion durante la clase.";
        }
    } else {
        out << ". Puede mejorar: atencion baja.";
        if (peor) {
            out << " Intervencion pedagogica sugerida. Enfocate en repasar el "
                   "contenido cubierto entre "
                << formatearTiempo(peor->tiempoInicioS) << " y "
                << formatearTiempo(peor->tiempoFinS)
                << ", tramo con la distraccion mas prolongada ("
                << nombreTipoDistraccion(peor->tipo) << ", "
                << std::setprecision(0)
                << (peor->tiempoFinS - peor->tiempoInicioS) << " s)"
                << (m.numEventosDistrac > 1
                        ? " — ademas hubo otros " +
                              std::to_string(m.numEventosDistrac - 1) +
                              " episodios a lo largo de la clase."
                        : ".");
        } else {
            out << " Intervencion pedagogica sugerida.";
        }
    }

    return out.str();
}

// exportarReporteJSON
// Vuelca el ReporteClase completo en un archivo JSON
// Retorna true si el archivo se escribió sin errores.

bool exportarReporteJSON(const ReporteClase& rep, const std::string& rutaSalida)
{
    std::ofstream f(rutaSalida);
    if (!f.is_open()) return false;

    f << "{\n";
    f << "  \"video\": " << jsonStr(rep.nombreVideo) << ",\n";
    f << "  \"duracion_total_s\": " << std::fixed << std::setprecision(3)
      << rep.duracionTotalS << ",\n";
    f << "  \"fps\": " << rep.fps << ",\n";
    f << "  \"total_frames\": " << rep.totalFrames << ",\n";
    f << "  \"atencion_global_pct\": "
      << std::fixed << std::setprecision(2)
      << rep.porcentajeAtencionGlobal << ",\n";
    f << "  \"total_eventos_distraccion\": " << rep.totalEventosDistraccion << ",\n";
    f << "  \"duracion_media_distrac_s\": "
      << std::fixed << std::setprecision(3) << rep.duracionMediaDistracS << ",\n";
    f << "  \"duracion_max_distrac_s\": "
      << std::fixed << std::setprecision(3) << rep.duracionMaxDistracS << ",\n";
    f << "  \"conclusion_general\": " << jsonStr(rep.conclusionGeneral) << ",\n";

    // Métricas por alumno
    f << "  \"alumnos\": [\n";
    for (size_t i = 0; i < rep.metricasPorAlumno.size(); ++i) {
        const auto& m = rep.metricasPorAlumno[i];
        f << "    {\n";
        f << "      \"id_puesto\": "            << m.idPuesto           << ",\n";
        f << "      \"nombre\": "               << jsonStr(m.nombreAlumno) << ",\n";
        f << "      \"frames_analizados\": "    << m.framesAnalizados   << ",\n";
        f << "      \"frames_atento\": "        << m.framesAtento       << ",\n";
        f << "      \"frames_distraido\": "     << m.framesDistraido    << ",\n";
        f << "      \"frames_sin_deteccion\": " << m.framesSinDeteccion << ",\n";
        f << "      \"porcentaje_atencion\": "
          << std::fixed << std::setprecision(2) << m.porcentajeAtencion << ",\n";
        f << "      \"num_eventos_distraccion\": " << m.numEventosDistrac << ",\n";
        f << "      \"duracion_media_distrac_s\": "
          << std::fixed << std::setprecision(3) << m.duracionMediaDistracS << ",\n";
        f << "      \"duracion_max_distrac_s\": "
          << std::fixed << std::setprecision(3) << m.duracionMaxDistracS << ",\n";

        // Conclusión (detallada, con referencia a tiempos del peor episodio)
        const std::string concl = generarConclusionAlumno(m, rep.eventos, rep.fps);
        f << "      \"conclusion\": " << jsonStr(concl) << "\n";
        f << "    }" << (i + 1 < rep.metricasPorAlumno.size() ? "," : "") << "\n";
    }
    f << "  ],\n";

    // Eventos de distracción
    f << "  \"eventos_distraccion\": [\n";
    for (size_t i = 0; i < rep.eventos.size(); ++i) {
        const auto& ev = rep.eventos[i];
        const double dur = ev.tiempoFinS - ev.tiempoInicioS;
        f << "    {\n";
        f << "      \"id_puesto\": "       << ev.idPuesto << ",\n";
        f << "      \"alumno\": "          << jsonStr(ev.nombreAlumno) << ",\n";
        f << "      \"frame_inicio\": "    << ev.frameInicio << ",\n";
        f << "      \"frame_fin\": "       << ev.frameFin    << ",\n";
        f << "      \"tiempo_inicio_s\": "
          << std::fixed << std::setprecision(3) << ev.tiempoInicioS << ",\n";
        f << "      \"tiempo_fin_s\": "
          << std::fixed << std::setprecision(3) << ev.tiempoFinS    << ",\n";
        f << "      \"duracion_s\": "
          << std::fixed << std::setprecision(3) << dur              << ",\n";
        f << "      \"tipo\": "
          << jsonStr(nombreTipoDistraccion(ev.tipo)) << "\n";
        f << "    }" << (i + 1 < rep.eventos.size() ? "," : "") << "\n";
    }
    f << "  ],\n";

    // Momentos críticos
    f << "  \"momentos_criticos\": [\n";
    for (size_t i = 0; i < rep.momentosCriticos.size(); ++i) {
        const auto& mc = rep.momentosCriticos[i];
        f << "    {\n";
        f << "      \"frame_inicio\": "    << mc.frameInicio    << ",\n";
        f << "      \"frame_fin\": "       << mc.frameFin       << ",\n";
        f << "      \"tiempo_inicio_s\": "
          << std::fixed << std::setprecision(3) << mc.tiempoInicioS << ",\n";
        f << "      \"tiempo_fin_s\": "
          << std::fixed << std::setprecision(3) << mc.tiempoFinS    << ",\n";
        f << "      \"num_frames_distraccion\": " << mc.numDistracciones << ",\n";
        f << "      \"densidad_distrac_por_s\": "
          << std::fixed << std::setprecision(3) << mc.densidad << "\n";
        f << "    }" << (i + 1 < rep.momentosCriticos.size() ? "," : "") << "\n";
    }
    f << "  ]\n";
    f << "}\n";

    return f.good();
}

// exportarMomentosCriticosCSV y pdf.

bool exportarMomentosCriticosCSV(const ReporteClase& rep,
                                 const std::string&  rutaSalida)
{
    std::ofstream f(rutaSalida);
    if (!f.is_open()) return false;

    f << "rank,frame_inicio,frame_fin,tiempo_inicio_s,tiempo_fin_s,"
         "num_frames_distraccion,densidad_distrac_por_s\n";

    for (size_t i = 0; i < rep.momentosCriticos.size(); ++i) {
        const auto& mc = rep.momentosCriticos[i];
        f << (i + 1) << ','
          << mc.frameInicio << ',' << mc.frameFin << ','
          << std::fixed << std::setprecision(3)
          << mc.tiempoInicioS << ',' << mc.tiempoFinS << ','
          << mc.numDistracciones << ','
          << std::fixed << std::setprecision(3) << mc.densidad << '\n';
    }

    return f.good();
}

// Exporta el reporte completo de la clase a un único PDF (pensado para
// entregar al docente sin que tenga que abrir cinco archivos distintos
// (JSON, CSVs, PNGs sueltos)). Se arma el reporte
// como un string HTML (tablas + imágenes) y se le pide a Qt que
// lo "imprima" a PDF con QTextDocument + QPrinter.
//
// rutaTimelinePNG y rutasTarjetasPNG son opcionales: si vienen vacíos,
// el PDF se genera igual pero sin esas secciones (útil si algo falló más
// arriba en el pipeline y no se llegaron a generar las imágenes).
bool exportarReportePDF(const ReporteClase& reporte,
                        const std::string& rutaSalida,
                        const std::string& rutaTimelinePNG,
                        const std::vector<std::string>& rutasTarjetasPNG)
{
    if (rutaSalida.empty())
        return false;

    // Configuración de la impresora/documento PDF
    //
    // Se crea y configura ANTES de construir el HTML porque necesitamos
    // conocer el ancho útil REAL de la página impresa para reescalar el
    // timeline y las tarjetas de desempeño a un tamaño que quepa siempre
    // dentro de la hoja (sin esto, las imágenes se insertaban a un ancho
    // fijo en píxeles (900) sin relación con la resolución/página final),
    // por lo que terminaban más anchas que el área imprimible y se
    // recortaban en el margen derecho.
    QPrinter printer(QPrinter::ScreenResolution);
    printer.setResolution(150); // suficiente para lectura/impresión normal
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(QString::fromStdString(rutaSalida));
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageMargins(QMarginsF(15, 15, 15, 15), QPageLayout::Millimeter);

    // Ancho útil disponible para imágenes: el ancho de página de
    // márgenes (pageRect() excluye los márgenes configurados arriba), con
    // un 6% de adicional para que la imagen nunca toque el borde
    // del área imprimible (para que se vea bonito).
    const int anchoUtilImagenesPx =
        static_cast<int>(printer.pageRect(QPrinter::DevicePixel).width() * 0.94);

    std::ostringstream html;

    html << "<html><head><meta charset='UTF-8'>";

    html << "<style>";
    html << "body { font-family: Arial, sans-serif; font-size: 10pt; color: #222; }";
    html << "h1 { color: #1f3b57; font-size: 20pt; }";
    html << "h2 { color: #264b6a; border-bottom: 1px solid #aaa; padding-bottom: 4px; }";
    html << "h3 { color: #333; }";
    html << "table { border-collapse: collapse; width: 100%; margin-bottom: 14px; }";
    html << "th { background: #e9eef5; font-weight: bold; }";
    html << "th, td { border: 1px solid #aaa; padding: 5px; }";
    // tabla de KPIs de la sección 1: mismas filas/columnas para forzar el
    // salto de línea, pero sin bordes visibles (no es una tabla de datos).
    html << "table.kpi-table, table.kpi-table td { border: none; }";
    html << "table.kpi-table td { padding: 2px 6px 2px 0; }";
    html << "table.kpi-table td:first-child { width: 220px; white-space: nowrap; }";
    html << ".ok { color: #177a35; font-weight: bold; }";
    html << ".warn { color: #b26a00; font-weight: bold; }";
    html << ".bad { color: #a32020; font-weight: bold; }";
    html << ".small { color: #666; font-size: 8pt; }";
    // el ancho real de cada <img> ya viene fijado por su atributo
    // width= (ver imagenADataURI); esta regla solo aporta el margen.
    html << "img { margin: 8px 0 16px 0; }";
    html << "</style>";

    html << "</head><body>";

    html << "<h1>Reporte Final de Atencion - AulaViva</h1>";

    // Sección 1: datos generales del video
    // Lo básico para ubicar el reporte: qué video es, cuánto dura, a qué
    // fps se procesó y cuántos alumnos/puestos se alcanzaron a analizar.
    // NOTA: esta sección antes usaba <span class='kpi' style="display:block">
    // por cada dato, esperando que cada uno cayera en su propia línea. El
    // motor de render HTML de Qt (QTextDocument) no respeta display:block
    // sobre <span>, los trata como inline, y el resultado era todo el texto
    // corrido sin espacio ("...mp4Duracion total: 00:25..."). Se reemplaza
    // por una tabla sin bordes visibles (misma técnica que el resto del
    // reporte), que sí garantiza una fila = una línea en Qt.
    html << "<h2>1. Datos generales del video</h2>";
    html << "<table class='kpi-table'>";
    html << "<tr><td><b>Video:</b></td><td>"
         << htmlEscape(reporte.nombreVideo) << "</td></tr>";
    html << "<tr><td><b>Duracion total:</b></td><td>"
         << formatearTiempo(reporte.duracionTotalS)
         << " (" << std::fixed << std::setprecision(2)
         << reporte.duracionTotalS << " s)</td></tr>";
    html << "<tr><td><b>FPS:</b></td><td>"
         << std::fixed << std::setprecision(2) << reporte.fps << "</td></tr>";
    html << "<tr><td><b>Total de frames:</b></td><td>"
         << reporte.totalFrames << "</td></tr>";
    html << "<tr><td><b>Alumnos/puestos analizados:</b></td><td>"
         << reporte.metricasPorAlumno.size() << "</td></tr>";
    html << "</table>";

    // Sección 2: resumen global de la clase
    // Una sola fila con los KPIs agregados, para que el docente tenga el
    // panorama completo sin leer el detalle por alumno si no le interesa.
    html << "<h2>2. Resumen global de la clase</h2>";
    html << "<table>";
    html << "<tr>"
         << "<th>Atencion promedio</th>"
         << "<th>Total eventos distraccion</th>"
         << "<th>Duracion media distraccion</th>"
         << "<th>Duracion maxima distraccion</th>"
         << "</tr>";

    html << "<tr>";
    html << "<td>" << std::fixed << std::setprecision(1)
         << reporte.porcentajeAtencionGlobal << " %</td>";
    html << "<td>" << reporte.totalEventosDistraccion << "</td>";
    html << "<td>" << std::fixed << std::setprecision(2)
         << reporte.duracionMediaDistracS << " s</td>";
    html << "<td>" << std::fixed << std::setprecision(2)
         << reporte.duracionMaxDistracS << " s</td>";
    html << "</tr>";
    html << "</table>";

    html << "<p><b>Conclusion general:</b> "
         << htmlEscape(reporte.conclusionGeneral)
         << "</p>";

    // Sección 3: detalle por alumno (tabla)
    // Una fila por alumno con todas sus métricas, para comparar entre
    // ellos de un vistazo (quién estuvo más atento, quién tuvo más
    // eventos, etc).
    html << "<h2>3. Detalle por alumno</h2>";
    html << "<table>";
    html << "<tr>"
         << "<th>Alumno / Puesto</th>"
         << "<th>Frames analizados</th>"
         << "<th>Tiempo atento</th>"
         << "<th>Tiempo distraido</th>"
         << "<th>Sin deteccion</th>"
         << "<th>% Atencion</th>"
         << "<th>Eventos</th>"
         << "<th>Duracion media</th>"
         << "<th>Duracion maxima</th>"
         << "</tr>";

    for (const auto& m : reporte.metricasPorAlumno)
    {
        // Si no se pudo asociar un nombre real al puesto (por ejemplo,
        // porque sala_config no traía el campo "estudiante"), se usa el
        // id del puesto como identificador de respaldo.
        const std::string nombre =
            m.nombreAlumno.empty()
                ? ("Puesto #" + std::to_string(m.idPuesto))
                : m.nombreAlumno;

        const double tiempoAtentoS = m.framesAtento / reporte.fps;
        const double tiempoDistS   = m.framesDistraido / reporte.fps;

        html << "<tr>";
        html << "<td>" << htmlEscape(nombre) << "</td>";
        html << "<td>" << m.framesAnalizados << "</td>";
        html << "<td>" << formatearTiempo(tiempoAtentoS) << "</td>";
        html << "<td>" << formatearTiempo(tiempoDistS) << "</td>";
        html << "<td>" << m.framesSinDeteccion << "</td>";
        html << "<td>" << std::fixed << std::setprecision(1)
             << m.porcentajeAtencion << " %</td>";
        html << "<td>" << m.numEventosDistrac << "</td>";
        html << "<td>" << std::fixed << std::setprecision(2)
             << m.duracionMediaDistracS << " s</td>";
        html << "<td>" << std::fixed << std::setprecision(2)
             << m.duracionMaxDistracS << " s</td>";
        html << "</tr>";
    }

    html << "</table>";

    // Sección 4: conclusiones pedagógicas por alumno
    // Acá se reutiliza generarConclusionAlumno(), la misma función que
    // arma el texto que va en la tarjeta de desempeño, para no duplicar
    // la lógica de redacción. Debajo de cada conclusión va la tabla de
    // frecuencia de tipos de distracción, ordenada de mayor a menor, para
    // que se vea de inmediato cuál fue el problema más recurrente.
    html << "<h2>4. Conclusiones por alumno</h2>";

    for (const auto& m : reporte.metricasPorAlumno)
    {
        const std::string nombre =
            m.nombreAlumno.empty()
                ? ("Puesto #" + std::to_string(m.idPuesto))
                : m.nombreAlumno;

        html << "<h3>" << htmlEscape(nombre) << "</h3>";
        html << "<p>"
             << htmlEscape(generarConclusionAlumno(m, reporte.eventos, reporte.fps))
             << "</p>";

        auto freq = contarTiposDistrac(reporte.eventos, m.idPuesto);
        if (!freq.empty())
        {
            // Se pasa de map<Tipo,int> a un vector de pares (conteo, tipo)
            // solo para poder ordenar por conteo descendente; el map por
            // sí solo ordena por clave, no por valor.
            std::vector<std::pair<int, TipoDistraccion>> ordenado;
            for (const auto& kv : freq)
                ordenado.emplace_back(kv.second, kv.first);

            std::sort(ordenado.begin(), ordenado.end(),
                      [](const auto& a, const auto& b) {
                          return a.first > b.first;
                      });

            html << "<table>";
            html << "<tr><th>Tipo de distraccion</th><th>Veces</th></tr>";

            for (const auto& kv : ordenado)
            {
                html << "<tr>";
                html << "<td>" << htmlEscape(nombreTipoDistraccion(kv.second)) << "</td>";
                html << "<td>" << kv.first << "</td>";
                html << "</tr>";
            }

            html << "</table>";
        }
    }

    // Sección 5: momentos críticos
    // Los tramos de la clase con mayor densidad de distracciones
    // simultáneas (ya calculados antes por identificarMomentosCriticos()
    // y guardados en reporte.momentosCriticos). Sirve para que el docente
    // sepa exactamente en qué minuto la clase "se le fue" a la mayoría.
    html << "<h2>5. Momentos criticos</h2>";

    if (reporte.momentosCriticos.empty())
    {
        html << "<p>No se detectaron momentos criticos relevantes.</p>";
    }
    else
    {
        html << "<table>";
        html << "<tr>"
             << "<th>#</th>"
             << "<th>Frame inicio</th>"
             << "<th>Frame fin</th>"
             << "<th>Tiempo inicio</th>"
             << "<th>Tiempo fin</th>"
             << "<th>Frames distraidos</th>"
             << "<th>Densidad distrac/s</th>"
             << "</tr>";

        for (size_t i = 0; i < reporte.momentosCriticos.size(); ++i)
        {
            const auto& mc = reporte.momentosCriticos[i];

            html << "<tr>";
            html << "<td>" << (i + 1) << "</td>";
            html << "<td>" << mc.frameInicio << "</td>";
            html << "<td>" << mc.frameFin << "</td>";
            html << "<td>" << formatearTiempo(mc.tiempoInicioS) << "</td>";
            html << "<td>" << formatearTiempo(mc.tiempoFinS) << "</td>";
            html << "<td>" << mc.numDistracciones << "</td>";
            html << "<td>" << std::fixed << std::setprecision(3)
                 << mc.densidad << "</td>";
            html << "</tr>";
        }

        html << "</table>";
    }

    // Sección 6: listado de eventos de distracción
    // Un detalle del reporte: cada distracción individual,
    // de quién fue, de qué tipo y cuánto duró. Es la sección más larga
    // si hubo muchos eventos, pero queda al final de las tablas para no
    // saturar el resumen ejecutivo de las primeras páginas.
    html << "<h2>6. Eventos de distraccion</h2>";

    if (reporte.eventos.empty())
    {
        html << "<p>No se registraron eventos de distraccion.</p>";
    }
    else
    {
        html << "<table>";
        html << "<tr>"
             << "<th>#</th>"
             << "<th>Alumno / Puesto</th>"
             << "<th>Tipo</th>"
             << "<th>Frame inicio</th>"
             << "<th>Frame fin</th>"
             << "<th>Inicio</th>"
             << "<th>Fin</th>"
             << "<th>Duracion</th>"
             << "</tr>";

        for (size_t i = 0; i < reporte.eventos.size(); ++i)
        {
            const auto& ev = reporte.eventos[i];

            const std::string nombre =
                ev.nombreAlumno.empty()
                    ? ("Puesto #" + std::to_string(ev.idPuesto))
                    : ev.nombreAlumno;

            const double duracion = ev.tiempoFinS - ev.tiempoInicioS;

            html << "<tr>";
            html << "<td>" << (i + 1) << "</td>";
            html << "<td>" << htmlEscape(nombre) << "</td>";
            html << "<td>" << htmlEscape(nombreTipoDistraccion(ev.tipo)) << "</td>";
            html << "<td>" << ev.frameInicio << "</td>";
            html << "<td>" << ev.frameFin << "</td>";
            html << "<td>" << formatearTiempo(ev.tiempoInicioS) << "</td>";
            html << "<td>" << formatearTiempo(ev.tiempoFinS) << "</td>";
            html << "<td>" << std::fixed << std::setprecision(2)
                 << duracion << " s</td>";
            html << "</tr>";
        }

        html << "</table>";
    }

    // Sección 7: timeline visual
    // imagenADataURI() lee el PNG desde disco, lo reescala/comprime y lo
    // devuelve como data-URI base64 listo para meter en el <img>. Si la
    // imagen no existe o no se pudo leer, devuelve "" y directamente se
    // omite toda la sección (no tiene sentido dejar el título sin imagen).
    if (!rutaTimelinePNG.empty())
    {
        const std::string imgTag = imagenADataURI(rutaTimelinePNG, anchoUtilImagenesPx);
        if (!imgTag.empty())
        {
            html << "<h2>7. Timeline visual de atencion</h2>";
            html << "<p class='small'>Verde = atento, rojo = distraido, gris = sin deteccion.</p>";
            html << imgTag;
        }
    }

    // Sección 8: tarjetas de desempeño por alumno
    // Mismo tratamiento que el timeline, pero una imagen por alumno. Cada
    // tarjeta que falle en cargarse simplemente se salta, sin que eso
    // rompa el resto del PDF.
    if (!rutasTarjetasPNG.empty())
    {
        html << "<h2>8. Tarjetas de desempeno por alumno</h2>";

        for (const auto& rutaTarjeta : rutasTarjetasPNG)
        {
            const std::string imgTag = imagenADataURI(rutaTarjeta, anchoUtilImagenesPx);
            if (!imgTag.empty())
            {
                html << imgTag;
            }
        }
    }

    // Sección 9: nota de cierre
    // Recordatorio de que este PDF es un resumen, no reemplaza los CSV
    // frame a frame que sí se guardan aparte para quien quiera hacer un
    // análisis más fino.
    html << "<h2>9. Archivos complementarios</h2>";
    html << "<p class='small'>";
    html << "Este PDF resume los resultados principales. Los datos frame a frame "
            "siguen disponibles en los CSV generados por el sistema: "
            "atencion_frames.csv, atencion_eventos.csv, atencion_metricas.csv, "
            "poses.csv, coordenadas_rostros.csv y momentos_criticos.csv.";
    html << "</p>";

    html << "</body></html>";

    // Render final: HTML -> PDF vía Qt
    // Se cronometra esta parte porque es, por lejos, la más pesada de toda
    // la función (todo lo de arriba es solo concatenar strings). Sirve
    // para detectar en consola si el PDF se está demorando más de lo
    // normal, por ejemplo si alguien vuelve a subir la resolución del
    // printer o si entran demasiadas tarjetas.
    const auto tInicio = std::chrono::steady_clock::now();

    QTextDocument documento;
    documento.setHtml(QString::fromUtf8(html.str().c_str()));
    documento.setPageSize(printer.pageRect(QPrinter::DevicePixel).size());
    documento.print(&printer);

    const auto tFin = std::chrono::steady_clock::now();
    const double segundos =
        std::chrono::duration<double>(tFin - tInicio).count();
    std::cout << "  [exportarReportePDF] generado en "
              << std::fixed << std::setprecision(2) << segundos << " s\n";

    // QPrinter::print() no devuelve un booleano de éxito, así que el
    // chequeo real es abrir el archivo resultante y confirmar que quedó
    // con contenido. Si algo falló silenciosamente (permisos, disco lleno,
    // ruta inválida), esto lo detecta en vez de reportar éxito.
    std::ifstream verif(rutaSalida, std::ios::binary | std::ios::ate);
    return verif.good() && verif.tellg() > 0;
}