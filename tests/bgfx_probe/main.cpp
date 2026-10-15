#include <QApplication>
#include <QDebug>
#include <QDialog>
#include <QElapsedTimer>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMoveEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>
#include <QWidget>

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

#ifdef Q_OS_WASM
#include <emscripten/emscripten.h>

// Emscripten normally looks up canvas selectors in the light DOM. Register
// a specialHTMLTargets alias for a canvas inside Qt's shadow-root screen,
// including for bgfx's later emscripten_set_canvas_element_size() calls.
// This avoids reparenting the GPU canvas (and losing its stacking context).
EM_JS(int, aowisBgfxProbePrepare, (), {
    const bridge = window.aowisBgfxProbe;
    if (!bridge || !bridge.prepare() || !bridge.attach())
        return 0;
    if (typeof specialHTMLTargets === 'undefined') {
        console.error('Emscripten specialHTMLTargets is unavailable');
        return 0;
    }
    specialHTMLTargets['!aowis-bgfx-probe'] = bridge.canvas();
    return 1;
});
EM_JS(void, aowisBgfxProbeGeometry, (int x, int y, int w, int h, int visible), {
    if (window.aowisBgfxProbe)
        window.aowisBgfxProbe.geometry(x, y, w, h, Boolean(visible));
});
// Query actual browser stacking, not just the structural Node test. The
// dialog must be present as an independent Qt window when it is open.
EM_JS(int, aowisBgfxProbeStackOk, (int expectDialog), {
    const result = window.aowisBgfxProbe?.inspect();
    return result?.ok && (!expectDialog || result.transientCount > 0) ? 1 : 0;
});
EM_JS(void, aowisBgfxProbeRemove, (), {
    if (typeof specialHTMLTargets !== 'undefined')
        delete specialHTMLTargets['!aowis-bgfx-probe'];
    if (window.aowisBgfxProbe)
        window.aowisBgfxProbe.remove();
});
#else
#if defined(Q_OS_LINUX) && defined(AOWIS_BGFX_NATIVE_WAYLAND_PRIVATE)
#include <qpa/qplatformnativeinterface.h>
#endif
#endif

namespace {
constexpr uint16_t kBgfxView = 0;
constexpr int kFrameReportInterval = 120;

class BgfxProbe final : public QObject
{
public:
    explicit BgfxProbe(QObject *parent = nullptr)
        : QObject(parent)
    {
        this->timer.setInterval(16);
        this->timer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&this->timer, &QTimer::timeout, this, [this]() { render(); });
    }

    ~BgfxProbe() override
    {
        this->timer.stop();
        if (this->initialized)
            bgfx::shutdown();
#ifdef Q_OS_WASM
        aowisBgfxProbeRemove();
#endif
    }

#ifdef Q_OS_WASM
    bool initialize(QWidget *placeholder)
    {
        this->placeholder = placeholder;
        if (!aowisBgfxProbePrepare()) {
            qCritical() << "bgfx probe: failed to create browser canvas";
            return false;
        }
        bgfx::Init settings{};
        settings.type = bgfx::RendererType::OpenGLES;
        settings.swapChain.nwh = const_cast<char *>("!aowis-bgfx-probe");
        const QSize pixels = physicalSize();
        settings.swapChain.width = static_cast<uint32_t>(pixels.width());
        settings.swapChain.height = static_cast<uint32_t>(pixels.height());
        settings.reset = BGFX_RESET_VSYNC;
        // bgfx probes supported renderbuffer/internal formats during WebGL 2
        // initialization. Chromium can report INVALID_ENUM for unsupported
        // formats queried by getInternalformatParameter(). Those warnings are
        // non-fatal when bgfx initializes successfully; keep them visible.
        qInfo().noquote()
            << "[AOWIS bgfx] BEGIN WebGL 2 initialization and format capability probing. "
               "Chromium may print 'WebGL: INVALID_ENUM: getInternalformatParameter: "
               "invalid internalformat' for unsupported formats.";
        const bool initSucceeded = bgfx::init(settings);
        qInfo().noquote()
            << (initSucceeded
                    ? "[AOWIS bgfx] END WebGL 2 initialization: success. "
                      "Any preceding format-probe INVALID_ENUM warnings were non-fatal."
                    : "[AOWIS bgfx] END WebGL 2 initialization: FAILED. "
                      "Inspect the preceding WebGL warnings and errors.");
        if (!initSucceeded) {
            qCritical() << "bgfx probe: WebGL 2 initialization failed";
            return false;
        }
        this->initialized = true;
        syncGeometry();
        start();
        return true;
    }

