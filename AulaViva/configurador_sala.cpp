// configurador_sala.cpp
//
// Ventana Qt interactiva para configurar la distribución de la sala:
// 1. Dibuja rectángulo rotado sobre la pizarra (drag + rueda para rotar)
// 2. Dibuja rectángulos sobre cada puesto de estudiante
// 3. Guarda sala_config.json
//
// NOTAS DE COMPILACIÓN:
// Requiere CONFIG += c++17 en el .pro
// El #include "<nombre>.moc" al FINAL del archivo es obligatorio cuando
// Q_OBJECT se usa dentro de un .cpp (no en un .h separado).
// Solo debe aparecer UNA VEZ y al final de todo el archivo.

#include "configurador_sala.h"
#include "sala_config.h"
#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QListWidget>
#include <QMessageBox>
#include <QInputDialog>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QPixmap>
#include <QImage>
#include <QStatusBar>
#include <QKeyEvent>
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <vector> //INCLUIMOS TODAS LAS COSAS POSIBLES
#include <functional>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Utilidades

// Convierte un frame de OpenCV (BGR) a QImage (RGB) para mostrarlo en Qt.
static QImage matAQImage(const cv::Mat& mat)
{
    cv::Mat rgb;
    cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
    return QImage(rgb.data, rgb.cols, rgb.rows,
                  static_cast<int>(rgb.step),
                  QImage::Format_RGB888).copy();
}

// CanvasSala (widget de dibujo interactivo)
//
// Muestra el frame de referencia y permite al usuario dibujar rectángulos
// encima con el mouse: uno para la pizarra (rotado con la rueda) y uno
// por cada puesto de estudiante. Emite señales Qt, por eso necesita Q_OBJECT
// y el #include .moc al final del archivo.

class CanvasSala : public QLabel
{
    Q_OBJECT

public:
    enum Modo { MODO_PIZARRA, MODO_PUESTO };

    explicit CanvasSala(QWidget* parent = nullptr) : QLabel(parent)
    {
        setMouseTracking(true);
        setCursor(Qt::CrossCursor);
    }

    void setFrame(const QImage& img)
    {
        baseImage_ = img;
        frameW_    = img.width();
        frameH_    = img.height();
        update();
    }

    void setModo(Modo m) { modo_ = m; }

    // Pizarra
    void setPizarra(const ConfigPizarra& p) { pizarra_ = p; tienePizarra_ = true; update(); }
    const ConfigPizarra& pizarra()   const { return pizarra_; }
    bool                 tienePizarra() const { return tienePizarra_; }

    // Puestos
    const std::vector<PuestoEstudiante>& puestos() const { return puestos_; }
    void setPuestos(const std::vector<PuestoEstudiante>& v) { puestos_ = v; update(); }
    void eliminarUltimoPuesto() { if (!puestos_.empty()) { puestos_.pop_back(); update(); } }
    void limpiarPuestos()       { puestos_.clear(); update(); }

