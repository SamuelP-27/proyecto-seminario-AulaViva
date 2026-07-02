// procesador_video.cpp
// Módulo principal de procesamiento de video, tracking de rostros en red y
// evaluación analítica de la atención colectiva en el aula (AulaViva). (ESTE ES EL CODIGO CENTRAL DEL PROYECTO)
// Migrado a OpenCV 5.0 / YuNet 2026 (para que pudiera detectar correctamente multiples rostros)
//
// OBJETIVO:
// Coordinar el bucle de lectura del flujo de video, ejecutar la canalización (pipeline)
// de visión artificial en tiempo real y consolidar los datos de comportamiento e interés
// pedagógico tanto a nivel grupal como individual por estudiante.
//
// RESPONSABILIDADES:
// 1. Detección Facial Avanzada (YuNet): Utiliza la API DNN
// de OpenCV 5.0 mediante el modelo "face_detection_yunet_2026may.onnx". Implementa
// un ajuste dinámico del tamaño de entrada ("setInputSize") por frame, optimizando la
// confianza en alumnos ubicados al fondo de la sala mediante escalado relativo. (en si se encarga de la correcta detección de multiples rostros)
//
// 2. Rastreo de Identidad Estudiante (Tracking): Implementa un algoritmo de continuidad
// espacial por cercanía para persistir los IDs de los alumnos entre frames,
// manejando el cambio de posiciones (y su desvanecimiento/oclusión).
//
// 3. Estimación de Pose y Dirección de Mirada (Gaze): Integra componentes matemáticos para
// calcular y suavizar la orientación de la cabeza (Yaw, Pitch, Roll) mediante filtros de
// atenuación y proyecta vectores vectoriales desde el centro interocular hacia la pizarra. (realmente solo llama a las funciones que lo hacen)
//
// 4. Mapeo Geométrico de Puestos: Asocia espacialmente las coordenadas tridimensionales de los
// rostros detectados con los límites físicos configurados en el aula ("ConfigSala"),
// asignando automáticamente las métricas a las identidades reales de los alumnos.
//
// 5. Interfaz Gráfica y Visualización:
// - Genera un overlay de video limpio y unificado con tarjetas ("cards") flotantes de estado.
// - Renderiza el "Panel de Atención General", un histograma dinámico frame a frame de barras
// apiladas (Atento/Distraído/Sin Detección) con curvas de tendencia superpuestas.
//
// 6. Analíticas de Salida: Gestiona la exportación estructurada del
// analisis generando logs (CSV de frames, coordenadas y eventos cronológicos)
// y emitiendo reportes agregados estructurados en JSON junto a una "tarjeta de desempeño"
// visual (PNG) con minimapa y conclusiones pedagógicas por alumno.
//
// NOTAS DE MIGRACIÓN (OPENCV 5.0):
// - Se eliminó los fallos relacionados al forzado a múltiplos de 32 de Yunet (necesitaba que fuera multiplo de 32 la resolución del video)
// (el motor dnn de OCV5 resuelve estos fallos de strides).
// - Se removieron los guards estáticos de redimensionamiento de grafo (no se adaptaba correctamente al video el analisis).
// - Uso de "cv::getNumCPUs()" para paralelismo portable en la persistencia asíncrona de frames (std::async).
// (esto es inspiración de las optimizaciones de la primera parte del proyecto).


#include "procesador_video.h"
#include <opencv2/opencv.hpp>
#include <opencv2/objdetect.hpp>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <string>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <limits>
#include <future>
#include <chrono>
#include <deque>
#include <vector>
#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QProgressDialog>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include "estimador.h"
#include "filtro_pose.h"
#include "sala_config.h"
#include "analizador_atencion.h" // Casi todo el proyecto esta incluido
#include "reporte_atencion.h"

namespace fs = std::filesystem; // SIEMPRE necesario
namespace
{

// Umbrales de detección y rastreo
constexpr double UMBRAL_CONFIANZA           = 0.45;   // score mínimo para aceptar un candidato
constexpr double DISTANCIA_MAX_CONTINUIDAD  = 0.25;   // fracción de la diagonal del frame para asociar rostros entre frames
constexpr int    MAX_FRAMES_SIN_DETECCION   = 30;     // frames consecutivos sin detectar antes de descartar una persona

// Downscale leve aplicado antes de pasar el frame a YuNet: al reducir la imagen,
// los rostros pequeños (estudiantes al fondo) ocupan mayor proporción relativa
// y el detector los encuentra con mayor confianza.
constexpr double FACTOR_ESCALA_DETECCION    = 0.75;

// Parámetros de YuNet (score mínimo de detección, umbral NMS (elimina detecciones duplicadas de un mismo rostro) y candidatos máximos)
constexpr float YUNET_UMBRAL_SCORE = 0.55f;
constexpr float YUNET_UMBRAL_NMS   = 0.3f;
constexpr int   YUNET_TOP_K        = 5000;

// Tamaño mínimo de rostro aceptado (en píxeles). Descarta detecciones muy pequeñas
// que en la práctica corresponden a ruido o reflejos. (puede configurarse si no se detectan rostros)
constexpr int YUNET_MIN_FACE_PX = 15;

// Dimensiones del panel de atención general y longitud del historial
// ANCHO/ALTO_PANEL_ATEN: tamaño en píxeles de la ventana "Atención General de la Sala".
// Se elige 480×300 para coincidir con el antiguo panel de pose y dejar espacio
// suficiente al gráfico de barras + panel inferior de contadores + leyenda.
// HISTORIAL_ATEN_MAX: número máximo de frames almacenados en el historial de
// barras apiladas. 300 frames ≈ 10-12 s a 25-30 fps; ventana temporal útil
// sin consumir memoria excesiva.
constexpr int ANCHO_PANEL_ATEN   = 480;
constexpr int ALTO_PANEL_ATEN    = 300;
constexpr int HISTORIAL_ATEN_MAX = 300;

// modelos: 2026 (shape dinámico) (ojo que tiene que estar en la carpeta del ejecutable .exe)
const std::vector<std::string> RUTAS_YUNET = {
    "face_detection_yunet_2026may.onnx",
    "modelos/face_detection_yunet_2026may.onnx",
};

// Un candidato de detección: bbox, confianza y landmarks faciales.
struct FaceCandidate {
    cv::Rect       rect;
    double         confianza;
    LandmarksYuNet landmarks;
};

// Estado de una persona activa en el rastreador: identidad persistente entre frames,
// filtros de suavizado de pose y contador de frames sin detección directa.
struct PersonaRastreada {
    int            id;
    cv::Rect       rect;
    double         confianza;
    LandmarksYuNet landmarks;
    FiltroPose     filtro;
    int            framesSinDeteccion = 0;
    bool           deteccionDirecta   = true;  // false = posición interpolada
    DatosPose      ultimaPose;
};

// Entrada de log por frame y persona, incluyendo el puesto de sala asignado.
struct LogDeteccionMulti {
    int    numeroFrame;
    int    idPersona;
    bool   rostroEncontrado;
    int    x, y, ancho, alto;
    double confianza;
    bool   deteccionDirecta;
    int    idPuesto = -1;   // ID del puesto asignado (-1 = no asociado a ningún puesto)
};

// Intenta cargar el modelo YuNet desde la primera ruta válida de la lista.
// El inputSize inicial es arbitrario (320×320); se sobreescribe en cada frame
// mediante setInputSize(), que en OpenCV 5 ya no corrompe el grafo interno.
cv::Ptr<cv::FaceDetectorYN> cargarYuNet(const std::vector<std::string>& rutas)
{
    for (const auto& ruta : rutas)
    {
        if (!fs::exists(ruta))
            continue;

        try
        {
            // inputSize
            auto det = cv::FaceDetectorYN::create(
                ruta, "", cv::Size(320, 320),
                YUNET_UMBRAL_SCORE, YUNET_UMBRAL_NMS, YUNET_TOP_K);

            std::cout << "YuNet cargado desde: " << ruta << "\n";
            return det;
        }
        catch (const cv::Exception& e)
        {
            std::cerr << "Fallo al cargar " << ruta << ": " << e.what() << "\n";
            continue;
        }
    }
    return nullptr;
}

// Gráfico de Atención General de la Sala
//
// Muestra en tiempo real:
// - Histograma/barras apiladas frame a frame:
// verde  = nº alumnos ATENTOS
// rojo   = nº alumnos DISTRAÍDOS
// gris   = nº alumnos SIN DETECCIÓN
// - Curva de % atención global (línea amarilla) superpuesta.
// - Panel inferior: contadores actuales + porcentaje de atención acumulado.
// - Leyenda de colores en la esquina inferior derecha.
//
// La ventana tiene las mismas dimensiones que el antiguo panel de pose y se
// muestra en la misma posición, sin cambiar nada más del bucle principal.

// VisualizadorAtencionSala — panel de atención en tiempo real
//
// Mantiene el historial de los últimos HISTORIAL_ATEN_MAX frames y construye
// el panel visual.

class VisualizadorAtencionSala
{
public:
    VisualizadorAtencionSala() = default;

