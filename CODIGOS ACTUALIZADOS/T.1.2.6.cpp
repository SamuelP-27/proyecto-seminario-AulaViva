// T.1.2.6 - guardar las coordenadas y dimensiones del cuadro delimitador

// Archivo: procesador_video.cpp

struct LogDeteccionMulti {
    int    numeroFrame;
    int    idPersona;
    bool   rostroEncontrado;
    int    x, y, ancho, alto;
    double confianza;
    bool   deteccionDirecta;
    int    idPuesto = -1;
};

// exportación a CSV (coordenadas_rostros.csv):

archivoCSV << "frame,id_persona,rostro_detectado,"
              "x,y,ancho,alto,confianza,deteccion_directa,id_puesto,nombre_estudiante\n";
for (const auto& r : resultadosDeteccion)
{
    archivoCSV << r.numeroFrame << ',' << r.idPersona << ','
               << (r.rostroEncontrado ? 1 : 0) << ',';
    if (r.rostroEncontrado)
        archivoCSV << r.x << ',' << r.y << ','
                   << r.ancho << ',' << r.alto << ','
                   << std::fixed << std::setprecision(3)
                   << r.confianza;
    ...
}