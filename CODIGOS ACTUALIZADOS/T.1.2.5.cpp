// T.1.2.5 - gestionar la presencia de múltiples rostros, enfocando en el alumno correspondiente

// Archivo: procesador_video.cpp + sala_config.cpp

// El sistema evolucionó de "un solo alumno objetivo" a un tracking multi-rostro que asocia cada cara detectada a su puesto real, 
// descartando las que no corresponden a ningún asiento configurado:

struct PersonaRastreada {
    int            id;
    cv::Rect       rect;
    double         confianza;
    LandmarksYuNet landmarks;
    FiltroPose     filtro;
    int            framesSinDeteccion = 0;
    bool           deteccionDirecta   = true;
    DatosPose      ultimaPose;
};


// Asocia el rostro a un puesto de la sala
const int idPuesto = configSala.valida()
                         ? asociarRostroAPuesto(p.rect, configSala)
                         : -1;


                    
// Archivo: sala_config.cpp, la función que descarta rostros que no pertenecen a ningún puesto registrado 
// (filtra ruido del entorno y enfoca el análisis en los estudiantes reales):

int asociarRostroAPuesto(const cv::Rect& rostro, const ConfigSala& config)
{
    const cv::Point2f centroRostro(
        rostro.x + rostro.width  / 2.f,
        rostro.y + rostro.height / 2.f);

    int    mejorId   = -1;
    double mejorDist = std::numeric_limits<double>::max();

    for (const auto& p : config.puestos) {
        const cv::Point2f centroPuesto(
            p.rect.x + p.rect.width  / 2.f,
            p.rect.y + p.rect.height / 2.f);
        const double dist    = cv::norm(centroRostro - centroPuesto);
        const double umbral  = p.rect.width * 1.5;   // tolerancia generosa (mucho)
        if (dist < mejorDist && dist < umbral) {
            mejorDist = dist;
            mejorId   = p.id;
        }
    }
    return mejorId;   // -1 si ningún puesto califica
}

