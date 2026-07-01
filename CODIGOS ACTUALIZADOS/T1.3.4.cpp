// T1.3.4 - filtrar el ruido de las mediciones para una lectura estable

// Archivo: filtro_pose.h / filtro_pose.cpp

class FiltroPose
{
public:
    explicit FiltroPose(int tamanoVentana = 5);
    double filtrarYaw(double valor);
    double filtrarPitch(double valor);
    double filtrarRoll(double valor);
    void reiniciar();
private:
    int tamanoVentana;
    std::deque<double> historialYaw, historialPitch, historialRoll;
    double aplicarPromedio(std::deque<double>& historial, double valor);
};



double FiltroPose::aplicarPromedio(std::deque<double>& historial, double valor)
{
    historial.push_back(valor);
    if (historial.size() > static_cast<size_t>(tamanoVentana))
        historial.pop_front();
    double suma = 0.0;
    for (double dato : historial) suma += dato;
    return suma / historial.size();
}


// aplicación práctica (un filtro independiente por estudiante rastreado), en procesador_video.cpp:

if (pose.validar)
{
    pose.yaw   = p.filtro.filtrarYaw(pose.yaw);
    pose.pitch = p.filtro.filtrarPitch(pose.pitch);
    pose.roll  = p.filtro.filtrarRoll(pose.roll);
}