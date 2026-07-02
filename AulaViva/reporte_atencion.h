#ifndef REPORTE_ATENCION_H
#define REPORTE_ATENCION_H

// reporte_atencion.h
//
// Módulo de reportabilidad y visualización final de atención.

#include "analizador_atencion.h"
#include "sala_config.h"

#include <string>
#include <vector>

// Momento crítico, mayor momento de distracciones.

struct MomentoCritico
{
    int    frameInicio    = 0;     // frame donde arranca la ventana crítica
    int    frameFin       = 0;     // frame donde termina la ventana crítica
    double tiempoInicioS  = 0.0;   // mismo inicio, expresado en segundos
    double tiempoFinS     = 0.0;   // mismo fin, expresado en segundos
    int    numDistracciones = 0;   // cantidad de distracciones en la ventana
    double densidad         = 0.0; // distracciones / segundo
};

// Reporte completo

struct ReporteClase
{
    // Datos de sesión
    std::string nombreVideo;
    double      duracionTotalS = 0.0;
    double      fps            = 25.0;
    int         totalFrames    = 0;

    // Métricas por alumno
    std::vector<MetricasPuesto> metricasPorAlumno;

    // Eventos y momentos críticos
    std::vector<EventoDistraccion> eventos;
    std::vector<MomentoCritico>    momentosCriticos;

    // Métricas de clase globales
    double porcentajeAtencionGlobal = 0.0;
    int    totalEventosDistraccion  = 0;
    double duracionMediaDistracS    = 0.0;
    double duracionMaxDistracS      = 0.0;

    // Conclusión pedagógica agregada de la clase, con referencia a tiempos
    // del momento crítico más denso (rellenada por calcularReporte)
    std::string conclusionGeneral;
};

// Funciones públicas

// Calcula el reporte completo a partir de las métricas y eventos del evaluador.
ReporteClase calcularReporte(
    const EvaluadorAtencion& evaluador,
    const ConfigSala&        configSala,
    const std::string&       nombreVideo,
    double                   fps,
    int                      totalFrames);

// Identifica los N momentos con mayor concentración de distracciones.
// ventanaFrames: tamaño de la ventana deslizante (en frames analizados).
// topN: cuántos momentos se devuelven (ordenados por densidad, mayor primero).
std::vector<MomentoCritico> identificarMomentosCriticos(
    const std::vector<EventoDistraccion>& eventos,
    double fps,
    int    totalFrames,
    int    ventanaFrames = 150,   // ≈ 5 s a 30 fps
    int    topN          = 5);

// Imprime el resumen completo por consola (para los tecnicos), por si acaso, en realidad lo puede ver tambien por la interfaz.
// no queria eliminar esta parte del codigo asi que se mantiene como función adicional.
void imprimirResumenConsola(const ReporteClase& reporte);

// Genera el timeline visual de atención por alumno:
// - Una fila por alumno, coloreada frame a frame (verde=atento, rojo=distraído,
// gris=sin detección).
// - Marcadores de momentos críticos (líneas amarillas).
// - Eje de tiempo en segundos.
// Devuelve la imagen y la guarda en rutaSalida si no está vacía.
cv::Mat generarTimelineVisual(
    const EvaluadorAtencion& evaluador,
    const ReporteClase&      reporte,
    const ConfigSala&        configSala,
    double                   fps,
    int                      totalFrames,
    const std::string&       rutaSalida = "");

// Genera una "tarjeta de desempeño" por alumno (imagen PNG) con:
// - Barra de progreso de atención.
// - Métricas clave (% atención, nº distracciones, duración media/máx).
// - Lista de los tipos de distracción más frecuentes.
// - Foto real del puesto (recorte del frame de referencia) en la zona
// inferior derecha, para identificar visualmente al alumno.
// - Conclusión pedagógica completa, con referencia a tiempos concretos.
// frameReferencia: primer frame del video (o cualquier frame representativo)
// usado para recortar la zona del puesto. Si está vacío se deja el área en gris.
// Devuelve la imagen compuesta, la guarda en rutaSalida si no está vacía.
cv::Mat generarTarjetaDesempeño(
    const MetricasPuesto&                 metricas,
    const std::vector<EventoDistraccion>& eventos,
    double                                fps,
    const ConfigSala&                     configSala     = ConfigSala{},
    const cv::Mat&                        frameReferencia = cv::Mat{},
    const std::string&                    rutaSalida      = "");

// Exporta el reporte completo a JSON (sin dependencias externas).
bool exportarReporteJSON(const ReporteClase&      reporte,
                         const std::string&       rutaSalida);

// Exporta un CSV extendido de momentos críticos.
bool exportarMomentosCriticosCSV(const ReporteClase& reporte,
                                 const std::string&  rutaSalida);

// Construye una conclusión pedagógica concreta para un alumno: incluye el
// % de atención logrado y, si hubo distracciones, el tramo de tiempo (MM:SS)
// del peor episodio de distracción para que el docente sepa exactamente
// dónde reforzar ("enfócate en las partes que cubriste entre los tiempos
// X y Y"). Se usa en consola, en el JSON exportado y en la tarjeta visual.
std::string generarConclusionAlumno(const MetricasPuesto&                  m,
                                    const std::vector<EventoDistraccion>& eventos,
                                    double                                fps);


// También la exporta como PDF.
bool exportarReportePDF(const ReporteClase& reporte,const std::string& rutaSalida, const std::string& rutaTimelinePNG = "", const std::vector<std::string>& rutasTarjetasPNG = {});

#endif // REPORTE_ATENCION_H