    // Llamar una vez por frame con los contadores del frame actual.
    // totalPuestos: número de puestos configurados (denominador del %).
    void registrar(int atentos, int distraidos, int sinDeteccion, int totalPuestos)
    {
        if (static_cast<int>(histAtentos_.size()) >= HISTORIAL_ATEN_MAX)
        {
            histAtentos_.pop_front();
            histDistraidos_.pop_front();
            histSinDet_.pop_front();
            histPct_.pop_front();
        }
        histAtentos_.push_back(atentos);
        histDistraidos_.push_back(distraidos);
        histSinDet_.push_back(sinDeteccion);

        const double den = (totalPuestos > 0) ? totalPuestos : 1.0;
        histPct_.push_back(100.0 * atentos / den);

        // Acumula para el porcentaje histórico global
        acumAtentos_   += atentos;
        acumTotal_     += totalPuestos;
    }

    // Construye y devuelve el panel (se puede llamar sin haber registrado nada).
    cv::Mat construirPanel(int atentos, int distraidos, int sinDet,
                           int totalPuestos) const
    {
        cv::Mat panel(ALTO_PANEL_ATEN, ANCHO_PANEL_ATEN,
                      CV_8UC3, cv::Scalar(18, 18, 24));

        // Título
        cv::putText(panel, "Atencion General de la Sala",
                    cv::Point(8, 16),
                    cv::FONT_HERSHEY_SIMPLEX, 0.48,
                    cv::Scalar(210, 210, 210), 1, cv::LINE_AA);

        // Zona de gráfico
        constexpr int MARGEN_IZQ  = 36;
        constexpr int MARGEN_SUP  = 22;
        constexpr int MARGEN_INF  = 58;   // deja espacio para el panel inferior
        const int anchoG = ANCHO_PANEL_ATEN - MARGEN_IZQ - 8;
        const int altoG  = ALTO_PANEL_ATEN  - MARGEN_SUP - MARGEN_INF;

        // Fondo del área de gráfico
        cv::rectangle(panel,
                      cv::Point(MARGEN_IZQ, MARGEN_SUP),
                      cv::Point(MARGEN_IZQ + anchoG, MARGEN_SUP + altoG),
                      cv::Scalar(30, 30, 40), -1);

        // Líneas de cuadrícula horizontales a 25 / 50 / 75 / 100 %
        for (int pct : {25, 50, 75, 100}) {
            const int yg = MARGEN_SUP + altoG - static_cast<int>(altoG * pct / 100.0);
            cv::line(panel,
                     cv::Point(MARGEN_IZQ,          yg),
                     cv::Point(MARGEN_IZQ + anchoG, yg),
                     cv::Scalar(50, 50, 65), 1, cv::LINE_AA);
            std::ostringstream ss;
            ss << pct << "%";
            cv::putText(panel, ss.str(),
                        cv::Point(2, yg + 4),
                        cv::FONT_HERSHEY_SIMPLEX, 0.26,
                        cv::Scalar(110, 110, 130), 1, cv::LINE_AA);
        }

        // Barras apiladas frame a frame
        const int n = static_cast<int>(histAtentos_.size());
        if (n > 0 && totalPuestos > 0) {
            const float barW = static_cast<float>(anchoG) / HISTORIAL_ATEN_MAX;

            for (int i = 0; i < n; ++i) {
                const float x0f = MARGEN_IZQ + i * barW;
                const int   x0  = static_cast<int>(x0f);
                const int   x1  = std::max(x0 + 1,
                                        static_cast<int>(x0f + barW));

                const int tot = totalPuestos;
                // Alturas proporcionales (en píxeles del área de gráfico)
                const int hA = static_cast<int>(altoG * histAtentos_[i]    / tot);
                const int hD = static_cast<int>(altoG * histDistraidos_[i] / tot);
                const int hS = static_cast<int>(altoG * histSinDet_[i]     / tot);

                // Apilado desde abajo: atentos (verde) | distraídos (rojo) | sin det (gris)
                int yBase = MARGEN_SUP + altoG;

                if (hA > 0) {
                    cv::rectangle(panel,
                                  cv::Point(x0, yBase - hA),
                                  cv::Point(x1, yBase),
                                  cv::Scalar(0, 180, 50), -1);
                    yBase -= hA;
                }
                if (hD > 0) {
                    cv::rectangle(panel,
                                  cv::Point(x0, yBase - hD),
                                  cv::Point(x1, yBase),
                                  cv::Scalar(30, 30, 200), -1);
                    yBase -= hD;
                }
                if (hS > 0) {
                    cv::rectangle(panel,
                                  cv::Point(x0, yBase - hS),
                                  cv::Point(x1, yBase),
                                  cv::Scalar(80, 80, 90), -1);
                }
            }

            // Curva de % atención (línea amarilla)
            for (int i = 1; i < n; ++i) {
                const int x0 = MARGEN_IZQ + static_cast<int>((i - 1) * barW);
                const int x1 = MARGEN_IZQ + static_cast<int>( i      * barW);
                const int y0p = MARGEN_SUP + altoG
                                - static_cast<int>(altoG * histPct_[i - 1] / 100.0);
                const int y1p = MARGEN_SUP + altoG
                                - static_cast<int>(altoG * histPct_[i]     / 100.0);
                cv::line(panel,
                         cv::Point(x0, y0p), cv::Point(x1, y1p),
                         cv::Scalar(0, 220, 230), 1, cv::LINE_AA);
            }
            // Punto final de la curva
            if (n >= 1) {
                const int xp = MARGEN_IZQ + static_cast<int>((n - 1) * barW);
                const int yp = MARGEN_SUP + altoG
                               - static_cast<int>(altoG * histPct_.back() / 100.0);
                cv::circle(panel, cv::Point(xp, yp), 3,
                           cv::Scalar(0, 240, 255), -1, cv::LINE_AA);
            }
        }

        // Borde del área de gráfico
        cv::rectangle(panel,
                      cv::Point(MARGEN_IZQ, MARGEN_SUP),
                      cv::Point(MARGEN_IZQ + anchoG, MARGEN_SUP + altoG),
                      cv::Scalar(70, 70, 90), 1);

        // Panel inferior: contadores actuales
        const int panelY = ALTO_PANEL_ATEN - MARGEN_INF + 6;

        const double pctActual =
            (totalPuestos > 0) ? 100.0 * atentos / totalPuestos : 0.0;
        const double pctAcum   =
            (acumTotal_ > 0) ? 100.0 * acumAtentos_ / acumTotal_ : 0.0;

        // Color del porcentaje actual según umbral
        const cv::Scalar colorPct =
            (pctActual >= 75.0) ? cv::Scalar(0,  220,  80) :
                (pctActual >= 50.0) ? cv::Scalar(0,  200, 230) :
                cv::Scalar(50,  50, 230);

        // Línea 1: Atención ahora
        {
            std::ostringstream ss;
            ss << "Ahora: " << std::fixed << std::setprecision(0)
               << pctActual << "%  ("
               << atentos << " atentos  "
               << distraidos << " distraidos  "
               << sinDet << " sin det)";
            cv::putText(panel, ss.str(),
                        cv::Point(MARGEN_IZQ, panelY + 14),
                        cv::FONT_HERSHEY_SIMPLEX, 0.36,
                        colorPct, 1, cv::LINE_AA);
        }

        // Línea 2: Porcentaje acumulado histórico
        {
            std::ostringstream ss;
            ss << "Acumulado: " << std::fixed << std::setprecision(1)
               << pctAcum << "% atencion  |  "
               << totalPuestos << " puestos configurados";
            cv::putText(panel, ss.str(),
                        cv::Point(MARGEN_IZQ, panelY + 30),
                        cv::FONT_HERSHEY_SIMPLEX, 0.33,
                        cv::Scalar(170, 170, 190), 1, cv::LINE_AA);
        }

        // Leyenda
        struct LegItem { cv::Scalar color; std::string label; };
        const std::vector<LegItem> leyenda = {
                                              { cv::Scalar(0,  180,  50), "Atento"   },
                                              { cv::Scalar(30,  30, 200), "Distraido" },
                                              { cv::Scalar(80,  80,  90), "Sin det."  },
                                              { cv::Scalar(0,  220, 230), "% aten."   },
                                              };
        int lx = MARGEN_IZQ;
        for (const auto& li : leyenda) {
            cv::rectangle(panel,
                          cv::Point(lx,     panelY + 38),
                          cv::Point(lx + 10, panelY + 48),
                          li.color, -1);
            cv::putText(panel, li.label,
                        cv::Point(lx + 13, panelY + 48),
                        cv::FONT_HERSHEY_SIMPLEX, 0.27,
                        cv::Scalar(180, 180, 200), 1, cv::LINE_AA);
            lx += 10 + 6 + static_cast<int>(li.label.size()) * 6 + 4;
        }

        return panel;
    }

private:
    std::deque<int>    histAtentos_;
    std::deque<int>    histDistraidos_;
    std::deque<int>    histSinDet_;
    std::deque<double> histPct_;