    //callback: se llama cuando el usuario termina de dibujar (un puesto)
    std::function<void(cv::Rect)> onNuevoPuesto;

signals:
    void pizarraActualizada();

protected:
    // Inicia el arrastre al hacer clic izquierdo; guarda el punto de origen.
    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton) {
            arrastrando_ = true;
            inicio_ = actual_ = e->pos();
        }
    }

    // Actualiza el rectángulo de arrastre en tiempo real; si el modo es pizarra,
    // recalcula la ConfigPizarra para que la vista previa sea inmediata.
    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (arrastrando_) {
            actual_ = e->pos();
            if (modo_ == MODO_PIZARRA) actualizarPizarraDesdeArrastre();
            update();
        }
    }

    // Al soltar el botón confirma el rectángulo: si es pizarra emite la señal,
    // si es puesto invoca el callback onNuevoPuesto con el rect en coords de frame.
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (e->button() != Qt::LeftButton || !arrastrando_) return;
        arrastrando_ = false;
        actual_      = e->pos();

        if (modo_ == MODO_PIZARRA) {
            actualizarPizarraDesdeArrastre();
            tienePizarra_ = (pizarra_.ancho > 5 && pizarra_.alto > 5);
            emit pizarraActualizada();
        } else {
            const QRect r = QRect(inicio_, actual_).normalized();
            if (r.width() > 5 && r.height() > 5) {
                if (onNuevoPuesto) onNuevoPuesto(widgetRectAFrame(r));
            }
        }
        update();
    }

    // La rueda del mouse rota la pizarra 1° por tick (solo en MODO_PIZARRA).
    void wheelEvent(QWheelEvent* e) override
    {
        if (modo_ == MODO_PIZARRA && tienePizarra_) {
            const float delta = (e->angleDelta().y() > 0) ? 1.f : -1.f;
            pizarra_.angulo   = std::fmod(pizarra_.angulo + delta + 360.f, 360.f);
            emit pizarraActualizada();
            update();
        }
    }

    // Redibuja todo el canvas: imagen de fondo escalada con aspect-ratio,
    // polígono de la pizarra (rojo), puestos (cyan) y las vistas previas
    // de arrastre en línea discontinua.
    void paintEvent(QPaintEvent*) override
    {
        if (baseImage_.isNull()) return;

        const QPixmap pm = QPixmap::fromImage(baseImage_)
                               .scaled(size(), Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation);
        QPainter p(this);
        offsetX_ = (width()  - pm.width())  / 2;
        offsetY_ = (height() - pm.height()) / 2;
        escala_  = static_cast<double>(pm.width()) / frameW_;

        p.drawPixmap(offsetX_, offsetY_, pm);

        // Pizarra (polígono rotado rojo)
        if (tienePizarra_) dibujarRectRotado(p, pizarra_, Qt::red, 2);

        // Vista previa mientras se arrastra la pizarra
        if (arrastrando_ && modo_ == MODO_PIZARRA) {
            p.setPen(QPen(Qt::red, 1, Qt::DashLine));
            p.drawRect(QRect(inicio_, actual_).normalized());
        }

        // Puestos (cyan)
        p.setPen(QPen(Qt::cyan, 2));
        for (const auto& puesto : puestos_) {
            const QRect wr = frameRectAWidget(puesto.rect);
            p.drawRect(wr);
            p.setPen(QPen(Qt::yellow, 1));
            p.drawText(wr.topLeft() + QPoint(2, 12),
                       QString("#%1 %2").arg(puesto.id)
                           .arg(QString::fromStdString(puesto.nombre)));
            p.setPen(QPen(Qt::cyan, 2));
        }

        // Vista previa de puesto mientras se arrastra
        if (arrastrando_ && modo_ == MODO_PUESTO) {
            p.setPen(QPen(Qt::cyan, 1, Qt::DashLine));
            p.drawRect(QRect(inicio_, actual_).normalized());
        }
    }

private:
    QImage  baseImage_;
    int     frameW_ = 1, frameH_ = 1;
    double  escala_ = 1.0;
    int     offsetX_ = 0, offsetY_ = 0;

    Modo    modo_        = MODO_PIZARRA;
    bool    arrastrando_ = false;
    QPoint  inicio_, actual_;

    ConfigPizarra              pizarra_;
    bool                       tienePizarra_ = false;
    std::vector<PuestoEstudiante> puestos_;

    // Convierte coordenadas de widget Qt a coordenadas del frame original,
    // teniendo en cuenta el offset de centrado y la escala de visualización.
    cv::Rect widgetRectAFrame(const QRect& r) const
    {
        const int x = static_cast<int>((r.x() - offsetX_) / escala_);
        const int y = static_cast<int>((r.y() - offsetY_) / escala_);
        const int w = static_cast<int>(r.width()  / escala_);
        const int h = static_cast<int>(r.height() / escala_);
        return cv::Rect(std::max(0, x), std::max(0, y),
                        std::min(w, frameW_ - x),
                        std::min(h, frameH_ - y));
    }

    // Operación inversa: convierte un rect del frame a coordenadas del widget.
    QRect frameRectAWidget(const cv::Rect& r) const
    {
        return QRect(static_cast<int>(r.x      * escala_) + offsetX_,
                     static_cast<int>(r.y      * escala_) + offsetY_,
                     static_cast<int>(r.width  * escala_),
                     static_cast<int>(r.height * escala_));
    }

    // Actualiza centro, ancho y alto de la pizarra a partir del rect de arrastre
    // actual, manteniendo el ángulo que el usuario ajustó con la rueda del mouse.
    void actualizarPizarraDesdeArrastre()
    {
        const QRect r = QRect(inicio_, actual_).normalized();
        pizarra_.centro.x = static_cast<float>((r.center().x() - offsetX_) / escala_);
        pizarra_.centro.y = static_cast<float>((r.center().y() - offsetY_) / escala_);
        pizarra_.ancho    = static_cast<float>(r.width()  / escala_);
        pizarra_.alto     = static_cast<float>(r.height() / escala_);
        // El ángulo se ajusta solo con la rueda del mouse
    }

    // Dibuja el polígono rotado de la pizarra sobre el QPainter dado,
    // con relleno semitransparente y etiqueta de ángulo actual.
    void dibujarRectRotado(QPainter& p, const ConfigPizarra& piz,
                           QColor color, int grosor) const
    {
        const auto verts = piz.vertices();
        QPolygonF poly;
        for (const auto& v : verts)
            poly << QPointF(v.x * escala_ + offsetX_,
                            v.y * escala_ + offsetY_);
        poly << poly[0];

        p.setPen(QPen(color, grosor));
        p.setBrush(QColor(color.red(), color.green(), color.blue(), 40));
        p.drawPolygon(poly);

        const QPointF centroW(piz.centro.x * escala_ + offsetX_,
                              piz.centro.y * escala_ + offsetY_);
        p.setPen(QPen(Qt::white, 1));
        p.drawText(centroW + QPointF(4, -4),
                   QString("Pizarra %1°").arg(static_cast<int>(piz.angulo)));
    }
};

