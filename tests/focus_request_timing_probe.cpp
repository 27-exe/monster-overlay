// Does Panel::requestEditFocus() do anything when it is called before
// app.exec()?
//
// The v0.15 keyboard fix calls it there, on the reasoning that entering the
// layer-shell focus chain and asking for activation is all that is needed.
// A diagnostic claimed the opposite: on a live Hyprland session the surface
// is not exposed yet at that point, requestActivate() has no window to
// activate, and the same three calls deferred into the event loop work.
//
// This probe reproduces panel.cpp's layer-shell assembly VERBATIM - the
// WA_ShowWithoutActivating, the KeyboardInteractivityNone -> OnDemand, the
// setActivateOnShow(false), the exclusive zone - because a plain QMainWindow
// does not go through the compositor path at all and answers the wrong
// question. Run it on the real session, not offscreen.

#include <QApplication>
#include <QMainWindow>
#include <QTimer>
#include <QWindow>

#include <LayerShellQt/Window>

#include <QTimer>

#include <cstdio>

static void arm(QMainWindow *w)
{
    w->setAttribute(Qt::WA_ShowWithoutActivating);
    w->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);

    w->winId();  // panel.cpp:252 — bootstraps the native handle
    LayerShellQt::Window *layer = LayerShellQt::Window::get(w->windowHandle());

    layer->setCloseOnDismissed(false);
    layer->setWantsToBeOnActiveScreen(false);
    layer->setLayer(LayerShellQt::Window::LayerOverlay);
    layer->setKeyboardInteractivity(
        LayerShellQt::Window::KeyboardInteractivityNone);
    layer->setExclusiveZone(-1);
    layer->setScope(QStringLiteral("focus-probe"));
    layer->setActivateOnShow(false);
    w->resize(300, 200);
}

static void report(const char *tag, QMainWindow *w)
{
    QWindow *h = w->windowHandle();
    std::printf("%-34s exposed=%d hasFocus=%d\n", tag,
                h ? (int)h->isExposed() : -1, (int)w->hasFocus());
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    // useLayerShell() has been a no-op since Qt 6.5 and is deprecated since
    // 6.6; the wayland-shell-integration plugin enables it. Calling it here
    // only earned a -Wdeprecated-declarations warning.

    // ---------- CASE A: request BEFORE the event loop ----------
    auto *early = new QMainWindow;
    arm(early);
    early->show();
    for (int i = 0; i < 20; ++i)
        QCoreApplication::processEvents();
    std::printf("--- CASE A: the v0.15 call site ---\n");
    report("  after show(), before exec()", early);

    LayerShellQt::Window::get(early->windowHandle())
        ->setKeyboardInteractivity(
            LayerShellQt::Window::KeyboardInteractivityOnDemand);
    if (QWindow *w = early->windowHandle())
        w->requestActivate();
    early->setFocus();
    for (int i = 0; i < 10; ++i)
        QCoreApplication::processEvents();
    report("  after requestActivate()", early);

    // ---------- CASE B: same calls, deferred until exposed ----------
    auto *deferred = new QMainWindow;
    arm(deferred);
    deferred->show();

    std::printf("--- CASE B: deferred into the event loop ---\n");
    bool requested = false;
    QTimer poll;
    poll.setInterval(25);
    QObject::connect(&poll, &QTimer::timeout, [&] {
        QWindow *w = deferred->windowHandle();
        if (!requested && w && w->isExposed()) {
            LayerShellQt::Window::get(w)->setKeyboardInteractivity(
                LayerShellQt::Window::KeyboardInteractivityOnDemand);
            w->requestActivate();
            deferred->setFocus();
            requested = true;
            std::printf("  requested at exposure: hasFocus=%d\n",
                        (int)deferred->hasFocus());
        }
    });
    poll.start();
    QTimer::singleShot(900, [&app] { app.quit(); });
    app.exec();
    for (int i = 0; i < 20; ++i)
        QCoreApplication::processEvents();
    report("  after deferred request", deferred);

    std::printf("\nVERDICT: %s\n",
                (early->hasFocus() ? "pre-exec works" : "pre-exec is a NO-OP"));
    return 0;
}
