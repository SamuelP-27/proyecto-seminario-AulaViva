// T.1.1.5 - implementar control de errores durante la lectura del video

// Archivo: procesador_video.cpp

try
{
    bool ret = cap.read(frame);
    if (!ret || frame.empty()) break;
    ...
}
catch (const std::exception& ex)
{
    std::cerr << "\nFallo mitigado en frame "
              << frameActual << ": " << ex.what() << "\n";
}

++frameActual;

// El bucle completo y la apertura del video además están envueltos en un try { ... } catch (const std::exception& e) 
// general a nivel de procesarVideo(), de modo que un fallo de E/S (cuadro dañado, fallo del decodificador, etc.) no cierra la aplicación, 
// sino que se reporta y el procesamiento continúa con el frame siguiente.