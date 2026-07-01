// T2.4.4 - clasificar el tipo de distracción según la dirección del rostro

// Archivo: analizador_atencion.cpp / analizador_atencion.h

const int nFuera = (!gazeOk ? 1 : 0)
                   + (pitchAbajo || pitchArriba ? 1 : 0)
                   + (rollFuera  ? 1 : 0);

if (nFuera == 0) {
    tipoOut = TipoDistraccion::Ninguna;
    return EstadoAtencion::Atento;
}
if (nFuera >= 2) {
    tipoOut = TipoDistraccion::Combinada;
} else if (!gazeOk) {
    tipoOut = TipoDistraccion::MiradaLateral;
} else if (pitchAbajo) {
    tipoOut = TipoDistraccion::MiradaAbajo;
} else if (pitchArriba) {
    tipoOut = TipoDistraccion::MiradaArriba;
} else {
    tipoOut = TipoDistraccion::CabeceoBrusco;
}
return EstadoAtencion::Distraido;

// etiquetas legibles (analizador_atencion.h):

inline std::string nombreTipoDistraccion(TipoDistraccion t)
{
    switch (t) {
    case TipoDistraccion::Ninguna:       return "Atento";
    case TipoDistraccion::MiradaLateral: return "Mirada lateral";
    case TipoDistraccion::MiradaAbajo:   return "Mirada abajo";
    case TipoDistraccion::MiradaArriba:  return "Mirada arriba";
    case TipoDistraccion::CabeceoBrusco: return "Cabeceo";
    case TipoDistraccion::SinRostro:     return "Sin rostro";
    case TipoDistraccion::Combinada:     return "Combinada";
    default:                             return "Desconocida";
    }
}