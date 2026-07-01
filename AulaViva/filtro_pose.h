#ifndef FILTRO_POSE_H
#define FILTRO_POSE_H

// filtro_pose.h
//
// Filtro de ventana para suavizar los valores
// de yaw, pitch y roll que produce el estimador frame a frame.
// Cada ángulo tiene su propio historial independiente, por lo que el
// filtro puede aplicarse de forma selectiva. Se crea una instancia por
// persona rastreada (PersonaRastreada) para mantener el estado separado.

#include <deque>

class FiltroPose
{
public:
    // tamanoVentana: número de muestras que se promedian.
    // un valor de 5 equivale a ~0.2 s a 25 fps
    explicit FiltroPose(int tamanoVentana = 5);
    // añade un nuevo valor de yaw y devuelve la media actual de la ventana
    double filtrarYaw(double valor);
    // añade un nuevo valor de pitch y devuelve la media actual de la ventana
    double filtrarPitch(double valor);
    // añade un nuevo valor de roll y devuelve la media actual de la ventana
    double filtrarRoll(double valor);
    // vacía los tres historiales (se llama al perder el tracking de una persona)
    void reiniciar();

private:
    int tamanoVentana;
    std::deque<double> historialYaw;
    std::deque<double> historialPitch;
    std::deque<double> historialRoll;
    // añade valor al historial, descarta el más antiguo si ya está lleno
    // y devuelve la media de los elementos actuales
    double aplicarPromedio(std::deque<double>& historial, double valor);
};

#endif // FILTRO_POSE_H
