// sala_config.cpp
//
// Implementación de las estructuras y funciones de sala_config.h:
// - geometría de la pizarra (rectángulo que puede estar rotado)
// - validación de la configuración de sala
// - lectura/escritura de la configuración en un JSON
// - asociación de un rostro detectado con el puesto más cercano
// - dibujo de la pizarra y los puestos sobre un frame, para depuración
// visual y para los videos anotados

#include "sala_config.h"
#include <fstream>
#include <sstream>
#include <cmath>
#include <iostream>
#include <limits>
#include <opencv2/imgproc.hpp>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ConfigPizarra

// Calcula los 4 vértices del rectángulo de la pizarra ya rotado y trasladado
// a coordenadas absolutas del frame. Se usa tanto para dibujar la pizarra
// como, en otros módulos, para saber exactamente qué zona del frame ocupa.
std::vector<cv::Point2f> ConfigPizarra::vertices() const
{
    const float rad  = angulo * static_cast<float>(M_PI) / 180.f;
    const float cosA = std::cos(rad);
    const float sinA = std::sin(rad);
    const float hw   = ancho / 2.f;
    const float hh   = alto  / 2.f;

    // Esquinas locales (sin rotar): TL, TR, BR, BL
    const cv::Point2f locales[4] = {
        {-hw, -hh}, { hw, -hh}, { hw, hh}, {-hw, hh}
    };

    // Rota cada esquina local alrededor del centro y la traslada al origen
    // real de la pizarra dentro del frame.
    std::vector<cv::Point2f> result;
    result.reserve(4);
    for (const auto& p : locales)
        result.push_back({ centro.x + p.x * cosA - p.y * sinA,
                          centro.y + p.x * sinA + p.y * cosA });
    return result;
}

// Indica si un punto (en coordenadas del frame) cae dentro de la pizarra,
// teniendo en cuenta su rotación. Para esto se hace la transformación
// inversa: se rota el punto al sistema de referencia local de la pizarra
// (donde esta queda alineada a los ejes) y ahí basta comparar con un
// rectángulo simple.
bool ConfigPizarra::contiene(cv::Point2f p) const
{
    // Transforma el punto al espacio local de la pizarra
    const float rad  = angulo * static_cast<float>(M_PI) / 180.f;
    const float cosA = std::cos(-rad);
    const float sinA = std::sin(-rad);
    const float dx   = p.x - centro.x;
    const float dy   = p.y - centro.y;
    const float lx   = dx * cosA - dy * sinA;
    const float ly   = dx * sinA + dy * cosA;
    return std::abs(lx) <= ancho / 2.f && std::abs(ly) <= alto / 2.f;
}

// ConfigSala::valida

// Chequeo mínimo antes de usar una ConfigSala: dimensiones del
// frame positivas, pizarra con tamaño real, y al menos un puesto definido con un rectángulo.
bool ConfigSala::valida() const
{
    if (anchoFrame <= 0 || altoFrame <= 0)  return false;
    if (pizarra.ancho <= 0 || pizarra.alto <= 0) return false;
    if (puestos.empty()) return false;
    for (const auto& p : puestos)
        if (p.rect.width <= 0 || p.rect.height <= 0) return false;
    return true;
}

// JSON

// Tiene comillas dobles dentro de un string para que no se rompa el JSON
static std::string escaparStr(const std::string& s)
{
    std::string r;
    for (char c : s) {
        if (c == '"') r += '\\';
        r += c;
    }
    return r;
}

// Vuelca la configuración completa de la sala (pizarra + puestos) a un archivo JSON
bool guardarConfigSala(const std::string& ruta, const ConfigSala& config)
{
    std::ofstream f(ruta);
    if (!f.is_open()) return false;

    f << "{\n";
    f << "  \"ancho_frame\": " << config.anchoFrame << ",\n";
    f << "  \"alto_frame\": "  << config.altoFrame  << ",\n";
    f << "  \"pizarra\": {\n";
    f << "    \"centro_x\": " << config.pizarra.centro.x << ",\n";
    f << "    \"centro_y\": " << config.pizarra.centro.y << ",\n";
    f << "    \"ancho\": "    << config.pizarra.ancho    << ",\n";
    f << "    \"alto\": "     << config.pizarra.alto     << ",\n";
    f << "    \"angulo\": "   << config.pizarra.angulo   << "\n";
    f << "  },\n";
    f << "  \"puestos\": [\n";
    for (size_t i = 0; i < config.puestos.size(); ++i) {
        const auto& p = config.puestos[i];
        f << "    {\n";
        f << "      \"id\": "          << p.id                      << ",\n";
        f << "      \"nombre\": \""    << escaparStr(p.nombre)      << "\",\n";
        f << "      \"estudiante\": \"" << escaparStr(p.estudiante) << "\",\n";
        f << "      \"x\": "           << p.rect.x                  << ",\n";
        f << "      \"y\": "     << p.rect.y           << ",\n";
        f << "      \"ancho\": " << p.rect.width        << ",\n";
        f << "      \"alto\": "  << p.rect.height       << "\n";
        f << "    }" << (i + 1 < config.puestos.size() ? "," : "") << "\n";
    }
    f << "  ]\n";
    f << "}\n";
    return f.good();
}

