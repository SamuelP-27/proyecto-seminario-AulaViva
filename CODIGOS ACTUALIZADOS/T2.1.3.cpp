// T.2.1.3 - registrar la ubicación exacta de cada puesto de estudiante

// Archivo: sala_config.h + configurador_sala.cpp

struct PuestoEstudiante
{
    int         id          = 0;
    std::string nombre      = "";   // etiqueta de posición, ej. "Fila1-Col2"
    std::string estudiante  = "";   // nombre del alumno asignado
    cv::Rect    rect        = {};   // bounding box del asiento en coords del frame
};

// captura interactiva del rectángulo del puesto y los datos del alumno:

canvas_->onNuevoPuesto = [=](cv::Rect rc) {
    ...
    PuestoEstudiante p;
    p.id         = nextId;
    p.nombre     = edtPos->text().trimmed().toStdString();
    p.estudiante = edtAlum->text().trimmed().toStdString();
    p.rect       = rc;

    auto puestos = canvas_->puestos();
    puestos.push_back(p);
    canvas_->setPuestos(puestos);
    ...
};
