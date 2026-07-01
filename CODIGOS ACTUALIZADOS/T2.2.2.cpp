// T2.2.2 - definir los rangos mínimos y máximos de yaw, pitch y roll para "mirar al frente"

// Archivo: analizador_atencion.h

struct RangosAtencion
{
    double yawMin = -110.0;
    double yawMax =  110.0;

    double pitchMin = -25.0;
    double pitchMax =  35.0;

    double rollTolerancia = 22.0;
    ...
};

// estos rangos se recalculan por puesto en analizador_atencion.cpp mediante calcularRangosParaPuesto(), que combina la geometría
// de la sala con los valores fijos calibrados.