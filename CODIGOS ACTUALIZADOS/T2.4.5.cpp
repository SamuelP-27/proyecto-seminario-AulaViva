// T2.4.5 - almacenar todos los eventos detectados en una lista

// Archivo: analizador_atencion.h / .cpp

std::vector<EventoDistraccion> eventos_;   // episodios cerrados (atributo de EvaluadorAtencion)

const std::vector<EventoDistraccion>& eventosDistraccion() const { return eventos_; }

// cerrarEvento() (ver T2.4.2) es el punto donde cada evento completo se agrega a esta lista:

eventos_.push_back(ep.eventoActivo);