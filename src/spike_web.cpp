// Minimal spike: prove QWebEngineView can render with a transparent
// background inside a Qt layer-shell window on KDE Plasma Wayland.
//
// Tests:
//   1. Background is transparent (you see desktop wallpaper through panel)
//   2. runJavaScript() updates DOM and Chromium repaints (HP pct updates)
//   3. CSS animation runs (the "POLL 250ms" red dot blinks)

#include <QApplication>
#include <QFile>
#include <QLayout>
#include <QTimer>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWidget>
#include <QWindow>

#include <LayerShellQt/Window>

class SpikeWindow : public QWidget {
public:
    explicit SpikeWindow(const QString &htmlPath);

    void tick(int frame);

private:
    QWebEngineView *m_view{nullptr};
};

SpikeWindow::SpikeWindow(const QString &htmlPath)
{
    // Ensure frameless — KWin has been seen adding a window frame to
    // layer-shell surfaces when the application does not explicitly
    // request a frameless hint.
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    // Do NOT setWindowTitle — calling it makes KWin treat the window as
    // a regular toplevel (with title bar / borders) instead of a
    // layer-shell surface. The layer-shell setup below also anchors all
    // four edges so KWin won't add decoration.

    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_OpaquePaintEvent, false);   // Qt 6.3+ replacement hint

    // Force native window creation BEFORE layer-shell setup — without
    // this, windowHandle() returns nullptr in the constructor and
    // LayerShellQt::Window::get() bails out, leaving the window as a
    // plain toplevel with KWin decoration.
    (void)winId();
    QWindow *native = windowHandle();
    if (!native) {
        qWarning("native window is null; layer-shell will not be set up");
        return;
    }
    LayerShellQt::Window *layer = LayerShellQt::Window::get(native);
    if (layer) {
        layer->setLayer(LayerShellQt::Window::LayerOverlay);
        layer->setMargins(QMargins(0, 0, 0, 0));
        layer->setKeyboardInteractivity(
            LayerShellQt::Window::KeyboardInteractivityOnDemand);
        layer->setExclusiveZone(-1);
        // Anchor all four edges → layer-shell fills the output, KWin
        // does NOT add any window decoration.
        auto anchors = static_cast<LayerShellQt::Window::Anchors>(
            LayerShellQt::Window::AnchorTop |
            LayerShellQt::Window::AnchorBottom |
            LayerShellQt::Window::AnchorLeft |
            LayerShellQt::Window::AnchorRight);
        layer->setAnchors(anchors);
    }

    m_view = new QWebEngineView(this);
    m_view->setAttribute(Qt::WA_TranslucentBackground);
    m_view->setStyleSheet(QStringLiteral("background: transparent;"));
    m_view->setAutoFillBackground(false);

    QVBoxLayout *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(m_view);

    // Chromium transparent background — three coordinated flags.
    QWebEngineScript script;
    script.setName(QStringLiteral("transparent-body"));
    script.setSourceCode(
        // Qt 6.3+ WebEngine regression: DocumentCreation fires too early.
        // Use DocumentReady (after DOM ready) and inject a stylesheet
        // with !important to defeat the Chromium default white background.
        "var s=document.createElement('style');"
        "s.innerHTML='html,body,div{background-color:transparent !important;"
        "background:none !important;margin:0;padding:0;}';"
        "document.head.appendChild(s);"
        "document.documentElement.style.backgroundColor='transparent';"
        "document.body.style.backgroundColor='transparent';"
    );
    script.setInjectionPoint(QWebEngineScript::DocumentReady);
    script.setWorldId(QWebEngineScript::MainWorld);
    m_view->page()->scripts().insert(script);

    m_view->page()->setBackgroundColor(Qt::transparent);

    setAttribute(Qt::WA_TransparentForMouseEvents);
    m_view->settings()->setAttribute(QWebEngineSettings::ShowScrollBars, false);

    QFile f(htmlPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qFatal("cannot open %s", qPrintable(htmlPath));
    }
    const QString html = QString::fromUtf8(f.readAll());
    m_view->setHtml(html, QUrl::fromLocalFile(htmlPath));

    resize(1920, 1080);
}

void SpikeWindow::tick(int frame)
{
    const float hpPct = 50.0F + 40.0F * std::sin(frame * 0.13F);
    const int hpCur = static_cast<int>(25800 * hpPct / 100.0F);
    const int enrageSec = std::max(0, 60 - frame / 4);

    QFile proc(QStringLiteral("/proc/self/status"));
    int memKb = 0;
    if (proc.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!proc.atEnd()) {
            const QByteArray line = proc.readLine();
            if (line.startsWith("VmRSS:")) {
                const QList<QByteArray> parts = line.split('\t');
                if (parts.size() >= 2) memKb = parts[1].toInt();
                break;
            }
        }
    }
    const int memMb = memKb / 1024;

    const QString js = QStringLiteral(
        "window.mhw.update({hpPct:%1,hpCur:%2,hpMax:25800,enrageSec:%3,"
        "frame:%4,memMb:%5});")
        .arg(hpPct, 0, 'f', 1)
        .arg(hpCur)
        .arg(enrageSec)
        .arg(frame)
        .arg(memMb);

    m_view->page()->runJavaScript(js);
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("monster-overlay-web"));
    QApplication::setOrganizationName(QStringLiteral("a27exe"));

    const QString htmlPath =
        QStringLiteral(SPIKE_HTML_PATH);
    SpikeWindow w(htmlPath);
    w.show();

    int frame = 0;
    QTimer t;
    QObject::connect(&t, &QTimer::timeout, [&] {
        w.tick(frame++);
    });
    t.start(250);

    return app.exec();
}