    // Acumuladores para el % histórico total
    long long acumAtentos_ = 0;
    long long acumTotal_   = 0;
};

} // namespace anónimo

// Anotación multi-rostro (sin cambios de lógica)
static cv::Mat anotarFrameMulti(const cv::Mat& frame,
                                const std::vector<PersonaRastreada>& personas)
{
    cv::Mat anotado = frame.clone();

    if (personas.empty())
    {
        cv::putText(anotado, "Sin rostros detectados",
                    cv::Point(20, 36), cv::FONT_HERSHEY_SIMPLEX, 0.8,
                    cv::Scalar(0, 0, 220), 2, cv::LINE_AA);
        return anotado;
    }

    for (const auto& p : personas)
    {
        const cv::Rect r = p.rect;
        cv::Scalar colorCaja = p.deteccionDirecta
                                   ? cv::Scalar(0, 230, 0)
                                   : cv::Scalar(0, 200, 255);
        cv::rectangle(anotado, r, colorCaja, 2, cv::LINE_AA);

        std::ostringstream tagStream;
        tagStream << "ID: " << p.id
                  << " [" << (p.deteccionDirecta ? "DET" : "INT") << "]";
        cv::putText(anotado, tagStream.str(),
                    cv::Point(r.x, std::max(r.y - 20, 12)),
                    cv::FONT_HERSHEY_SIMPLEX, 0.45, colorCaja, 1, cv::LINE_AA);

        std::ostringstream confStr;
        confStr << "Conf: " << std::fixed << std::setprecision(2) << p.confianza;
        cv::putText(anotado, confStr.str(),
                    cv::Point(r.x, std::max(r.y - 6, 24)),
                    cv::FONT_HERSHEY_SIMPLEX, 0.38,
                    cv::Scalar(220, 220, 220), 1, cv::LINE_AA);

        const cv::Point centro(r.x + r.width / 2, r.y + r.height / 2);
        cv::drawMarker(anotado, centro, colorCaja,
                       cv::MARKER_CROSS, 10, 1, cv::LINE_AA);

        // Dibuja landmarks faciales (ojos = azul, nariz = blanco, boca = amarillo)
        if (p.deteccionDirecta)
        {
            cv::circle(anotado, p.landmarks.ojoIzq,  3, cv::Scalar(255, 100,  50), -1, cv::LINE_AA);
            cv::circle(anotado, p.landmarks.ojoDer,  3, cv::Scalar(255, 100,  50), -1, cv::LINE_AA);
            cv::circle(anotado, p.landmarks.nariz,   3, cv::Scalar(255, 255, 255), -1, cv::LINE_AA);
            cv::circle(anotado, p.landmarks.bocaIzq, 3, cv::Scalar(  0, 220, 220), -1, cv::LINE_AA);
            cv::circle(anotado, p.landmarks.bocaDer, 3, cv::Scalar(  0, 220, 220), -1, cv::LINE_AA);
        }

        // Solo dibuja la flecha de gaze (sin texto duplicado aquí)
        if (p.ultimaPose.gazeValido && p.deteccionDirecta)
        {
            const cv::Point2f M  = (p.landmarks.ojoIzq + p.landmarks.ojoDer) * 0.5f;
            const cv::Point2f Pg = estimador::proyectarGaze(M, p.ultimaPose.gazeDir, 120.f);
            cv::arrowedLine(anotado,
                            cv::Point(static_cast<int>(M.x),  static_cast<int>(M.y)),
                            cv::Point(static_cast<int>(Pg.x), static_cast<int>(Pg.y)),
                            cv::Scalar(255, 0, 220), 2, cv::LINE_AA, 0, 0.35);
        }
    }
    return anotado;
}

// Información de atención por persona (para la capa de visualización)
struct InfoAtencionVis {
    int             idPersona = -1;
    int             idPuesto  = -1;
    std::string     nombre;       // nombre del alumno (pu.estudiante)
    std::string     seatLabel;    // etiqueta del puesto, ej. "Fila1-Col2"
    EstadoAtencion  estado    = EstadoAtencion::SinDeteccion;
    TipoDistraccion tipo      = TipoDistraccion::SinRostro;
    bool            gazeEnPiz = false;
};

