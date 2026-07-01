#ifndef ANALIZADOR_ATENCION_H
#define ANALIZADOR_ATENCION_H

// analizador_atencion.h
//
// Módulo central de clasificación de atención. Dado un frame de video y la pose
// de un alumno, decide si está atento o distraído y de qué tipo es la distracción.

#include "sala_config.h"
#include "estimador.h"
#include <string>
#include <vector>
#include <cmath>
#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

// Tipos de distracción
//
// Clasifica la causa de una distracción detectada.
// "Combinada" indica que se dispararon dos o más indicadores a la vez.

enum class TipoDistraccion
{
    Ninguna,
    MiradaLateral,     // el gaze no apunta a la pizarra (eje horizontal)
    MiradaAbajo,       // pitch positivo excesivo: alumno mira la mesa
    MiradaArriba,      // pitch negativo excesivo: alumno mira el techo
    CabeceoBrusco,     // roll excesivo: cabeza muy inclinada lateralmente
    SinRostro,         // YuNet no detectó ningún rostro en el frame
    Combinada          // dos o más indicadores activos simultáneamente
};

// Devuelve una cadena legible del tipo de distracción, útil para CSV y logs.
inline std::string nombreTipoDistraccion(TipoDistraccion t)
{
    switch (t) {
    case TipoDistraccion::Ninguna:       return "Atento";
    case TipoDistraccion::MiradaLateral: return "Mirada lateral";
    case TipoDistraccion::MiradaAbajo:   return "Mirada abajo";
    case TipoDistraccion::MiradaArriba:  return "Mirada arriba";
    case TipoDistraccion::CabeceoBrusco: return "Cabeceo";
    case TipoDistraccion::SinRostro:     return "Sin rostro";
    case TipoDistraccion::Combinada:     return "Combinada";
    default:                             return "Desconocida";
    }
}

// Estado instantáneo de atención
//
// Resultado de clasificar un único frame para un alumno concreto.
// "SinDeteccion" ocurre cuando YuNet no encontró ningún rostro en el puesto.

enum class EstadoAtencion { Atento, Distraido, SinDeteccion };

// Devuelve una cadena legible del estado, útil para CSV y logs. (si, como antes)
inline std::string nombreEstado(EstadoAtencion e)
{
    switch (e) {
    case EstadoAtencion::Atento:       return "Atento";
    case EstadoAtencion::Distraido:    return "Distraído";
    case EstadoAtencion::SinDeteccion: return "Sin detección";
    default:                           return "Desconocido";
    }
}

// Parámetros de atención por puesto
//
// Se calculan UNA VEZ por puesto al construir el EvaluadorAtencion, a partir
// de la geometría de la sala (ConfigSala). No se recalculan frame a frame.

struct RangosAtencion
{
    // Fallback yaw (usado cuando gazeValido == false)
    // Define el rango de yaw aceptable para considerar que el alumno mira
    // hacia la pizarra. Se usa SOLO cuando el estimador no puede calcular
    // un vector de gaze confiable (cara de perfil extremo, oclusión, etc.).
    double yawMin = -110.0;
    double yawMax =  110.0;

    // Pitch: rango fijo calibrado empíricamente
    // valores en grados. pitchMin < 0 (mira ligeramente arriba) y
    // pitchMax > 0 (mira abajo hacia la mesa). Derivados de la geometría
    // estimador.cpp: pitch proxy = (nariz.y − M.y) / bbox.height × 90°.
    double pitchMin = -25.0;
    double pitchMax =  35.0;

    // Roll: tolerancia simétrica
    // El alumno está distraído si |roll| > rollTolerancia.
    // 22° permite movimientos naturales de cabeza sin falsos positivos. (se puede configurar)
    double rollTolerancia = 22.0;

    // Gaze-ray: parámetros para el test de intersección
    // margenRelativoPizarra: fracción del tamaño de la pizarra que se añade
    // como margen al calcular el bbox expandido. 0.30 = 30% extra por lado.
    // NOTA: este margen se acota adicionalmente por la distancia puesto→pizarra
    // para que puestos muy cercanos no capturen la pared adyacente.
    double margenRelativoPizarra = 0.30;

    // Bounding box de la pizarra expandido con el margen anterior, en coordenadas
    // de píxeles del frame de referencia. Se precalcula al construir el evaluador.
    cv::Rect2f bboxPizarraExp;

    // Distancia máxima (en píxeles) a la que se proyecta el rayo de gaze
    // desde el centro interocular. Debe superar la distancia puesto→pizarra
    // para que el rayo siempre alcance la zona objetivo.
    // Se calcula como max(distPuesto→pizarra × 2.0, 300 px).
    float distProyeccionGaze = 800.f;

    // Centro geométrico del puesto en píxeles del frame de referencia.
    // Guardado aquí para que gazeApuntaAPizarra acceda a él sin parámetros extra. (por si acaso)
    cv::Point2f centroPuesto = {};