// VentanaConfiguradorSala — ventana principal del configurador
//
// QMainWindow que contiene el CanvasSala a la izquierda y un panel de controles
// a la derecha (botones para cambiar modo, lista de puestos y botón de guardado).
// Al cerrarse sin guardar, guardadoExitoso() devuelve false.

class VentanaConfiguradorSala : public QMainWindow
{
    Q_OBJECT

public:
    VentanaConfiguradorSala(const cv::Mat& frame,
                            int            ancho,
                            int            alto,
                            const QString& rutaGuardado,
                            QWidget* parent = nullptr)
        : QMainWindow(parent)
        , rutaGuardado_(rutaGuardado)
        , anchoFrame_(ancho)
        , altoFrame_(alto)
    {
        setWindowTitle("Configuración de Sala — AulaViva");
        resize(1100, 700);

        auto* central  = new QWidget(this);
        auto* layoutH  = new QHBoxLayout(central);
        setCentralWidget(central);

        // Canvas
        canvas_ = new CanvasSala(this);
        canvas_->setMinimumSize(640, 480);
        canvas_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        canvas_->setFrame(matAQImage(frame));
        layoutH->addWidget(canvas_, 3);

        // Panel derecho
        auto* panelDer = new QWidget(this);
        auto* layoutV  = new QVBoxLayout(panelDer);
        layoutH->addWidget(panelDer, 1);

        // Grupo Pizarra
        auto* grpPiz = new QGroupBox("1. Pizarra / Foco de atención", panelDer);
        auto* vPiz   = new QVBoxLayout(grpPiz);
        layoutV->addWidget(grpPiz);

        lblPizarra_ = new QLabel("Sin definir", grpPiz);
        lblPizarra_->setStyleSheet("color:#ff6060;");
        vPiz->addWidget(lblPizarra_);

        auto* btnPizarra = new QPushButton("Dibujar pizarra", grpPiz);
        btnPizarra->setCheckable(true);
        btnPizarra->setToolTip(
            "Arrastra para definir el área.\nRueda del mouse para rotar.");
        vPiz->addWidget(btnPizarra);

        auto* lblRueda = new QLabel("Rueda del mouse = rotar", grpPiz);
        lblRueda->setStyleSheet("color:#aaaaaa; font-size:10px;");
        vPiz->addWidget(lblRueda);

        // Grupo Puestos
        auto* grpPue = new QGroupBox("2. Puestos de estudiantes", panelDer);
        auto* vPue   = new QVBoxLayout(grpPue);
        layoutV->addWidget(grpPue);

        auto* btnPuesto = new QPushButton("Añadir puesto", grpPue);
        btnPuesto->setCheckable(true);
        vPue->addWidget(btnPuesto);

        listaPuestos_ = new QListWidget(grpPue);
        listaPuestos_->setMaximumHeight(200);
        vPue->addWidget(listaPuestos_);

        auto* hBtnPue = new QHBoxLayout();
        auto* btnElim = new QPushButton("Eliminar último", grpPue);
        auto* btnLimp = new QPushButton("Limpiar todos",   grpPue);
        hBtnPue->addWidget(btnElim);
        hBtnPue->addWidget(btnLimp);
        vPue->addLayout(hBtnPue);

        // Botones finales
        layoutV->addStretch();

        auto* btnGuardar = new QPushButton("💾 Guardar configuración", panelDer);
        btnGuardar->setStyleSheet(
            "background:#2a7a2a; color:white; font-weight:bold; padding:8px;");
        layoutV->addWidget(btnGuardar);

        auto* btnCancelar = new QPushButton("Cancelar", panelDer);
        layoutV->addWidget(btnCancelar);

        statusBar()->showMessage(
            "Paso 1: Haz clic en 'Dibujar pizarra' y arrastra sobre el área de la pizarra.");

        // Conexiones
        connect(btnPizarra, &QPushButton::toggled, this, [=](bool on) {
            canvas_->setModo(on ? CanvasSala::MODO_PIZARRA : CanvasSala::MODO_PUESTO);
            btnPuesto->setChecked(false);
            if (on) statusBar()->showMessage(
                    "Arrastra para definir la pizarra · Rueda del mouse para rotar el rectángulo");
        });

        connect(btnPuesto, &QPushButton::toggled, this, [=](bool on) {
            canvas_->setModo(on ? CanvasSala::MODO_PUESTO : CanvasSala::MODO_PIZARRA);
            btnPizarra->setChecked(false);
            if (on) statusBar()->showMessage(
                    "Arrastra para dibujar el área de un puesto de estudiante");
        });

        connect(canvas_, &CanvasSala::pizarraActualizada, this, [=]() {
            const auto& piz = canvas_->pizarra();
            lblPizarra_->setText(
                QString("Centro: (%1, %2)\nTamaño: %3×%4 px\nÁngulo: %5°")
                    .arg(static_cast<int>(piz.centro.x))
                    .arg(static_cast<int>(piz.centro.y))
                    .arg(static_cast<int>(piz.ancho))
                    .arg(static_cast<int>(piz.alto))
                    .arg(static_cast<int>(piz.angulo)));
            lblPizarra_->setStyleSheet("color:#60ff60;");
        });

        canvas_->onNuevoPuesto = [=](cv::Rect rc) {
            const int nextId = static_cast<int>(canvas_->puestos().size()) + 1;

            // Diálogo con dos campos
            QDialog dlg(this);
            dlg.setWindowTitle(QString("Puesto #%1").arg(nextId));
            dlg.setMinimumWidth(340);

            auto* form    = new QFormLayout(&dlg);
            auto* edtPos  = new QLineEdit(QString("Puesto-%1").arg(nextId), &dlg);
            auto* edtAlum = new QLineEdit(&dlg);
            edtAlum->setPlaceholderText("Nombre del estudiante (opcional)");

            form->addRow("Posición / ID del puesto:", edtPos);
            form->addRow("Nombre del estudiante:",    edtAlum);

            auto* btns = new QDialogButtonBox(
                QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
            form->addRow(btns);

            connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
            connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

            if (dlg.exec() != QDialog::Accepted) return;

            PuestoEstudiante p;
            p.id         = nextId;
            p.nombre     = edtPos->text().trimmed().toStdString();
            p.estudiante = edtAlum->text().trimmed().toStdString();
            p.rect       = rc;

            auto puestos = canvas_->puestos();
            puestos.push_back(p);
            canvas_->setPuestos(puestos);
            actualizarListaPuestos();
            statusBar()->showMessage(
                QString("Puesto #%1 (%2) añadido. Continúa dibujando o guarda la configuración.")
                    .arg(nextId)
                    .arg(QString::fromStdString(
                        p.estudiante.empty() ? p.nombre : p.estudiante)));
        };

        connect(btnElim, &QPushButton::clicked, this, [=]() {
            canvas_->eliminarUltimoPuesto();
            actualizarListaPuestos();
        });

        connect(btnLimp, &QPushButton::clicked, this, [=]() {
            if (QMessageBox::question(this, "Confirmar",
                                      "¿Eliminar todos los puestos?") == QMessageBox::Yes) {
                canvas_->limpiarPuestos();
                actualizarListaPuestos();
            }
        });

        connect(btnGuardar,  &QPushButton::clicked, this, &VentanaConfiguradorSala::guardar);
        connect(btnCancelar, &QPushButton::clicked, this, &QMainWindow::close);

        // Activa modo pizarra por defecto
        btnPizarra->setChecked(true);
    }

    bool guardadoExitoso() const { return guardadoOk_; }

private slots:
    // Valida que existan pizarra y al menos un puesto, construye el ConfigSala
    // y lo escribe en disco con guardarConfigSala(). Cierra la ventana si tiene éxito.
    void guardar()
    {
        if (!canvas_->tienePizarra()) {
            QMessageBox::warning(this, "Falta la pizarra",
                                 "Define el área de la pizarra antes de guardar.");
            return;
        }
        if (canvas_->puestos().empty()) {
            QMessageBox::warning(this, "Sin puestos",
                                 "Agrega al menos un puesto de estudiante.");
            return;
        }

        ConfigSala config;
        config.anchoFrame = anchoFrame_;
        config.altoFrame  = altoFrame_;
        config.pizarra    = canvas_->pizarra();
        config.puestos    = canvas_->puestos();

        if (!config.valida()) {
            QMessageBox::critical(this, "Error",
                                  "La configuración no es válida. Revisa los datos.");
            return;
        }

        if (guardarConfigSala(rutaGuardado_.toStdString(), config)) {
            guardadoOk_ = true;
            QMessageBox::information(this, "Guardado",
                                     QString("Configuración guardada en:\n%1")
                                         .arg(rutaGuardado_));
            close();
        } else {
            QMessageBox::critical(this, "Error",
                                  "No se pudo guardar el archivo JSON.");
        }
    }

private:
    // Reconstruye la QListWidget con los puestos actuales del canvas,
    // mostrando ID, nombre del puesto, estudiante asignado y dimensiones en píxeles.
    void actualizarListaPuestos()
    {
        listaPuestos_->clear();
        for (const auto& p : canvas_->puestos()) {
            const QString alumno = p.estudiante.empty()
            ? "(sin asignar)"
            : QString::fromStdString(p.estudiante);
            listaPuestos_->addItem(
                QString("#%1  %2  — %3  [%4×%5 px @ (%6,%7)]")
                    .arg(p.id)
                    .arg(QString::fromStdString(p.nombre))
                    .arg(alumno)
                    .arg(p.rect.width).arg(p.rect.height)
                    .arg(p.rect.x).arg(p.rect.y));
        }
    }

    CanvasSala*  canvas_       = nullptr;
    QLabel*      lblPizarra_   = nullptr;
    QListWidget* listaPuestos_ = nullptr;
    QString      rutaGuardado_;
    int          anchoFrame_, altoFrame_;
    bool         guardadoOk_   = false;
};

// API pública

// Abre el configurador de sala de forma modal: extrae el primer frame del video
// como fondo de referencia, muestra la ventana y bloquea hasta que el usuario
// guarda o cancela. Devuelve true solo si el archivo JSON fue guardado con éxito.
bool lanzarConfiguradorSala(const QString& rutaVideo,
                            const QString& rutaConfigSala)
{
    // Extrae primer frame del video como fondo de referencia
    cv::VideoCapture cap(rutaVideo.toStdString());
    if (!cap.isOpened()) {
        QMessageBox::critical(nullptr, "Error",
                              "No se pudo abrir el video para la configuración de sala.");
        return false;
    }

    cv::Mat frame;
    cap >> frame;
    cap.release();

    if (frame.empty()) {
        QMessageBox::critical(nullptr, "Error",
                              "No se pudo extraer el primer frame del video.");
        return false;
    }

    VentanaConfiguradorSala ventana(frame, frame.cols, frame.rows, rutaConfigSala);
    ventana.show();

    // Bucle de eventos propio — bloquea hasta que el usuario cierre la ventana.
    // Se usa exec() del QApplication global en lugar de crear un QEventLoop
    // adicional para evitar bucles anidados en caso de que se llame desde
    // dentro del bucle principal de la app.
    qApp->exec();

    return ventana.guardadoExitoso();
}

// OBLIGATORIO: incluir el .moc una única vez, al final del archivo
// Necesario porque Q_OBJECT está en clases definidas en un .cpp, no en un .h.
#include "configurador_sala.moc"