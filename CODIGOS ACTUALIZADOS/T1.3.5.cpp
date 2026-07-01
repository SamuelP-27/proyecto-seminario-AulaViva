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