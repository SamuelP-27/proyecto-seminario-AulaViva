// T3.1.2 - calcular el porcentaje general de atención durante la clase

// Archivo: analizador_atencion.cpp

const int framesConDet = m.framesAtento + m.framesDistraido;
m.porcentajeAtencion = (framesConDet > 0)
                           ? (100.0 * m.framesAtento / framesConDet) : 0.0;

// agregado a nivel de toda la clase, en reporte_atencion.cpp (calcularReporte):

double sumaPct = 0.0;
int    cntPuesto = 0;
for (const auto& m : rep.metricasPorAlumno) {
    sumaPct += m.porcentajeAtencion;
    ++cntPuesto;
}
rep.porcentajeAtencionGlobal = (cntPuesto > 0) ? sumaPct / cntPuesto : 0.0;