// T.1.2.4 - manejar la ausencia de rostro de manera inteligente

// Archivo: procesador_video.cpp

constexpr int MAX_FRAMES_SIN_DETECCION = 30; // frames consecutivos sin detectar antes de descartar

// Tracking: si no se encuentra candidato cercano, se marca como interpolado
if (mejorIdx != -1)
{
    ...
    p.deteccionDirecta = true;
}
else
{
    p.framesSinDeteccion++;
    p.deteccionDirecta = false;   // mantiene la última posición conocida (continuidad)
}

// Purga personas perdidas tras demasiados frames sin detección
personasActivas.erase(
    std::remove_if(personasActivas.begin(), personasActivas.end(),
                   [](const PersonaRastreada& p) {
                       return p.framesSinDeteccion > MAX_FRAMES_SIN_DETECCION;
                   }),
    personasActivas.end());

// a nivel de clasificación de atención, la ausencia total de rostro también se gestiona explícitamente (ver T2.3.3) marcando el estado como SinDeteccion en vez de fallar.