// Anotación limpia: UNA sola capa de información por alumno
// - Borde del bbox: verde = atento, rojo = distraído, gris = sin asignar
// - Flecha magenta de gaze (desde centro interocular)
// - Landmarks faciales
// - Card de texto encima del bbox:
// Línea 1: "<nombre> | ATENTO"  o  "<nombre> | DISTRAIDO: <tipo>"
// Línea 2: "GAZE:PIZ Y:xx P:xx"  o  "GAZE:NO  Y:xx P:xx"
// -Etiqueta del puesto debajo del bbox
static cv::Mat anotarFrameConAtencion(
    const cv::Mat&                       frame,
    const std::vector<PersonaRastreada>& personas,
    const std::vector<InfoAtencionVis>&  infoAtencion)
{
    cv::Mat anotado = frame.clone();

    if (personas.empty()) {
        cv::putText(anotado, "Sin rostros detectados",
                    cv::Point(20, 36), cv::FONT_HERSHEY_SIMPLEX, 0.8,
                    cv::Scalar(0, 0, 220), 2, cv::LINE_AA);
        return anotado;
    }

    for (const auto& p : personas) {
        // Busca la info de atención de esta persona
        const InfoAtencionVis* info = nullptr;
        for (const auto& ia : infoAtencion)
            if (ia.idPersona == p.id) { info = &ia; break; }

        const cv::Rect r = p.rect;

        // Color del bbox según gaze (respuesta inmediata, sin suavizado)
        // - gazeEnPiz=true  + pose válida  → verde   (ATENTO)
        // - gazeEnPiz=false + pose válida  → rojo    (DISTRAÍDO)
        // - sin pose válida / sin asignar  → gris
        const bool poseYGazeValidos = p.ultimaPose.validar && p.ultimaPose.gazeValido;
        const bool displayAtento    = poseYGazeValidos
                                       ? (info && info->idPuesto >= 0 && info->gazeEnPiz)
                                       : (info && info->estado == EstadoAtencion::Atento);

        cv::Scalar colorBbox;
        if (!info || info->idPuesto < 0 || !p.ultimaPose.validar)
            colorBbox = cv::Scalar(120, 120, 120);   // gris: sin asignar / sin pose
        else if (displayAtento)
            colorBbox = cv::Scalar(0, 220, 60);       // verde: atento
        else
            colorBbox = cv::Scalar(30, 30, 220);      // rojo: distraído

        cv::rectangle(anotado, r, colorBbox, 2, cv::LINE_AA);

        // Landmarks faciales
        if (p.deteccionDirecta) {
            cv::circle(anotado, p.landmarks.ojoIzq,  3, cv::Scalar(255, 180,  60), -1, cv::LINE_AA);
            cv::circle(anotado, p.landmarks.ojoDer,  3, cv::Scalar(255, 180,  60), -1, cv::LINE_AA);
            cv::circle(anotado, p.landmarks.nariz,   3, cv::Scalar(255, 255, 255), -1, cv::LINE_AA);
            cv::circle(anotado, p.landmarks.bocaIzq, 3, cv::Scalar(  0, 220, 220), -1, cv::LINE_AA);
            cv::circle(anotado, p.landmarks.bocaDer, 3, cv::Scalar(  0, 220, 220), -1, cv::LINE_AA);
        }

        // Flecha de gaze (magenta)
        if (p.ultimaPose.gazeValido && p.deteccionDirecta) {
            const cv::Point2f M  = (p.landmarks.ojoIzq + p.landmarks.ojoDer) * 0.5f;
            const cv::Point2f Pg = estimador::proyectarGaze(M, p.ultimaPose.gazeDir, 120.f);
            cv::arrowedLine(anotado,
                            cv::Point(static_cast<int>(M.x),  static_cast<int>(M.y)),
                            cv::Point(static_cast<int>(Pg.x), static_cast<int>(Pg.y)),
                            cv::Scalar(255, 0, 220), 2, cv::LINE_AA, 0, 0.35);
        }

        // Card de texto (una sola capa, encima del bbox)
        const int cardY = std::max(r.y - 50, 2);

        if (info && info->idPuesto >= 0) {
            // Línea 1: nombre + estado (basado en gaze crudo para respuesta inmediata)
            std::string line1;
            if (!info->nombre.empty()) line1 = info->nombre + " | ";

            if (!p.ultimaPose.validar) {
                line1 += "Sin deteccion";
            } else if (displayAtento) {
                line1 += "ATENTO";
            } else {
                // DISTRAIDO: el tipo proviene del evaluador (puede ser Lateral, Abajo, etc.)
                // Si el evaluador aún no confirmó, usamos MiradaLateral como fallback
                const TipoDistraccion tipoVis =
                    (info->tipo != TipoDistraccion::Ninguna && info->tipo != TipoDistraccion::SinRostro)
                        ? info->tipo : TipoDistraccion::MiradaLateral;
                line1 += "DISTRAIDO: " + nombreTipoDistraccion(tipoVis);
            }

            const cv::Scalar colorLine1 =
                (!p.ultimaPose.validar)  ? cv::Scalar(160, 160, 160) :
                    displayAtento            ? cv::Scalar(0, 230,  80)   :
                    cv::Scalar(60,  60, 255);
            cv::putText(anotado, line1,
                        cv::Point(r.x, cardY + 14),
                        cv::FONT_HERSHEY_SIMPLEX, 0.42, colorLine1, 1, cv::LINE_AA);

            // Línea 2: gaze + yaw/pitch
            std::ostringstream ss2;
            ss2 << (info->gazeEnPiz ? "GAZE:PIZ" : "GAZE:NO ");
            if (p.ultimaPose.validar)
                ss2 << " Y:" << std::fixed << std::setprecision(1)
                    << p.ultimaPose.yaw << " P:" << p.ultimaPose.pitch;
            const cv::Scalar colorGaze = info->gazeEnPiz
                                             ? cv::Scalar(0, 255, 180) : cv::Scalar(50, 50, 255);
            cv::putText(anotado, ss2.str(),
                        cv::Point(r.x, cardY + 30),
                        cv::FONT_HERSHEY_SIMPLEX, 0.38, colorGaze, 1, cv::LINE_AA);

            // Etiqueta del puesto (debajo del bbox)
            if (!info->seatLabel.empty())
                cv::putText(anotado, info->seatLabel,
                            cv::Point(r.x + 2,
                                      std::min(anotado.rows - 4, r.y + r.height + 14)),
                            cv::FONT_HERSHEY_SIMPLEX, 0.35,
                            cv::Scalar(0, 200, 200), 1, cv::LINE_AA);
        } else {
            // No asignado a puesto: label mínima (ID + confianza)
            std::ostringstream tag;
            tag << "ID:" << p.id
                << " C:" << std::fixed << std::setprecision(2) << p.confianza;
            cv::putText(anotado, tag.str(),
                        cv::Point(r.x, cardY + 14),
                        cv::FONT_HERSHEY_SIMPLEX, 0.38,
                        cv::Scalar(180, 180, 180), 1, cv::LINE_AA);
        }
    }
    return anotado;
}

static bool preguntarSiNo(const QString& mensaje)
{
    return QMessageBox::question(nullptr, "Confirmacion", mensaje,
                                 QMessageBox::Yes | QMessageBox::No)
           == QMessageBox::Yes;
}

static std::string nombreFrame(const fs::path& carpeta, int numero)
{
    std::ostringstream ss;
    ss << "frame_" << std::setfill('0') << std::setw(5) << numero << ".jpg";
    return (carpeta / ss.str()).string();
}

static void eliminarFrames(const fs::path& carpeta)
{
    std::error_code ec;
    fs::remove_all(carpeta, ec);
}

// API pública

std::string seleccionarArchivo()
{
    QString ruta = QFileDialog::getOpenFileName(
        nullptr, "Selecciona el video MP4", QString(),
        "Archivos MP4 (*.mp4);;Todos los archivos (*.*)");
    return ruta.isEmpty() ? "" : ruta.toStdString();
}

// Nodo de compatibilidad: el .h lo declara pero el flujo real usa la versión
// interna con LogDeteccionMulti.
bool guardarCoordenadasCSV(const std::string& rutaCSV,
                           const std::vector<ResultadoDeteccion>& resultados)
{
    std::ofstream archivo(rutaCSV);
    if (!archivo.is_open()) return false;
    archivo << "frame,rostro_detectado,candidatos,x,y,ancho,alto,confianza,deteccion_directa\n";
    for (const auto& r : resultados)
    {
        archivo << r.numeroFrame << ',' << (r.rostroEncontrado ? 1 : 0) << ','
                << r.candidatosDetectados << ',';
        if (r.rostroEncontrado)
            archivo << r.rostro.x << ',' << r.rostro.y << ','
                    << r.rostro.ancho << ',' << r.rostro.alto << ','
                    << std::fixed << std::setprecision(3) << r.rostro.confianza;
        else
            archivo << ",,,,";
        archivo << ',' << (r.deteccionDirecta ? 1 : 0) << '\n';
    }
    return archivo.good();
}

// Detección con YuNet (versión OpenCV 5)

