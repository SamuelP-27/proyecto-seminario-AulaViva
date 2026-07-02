QT += widgets printsupport

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = AulaViva

SOURCES += \
    main.cpp \
    procesador_video.cpp \
    estimador.cpp \
    filtro_pose.cpp \
    sala_config.cpp \
    configurador_sala.cpp \
    analizador_atencion.cpp \
    reporte_atencion.cpp

HEADERS += \
    procesador_video.h \
    estimador.h \
    filtro_pose.h \
    sala_config.h \
    configurador_sala.h \
    analizador_atencion.h \
    reporte_atencion.h

# CONFIGURACIÓN DE OPENCV 5.0 (build oficial MSVC)
#
# Requisitos previos:
#   1. Descarga opencv-5.0.0-windows.exe desde:
#      https://github.com/opencv/opencv/releases/download/5.0.0/opencv-5.0.0-windows.exe
#   2. Ejecútalo y extrae en la carpeta parecida a la de abajo (o donde quieras)
#   3. En Qt Creator → Projects → Build → Kit: usa "MSVC2022 64-bit" (NO MinGW)

OPENCV_DIR = C:/Samuel/Programas/OpenCV-5.0.0/build # Recuerda cambiar esto en base a tu directorio de instalación de OPENCV.

INCLUDEPATH += $$OPENCV_DIR/include

CONFIG(debug, debug|release) {
    LIBS += -L$$OPENCV_DIR/x64/vc16/lib \
            -lopencv_world500d
} else {
    LIBS += -L$$OPENCV_DIR/x64/vc16/lib \
            -lopencv_world500
}
