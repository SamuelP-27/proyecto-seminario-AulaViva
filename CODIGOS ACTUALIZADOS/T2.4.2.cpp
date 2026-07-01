// T2.4.2 - registrar el tiempo de inicio y fin de cada periodo de distracción

// Archivo: analizador_atencion.cpp

void EvaluadorAtencion::abrirEvento(EstadoPuesto& ep, int frame, TipoDistraccion tipo)
{
    if (ep.enDistraccion) return;
    ep.enDistraccion                = true;
    ep.eventoActivo.frameInicio     = frame;
    ep.eventoActivo.tiempoInicioS   = frame / fps_;
    ep.eventoActivo.tipo            = tipo;
    ep.eventoActivo.idPuesto        = ep.idPuesto;
    ep.eventoActivo.nombreAlumno    = ep.nombreAlumno;
}

void EvaluadorAtencion::cerrarEvento(EstadoPuesto& ep, int frame)
{
    if (!ep.enDistraccion) return;
    ep.eventoActivo.frameFin   = frame;
    ep.eventoActivo.tiempoFinS = frame / fps_;
    eventos_.push_back(ep.eventoActivo);
    ep.enDistraccion = false;
}