    void syncGeometry()
    {
        if (!this->placeholder)
            return;
        const QPoint position = this->placeholder->mapToGlobal(QPoint(0, 0));
        const QSize logical = this->placeholder->size();
        aowisBgfxProbeGeometry(position.x(), position.y(), logical.width(), logical.height(),
                               this->placeholder->isVisible() ? 1 : 0);
        reset();
    }
#else
    bool initialize(QWindow *window)
    {
        this->window = window;
        bgfx::Init settings{};
        settings.type = bgfx::RendererType::OpenGL;
        settings.swapChain.nwh = reinterpret_cast<void *>(this->window->winId());
#if defined(Q_OS_LINUX)
        const QString platform = QGuiApplication::platformName();
        if (platform.startsWith(QStringLiteral("wayland"))) {
#if defined(AOWIS_BGFX_NATIVE_WAYLAND_PRIVATE)
            QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
            settings.swapChain.ndt = native
                ? native->nativeResourceForIntegration(QByteArrayLiteral("display"))
                : nullptr;
            settings.platformData.type = bgfx::NativeWindowHandleType::Wayland;
            settings.swapChain.nwh = native
                ? native->nativeResourceForWindow(QByteArrayLiteral("surface"), this->window)
                : nullptr;
#else
            qCritical() << "bgfx probe: Wayland needs Qt6::GuiPrivate for wl_surface";
            return false;
#endif
        } else if (platform == QStringLiteral("xcb")) {
#if defined(AOWIS_BGFX_NATIVE_WAYLAND_PRIVATE)
            QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
            settings.swapChain.ndt = native
                ? native->nativeResourceForIntegration(QByteArrayLiteral("display"))
                : nullptr;
#else
            qCritical() << "bgfx probe: X11 needs Qt6::GuiPrivate for native display";
            return false;
#endif
        }
#endif
        if (!settings.swapChain.nwh || (QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))
                                       && !settings.swapChain.ndt)) {
            qCritical() << "bgfx probe: native window/display unavailable";
            return false;
        }
        const QSize pixels = physicalSize();
        settings.swapChain.width = static_cast<uint32_t>(pixels.width());
        settings.swapChain.height = static_cast<uint32_t>(pixels.height());
        settings.reset = BGFX_RESET_VSYNC;
        if (!bgfx::init(settings)) {
            qCritical() << "bgfx probe: native renderer initialization failed";
            return false;
        }
        this->initialized = true;
        start();
        return true;
    }

    void syncGeometry() { reset(); }
#endif

public:
    void setDialogOpen(bool open)
    {
        this->dialogOpen = open;
        this->framesSinceMeasurement = 0;
        this->measurement.restart();
    }

private:
    QSize physicalSize() const
    {
#ifdef Q_OS_WASM
        const QWidget *surface = this->placeholder;
#else
        const QWindow *surface = this->window;
#endif
        if (!surface)
            return QSize(1, 1);
        const qreal ratio = surface->devicePixelRatio();
        return QSize(std::max(1, qRound(surface->width() * ratio)),
                     std::max(1, qRound(surface->height() * ratio)));
    }

    void reset()
    {
        if (!this->initialized)
            return;
        const QSize size = physicalSize();
        if (size == this->lastSize)
            return;
        this->lastSize = size;
        bgfx::SwapChain swapChain{};
        swapChain.width = static_cast<uint32_t>(size.width());
        swapChain.height = static_cast<uint32_t>(size.height());
        bgfx::reset(BGFX_RESET_VSYNC, &swapChain);
    }

    void start()
    {
        bgfx::setDebug(BGFX_DEBUG_TEXT);
        this->measurement.start();
        reset();
        this->timer.start();
    }

    void render()
    {
        if (!this->initialized)
            return;
        reset();
        const QSize size = physicalSize();
        bgfx::setViewRect(kBgfxView, 0, 0,
                          static_cast<uint16_t>(std::min(65535, size.width())),
                          static_cast<uint16_t>(std::min(65535, size.height())));
        bgfx::setViewClear(kBgfxView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                           0x16324aff, 1.0f, 0);
        bgfx::touch(kBgfxView);
        bgfx::dbgTextClear();
        bgfx::dbgTextPrintf(2, 2, 0x0f, "AOWIS bgfx independent GPU canvas");
        bgfx::dbgTextPrintf(2, 4, 0x0f, "No QWidget overlay on the bgfx surface");
        bgfx::dbgTextPrintf(2, 6, 0x0f, "Frames: %llu", static_cast<unsigned long long>(this->frames));
        bgfx::dbgTextPrintf(2, 8, 0x0f, "Open a Qt dialog: it must stay above this canvas");
        bgfx::frame();
        ++this->frames;
        ++this->framesSinceMeasurement;
        if (this->framesSinceMeasurement == kFrameReportInterval) {
            const qint64 elapsed = this->measurement.elapsed();
            QString report = QStringLiteral(
                "{\"kind\":\"aowis_bgfx_probe\",\"frames\":%1,\"elapsed_ms\":%2,\"fps\":%3,\"width\":%4,\"height\":%5,\"dialog_open\":%6")
                .arg(kFrameReportInterval).arg(elapsed)
                .arg(elapsed > 0 ? (1000.0 * kFrameReportInterval / elapsed) : 0.0, 0, 'f', 2)
                .arg(size.width()).arg(size.height())
                .arg(this->dialogOpen ? QStringLiteral("true") : QStringLiteral("false"));
#ifdef Q_OS_WASM
            const bool stackingOk = aowisBgfxProbeStackOk(this->dialogOpen ? 1 : 0) != 0;
            report += QStringLiteral(",\"stacking_ok\":%1")
                .arg(stackingOk ? QStringLiteral("true") : QStringLiteral("false"));
            if (!stackingOk)
                qWarning("bgfx probe: actual browser canvas/dialog stacking invariant failed");
#endif
            report += QLatin1Char('}');
            qInfo().noquote() << report;
            this->measurement.restart();
            this->framesSinceMeasurement = 0;
        }
    }

    QTimer timer;
    QElapsedTimer measurement;
    QSize lastSize;
    quint64 frames = 0;
    int framesSinceMeasurement = 0;
    bool dialogOpen = false;
    bool initialized = false;