    // Margen angular de la pizarra visto desde el puesto (en grados). Se
    // calcula como el semi-ángulo que subtiende la pizarra + tolerancia, con
    // un mínimo de 10° y un máximo de 40°. Solo se usa en diagnóstico/JSON;
    // el test de clasificación real usa el test rayo-bbox/polígono.
    double margenAngularPizarraDeg = 18.0;

    // Tolerancia extra en grados, conservada por compatibilidad con el JSON.
    double toleranciaExtra = 5.0;
};

// Eventos y resultados
// Representa un episodio continuo de distracción (varios frames seguidos).
// Se abre cuando el evaluador confirma el estado Distraido y se cierra al
// recuperar el estado Atento.
struct EventoDistraccion
{
    int             frameInicio    = 0;
    int             frameFin       = 0;
    double          tiempoInicioS  = 0.0; // en segundos desde el inicio del video
    double          tiempoFinS     = 0.0;
    TipoDistraccion tipo           = TipoDistraccion::Ninguna;
    int             idPuesto       = -1;
    std::string     nombreAlumno;
};

// Resultado de clasificar un único frame para un único alumno.
// El EvaluadorAtencion produce uno de estos por cada llamada a evaluarFrame().
struct ResultadoFrameAtencion
{
    int             frame         = 0;
    int             idPuesto      = -1;
    std::string     nombreAlumno;
    EstadoAtencion  estado        = EstadoAtencion::SinDeteccion;
    TipoDistraccion tipoDistrac   = TipoDistraccion::SinRostro;
    double          yaw           = 0.0;
    double          pitch         = 0.0;
    double          roll          = 0.0;
    bool            poseValida    = false;
    bool            gazeEnPizarra = false; // true si el rayo de gaze intersecta la pizarra
};

// Métricas agregadas de un alumno al final del video.
struct MetricasPuesto
{
    int         idPuesto              = -1;
    std::string nombreAlumno;
    int         framesAnalizados      = 0;
    int         framesAtento          = 0;
    int         framesDistraido       = 0;
    int         framesSinDeteccion    = 0;
    double      porcentajeAtencion    = 0.0;  // framesAtento / (Atento+Distraido) × 100
    int         numEventosDistrac     = 0;
    double      duracionMediaDistracS = 0.0;
    double      duracionMaxDistracS   = 0.0;
};

// Funciones de geometría
// Calcula los RangosAtencion para un puesto concreto a partir de la geometría
// de la sala. Solo necesita llamarse una vez al inicio del análisis.
// NOTA: tambien esta toleranciaExtra: margen adicional en grados para el fallback de yaw y pitch.
RangosAtencion calcularRangosParaPuesto(
    const PuestoEstudiante& puesto,
    const ConfigPizarra&    pizarra,
    int anchoFrame,
    int altoFrame,
    double toleranciaExtra = 5.0);

// Test principal de gaze
// Lanza un rayo desde el centro interocular M en dirección pose.gazeDir y
// verifica si el segmento [M, M + gazeDir × distProyeccionGaze] intersecta:
//   1. El bbox expandido de la pizarra (test rápido axis-aligned).
//   2. El polígono rotado de la pizarra (test preciso en espacio local).
// Si pose.gazeValido == false (cara de perfil extremo u oclusión), delega al
// fallback de yaw: retorna true si pose.yaw ∈ [rangos.yawMin, rangos.yawMax].
bool gazeApuntaAPizarra(const DatosPose&     pose,
                        const LandmarksYuNet& lm,
                        const cv::Rect&       bboxRostro,
                        const RangosAtencion& rangos,
                        const ConfigPizarra&  pizarra);

// Clase principal
// Mantiene el estado de atención de todos los puestos de la sala a lo largo
// del video. Cada llamada a evaluarFrame() procesa un alumno en un frame y
// actualiza la ventana temporal de suavizado.

class EvaluadorAtencion
{
public:
    // configSala: distribución de sala ya cargada (pizarra + puestos).
    // ventanaTemporalFrames: número de frames consecutivos que deben ser
    // "distraídos" antes de confirmar el estado. Evita
    // que movimientos momentáneos generen eventos no deseados.
    // fps: frames por segundo del video (para calcular tiempos).
    explicit EvaluadorAtencion(const ConfigSala& configSala,
                               int ventanaTemporalFrames = 6,
                               double fps = 25.0);

    // Evalúa el estado de atención de un alumno en un frame concreto.
    // Requiere la pose estimada, los landmarks y el bbox del rostro del alumno,
    // más el id del puesto al que está asignado y el número de frame actual.
    ResultadoFrameAtencion evaluarFrame(const DatosPose&      pose,
                                        const LandmarksYuNet& lm,
                                        const cv::Rect&       bboxRostro,
                                        int                   idPuesto,
                                        int                   frame);

    // Sobrecarga de compatibilidad sin landmarks: usa solo yaw/pitch para
    // clasificar (gazeValido = false siempre). Útil para pruebas o cuando
    // YuNet no entrega los 5 landmarks.
    ResultadoFrameAtencion evaluarFrame(const DatosPose& pose,
                                        int idPuesto,
                                        int frame);