// Las tres funciones siguientes (leerDouble, leerInt, leerStr) no son un
// parser (traduce el texto a una estructura de datos) JSON general: busca literalmente la clave "clave": dentro del
// texto y lee el valor que sigue.

// Busca "clave": dentro del texto y convierte lo que sigue a double.
static bool leerDouble(const std::string& json, const std::string& clave, double& val)
{
    const std::string buscar = "\"" + clave + "\":";
    auto pos = json.find(buscar);
    if (pos == std::string::npos) return false;
    pos += buscar.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n')) ++pos;
    try { val = std::stod(json.substr(pos)); return true; }
    catch (...) { return false; }
}

// Igual que leerDouble pero cambiando el resultado a int.
static bool leerInt(const std::string& json, const std::string& clave, int& val)
{
    double d = 0;
    if (!leerDouble(json, clave, d)) return false;
    val = static_cast<int>(d);
    return true;
}

// Busca "clave": "..." y devuelve el contenido entre comillas, respetando
// el escape de comillas internas (\") generado por escaparStr.
static std::string leerStr(const std::string& json, const std::string& clave)
{
    const std::string buscar = "\"" + clave + "\": \"";
    auto pos = json.find(buscar);
    if (pos == std::string::npos) return "";
    pos += buscar.size();
    std::string r;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\') ++pos;
        r += json[pos++];
    }
    return r;
}

// Lee un archivo JSON previamente generado por guardarConfigSala y
// reconstruye la ConfigSala completa: dimensiones de frame, pizarra y
// lista de puestos. Devuelve false si el archivo no se pudo abrir o si,
// al final, la configuración resultante no pasa ConfigSala::valida().
bool cargarConfigSala(const std::string& ruta, ConfigSala& config)
{
    std::ifstream f(ruta);
    if (!f.is_open()) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string json = ss.str();

    leerInt(json, "ancho_frame", config.anchoFrame);
    leerInt(json, "alto_frame",  config.altoFrame);

    // Bloque pizarra: se aísla el objeto { ... } correspondiente y se leen
    // sus campos por separado, para no confundirlos con los de los puestos.
    auto blkPos = json.find("\"pizarra\":");
    if (blkPos != std::string::npos) {
        auto ini = json.find('{', blkPos);
        auto fin = json.find('}', ini);
        if (ini != std::string::npos && fin != std::string::npos) {
            const std::string blk = json.substr(ini, fin - ini + 1);
            double v = 0;
            if (leerDouble(blk, "centro_x", v)) config.pizarra.centro.x = static_cast<float>(v);
            if (leerDouble(blk, "centro_y", v)) config.pizarra.centro.y = static_cast<float>(v);
            if (leerDouble(blk, "ancho",    v)) config.pizarra.ancho    = static_cast<float>(v);
            if (leerDouble(blk, "alto",     v)) config.pizarra.alto     = static_cast<float>(v);
            if (leerDouble(blk, "angulo",   v)) config.pizarra.angulo   = static_cast<float>(v);
        }
    }

    // Array de puestos: se recorre el arreglo "puestos": [ ... ] contando
    // llaves para encontrar dónde empieza y termina cada objeto individual,
    // ya que pueden contener anidamiento implícito (aunque en este formato
    // no lo usan, el conteo de profundidad lo deja a prueba de eso).
    auto arrPos = json.find("\"puestos\":");
    if (arrPos != std::string::npos) {
        auto arrIni = json.find('[', arrPos);
        auto arrFin = json.rfind(']');
        if (arrIni != std::string::npos && arrFin != std::string::npos) {
            size_t cur = arrIni + 1;
            while (cur < arrFin) {
                auto objIni = json.find('{', cur);
                if (objIni == std::string::npos || objIni >= arrFin) break;
                int    depth  = 1;
                size_t objFin = objIni + 1;
                while (objFin < json.size() && depth > 0) {
                    if      (json[objFin] == '{') ++depth;
                    else if (json[objFin] == '}') --depth;
                    ++objFin;
                }
                const std::string obj = json.substr(objIni, objFin - objIni);
                PuestoEstudiante p;
                leerInt(obj, "id",    p.id);
                p.nombre     = leerStr(obj, "nombre");
                p.estudiante = leerStr(obj, "estudiante");
                leerInt(obj, "x",     p.rect.x);
                leerInt(obj, "y",     p.rect.y);
                leerInt(obj, "ancho", p.rect.width);
                leerInt(obj, "alto",  p.rect.height);
                config.puestos.push_back(p);
                cur = objFin;
            }
        }
    }

    return config.valida();
}

