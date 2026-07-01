#ifndef CONFIGURADOR_SALA_H
#define CONFIGURADOR_SALA_H

// configurador_sala.h
//
// Declara la función pública que abre el asistente gráfico (Qt) para que
// el docente defina, dibujando sobre el primer frame del video, dónde está
// la pizarra y dónde se sienta cada alumno. El resultado se guarda como
// JSON mediante sala_config.h/.cpp.

#include <QString>

// Lanza la ventana Qt interactiva para configurar la sala.
// Parámetros:
// rutaVideo: ruta al video de clase (se extrae el primer frame como
// fondo de referencia)
// rutaConfigSala: donde se leerá/guardará sala_config.json

bool lanzarConfiguradorSala(const QString& rutaVideo,
                            const QString& rutaConfigSala);

#endif // CONFIGURADOR_SALA_H