    // Acceso a resultados acumulados
    const std::vector<EventoDistraccion>&      eventosDistraccion() const { return eventos_; }
    const std::vector<ResultadoFrameAtencion>& historial()          const { return historial_; }
    std::vector<MetricasPuesto>                metricasPorPuesto()  const;

    // Exporta resultados a CSV (una fila por frame / evento / puesto)
    bool guardarCSVFrames  (const std::string& ruta) const;
    bool guardarCSVEventos (const std::string& ruta) const;
    bool guardarCSVMetricas(const std::string& ruta) const;

    // Devuelve los rangos precalculados de un puesto (útil para debug/visualización)
    const RangosAtencion* rangosParaPuesto(int idPuesto) const;

private:
    // Estado interno por puesto: buffer circular de estados y eventos activos.
    struct EstadoPuesto
    {
        int              idPuesto    = -1;
        std::string      nombreAlumno;
        RangosAtencion   rangos;
        ConfigPizarra    pizarra;       // copia local para el test de gaze

        // Buffer circular de tamaño ventana_: almacena los últimos N estados
        // para implementar el suavizado temporal (ventana deslizante).
        std::vector<EstadoAtencion> bufferEstados;
        int                         indiceBuffer    = 0;
        // Número de entradas del buffer que NO son Atento (contador diferencial).
        // Cuando countFueraRango >= ventana_, se confirma el estado Distraido.
        int                         countFueraRango = 0;

        EstadoAtencion estadoActual = EstadoAtencion::SinDeteccion;

        bool              enDistraccion = false; // hay un evento de distracción abierto
        EventoDistraccion eventoActivo;         // evento en curso (incompleto)
    };

    // Clasifica la pose de un alumno (antes del suavizado temporal).
    // Llama a gazeApuntaAPizarra() y evalúa pitch y roll de forma independiente.
    EstadoAtencion clasificarPose(const DatosPose&      pose,
                                  const LandmarksYuNet& lm,
                                  const cv::Rect&       bbox,
                                  const EstadoPuesto&   ep,
                                  TipoDistraccion&      tipoOut) const;

    // Aplica el suavizado temporal: actualiza el buffer circular y devuelve
    // el estado confirmado. Solo marca como Distraido cuando todos los frames
    // de la ventana lo son; de lo contrario devuelve Atento.
    EstadoAtencion actualizarVentana(EstadoPuesto&    ep,
                                     EstadoAtencion   estadoBruto,
                                     TipoDistraccion& tipoInOut) const;

    // Gestión del ciclo de vida de un evento de distracción.
    void abrirEvento (EstadoPuesto& ep, int frame, TipoDistraccion tipo);
    void cerrarEvento(EstadoPuesto& ep, int frame);

    // Búsqueda de puesto por id en el vector interno.
    EstadoPuesto*       buscarPuesto(int idPuesto);
    const EstadoPuesto* buscarPuesto(int idPuesto) const;

    std::vector<EstadoPuesto>           puestos_;   // estado por alumno
    std::vector<EventoDistraccion>      eventos_;   // episodios cerrados
    std::vector<ResultadoFrameAtencion> historial_; // resultado por frame×puesto

    int    ventana_;  // tamaño de la ventana temporal de suavizado
    double fps_;      // frames por segundo del video
};

// Guardado de rangos

// Guarda los RangosAtencion de todos los puestos de configSala en un archivo JSON.
// Útil para depuración; no es necesario para el análisis en sí.
bool guardarRangosAtencion(const std::string& ruta,
                           const ConfigSala&  config,
                           double toleranciaExtra = 5.0);

// Carga los rangos previamente guardados por guardarRangosAtencion().
// rangosOut recibe pares (idPuesto, RangosAtencion).
bool cargarRangosAtencion(const std::string& ruta,
                          const ConfigSala&  config,
                          std::vector<std::pair<int, RangosAtencion>>& rangosOut);

// Funciones utilitarias (conservadas por compatibilidad con tests)

// Calcula el rango de yaw aceptable para un puesto dado los extremos horizontales
// de la pizarra. Se usa como fallback cuando gazeValido == false.
void calcularRangosYaw(
    cv::Point2f centroPuesto,
    cv::Point2f extremoIzqPiz,
    cv::Point2f extremoDerPiz,
    double      toleranciaExtra,
    double&     yawMinOut,
    double&     yawMaxOut);

// Calcula el rango de pitch aceptable para un puesto.
// En la práctica usa valores fijos calibrados, ajustados
// ligeramente si la pizarra está muy por encima del nivel del alumno.
void calcularRangosPitch(
    cv::Point2f centroPuesto,
    cv::Point2f centroPizarra,
    float       altoPizarra,
    int         altoFrame,
    double      toleranciaExtra,
    double&     pitchMinOut,
    double&     pitchMaxOut);

#endif // ANALIZADOR_ATENCION_H