// Asociación rostro → puesto

// Dado un rostro detectado (rectángulo en coordenadas del frame), busca el
// puesto cuyo centro esté más cerca del centro del rostro, descartando
// candidatos que estén demasiado lejos (umbral proporcional al ancho del
// puesto, para tolerar que el alumno se mueva un poco en su lugar).
// Devuelve el id del puesto más cercano, o -1 si ninguno califica.
int asociarRostroAPuesto(const cv::Rect& rostro, const ConfigSala& config)
{
    const cv::Point2f centroRostro(
        rostro.x + rostro.width  / 2.f,
        rostro.y + rostro.height / 2.f);

    int    mejorId   = -1;
    double mejorDist = std::numeric_limits<double>::max();

    for (const auto& p : config.puestos) {
        const cv::Point2f centroPuesto(
            p.rect.x + p.rect.width  / 2.f,
            p.rect.y + p.rect.height / 2.f);
        const double dist    = cv::norm(centroRostro - centroPuesto);
        const double umbral  = p.rect.width * 1.5;   // tolerancia generosa
        if (dist < mejorDist && dist < umbral) {
            mejorDist = dist;
            mejorId   = p.id;
        }
    }
    return mejorId;
}

// Dibujo sobre frame

// Dibuja, sobre un frame del video, la pizarra (como polígono rojo
// semitransparente) y cada puesto configurado (rectángulo cyan con
// etiqueta de id/nombre y, debajo, el nombre del estudiante asignado).
// Se usa tanto en el asistente de configuración como en los frames
// anotados que genera el procesador de video, por eso reescala las
// coordenadas guardadas si el frame actual tiene otra resolución.
void dibujarSobreFrame(cv::Mat& frame, const ConfigSala& config)
{
    if (frame.empty()) return;

    // Calcula factor de escala por si el frame fue redimensionado
    const float sx = static_cast<float>(frame.cols) / config.anchoFrame;
    const float sy = static_cast<float>(frame.rows) / config.altoFrame;

    // Pizarra (rectángulo rotado, rojo semitransparente)
    {
        auto verts = config.pizarra.vertices();
        std::vector<cv::Point> poly;
        poly.reserve(4);
        for (const auto& v : verts)
            poly.push_back({static_cast<int>(v.x * sx),
                            static_cast<int>(v.y * sy)});

        // Relleno semitransparente
        cv::Mat overlay = frame.clone();
        cv::fillConvexPoly(overlay, poly, cv::Scalar(0, 0, 200));
        cv::addWeighted(overlay, 0.18, frame, 0.82, 0, frame);

        // Contorno
        for (int i = 0; i < 4; ++i)
            cv::line(frame, poly[i], poly[(i + 1) % 4],
                     cv::Scalar(0, 50, 255), 2, cv::LINE_AA);

        // Etiqueta
        cv::Point centroW(static_cast<int>(config.pizarra.centro.x * sx),
                          static_cast<int>(config.pizarra.centro.y * sy));
        cv::putText(frame, "PIZARRA",
                    centroW + cv::Point(-30, 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5,
                    cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
    }

    // Puestos (rectángulos cyan con etiqueta de id y nombre)
    for (const auto& p : config.puestos) {
        const cv::Rect r(
            static_cast<int>(p.rect.x      * sx),
            static_cast<int>(p.rect.y      * sy),
            static_cast<int>(p.rect.width  * sx),
            static_cast<int>(p.rect.height * sy));

        cv::rectangle(frame, r, cv::Scalar(0, 200, 200), 1, cv::LINE_AA);

        const std::string etiq = "#" + std::to_string(p.id)
                                 + (p.nombre.empty() ? "" : " " + p.nombre);
        cv::putText(frame, etiq,
                    cv::Point(r.x + 2, r.y + 13),
                    cv::FONT_HERSHEY_SIMPLEX, 0.35,
                    cv::Scalar(0, 220, 220), 1, cv::LINE_AA);

        // Nombre del estudiante (segunda línea, en blanco)
        if (!p.estudiante.empty())
            cv::putText(frame, p.estudiante,
                        cv::Point(r.x + 2, r.y + 27),
                        cv::FONT_HERSHEY_SIMPLEX, 0.38,
                        cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
    }
}
