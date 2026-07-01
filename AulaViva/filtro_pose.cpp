// filtro_pose.cpp
//
// Implementación del filtro de ventana para
// yaw, pitch y roll. Cada eje mantiene su propio deque de muestras;
// cuando la ventana se llena, el elemento más antiguo se descarta
// antes de añadir el nuevo (ventana deslizante estricta).

#include "filtro_pose.h"

FiltroPose::FiltroPose(int tamanoVentana)
    : tamanoVentana(tamanoVentana)
{
}

double FiltroPose::filtrarYaw(double valor)
{
    return aplicarPromedio(historialYaw, valor);
}

double FiltroPose::filtrarPitch(double valor)
{
    return aplicarPromedio(historialPitch, valor);
}

double FiltroPose::filtrarRoll(double valor)
{
    return aplicarPromedio(historialRoll, valor);
}

// vacía los tres historiales; se llama cuando se pierde el tracking
// de una persona y no queremos contaminar la siguiente detección
void FiltroPose::reiniciar()
{
    historialYaw.clear();
    historialPitch.clear();
    historialRoll.clear();
}

// añade valor al historial, lo acota a tamanoVentana elementos
// y devuelve la media aritmética del contenido actual
double FiltroPose::aplicarPromedio(std::deque<double>& historial, double valor)
{
    historial.push_back(valor);
    // descarta el elemento más antiguo si la ventana ya está llena
    if (historial.size() > static_cast<size_t>(tamanoVentana))
        historial.pop_front();
    double suma = 0.0;
    for (double dato : historial)
        suma += dato;
    return suma / historial.size();
}