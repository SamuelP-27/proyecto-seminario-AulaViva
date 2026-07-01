// main.cpp
//
// Punto de entrada de AulaViva.
//
// Flujo general del programa:
// 1. El usuario elige el video de la clase mediante un diálogo de archivo.
// 2. Se busca (o se crea) el archivo de configuración de sala asociado a
// ese video (pizarra + puestos de los alumnos).
// 3. Si no existe configuración previa, o el usuario decide reconfigurar,
// se abre el asistente interactivo de configuración de sala.
// 4. Con la sala ya configurada, se procesa el video completo: detección
// de rostros, asociación a puestos y generación de reportes.
//
// Este archivo solo orquesta el flujo, la lógica de cada paso
// vive en sus respectivos módulos (procesador_video, configurador_sala,
// sala_config).

#include <iostream>
#include <filesystem>
#ifdef _WIN32
#  include <cstdlib>   // _putenv_s (MSVC/Windows)
#endif
#include <QApplication>
#include <QCoreApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QString>
#include "procesador_video.h"
#include "configurador_sala.h"
#include "sala_config.h"

namespace fs = std::filesystem; // alias corto para no repetir std::filesystem en todo el archivo (SIEMPRE necesario)

int main(int argc, char* argv[])
{
    // Activa el nuevo motor DNN de OpenCV 5.x
    // Debe hacerse ANTES de cualquier uso de cv:: para que la variable de entorno
    // sea visible cuando OpenCV inicialice su runtime DNN.
    // Sin esto, shapes dinámicos del modelo YuNet 2026 fallan igual que en 4.x.
    // Referencia: https://github.com/opencv/opencv/wiki/OpenCV-4-to-5-migration
#ifdef _WIN32
    _putenv_s("OPENCV_FORCE_DNN_ENGINE", "4");
#else
    setenv("OPENCV_FORCE_DNN_ENGINE", "4", 1);
#endif
    // Inicializa el entorno gráfico de Qt, obligatorio antes de usar cualquier
    // widget (QFileDialog, QMessageBox, etc.)
    QApplication app(argc, argv);
    std::cout << "=== AulaViva — Sistema de análisis de atención en clase ===\n";

    // 1. Selección del video de clase
    const QString rutaVideo = QFileDialog::getOpenFileName(
        nullptr,
        "Selecciona el video de la clase",
        QString(),
        "Videos (*.mp4 *.avi *.mkv *.mov);;Todos los archivos (*.*)");
    if (rutaVideo.isEmpty()) {
        // El usuario cerró el diálogo sin elegir nada: terminamos sin error.
        std::cout << "Operación cancelada: no se seleccionó ningún archivo.\n";
        return 0;
    }

    // 2. Ruta del archivo de configuración de sala
    // Se guarda en el directorio del ejecutable (proyecto) con el nombre base
    // del video + _sala_config.json, para mantener todo el proyecto junto.
    const fs::path rutaVideoFs(rutaVideo.toStdString());
    const fs::path dirEjecutable(QCoreApplication::applicationDirPath().toStdString());
    const QString  rutaConfig = QString::fromStdString(
        (dirEjecutable / (rutaVideoFs.stem().string() + "_sala_config.json"))
            .string());

    // 3. Configuración de sala: carga o asistente interactivo
    // ConfigSala vacía que se irá llenando ya sea desde el JSON existente
    // o desde el asistente interactivo más abajo.
    ConfigSala configSala;
    bool       configCargada = false;

    if (fs::exists(rutaConfig.toStdString())) {
        // Ya existe un JSON de configuración para este video: intentamos leerlo
        if (cargarConfigSala(rutaConfig.toStdString(), configSala)) {
            configCargada = true;
            std::cout << "Configuración de sala cargada desde:\n  "
                      << rutaConfig.toStdString() << "\n";

            // Le preguntamos al docente si quiere reutilizar la sala guardada
            // o prefiere rehacer el dibujo (por ejemplo si cambió el salón, o los puestos).
            const auto respuesta = QMessageBox::question(
                nullptr,
                "Configuración de sala",
                QString("Se encontró una configuración existente para este video:\n%1\n\n"
                        "¿Deseas usarla, o reconfigurar la sala?")
                    .arg(rutaConfig),
                "Usar existente", "Reconfigurar", QString(), 0, 1);

            if (respuesta == 1)   // "Reconfigurar"
                configCargada = false;
        } else {
            // El archivo existe pero está corrupto o con formato inválido
            QMessageBox::warning(nullptr, "Configuración inválida",
                                 "El archivo de configuración de sala existe pero no es válido.\n"
                                 "Se abrirá el asistente de configuración.");
        }
    }

    if (!configCargada) {
        // No había configuración previa válida, o el docente pidió reconfigurar:
        // mostramos instrucciones y abrimos el asistente visual (Qt) para que
        // dibuje la pizarra y los puestos sobre el primer frame del video.
        QMessageBox::information(
            nullptr,
            "Configuración de sala",
            "Se abrirá el asistente de configuración de sala.\n\n"
            "  1. Arrastra un rectángulo sobre la pizarra\n"
            "     (rueda del mouse para rotar)\n"
            "  2. Dibuja un rectángulo por cada puesto de estudiante\n"
            "  3. Pulsa «Guardar configuración»\n\n"
            "Esta configuración se reutilizará automáticamente la próxima\n"
            "vez que proceses el mismo video.");

        if (!lanzarConfiguradorSala(rutaVideo, rutaConfig)) {
            // El usuario cerró el asistente sin guardar, o falló el proceso:
            // sin sala configurada no tiene sentido continuar.
            QMessageBox::critical(nullptr, "Error",
                                  "No se completó la configuración de sala.\n"
                                  "El análisis no puede continuar.");
            return 1;
        }

        // Recarga la configuración recién guardada por el asistente
        if (!cargarConfigSala(rutaConfig.toStdString(), configSala)) {
            QMessageBox::critical(nullptr, "Error interno",
                                  "No se pudo leer la configuración que se acaba de guardar.");
            return 1;
        }
        configCargada = true;
        std::cout << "Configuración de sala guardada en:\n  "
                  << rutaConfig.toStdString() << "\n";
    }

    // Verificación final antes de procesar: aunque la carga haya "funcionado",
    // nos asegura que la sala tenga al menos pizarra y puestos coherentes.
    if (!configSala.valida()) {
        QMessageBox::critical(nullptr, "Error de configuración",
                              "La configuración de sala cargada no es válida.\n"
                              "Elimina el archivo JSON y vuelve a configurar.");
        return 1;
    }

    std::cout << "Puestos configurados: " << configSala.puestos.size() << "\n";

    // 4. Procesamiento del video: aquí ocurre todo el trabajo pesado
    // (detección de rostros, asociación a puestos, generación de reportes).
    // Intervalo 5 = analiza 1 de cada 5 frames (5× más rápido, igual de preciso
    // para video de clase donde el movimiento es lento).
    procesarVideo(rutaVideo.toStdString(),
                  "frames_extraidos",
                  /*guardarVisualizacion=*/ true,
                  /*intervaloDeteccion=*/   5,
                  /*calidadJPEG=*/          90,
                  configSala);

    return 0;
}