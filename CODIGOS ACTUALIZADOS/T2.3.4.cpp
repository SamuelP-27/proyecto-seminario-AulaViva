// T.2.3.4 - implementar una ventana temporal para evitar falsos positivos por movimientos rápidos

Archivo: analizador_atencion.cpp (función actualizarVentana)

EstadoAtencion EvaluadorAtencion::actualizarVentana(
    EstadoPuesto& ep, EstadoAtencion estadoBruto, TipoDistraccion& tipoInOut) const
{
    EstadoAtencion saliente = ep.bufferEstados[ep.indiceBuffer];
    ep.bufferEstados[ep.indiceBuffer] = estadoBruto;
    ep.indiceBuffer = (ep.indiceBuffer + 1) % ventana_;

    if (saliente   != EstadoAtencion::Atento) ep.countFueraRango--;
    if (estadoBruto != EstadoAtencion::Atento) ep.countFueraRango++;

    // Solo se confirma "Distraido" si TODA la ventana está fuera de rango
    if (ep.countFueraRango >= ventana_)
        return estadoBruto;

    tipoInOut = TipoDistraccion::Ninguna;
    return EstadoAtencion::Atento;
}

// tamaño de la ventana configurado según el intervalo de muestreo del video (procesador_video.cpp):

const int ventanaFrames = std::max(3, 6 / std::max(1, intervaloDeteccion));
EvaluadorAtencion evaluador(configSala, ventanaFrames, fps > 0.0 ? fps : 25.0);