#ifdef Q_OS_WASM
    QWidget *placeholder = nullptr;
#else
    QWindow *window = nullptr;
#endif
};

class ProbeViewport final : public QWidget
{
public:
    explicit ProbeViewport(BgfxProbe *probe, QWidget *parent = nullptr)
        : QWidget(parent), probe(probe)
    {
        this->setMinimumSize(480, 280);
    }

protected:
    void showEvent(QShowEvent *event) override
    {
        QWidget::showEvent(event);
        QTimer::singleShot(0, this, [this]() { this->probe->syncGeometry(); });
    }
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        this->probe->syncGeometry();
    }
    void moveEvent(QMoveEvent *event) override
    {
        QWidget::moveEvent(event);
        this->probe->syncGeometry();
    }
    void hideEvent(QHideEvent *event) override
    {
        QWidget::hideEvent(event);
        this->probe->syncGeometry();
    }
private:
    BgfxProbe *probe;
};
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QMainWindow mainWindow;
    mainWindow.setWindowTitle(QStringLiteral("AOWIS bgfx stacking probe"));
    QWidget *central = new QWidget(&mainWindow);
    QVBoxLayout *layout = new QVBoxLayout(central);
    QHBoxLayout *controls = new QHBoxLayout();
    QLabel *instruction = new QLabel(QStringLiteral("GPU canvas should cover Qt only in the viewport."), central);
    QPushButton *openDialog = new QPushButton(QStringLiteral("Open Qt dialog"), central);
    controls->addWidget(instruction);
    controls->addWidget(openDialog);
    layout->addLayout(controls);
    BgfxProbe probe(&mainWindow);
#ifdef Q_OS_WASM
    ProbeViewport *placeholder = new ProbeViewport(&probe, central);
    layout->addWidget(placeholder, 1);
#else
    QWindow *nativeWindow = new QWindow();
    nativeWindow->setSurfaceType(QSurface::OpenGLSurface);
    QWidget *container = QWidget::createWindowContainer(nativeWindow, central);
    container->setMinimumSize(480, 280);
    layout->addWidget(container, 1);
    QObject::connect(nativeWindow, &QWindow::widthChanged, &probe, [&probe]() { probe.syncGeometry(); });
    QObject::connect(nativeWindow, &QWindow::heightChanged, &probe, [&probe]() { probe.syncGeometry(); });
#endif
    mainWindow.setCentralWidget(central);
    QObject::connect(openDialog, &QPushButton::clicked, &mainWindow, [&mainWindow, &probe]() {
        QDialog *dialog = new QDialog(&mainWindow);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(QStringLiteral("Qt above bgfx"));
        QVBoxLayout *dialogLayout = new QVBoxLayout(dialog);
        dialogLayout->addWidget(new QLabel(QStringLiteral("This top-level QDialog must overlay bgfx."), dialog));
        QPushButton *closeButton = new QPushButton(QStringLiteral("Close"), dialog);
        dialogLayout->addWidget(closeButton);
        QObject::connect(closeButton, &QPushButton::clicked, dialog, &QDialog::accept);
        dialog->resize(350, 140);
        probe.setDialogOpen(true);
        QObject::connect(dialog, &QDialog::finished, &probe,
                         [&probe](int) { probe.setDialogOpen(false); });
        dialog->open();
    });
    mainWindow.resize(900, 600);
    mainWindow.show();
    QTimer::singleShot(250, &mainWindow, [&probe
#ifdef Q_OS_WASM
      , placeholder
#else
      , nativeWindow
#endif
    ]() {
#ifdef Q_OS_WASM
        probe.initialize(placeholder);
#else
        probe.initialize(nativeWindow);
#endif
    });
    return app.exec();
}
