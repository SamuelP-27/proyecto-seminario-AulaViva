// ANEXO - funcionalidades adicionales implementadas fuera del alcance original

// A diferencia de las secciones anteriores, los siguientes fragmentos de código no responden a ninguna tarea específica del EDT/carta Gantt. 
// Surgieron durante la implementación como necesidades técnicas propias del desarrollo 
// (verificar en vivo que la detección, la pose y la clasificación de atención, sin tener que esperar a que termine el procesamiento completo del video), 
// sobre todo durante la etapa de calibración de la sala y de los rangos de atención (RangosAtencion), donde un error de geometría es mucho más fácil de detectar viendo 
// el video anotado en tiempo real que revisando un CSV al final (Además del propio hecho de que fueron seguridas por el profesor en el hito anterior).

// Se documentan aquí como un bloque aparte, en vez de forzarlas dentro de una tarea existente, para no distorsionar la trazabilidad tarea → código de las 
// secciones anteriores. Cada punto indica con qué tarea(s) del EDT se relaciona más de cerca (porque reutiliza sus estructuras o su información), dejando 
// explícito que no es esa tarea, sino una funcionalidad nueva de depuración/verificación en tiempo real.


// A.1 - Ventana de visualización en vivo del video anotado (multi-rostro, landmarks y vector de gaze)

// Archivo: procesador_video.cpp (struct InfoAtencionVis, función anotarFrameConAtencion, bucle principal de procesarVideo)

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


// Color del bbox según gaze (respuesta inmediata, sin suavizado)
// - gazeEnPiz=true  + pose válida  → verde   (ATENTO)
// - gazeEnPiz=false + pose válida  → rojo    (DISTRAÍDO)
// - sin pose válida / sin asignar  → gris
cv::Scalar colorBbox;
if (!info || info->idPuesto < 0 || !p.ultimaPose.validar)
    colorBbox = cv::Scalar(120, 120, 120);
else if (displayAtento)
    colorBbox = cv::Scalar(0, 220, 60);
else
    colorBbox = cv::Scalar(30, 30, 220);

cv::rectangle(anotado, r, colorBbox, 2, cv::LINE_AA);

// Flecha de gaze (magenta), proyectada desde el centro interocular
if (p.ultimaPose.gazeValido && p.deteccionDirecta) {
    const cv::Point2f M  = (p.landmarks.ojoIzq + p.landmarks.ojoDer) * 0.5f;
    const cv::Point2f Pg = estimador::proyectarGaze(M, p.ultimaPose.gazeDir, 120.f);
    cv::arrowedLine(anotado,
                    cv::Point(static_cast<int>(M.x),  static_cast<int>(M.y)),
                    cv::Point(static_cast<int>(Pg.x), static_cast<int>(Pg.y)),
                    cv::Scalar(255, 0, 220), 2, cv::LINE_AA, 0, 0.35);
}


// ventana OpenCV que muestra este frame anotado en vivo mientras se procesa el video, con posibilidad de cancelar con la tecla Esc:

const std::string VENTANA_VIDEO =
    "Clase en vivo (Vista Panoramica Multi-Rostro) [Esc = Salir]";
cv::namedWindow(VENTANA_VIDEO, cv::WINDOW_NORMAL);
cv::resizeWindow(VENTANA_VIDEO, std::min(ancho, 960), std::min(alto, 540));
...
cv::imshow(VENTANA_VIDEO, frameAnotado);
...
if (cv::waitKey(1) == 27) cancelado = true;

// Por qué se agregó (una justificación fuera de que fue una sugerencia del profesor): el pipeline completo (detección → pose → clasificación → reporte) 
// sólo entrega resultados verificables al terminar todo el video, lo que en clases 
// largas puede tardar varios minutos. Esta ventana permite comprobar, frame a frame, que el bounding box, los landmarks y el vector de gaze estimado son correctos 
// mientras el video se está procesando, algo especialmente útil al calibrar una sala nueva (pizarra mal definida, rangos de yaw/pitch mal calibrados), porque el 
// error se ve inmediatamente como un bbox del color equivocado o una flecha de gaze apuntando fuera de la pizarra, en vez de descubrirse recién al revisar el CSV o 
// el reporte final. El atajo Esc además permite abortar el procesamiento a mitad de camino si se detecta que algo está mal configurado, sin esperar a que termine.