static std::vector<FaceCandidate>
detectarRostrosEnFrame(const cv::Mat& frame,
                       cv::Ptr<cv::FaceDetectorYN>& detector)
{
    std::vector<FaceCandidate> candidatos;
    if (frame.empty() || detector.empty()) return candidatos;

    // Escala opcional (1.0 = sin reducción)
    cv::Mat entrada;
    if (FACTOR_ESCALA_DETECCION < 1.0)
    {
        cv::resize(frame, entrada,
                   cv::Size(static_cast<int>(frame.cols * FACTOR_ESCALA_DETECCION),
                            static_cast<int>(frame.rows * FACTOR_ESCALA_DETECCION)),
                   0, 0, cv::INTER_LINEAR);
    }
    else
    {
        entrada = frame;
    }

    // OpenCV 5: setInputSize() en cada frame es seguro y necesario para
    // que el modelo dinámico ajuste sus shapes de salida.
    detector->setInputSize(entrada.size());

    cv::Mat resultados;  // Nx15: x,y,w,h, 5 landmarks(x,y), score
    detector->detect(entrada, resultados);

    if (resultados.empty()) return candidatos;

    const double escalaX = static_cast<double>(frame.cols) / entrada.cols;
    const double escalaY = static_cast<double>(frame.rows) / entrada.rows;
    const cv::Rect limites(0, 0, frame.cols, frame.rows);

    for (int i = 0; i < resultados.rows; ++i)
    {
        const float x      = resultados.at<float>(i, 0);
        const float y      = resultados.at<float>(i, 1);
        const float ancho  = resultados.at<float>(i, 2);
        const float alto   = resultados.at<float>(i, 3);
        const float score  = resultados.at<float>(i, 14);

        cv::Rect r(
            static_cast<int>(std::lround(x     * escalaX)),
            static_cast<int>(std::lround(y     * escalaY)),
            static_cast<int>(std::lround(ancho * escalaX)),
            static_cast<int>(std::lround(alto  * escalaY)));
        r &= limites;

        if (r.area() <= 0) continue;

        const double conf = std::clamp(static_cast<double>(score), 0.0, 1.0);
        if (conf >= UMBRAL_CONFIANZA
            && r.width  >= YUNET_MIN_FACE_PX
            && r.height >= YUNET_MIN_FACE_PX)
        {
            // Columnas 4-13: 5 landmarks (x,y) × 5 puntos
            // orden YuNet: ojo_izq, ojo_der, nariz, boca_izq, boca_der
            LandmarksYuNet lm;
            lm.ojoIzq  = { resultados.at<float>(i,  4) * static_cast<float>(escalaX),
                         resultados.at<float>(i,  5) * static_cast<float>(escalaY) };
            lm.ojoDer  = { resultados.at<float>(i,  6) * static_cast<float>(escalaX),
                         resultados.at<float>(i,  7) * static_cast<float>(escalaY) };
            lm.nariz   = { resultados.at<float>(i,  8) * static_cast<float>(escalaX),
                        resultados.at<float>(i,  9) * static_cast<float>(escalaY) };
            lm.bocaIzq = { resultados.at<float>(i, 10) * static_cast<float>(escalaX),
                          resultados.at<float>(i, 11) * static_cast<float>(escalaY) };
            lm.bocaDer = { resultados.at<float>(i, 12) * static_cast<float>(escalaX),
                          resultados.at<float>(i, 13) * static_cast<float>(escalaY) };
            candidatos.push_back({r, conf, lm});
        }
    }

    return candidatos;
}

