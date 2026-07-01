// T2.4.1 - detectar los cambios de estado entre atención y distracción

// Archivo: analizador_atencion.cpp

const EstadoAtencion estadoAnterior = ep->estadoActual;
ep->estadoActual = estadoConfirmado;

// detecta transiciones de estado para abrir/cerrar eventos de distracción
if (estadoConfirmado != EstadoAtencion::Atento && estadoAnterior == EstadoAtencion::Atento)
    abrirEvento(*ep, frame, tipoConfirmado);
else if (estadoConfirmado == EstadoAtencion::Atento && estadoAnterior != EstadoAtencion::Atento)
    cerrarEvento(*ep, frame);
else if (ep->enDistraccion && tipoConfirmado != TipoDistraccion::Ninguna)
    ep->eventoActivo.tipo = tipoConfirmado;
