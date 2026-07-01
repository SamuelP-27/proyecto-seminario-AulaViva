// T2.3.3 - marcar como desatención los momentos en que no se detecta el rostro

// Archivo: analizador_atencion.cpp

if (!pose.validar) {
    tipoOut = TipoDistraccion::SinRostro;
    return EstadoAtencion::SinDeteccion;
}

// estado correspondiente declarado en analizador_atencion.h:

enum class EstadoAtencion { Atento, Distraido, SinDeteccion };
enum class TipoDistraccion { Ninguna, MiradaLateral, MiradaAbajo, MiradaArriba,
                              CabeceoBrusco, SinRostro, Combinada };

