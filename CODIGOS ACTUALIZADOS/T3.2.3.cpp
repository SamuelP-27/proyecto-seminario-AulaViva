// T3.2.3 - mostrar los resultados principales por una interfaz

// Archivo: reporte_atencion.cpp (consola) + procesador_video.cpp (Qt)

void imprimirResumenConsola(const ReporteClase& rep)
{
    std::cout << "  REPORTE FINAL DE ATENCIÓN — AulaViva\n";
    std::cout << "  Atención promedio de la clase : "
              << rep.porcentajeAtencionGlobal << " %\n";
    ...
}


// interfaz gráfica (Qt) con el resumen y las tarjetas navegables por alumno, en procesador_video.cpp:

QMessageBox msgReporte(nullptr);
msgReporte.setWindowTitle("Reporte Final de Atencion — AulaViva");
msgReporte.setText(
    QString("Atencion promedio de la clase: %1%%\n"
            "Total de eventos de distraccion: %2\n...")
        .arg(reporte.porcentajeAtencionGlobal, 0, 'f', 1)
        .arg(reporte.totalEventosDistraccion));
msgReporte.exec();


// Diálogo con botones ◀ Anterior / Siguiente ▶ para navegar las tarjetas de cada alumno
QDialog dlgTarjeta(nullptr);
dlgTarjeta.setWindowTitle("Reporte Final - Tarjeta de Desempeño");
...
QObject::connect(btnSiguiente, &QPushButton::clicked, [&]() {
    idxTarjeta = (idxTarjeta + 1) % numTarjetas;
    mostrarTarjeta(idxTarjeta);
});