// Relación con el EDT: reutiliza estructuras y funciones de T1.2.2 (cuadro delimitador del rostro), T1.3.1 (landmarks) y T2.3.2 (test de gaze contra la pizarra), 
// y complementa a T3.2.3 (mostrar los resultados por una interfaz). No obstante, constituye una funcionalidad nueva de depuración/verificación en tiempo real durante
// el análisis, no contemplada originalmente en el EDT (que sólo preveía mostrar resultados una vez terminado el procesamiento).



// A.2 - Panel en vivo de atención general de la sala (dashboard de barras apiladas por frame)

// Archivo: procesador_video.cpp (clase VisualizadorAtencionSala)

// VisualizadorAtencionSala - panel de atención en tiempo real
//
// Mantiene el historial de los últimos HISTORIAL_ATEN_MAX frames y construye
// el panel visual.
class VisualizadorAtencionSala
{
public:
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

        acumAtentos_ += atentos;
        acumTotal_   += totalPuestos;
    }

    cv::Mat construirPanel(int atentos, int distraidos, int sinDet,
                           int totalPuestos) const
    {
        // Barras apiladas frame a frame: atentos (verde) | distraídos (rojo) | sin det (gris)
        // + curva de % de atención (línea amarilla) sobre el historial reciente
        // + línea "Ahora: XX% (n atentos, n distraidos, n sin det)"
        // + línea "Acumulado: XX% atencion | n puestos configurados"
        ...
    }
    ...
};


// uso dentro del bucle principal, junto a la ventana de video de A.1:

visSala.registrar(cntAtento, cntDist, cntSinDet, totalPuestos);
cv::imshow(VENTANA_POSE,
           visSala.construirPanel(cntAtento, cntDist, cntSinDet, totalPuestos));

// Por qué se agregó: el timeline final (T3.2.1) y las tarjetas de desempeño (T3.2.2) sólo están disponibles una vez terminado todo el análisis del video. 
// Para clases largas, eso implica esperar minutos sin ninguna señal de si la clasificación de atención está funcionando de forma razonable. 
// Este panel entrega, en tiempo real, un resumen agregado de la sala completa (conteo de atentos/distraídos/sin detección, porcentaje instantáneo y acumulado, 
// e histórico de las últimas muestras) que permite confirmar que la clasificación global tiene sentido mientras el video se sigue procesando,
// la misma lógica de verificación temprana que motivó A.1, pero a nivel de sala en vez de a nivel de un rostro individual.

// Relación con el EDT: se apoya en los mismos conceptos de T3.1.1–T3.1.2 (tiempo/porcentaje de atención) y anticipa, de forma agregada y en vivo, 
// lo que T3.2.1 (timeline visual) entrega recién al final. No estaba contemplada en el EDT como entregable independiente, 
// es un dashboard de monitoreo en tiempo real.





// Nota - otras variaciones menores de la misma naturaleza

// Ligadas a A.1 y A.2, y sin ameritar una sección propia, se agregaron además:

// Un rótulo F:<frame> A:<activos> D:<distraidos> ?:<sin_deteccion> superpuesto en la esquina superior derecha del video en vivo (procesador_video.cpp), 
// como resumen numérico rápido del frame actual sin tener que leer el panel completo de A.2.

// Una vista previa del timeline final (generarTimelineVisual, T3.2.1) en una ventana OpenCV (VENTANA_TIMELINE) antes de mostrar el diálogo Qt de tarjetas navegables, 
// para confirmar visualmente que el reporte se generó correctamente antes de cerrar el flujo.


// Ambas son detalles de UX de depuración, no tareas nuevas en sí mismas, y comparten la misma justificación de fondo que A.1 y A.2: 
// dar retroalimentación visual inmediata en cada etapa, en vez de esperar hasta el final del procesamiento.