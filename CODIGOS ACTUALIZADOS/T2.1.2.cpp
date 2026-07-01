// T.2.1.2 - definir la posición y dimensiones de la pizarra

// Archivo: sala_config.h

// la pizarra se modela como un rectángulo centrado, opcionalmente rotado.
struct ConfigPizarra
{
    cv::Point2f centro = {};
    float       ancho  = 0.f;
    float       alto   = 0.f;
    float       angulo = 0.f;   // rotación en grados, sentido horario

    std::vector<cv::Point2f> vertices() const;
    bool contiene(cv::Point2f p) const;
};

// cálculo de los 4 vértices reales (sala_config.cpp), necesario porque la pizarra puede estar en perspectiva/rotada:

std::vector<cv::Point2f> ConfigPizarra::vertices() const
{
    const float rad  = angulo * static_cast<float>(M_PI) / 180.f;
    const float cosA = std::cos(rad);
    const float sinA = std::sin(rad);
    const float hw   = ancho / 2.f;
    const float hh   = alto  / 2.f;

    const cv::Point2f locales[4] = { {-hw,-hh}, {hw,-hh}, {hw,hh}, {-hw,hh} };
    std::vector<cv::Point2f> result;
    for (const auto& p : locales)
        result.push_back({ centro.x + p.x * cosA - p.y * sinA,
                          centro.y + p.x * sinA + p.y * cosA });
    return result;
}

// en el asistente visual, el docente dibuja este rectángulo arrastrando el mouse y lo rota con la rueda (configurador_sala.cpp):

void wheelEvent(QWheelEvent* e) override
{
    if (modo_ == MODO_PIZARRA && tienePizarra_) {
        const float delta = (e->angleDelta().y() > 0) ? 1.f : -1.f;
        pizarra_.angulo   = std::fmod(pizarra_.angulo + delta + 360.f, 360.f);
        emit pizarraActualizada();
        update();
    }
}