// Función principal
void procesarVideo(const std::string& rutaVideo,
                   const std::string& carpetaSalida,
                   bool               guardarVisualizacionRostros,
                   int                intervaloDeteccion,
                   int                calidadJPEG,
                   const ConfigSala&  configSala)
{
    cv::setUseOptimized(true);
    cv::setNumThreads(cv::getNumberOfCPUs());

    if (!fs::exists(rutaVideo))
    {
        QMessageBox::critical(nullptr, "Error",
                              QString("El archivo no existe:\n%1")
                                  .arg(QString::fromStdString(rutaVideo)));
        return;
    }

    if (intervaloDeteccion < 1) intervaloDeteccion = 1;
    calidadJPEG = std::clamp(calidadJPEG, 0, 100);
    const std::vector<int> parametrosJPEG = {cv::IMWRITE_JPEG_QUALITY, calidadJPEG};

    // Carga YuNet
    cv::Ptr<cv::FaceDetectorYN> detectorRostro = cargarYuNet(RUTAS_YUNET);
    if (!detectorRostro)
    {
        QMessageBox::critical(nullptr, "Error",
                              "No se encontro ningun modelo YuNet.\n"
                              "Descarga face_detection_yunet_2026may.onnx desde:\n"
                              "https://github.com/opencv/opencv_zoo/tree/main/models/face_detection_yunet");
        return;
    }

    cv::VideoCapture cap;
    try
    {
        cap.open(rutaVideo);
        if (!cap.isOpened())
        {
            QMessageBox::critical(nullptr, "Error",
                                  "No se pudo abrir el flujo de video.");
            return;
        }

        const double fps         = cap.get(cv::CAP_PROP_FPS);
        const int    totalFrames = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));
        const int    ancho       = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
        const int    alto        = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));

        QMessageBox::information(nullptr, "Metadatos del Video",
                                 QString("Resolucion: %1 x %2\nFPS: %3\nFrames Totales: %4")
                                     .arg(ancho).arg(alto).arg(fps).arg(totalFrames));

        if (!preguntarSiNo("Desea continuar con la extraccion de frames multi-rostro?"))
        {
            cap.release();
            return;
        }

        const fs::path carpeta(carpetaSalida);
        std::error_code ec;
        if (!fs::exists(carpeta)) fs::create_directories(carpeta, ec);

        const fs::path carpetaAnotados = carpeta / "anotados";
        if (guardarVisualizacionRostros && !fs::exists(carpetaAnotados))
            fs::create_directories(carpetaAnotados, ec);

        QProgressDialog progreso("Procesando video multideteccion...",
                                 "Cancelar", 0, totalFrames, nullptr);
        progreso.setWindowTitle("Extraccion de frames y Analisis Colectivo");
        progreso.setWindowModality(Qt::ApplicationModal);
        progreso.setValue(0);

        const std::string VENTANA_VIDEO =
            "Clase en vivo (Vista Panoramica Multi-Rostro) [Esc = Salir]";
        const std::string VENTANA_POSE = "Atencion General de la Sala";
        cv::namedWindow(VENTANA_VIDEO, cv::WINDOW_NORMAL);
        cv::namedWindow(VENTANA_POSE,  cv::WINDOW_NORMAL);
        cv::resizeWindow(VENTANA_VIDEO, std::min(ancho, 960), std::min(alto, 540));
        cv::resizeWindow(VENTANA_POSE,  ANCHO_PANEL_ATEN, ALTO_PANEL_ATEN);

        VisualizadorAtencionSala visSala;
        estimador        poseEstimator;

        int frameActual            = 0;
        int framesGuardados        = 0;
        int framesConRostro        = 0;
        int framesSinRostro        = 0;
        int framesConMultiplesCand = 0;
        bool cancelado             = false;

        std::vector<LogDeteccionMulti> resultadosDeteccion;
        std::vector<PersonaRastreada>  personasActivas;
        int siguienteIdPersona = 0;

        const fs::path rutaPoseCSV = carpeta / "poses.csv";
        std::ofstream  archivoPose(rutaPoseCSV.string());
        archivoPose << "frame,timestamp_p,id_persona,id_puesto,nombre_estudiante,"
                       "pose_valida,yaw,pitch,roll,estado_atencion,tipo_distraccion\n";

        // Evaluador de atención
        // Ventana temporal: con intervaloDeteccion=5 a 30fps, cada frame analizado
        // representa ~0,17 s (6 frames/s).
        const int ventanaFrames = std::max(3, 6 / std::max(1, intervaloDeteccion));
        EvaluadorAtencion evaluador(configSala, ventanaFrames,
                                    fps > 0.0 ? fps : 25.0);

        // Guarda rangos precalculados (útil para debug y auditoría)
        if (configSala.valida()) {
            const fs::path rutaRangos = carpeta / "rangos_atencion.json";
            guardarRangosAtencion(rutaRangos.string(), configSala);
        }

        std::vector<std::future<void>> tareasEscritura;
        cv::Mat frame;

        // Captura el primer frame del video para usarlo como referencia visual
        // en las tarjetas de desempeño (foto del puesto del alumno).
        cv::Mat primerFrame;
        {
            cap.set(cv::CAP_PROP_POS_FRAMES, 0);
            cap.read(primerFrame);
            cap.set(cv::CAP_PROP_POS_FRAMES, 0);   // rebobina al inicio
        }

        while (true)
        {
            const bool tocaDetectar = (intervaloDeteccion <= 1)
            || (frameActual % intervaloDeteccion == 0)
                || personasActivas.empty();

            if (!tocaDetectar)
            {
                cap.grab();
                for (auto& p : personasActivas)
                {
                    p.deteccionDirecta = false;
                    resultadosDeteccion.push_back(
                        {frameActual, p.id, true,
                         p.rect.x, p.rect.y, p.rect.width, p.rect.height,
                         p.confianza, false});
                }
                if (personasActivas.empty()) framesSinRostro++;
                else
                {
                    framesConRostro++;
                    if (personasActivas.size() > 1) framesConMultiplesCand++;
                }
                if (frameActual % 15 == 0)
                {
                    progreso.setValue(frameActual + 1);
                    QCoreApplication::processEvents();
                }
                ++frameActual;
                continue;
            }

            try
            {
                bool ret = cap.read(frame);
                if (!ret || frame.empty()) break;

                // Guarda frame
                const std::string nombreArch = nombreFrame(carpeta, frameActual);
                cv::Mat frameParaGuardar = frame.clone();
                tareasEscritura.push_back(
                    std::async(std::launch::async,
                               [nombreArch, frameParaGuardar, parametrosJPEG]() {
                                   cv::imwrite(nombreArch, frameParaGuardar,
                                               parametrosJPEG);
                               }));
                ++framesGuardados;

                // Detección
                std::vector<FaceCandidate> candidatos =
                    detectarRostrosEnFrame(frame, detectorRostro);

                if (candidatos.empty() && personasActivas.empty())
                    framesSinRostro++;
                else
                {
                    framesConRostro++;
                    if (candidatos.size() > 1 || personasActivas.size() > 1)
                        framesConMultiplesCand++;
                }

                const double diagonal =
                    std::sqrt(static_cast<double>(frame.cols) * frame.cols
                              + static_cast<double>(frame.rows) * frame.rows);
                const double distMaxPermitida = diagonal * DISTANCIA_MAX_CONTINUIDAD;

                std::vector<bool> candidatoAsignado(candidatos.size(), false);

                // Tracking: asignar candidatos a personas existentes
                for (auto& p : personasActivas)
                {
                    cv::Point2f centroPrevio(
                        p.rect.x + p.rect.width  / 2.0f,
                        p.rect.y + p.rect.height / 2.0f);
                    int    mejorIdx  = -1;
                    double mejorDist = distMaxPermitida;

                    for (size_t j = 0; j < candidatos.size(); ++j)
                    {
                        if (candidatoAsignado[j]) continue;
                        cv::Point2f centroCurr(
                            candidatos[j].rect.x + candidatos[j].rect.width  / 2.0f,
                            candidatos[j].rect.y + candidatos[j].rect.height / 2.0f);
                        double d = cv::norm(centroCurr - centroPrevio);
                        if (d < mejorDist) { mejorDist = d; mejorIdx = static_cast<int>(j); }
                    }

                    if (mejorIdx != -1)
                    {
                        p.rect = candidatos[mejorIdx].rect;
                        p.confianza = candidatos[mejorIdx].confianza;
                        p.landmarks = candidatos[mejorIdx].landmarks;
                        p.framesSinDeteccion = 0;
                        p.deteccionDirecta = true;
                        candidatoAsignado[mejorIdx] = true;
                    }
                    else
                    {
                        p.framesSinDeteccion++;
                        p.deteccionDirecta = false;
                    }
                }

                // Nuevas personas
                for (size_t j = 0; j < candidatos.size(); ++j)
                {
                    if (!candidatoAsignado[j])
                    {
                        PersonaRastreada nuevoAlumno;
                        nuevoAlumno.id = siguienteIdPersona++;
                        nuevoAlumno.rect = candidatos[j].rect;
                        nuevoAlumno.confianza = candidatos[j].confianza;
                        nuevoAlumno.landmarks = candidatos[j].landmarks;
                        nuevoAlumno.framesSinDeteccion = 0;
                        nuevoAlumno.deteccionDirecta = true;
                        personasActivas.push_back(nuevoAlumno);
                    }
                }

                // Purga personas perdidas
                personasActivas.erase(
                    std::remove_if(personasActivas.begin(), personasActivas.end(),
                                   [](const PersonaRastreada& p) {
                                       return p.framesSinDeteccion > MAX_FRAMES_SIN_DETECCION;
                                   }),
                    personasActivas.end());

                // Pose + log
                for (auto& p : personasActivas)
                {
                    DatosPose pose = poseEstimator.calcularpose(p.rect, p.landmarks);
                    if (pose.validar)
                    {
                        pose.yaw   = p.filtro.filtrarYaw(pose.yaw);
                        pose.pitch = p.filtro.filtrarPitch(pose.pitch);
                        pose.roll  = p.filtro.filtrarRoll(pose.roll);
                    }
                    p.ultimaPose = pose;

                    // Asocia el rostro a un puesto de la sala
                    const int idPuesto = configSala.valida()
                                             ? asociarRostroAPuesto(p.rect, configSala)
                                             : -1;

                    // Recupera el nombre del estudiante si hay puesto asociado
                    std::string nomEstudiante;
                    if (idPuesto >= 0 && configSala.valida()) {
                        for (const auto& pu : configSala.puestos)
                            if (pu.id == idPuesto) { nomEstudiante = pu.estudiante; break; }
                    }

                    // Evaluación de atención (v2: pasa landmarks y bbox para test de gaze)
                    const ResultadoFrameAtencion resAten =
                        evaluador.evaluarFrame(pose, p.landmarks, p.rect,
                                               idPuesto, frameActual);

                    const double timestampS= (fps>0.0) ? static_cast<double>(frameActual) /fps : 0.0;
                    archivoPose << frameActual << ','
                                << std::fixed << std::setprecision(3) << timestampS << ','
                                << p.id << ','
                                << idPuesto << ','
                                << nomEstudiante << ','
                                << (pose.validar ? 1 : 0) << ','
                                << pose.yaw << ',' << pose.pitch << ','
                                << pose.roll << ','
                                << nombreEstado(resAten.estado) << ','
                                << nombreTipoDistraccion(resAten.tipoDistrac) << "\n";

                    resultadosDeteccion.push_back(
                        {frameActual, p.id, true,
                         p.rect.x, p.rect.y, p.rect.width, p.rect.height,
                         p.confianza, p.deteccionDirecta, idPuesto});
                }

                if (personasActivas.empty())
                    resultadosDeteccion.push_back(
                        {frameActual, -1, false, 0, 0, 0, 0, 0.0, false});

                // Visualización unificada
                // 1. Construye la info de atención para cada persona activa
                std::vector<InfoAtencionVis> infoAtenVec;
                if (configSala.valida()) {
                    for (const auto& p : personasActivas) {
                        InfoAtencionVis ia;
                        ia.idPersona = p.id;
                        ia.idPuesto  = asociarRostroAPuesto(p.rect, configSala);
                        if (ia.idPuesto >= 0) {
                            for (const auto& pu : configSala.puestos) {
                                if (pu.id == ia.idPuesto) {
                                    ia.nombre    = pu.estudiante;
                                    ia.seatLabel = pu.nombre;
                                    break;
                                }
                            }
                            // Recupera estado + gaze del historial de este frame
                            for (auto it = evaluador.historial().rbegin();
                                 it != evaluador.historial().rend(); ++it) {
                                if (it->frame == frameActual && it->idPuesto == ia.idPuesto) {
                                    ia.estado    = it->estado;
                                    ia.tipo      = it->tipoDistrac;
                                    ia.gazeEnPiz = it->gazeEnPizarra;
                                    break;
                                }
                            }
                        }
                        infoAtenVec.push_back(ia);
                    }
                }

                // 2. Anota el frame con una sola capa limpia por alumno
                cv::Mat frameAnotado = anotarFrameConAtencion(
                    frame, personasActivas, infoAtenVec);

                // 3. Superpone distribución de sala (pizarra + puestos)
                if (configSala.valida())
                    dibujarSobreFrame(frameAnotado, configSala);

                // 4. Label de frame en la esquina superior derecha
                std::ostringstream fLabel;
                fLabel << "F:" << frameActual
                       << " A:" << personasActivas.size()
                       << " D:"
                       << std::count_if(infoAtenVec.begin(), infoAtenVec.end(),
                                        [](const InfoAtencionVis& ia){
                                            return ia.estado == EstadoAtencion::Distraido;})
                << " ?:"
                << std::count_if(infoAtenVec.begin(), infoAtenVec.end(),
                                 [](const InfoAtencionVis& ia){
                                     return ia.estado == EstadoAtencion::SinDeteccion;});
                cv::putText(frameAnotado, fLabel.str(),
                            cv::Point(frameAnotado.cols - 180, 18),
                            cv::FONT_HERSHEY_SIMPLEX, 0.45,
                            cv::Scalar(200, 200, 200), 1, cv::LINE_AA);

                cv::imshow(VENTANA_VIDEO, frameAnotado);

                // Gráfico de atención general de la sala
                {
                    // Cuenta estados actuales de todos los puestos en este frame
                    int cntAtento = 0, cntDist = 0, cntSinDet = 0;
                    const int totalPuestos = configSala.valida()
                                                 ? static_cast<int>(configSala.puestos.size()) : 0;

                    if (configSala.valida()) {
                        for (const auto& ia : infoAtenVec) {
                            if (ia.idPuesto < 0) continue;
                            if      (ia.estado == EstadoAtencion::Atento)       ++cntAtento;
                            else if (ia.estado == EstadoAtencion::Distraido)    ++cntDist;
                            else                                                 ++cntSinDet;
                        }
                        // Puestos sin persona visible → SinDeteccion
                        const int puestosConPersona =
                            cntAtento + cntDist + cntSinDet;
                        cntSinDet += std::max(0, totalPuestos - puestosConPersona);
                    }

                    visSala.registrar(cntAtento, cntDist, cntSinDet, totalPuestos);
                    cv::imshow(VENTANA_POSE,
                               visSala.construirPanel(cntAtento, cntDist,
                                                      cntSinDet, totalPuestos));
                }

                if (cv::waitKey(1) == 27) cancelado = true;

                if (guardarVisualizacionRostros)
                {
                    const std::string nombreAnotado =
                        nombreFrame(carpetaAnotados, frameActual);
                    tareasEscritura.push_back(
                        std::async(std::launch::async,
                                   [nombreAnotado, frameAnotado, parametrosJPEG]() {
                                       cv::imwrite(nombreAnotado, frameAnotado,
                                                   parametrosJPEG);
                                   }));
                }

                // Limpia futuros completados
                tareasEscritura.erase(
                    std::remove_if(tareasEscritura.begin(), tareasEscritura.end(),
                                   [](const std::future<void>& f) {
                                       return f.wait_for(std::chrono::seconds(0))
                                       == std::future_status::ready;
                                   }),
                    tareasEscritura.end());

                if (frameActual % 15 == 0)
                {
                    int porcentaje = static_cast<int>(
                        100.0 * (frameActual + 1) / totalFrames);
                    progreso.setValue(frameActual + 1);
                    progreso.setLabelText(
                        QString("Frame %1/%2 (%3%)\nAlumnos en Clase: %4")
                            .arg(frameActual + 1).arg(totalFrames)
                            .arg(porcentaje).arg(personasActivas.size()));

                    std::cout << "\rProcesando frame " << (frameActual + 1)
                              << "/" << totalFrames
                              << " - Personas: " << personasActivas.size()
                              << std::flush;
                    QCoreApplication::processEvents();
                    if (progreso.wasCanceled()) cancelado = true;
                }

                if (cancelado) break;
            }
            catch (const std::exception& ex)
            {
                std::cerr << "\nFallo mitigado en frame "
                          << frameActual << ": " << ex.what() << "\n";
            }

            ++frameActual;
        }

        progreso.close();
        cv::destroyWindow(VENTANA_VIDEO);
        cv::destroyWindow(VENTANA_POSE);
        for (auto& t : tareasEscritura) if (t.valid()) t.wait();

        // Guardado de resultados de atención
        if (configSala.valida()) {
            const fs::path rutaCSVFrames   = carpeta / "atencion_frames.csv";
            const fs::path rutaCSVEventos  = carpeta / "atencion_eventos.csv";
            const fs::path rutaCSVMetricas = carpeta / "atencion_metricas.csv";

            evaluador.guardarCSVFrames  (rutaCSVFrames.string());
            evaluador.guardarCSVEventos (rutaCSVEventos.string());
            evaluador.guardarCSVMetricas(rutaCSVMetricas.string());

            std::cout << "\nResultados de atencion guardados en:\n"
                      << "  " << rutaCSVFrames.string()   << "\n"
                      << "  " << rutaCSVEventos.string()  << "\n"
                      << "  " << rutaCSVMetricas.string() << "\n";

            // REPORTE FINAL — resumen, timeline visual, tarjetas, JSON/CSV

            // 1. Calcula el reporte agregado de la clase (métricas globales,
            // eventos, momentos críticos y conclusión pedagógica).
            const ReporteClase reporte = calcularReporte(
                evaluador, configSala,
                fs::path(rutaVideo).filename().string(),
                fps, frameActual);

            // 2. Resultados principales por consola.
            imprimirResumenConsola(reporte);

            // 3. Timeline visual de atención de toda la clase.
            const fs::path rutaTimeline = carpeta / "timeline_atencion.png";
            const cv::Mat timeline = generarTimelineVisual(
                evaluador, reporte, configSala, fps, frameActual,
                rutaTimeline.string());

            // 4. Tarjeta de desempeño (visualización simple) por alumno,
            // con minimapa de ubicación del puesto y conclusión completa.
            const fs::path carpetaTarjetas = carpeta / "tarjetas_desempeño";
            if (!fs::exists(carpetaTarjetas)) fs::create_directories(carpetaTarjetas, ec);

            std::vector<cv::Mat>       tarjetas;
            std::vector<std::string>   nombresAlumnos;
            std::vector<std::string>   rutasTarjetasPDF;
            for (const auto& m : reporte.metricasPorAlumno) {
                const std::string nombre = m.nombreAlumno.empty()
                ? ("Puesto #" + std::to_string(m.idPuesto))
                : m.nombreAlumno;
                const std::string nombreArchivo = "tarjeta_" +
                                                  (m.nombreAlumno.empty() ? ("puesto_" + std::to_string(m.idPuesto))
                                                                          : m.nombreAlumno) + ".png";
                const fs::path rutaTarjeta = carpetaTarjetas / nombreArchivo;
                rutasTarjetasPDF.push_back(rutaTarjeta.string());
                tarjetas.push_back(generarTarjetaDesempeño(
                    m, reporte.eventos, reporte.fps, configSala,
                    primerFrame, rutaTarjeta.string()));
                nombresAlumnos.push_back(nombre);
            }

            // 5. Exporta datos y métricas estandarizadas a JSON, pdf y CSV.
            const fs::path rutaReporteJSON = carpeta / "reporte_atencion.json";
            const fs::path rutaMomentosCSV = carpeta / "momentos_criticos.csv";
            const fs::path rutaReportePDF  = carpeta / "reporte_atencion.pdf";

            const bool jsonOk = exportarReporteJSON(
                reporte,
                rutaReporteJSON.string()
                );

            const bool momentosOk = exportarMomentosCriticosCSV(
                reporte,
                rutaMomentosCSV.string()
                );

            const bool pdfOk = exportarReportePDF(
                reporte,
                rutaReportePDF.string(),
                rutaTimeline.string(),
                rutasTarjetasPDF
                );

            std::cout << "Reporte final exportado en:\n"
                      << "  " << rutaTimeline.string()    << "\n"
                      << "  " << carpetaTarjetas.string() << " (tarjetas por alumno)\n"
                      << "  " << rutaReporteJSON.string() << (jsonOk ? "" : "  [ERROR]") << "\n"
                      << "  " << rutaMomentosCSV.string() << (momentosOk ? "" : "  [ERROR]") << "\n"
                      << "  " << rutaReportePDF.string()  << (pdfOk ? "" : "  [ERROR]") << "\n";

            // 6. Muestra los resultados principales en una interfaz visual Qt:
            // - Primero el QMessageBox de resumen (informe final) para que
            // aparezca correctamente sin quedar en blanco.
            // - Luego el timeline en ventana OpenCV.
            // - Luego las tarjetas de desempeño en un diálogo Qt propio con
            // botones ← / → presionables (soluciona el problema de foco
            // de teclado en Windows con cv::waitKey).

            QMessageBox msgReporte(nullptr);
            msgReporte.setWindowTitle("Reporte Final de Atencion — AulaViva");
            msgReporte.setIcon(QMessageBox::Information);
            msgReporte.setText(
                QString(
                    "Atencion promedio de la clase: %1%%\n"
                    "Total de eventos de distraccion: %2\n"
                    "Duracion media de distraccion: %3 s\n\n"
                    "%4\n"
                    "Archivos generados en:\n%5")
                    .arg(reporte.porcentajeAtencionGlobal, 0, 'f', 1)
                    .arg(reporte.totalEventosDistraccion)
                    .arg(reporte.duracionMediaDistracS, 0, 'f', 2)
                    .arg(QString::fromStdString(reporte.conclusionGeneral))
                    .arg(QString::fromStdString(fs::absolute(carpeta).string())));
            msgReporte.addButton(QMessageBox::Ok);
            msgReporte.exec();

            // Timeline en ventana OpenCV
            const std::string VENTANA_TIMELINE = "Reporte Final - Timeline de Atencion";
            if (!timeline.empty()) {
                cv::namedWindow(VENTANA_TIMELINE, cv::WINDOW_NORMAL);
                cv::resizeWindow(VENTANA_TIMELINE,
                                 std::min(timeline.cols, 1200),
                                 std::min(timeline.rows, 700));
                cv::imshow(VENTANA_TIMELINE, timeline);
                cv::waitKey(1);   // fuerza refresco sin bloquear
            }

            // Tarjetas de desempeño: diálogo Qt con botones ← / →
            // Se convierte cada tarjeta cv::Mat a QPixmap para mostrarla en
            // un QLabel dentro de un QDialog con botones presionables.
            // Esto evita el problema de cv::waitKey que en Windows no recibe
            // el foco de teclado cuando hay otras ventanas Qt activas.
            if (!tarjetas.empty()) {
                QDialog dlgTarjeta(nullptr);
                dlgTarjeta.setWindowTitle("Reporte Final - Tarjeta de Desempeño");
                dlgTarjeta.setWindowFlags(
                    dlgTarjeta.windowFlags() | Qt::WindowMinMaxButtonsHint);

                // Layout principal vertical
                auto* layoutV = new QVBoxLayout(&dlgTarjeta);
                layoutV->setContentsMargins(8, 8, 8, 8);
                layoutV->setSpacing(6);

                // Label para la imagen de la tarjeta.
                auto* labelImg = new QLabel(&dlgTarjeta);
                labelImg->setAlignment(Qt::AlignCenter);
                labelImg->setMinimumSize(880, 590);
                layoutV->addWidget(labelImg);

                // Label con nombre del alumno actual
                auto* labelNombre = new QLabel(&dlgTarjeta);
                labelNombre->setAlignment(Qt::AlignCenter);
                QFont fNombre;
                fNombre.setPointSize(10);
                fNombre.setBold(true);
                labelNombre->setFont(fNombre);
                layoutV->addWidget(labelNombre);

                // Fila de botones ← / →  +  Cerrar
                auto* layoutH = new QHBoxLayout();
                auto* btnAnterior = new QPushButton("◀  Anterior", &dlgTarjeta);
                auto* btnSiguiente = new QPushButton("Siguiente  ▶", &dlgTarjeta);
                auto* btnCerrar   = new QPushButton("Cerrar", &dlgTarjeta);
                btnAnterior->setMinimumWidth(120);
                btnSiguiente->setMinimumWidth(120);
                btnCerrar->setMinimumWidth(80);
                layoutH->addWidget(btnAnterior);
                layoutH->addStretch();
                layoutH->addWidget(btnCerrar);
                layoutH->addStretch();
                layoutH->addWidget(btnSiguiente);
                layoutV->addLayout(layoutH);

                // Índice actual y lambda para actualizar la vista
                int idxTarjeta = 0;
                const int numTarjetas = static_cast<int>(tarjetas.size());

                auto mostrarTarjeta = [&](int idx) {
                    const cv::Mat& mat = tarjetas[static_cast<size_t>(idx)];
                    // Convierte BGR→RGB y luego a QImage→QPixmap
                    cv::Mat rgb;
                    cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
                    QImage qimg(rgb.data, rgb.cols, rgb.rows,
                                static_cast<int>(rgb.step),
                                QImage::Format_RGB888);
                    labelImg->setPixmap(
                        QPixmap::fromImage(qimg).scaled(
                            labelImg->size(),
                            Qt::KeepAspectRatio,
                            Qt::SmoothTransformation));
                    labelNombre->setText(
                        QString("%1 / %2  —  %3")
                            .arg(idx + 1)
                            .arg(numTarjetas)
                            .arg(QString::fromStdString(nombresAlumnos[static_cast<size_t>(idx)])));
                    dlgTarjeta.setWindowTitle(
                        QString("Tarjeta de Desempeño  [%1]")
                            .arg(QString::fromStdString(nombresAlumnos[static_cast<size_t>(idx)])));
                    // Actualiza habilitación de botones
                    btnAnterior->setEnabled(numTarjetas > 1);
                    btnSiguiente->setEnabled(numTarjetas > 1);
                };

                // Conexiones de botones
                QObject::connect(btnAnterior, &QPushButton::clicked, [&]() {
                    idxTarjeta = (idxTarjeta - 1 + numTarjetas) % numTarjetas;
                    mostrarTarjeta(idxTarjeta);
                });
                QObject::connect(btnSiguiente, &QPushButton::clicked, [&]() {
                    idxTarjeta = (idxTarjeta + 1) % numTarjetas;
                    mostrarTarjeta(idxTarjeta);
                });
                QObject::connect(btnCerrar, &QPushButton::clicked,
                                 &dlgTarjeta, &QDialog::accept);

                mostrarTarjeta(0);
                dlgTarjeta.resize(900, 700);   // Cabe en 1080p sin solapar la barra de tareas
                dlgTarjeta.exec();
            }

            // Cierra la ventana del timeline después de que el usuario cerró las tarjetas
            cv::destroyWindow(VENTANA_TIMELINE);
        }

        // Guardado CSV de coordenadas
        const fs::path rutaCSV = carpeta / "coordenadas_rostros.csv";
        {
            std::ofstream archivoCSV(rutaCSV.string());
            bool csvOk = false;
            if (archivoCSV.is_open())
            {
                archivoCSV << "frame,id_persona,rostro_detectado,"
                              "x,y,ancho,alto,confianza,deteccion_directa,id_puesto,nombre_estudiante\n";
                for (const auto& r : resultadosDeteccion)
                {
                    // Recupera el nombre del estudiante del puesto asignado
                    std::string nomEst;
                    if (r.idPuesto >= 0 && configSala.valida()) {
                        for (const auto& pu : configSala.puestos)
                            if (pu.id == r.idPuesto) { nomEst = pu.estudiante; break; }
                    }

                    archivoCSV << r.numeroFrame << ',' << r.idPersona << ','
                               << (r.rostroEncontrado ? 1 : 0) << ',';
                    if (r.rostroEncontrado)
                        archivoCSV << r.x << ',' << r.y << ','
                                   << r.ancho << ',' << r.alto << ','
                                   << std::fixed << std::setprecision(3)
                                   << r.confianza;
                    else
                        archivoCSV << ",,,,";
                    archivoCSV << ',' << (r.deteccionDirecta ? 1 : 0)
                               << ',' << r.idPuesto
                               << ',' << nomEst << '\n';
                }
                csvOk = archivoCSV.good();
            }

            // Reporte final
            QString resumen = QString(
                                  "\n\n--- RESUMEN COLECTIVO DE DETECCION ---\n"
                                  "Frames con algun alumno visible: %1\n"
                                  "Frames vacios (sin rostros): %2\n"
                                  "Frames con multiples alumnos simultaneos: %3\n"
                                  "Ruta del Registro CSV: %4")
                                  .arg(framesConRostro)
                                  .arg(framesSinRostro)
                                  .arg(framesConMultiplesCand)
                                  .arg(csvOk
                                           ? QString::fromStdString(fs::absolute(rutaCSV).string())
                                           : QString("ERROR al guardar el archivo CSV"));

            QString encabezado = cancelado
                                     ? "Proceso CANCELADO por el usuario.\n"
                                     : "Proceso completado con exito.\n";

            QMessageBox::information(nullptr, "Extraccion completada",
                                     QString("%1Se han extraido %2 imagenes en:\n%3%4")
                                         .arg(encabezado)
                                         .arg(framesGuardados)
                                         .arg(QString::fromStdString(fs::absolute(carpeta).string()))
                                         .arg(resumen));
        }

        if (!preguntarSiNo("Los frames extraidos son correctos?"))
        {
            eliminarFrames(carpeta);
            QMessageBox::information(nullptr, "Listo",
                                     "Los frames fueron eliminados del almacenamiento.");
        }
    }
    catch (const std::exception& e)
    {
        QMessageBox::critical(nullptr, "Error Critico",
                              QString("Fallo general: %1").arg(e.what()));
    }

    if (cap.isOpened()) cap.release();
}