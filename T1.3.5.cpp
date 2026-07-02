// T1.3.5 - asociar los ángulos calculados a cada frame mediante su timestamp

// Archivo: procesador_video.cpp (CSV poses.csv)

archivoPose << "frame,id_persona,id_puesto,nombre_estudiante,"
               "pose_valida,yaw,pitch,roll,estado_atencion,tipo_distraccion\n";
...
archivoPose << frameActual << ',' << p.id << ','
            << idPuesto << ',' << nomEstudiante << ','
            << (pose.validar ? 1 : 0) << ','
            << pose.yaw << ',' << pose.pitch << ',' << pose.roll << ','
            << nombreEstado(resAten.estado) << ','
            << nombreTipoDistraccion(resAten.tipoDistrac) << "\n";

            
// la conversión de número de frame a tiempo real en segundos (timestamp) se hace en analizador_atencion.cpp:

ep.eventoActivo.frameInicio   = frame;
ep.eventoActivo.tiempoInicioS = frame / fps_;

// Actualización: el CSV poses.csv ahora incluye directamente una columna timestamp_p con el segundo exacto de cada fila 
// (antes solo tenía frame, y había que reconstruir el tiempo dividiendo por el fps por separado). 
// Esto hace más directa la trazabilidad frame→tiempo que ya pedía esta tarea:

archivoPose << "frame,timestamp_p,id_persona,id_puesto,nombre_estudiante,"
               "pose_valida,yaw,pitch,roll,estado_atencion,tipo_distraccion\n";
  

               
const double timestampS = (fps > 0.0) ? static_cast<double>(frameActual) / fps : 0.0;
archivoPose << frameActual << ','
            << std::fixed << std::setprecision(3) << timestampS << ','
            << p.id << ','
            << idPuesto << ','
            << nomEstudiante << ','
            << (pose.validar ? 1 : 0) << ','
            << pose.yaw << ',' << pose.pitch << ',' << pose.roll << ','
            << nombreEstado(resAten.estado) << ','
            << nombreTipoDistraccion(resAten.tipoDistrac) << "\n";