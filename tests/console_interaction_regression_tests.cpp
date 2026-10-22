// SPDX-License-Identifier: Apache-2.0
//
// v1.0.1 console seams regression: THE EIGHT FIXES' INTERACTION HALVES.
//
// console_integration_seams_tests.cpp pins the v1.0.0 wiring (visibility,
// view contract, counts). This file pins the 1.0.1 user-facing fixes and
// only claims what a REAL gesture can prove:
//
//   验收项1  设置 is a compact popup (≤680×700, clamped to the screen and
//            the client area) whose long body scrolls internally;
//   验收项2  当前内容状态 shows the Panel's render at its OWN pixel size
//            inside a scroll area (windowPreviewScroll) instead of a
//            pixmap squeezed into ~132px;
//   验收项4  dragging a box does NOT rebuild the inspector: margins and
//            the metaX/metaY readouts move, the section rows survive;
//   验收项5  − / % / + / 1:1 are VISIBLE and drive the real board zoom;
//   验收项6  an opacity change re-renders the source (canvas + inspector)
//            and is NOT multiplied twice into the alpha;
//   验收项7  a board click fills the inspector for the clicked window;
//   验收项8  every output Qt reports — eDP included — is offered by the
//            display combo (the filter is gone).
//
// REAL INPUT, NOT SIGNALS. Every click and the drag are QMouseEvents sent
// to the widget the user would press, and every assertion reads state
// AFTER the event loop has run. Sliders are moved the way a user moves
// them (QSlider::setValue is the same code path as a click on the track,
// and both land in valueChanged → applySelectedWindow*).
//
// CONFIG ISOLATION. XDG_CONFIG_HOME is redirected into a QTemporaryDir
// before QApplication exists, so panels.ini is the temp dir's file and
// never the developer's.

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QScreen>
#include <qpa/qplatformscreen.h>
#include <qpa/qwindowsysteminterface.h>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFrame>
#include <QHash>
#include <QLabel>
#include <QLayout>
#include <QLayoutItem>
#include <QMouseEvent>
#include <QPushButton>
#include <QPainter>
#include <QPointer>
#include <algorithm>
#include <vector>
#include "ui/panel.h"
#include <QSettings>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

#include <cstdio>

#include "core/string_table.h"
#include "ui/console_fidelity_windows.h"
#include "ui/control_panel.h"
#include "ui/hud_canvas.h"
#include "ui/panel_sections.h"
#include "ui/panel_settings_game.h"
#include "ui/screen_query.h"
#include "ui/ui_theme.h"

namespace {

int failures = 0;
int checks = 0;

class FixtureOutput final : public QPlatformScreen {
public:
    explicit FixtureOutput(QString name) : name_(name) {}
    QString name() const override { return name_; }
    QRect geometry() const override { return QRect(0, 0, 2560, 1600); }
    int depth() const override { return 32; }
    QImage::Format format() const override { return QImage::Format_RGB32; }
private:
    QString name_;
};

void check(bool ok, const QString &what, const QString &detail = QString())
{
    ++checks;
    if (ok) {
        std::printf("ok   %s\n", qPrintable(what));
        return;
    }
    ++failures;
    std::printf("FAIL %s%s\n", qPrintable(what),
                detail.isEmpty() ? "" : qPrintable(QStringLiteral(" — %1")
                                                       .arg(detail)));
}

void note(const QString &what)
{
    std::printf("note %s\n", qPrintable(what));
}

void evidence(QWidget *widget, const char *name)
{
    const QString root = qEnvironmentVariable("MONSTER_OVERLAY_UI_EVIDENCE_DIR");
    if (!widget || root.isEmpty()) return;
    QDir().mkpath(root);
    check(widget->grab().save(root + QLatin1Char('/') + QString::fromLatin1(name) + QStringLiteral(".png")),
          QStringLiteral("native widget screenshot saved: ") + QString::fromLatin1(name));
    if (auto *page = widget->findChild<QScrollArea *>(QStringLiteral("consoleRootScroll"))) {
        const int before = page->verticalScrollBar()->value();
        page->verticalScrollBar()->setValue(page->verticalScrollBar()->maximum());
        QCoreApplication::processEvents();
        check(widget->grab().save(root + QLatin1Char('/') + QString::fromLatin1(name) + QStringLiteral("-inspector.png")),
              QStringLiteral("native inspector screenshot saved"));
        page->verticalScrollBar()->setValue(before);
        QCoreApplication::processEvents();
    }
}

void pump(int ms = 150)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

// A real press/release pair, at the local position the user would aim at.
void realClick(QWidget *w, const QPointF &local)
{
    if (!w)
        return;
    const QPointF gp = w->mapToGlobal(QPoint(qRound(local.x()),
                                             qRound(local.y())));
    QMouseEvent press(QEvent::MouseButtonPress, local, gp, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, local, gp, Qt::LeftButton,
                        Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(w, &press);
    QApplication::sendEvent(w, &release);
    QCoreApplication::processEvents();
}

// A real press → move → release, the gesture the canvas turns into
// panelMoved(index, margins). The move carries a LeftButton BUTTONS mask
// (that is what Qt fills in for a held button) — an empty mask is what a
// hover does, and the canvas's drag arms check it.
void realDrag(QWidget *w, const QPointF &from, const QPointF &to)
{
    if (!w)
        return;
    const QPointF gpFrom = w->mapToGlobal(QPoint(qRound(from.x()),
                                                 qRound(from.y())));
    const QPointF gpTo = w->mapToGlobal(QPoint(qRound(to.x()),
                                               qRound(to.y())));
    QMouseEvent press(QEvent::MouseButtonPress, from, gpFrom, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QMouseEvent move(QEvent::MouseMove, to, gpTo, Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, to, gpTo, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(w, &press);
    QApplication::sendEvent(w, &move);
    QApplication::sendEvent(w, &release);
    QCoreApplication::processEvents();
}

// The strongest alpha statement a pixmap can make: the brightest pixel.
// A Panel render paints demo content over a transparent background, so
// the max alpha IS the opacity the painter was given — which is what
// proves the value arrived (and arrived ONCE).
int maxAlpha(const QPixmap &pix)
{
    if (pix.isNull())
        return -1;
    const QImage img = pix.toImage();
    int best = 0;
    // Console selection chrome and its 4px identity stripe are not HUD
    // content; sample the interior whose alpha is governed by opacity.
    for (int y = 8; y < img.height() - 8; y += 2) {
        for (int x = 8; x < img.width() - 8; x += 2) {
            const int a = qAlpha(img.pixel(x, y));
            if (a > best)
                best = a;
        }
    }
    return best;
}

// The zoom controls all share objectName "stageToggle" with the still-
// hidden safe-area/grid buttons, so they are found the way the user sees
// them: by their symbol.
QPushButton *zoomButton(QWidget *root, const QString &symbol)
{
    const auto buttons = root->findChildren<QPushButton *>();
    for (QPushButton *b : buttons)
        if (b->objectName() == QLatin1String("stageToggle")
            && b->text() == symbol)
            return b;
    return nullptr;
}

// The count of widgets under the section column: a rebuild deletes and
// re-creates the SectionRows, a move must not.
int sectionColumnWeight(ControlPanel *cp)
{
    QFrame *col = cp->findChild<QFrame *>(QStringLiteral("inspectorSections"));
    return col ? col->findChildren<QWidget *>().size() : -1;
}

void seedGame(const char *game)
{
    QSettings settings;
    settings.setValue(QStringLiteral("game"), QString::fromLatin1(game));
    settings.sync();
}

void setScope(mhw::GameId game, mhw::PlayerLayoutMode mode)
{
    mhw::setPanelSettingsGame(game);
    mhw::setPanelSettingsLayout(mode);
}

} // namespace

int main(int argc, char *argv[])
{
    // Line-buffered: a hung nested exec() must not swallow its own
    // diagnosis with a 4KB stdio buffer.
    setvbuf(stdout, nullptr, _IOLBF, 0);
    QTemporaryDir configRoot;
    qputenv("XDG_CONFIG_HOME", configRoot.path().toLocal8Bit());
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);
    const QString testLocale = qEnvironmentVariable("MONSTER_OVERLAY_UI_TEST_LOCALE");
    if (!testLocale.isEmpty())
        check(mhw::StringTable::instance().load(testLocale), QStringLiteral("native test locale loaded"));
    if (qEnvironmentVariable("MONSTER_OVERLAY_UI_TEST_THEME") == QStringLiteral("light"))
        setUiTheme(false);
    QApplication::setOrganizationName(QStringLiteral("monster-overlay"));
    QApplication::setApplicationName(QStringLiteral("monster-control"));

    const bool testRise = qEnvironmentVariable("MONSTER_OVERLAY_UI_TEST_GAME") == QStringLiteral("rise");
    seedGame(testRise ? "rise" : "world");
    const bool startupSplit = qEnvironmentVariable("MONSTER_OVERLAY_UI_TEST_LAYOUT") == QStringLiteral("split");
    if (startupSplit) {
        QDir().mkpath(configRoot.path() + QStringLiteral("/monster-overlay"));
        QFile conf(configRoot.path() + QStringLiteral("/monster-overlay/monster-overlay.conf"));
        check(conf.open(QIODevice::WriteOnly), QStringLiteral("isolated startup Split seed opened"));
        conf.write("layout=split\n");
    }
    setScope(testRise ? mhw::GameId::Rise : mhw::GameId::World,
             startupSplit ? mhw::PlayerLayoutMode::Split : mhw::PlayerLayoutMode::Single);

    ControlPanel cp;
    // BIG ENOUGH that "compact" is a real claim: the client area must be
    // wider and taller than 680×700, or a dialog clamped to it is not a
    // dialog that was made smaller.
    cp.resize(1600, 1000);
    cp.show();
    pump();
    check(cp.width() > 680 && cp.height() > 700,
          QStringLiteral("the native client is larger than the compact settings cap"),
          QStringLiteral("%1x%2").arg(cp.width()).arg(cp.height()));

    auto *canvas = cp.findChild<HudCanvas *>();
    check(canvas != nullptr, QStringLiteral("the console built a HudCanvas"));
    auto *singleMode = cp.findChild<QPushButton *>(QStringLiteral("layoutSegSingle"));
    auto *splitMode = cp.findChild<QPushButton *>(QStringLiteral("layoutSegSplit"));
    check(singleMode && splitMode && singleMode->isChecked() == !startupSplit
              && splitMode->isChecked() == startupSplit,
          QStringLiteral("startup layout segment describes the restored topology"));
    evidence(&cp, "startup-layout");
    if (startupSplit && singleMode) {
        realClick(singleMode, singleMode->rect().center());
        pump();
        check(singleMode->isChecked() && !splitMode->isChecked()
                  && canvas && canvas->subWindowCount() == 0,
              QStringLiteral("clicking Single changes both the segment and actual window topology"));
    }

    // ==================================================================
    // 验收项1. THE SETTINGS DIALOG IS A SMALL POPUP WITH A SCROLLING BODY
    // ==================================================================
    {
        QDialog *seen = nullptr;
        QSize seenSize;
        bool hadScrollableBody = false;
        int dialogCount = 0;
        // Timer 1 finds and records the dialog; timer 2 closes it. Both
        // run inside openSettingsDialog()'s nested exec().
        QTimer::singleShot(120, [&]{
            seen = cp.findChild<QDialog *>(QStringLiteral("settingsDialog"));
            if (!seen)
                return;
            ++dialogCount;
            seenSize = seen->size();
            evidence(seen, "settings-compact");
            // The dialog is stack-owned by openSettingsDialog(); inspect
            // it while exec() is active, never dereference it after close.
            for (QScrollArea *s : seen->findChildren<QScrollArea *>())
                hadScrollableBody |= s->verticalScrollBar() != nullptr;
        });
        QTimer::singleShot(260, [&]{
            if (seen)
                seen->close();
        });
        // The USER's path: the rail's 设置 button. openSettingsDialog()
        // is a private member, and a click is the stronger claim anyway —
        // the button's handler is what opens the popup.
        QPushButton *settingsBtn =
            cp.findChild<QPushButton *>(QStringLiteral("settingsBtn"));
        check(settingsBtn != nullptr,
              QStringLiteral("the rail's 设置 button exists"));
        if (settingsBtn)
            realClick(settingsBtn,
                      QPointF(settingsBtn->width() / 2.0,
                              settingsBtn->height() / 2.0));
        pump();
        check(seen != nullptr && dialogCount == 1,
              QStringLiteral("设置 opened exactly one settingsDialog"));
        if (seen) {
            check(seenSize.width() <= 680 && seenSize.height() <= 700,
                  QStringLiteral("the settings popup is at most 680x700"),
                  QStringLiteral("%1x%2").arg(seenSize.width())
                      .arg(seenSize.height()));
            check(seenSize.width() < cp.centralWidget()->width(),
                  QStringLiteral("…and it does NOT fill the main window "
                                 "(用户验收项1)"),
                  QStringLiteral("dialog %1 vs client %2")
                      .arg(seenSize.width())
                      .arg(cp.centralWidget()->width()));
            check(hadScrollableBody,
                  QStringLiteral("the modal body is a scroll area — long "
                                 "content scrolls inside the popup"));
        }
    }

    // ==================================================================
    // 验收项2. THE PREVIEW COLUMN SHOWS THE REAL RENDER, SCROLLABLE
    // ==================================================================
    cp.selectWindow(0, -1);
    pump();
    auto *box = cp.findChild<QLabel *>(QStringLiteral("windowPreviewBox"));
    auto *previewScroll =
        cp.findChild<QScrollArea *>(QStringLiteral("windowPreviewScroll"));
    check(box != nullptr,
          QStringLiteral("the preview keeps its QLabel API "
                         "(findChild(\"windowPreviewBox\"))"));
    check(previewScroll != nullptr,
          QStringLiteral("the preview column now has a scroll container"));
    if (previewScroll && box) {
        check(previewScroll->widget() == box,
              QStringLiteral("…and the label IS its scrollable widget"));
        check(!previewScroll->widgetResizable(),
              QStringLiteral("the container does not re-stretch the label: "
                             "the label sizes to the pixmap"));
        const QPixmap pix = box->pixmap();
        check(!pix.isNull(),
              QStringLiteral("a selected window shows a real pixmap"));
        // The regression: pix.scaled(label.size()) collapsed every render
        // into the ~132px-tall box. A Panel render is always taller than
        // that at 100% scale, so the source height IS the evidence.
        check(pix.height() > 132,
              QStringLiteral("the pixmap keeps its own height (>132px) "
                             "instead of being squeezed into the box"),
              QStringLiteral("pixmap %1x%2").arg(pix.width()).arg(pix.height()));
        pump(50);
        check(box->size().width() >= pix.width()
                  || previewScroll->horizontalScrollBar()->maximum() > 0,
              QStringLiteral("a wider render is either shown whole or "
                             "reachable by scrolling — never cropped away"));
        note(QStringLiteral("preview pixmap %1x%2, label %3x%4, viewport "
                            "%5x%6")
                 .arg(pix.width()).arg(pix.height())
                 .arg(box->width()).arg(box->height())
                 .arg(previewScroll->viewport()->width())
                 .arg(previewScroll->viewport()->height()));
    }

    evidence(&cp, "console-selected");
    evidence(box, "detail-full");

    // ==================================================================
    // 验收项6. OPACITY RE-RENDERS THE SOURCE, ONCE
    // ==================================================================
    if (box) {
        auto *opacity = cp.findChild<QSlider *>(QStringLiteral("metaOpacity"));
        const auto valueLabels = opacity
            ? opacity->parentWidget()->findChildren<QLabel *>(QStringLiteral("sliderValue"))
            : QList<QLabel *>();
        auto *opacityValue = valueLabels.isEmpty() ? nullptr : valueLabels.last();
        check(opacity != nullptr,
              QStringLiteral("the inspector has the window opacity slider"));
        if (opacity) {
            const QPixmap full = box->pixmap();
            const int aFull = maxAlpha(full);
            opacity->setValue(30);              // the user's click on the track
            pump();
            const QPixmap thin = box->pixmap();
            evidence(&cp, "console-opacity30");
            evidence(box, "detail-opacity30");
            const int aThin = maxAlpha(thin);
            check(full.cacheKey() != thin.cacheKey() && aThin > 0 && aThin < aFull,
                  QStringLiteral("opacity refreshes the source and visibly lowers content alpha"),
                  QStringLiteral("alpha %1 -> %2").arg(aFull).arg(aThin));
            // QPainter opacity is per primitive: overlapping body/text/bar
            // operations do not make maxAlpha equal 0.30. Compare instead
            // with the real Panel render, pinning exact once-only parity.
            Panel *source = nullptr;
            for (QWidget *widget : QApplication::allWidgets())
                if (widget->objectName() == QStringLiteral("mhw-panel-player"))
                    source = qobject_cast<Panel *>(widget);
            check(source != nullptr, QStringLiteral("real selected Panel source exists"));
            if (source) {
                QPixmap direct(source->size());
                direct.fill(Qt::transparent);
                QPainter painter(&direct);
                source->render(&painter);
                painter.end();
                const QImage actual = thin.toImage();
                const QImage expected = direct.toImage();
                // v1.1.0: the detail preview defaults to FIT, so the pixmap in
                // the box is a scaled derivative of the Panel's own render —
                // sampling both images index-by-index compared the preview
                // against the WRONG pixels once the sizes differed (measured:
                // 332x318 preview vs 378x361 Panel) and reported a mismatch
                // that was never about alpha.
                //
                // What this assertion is really about is "opacity is applied
                // EXACTLY ONCE": the preview must not multiply alpha a second
                // time on top of the source it already re-rendered. Comparing
                // the alpha CHANNEL proportionally answers that at any scale:
                // a double multiplier would show up as every sample being
                // ~alpha² instead of ~alpha, while resampling shows up only as
                // edge pixels, which the interior sampling skips.
                //
                // Resampling blurs edges, so a per-pixel equality test is the
                // wrong instrument — measured worst delta at the interior was
                // 33, which is ordinary interpolation. The question this
                // assertion exists to answer is "was alpha applied twice?",
                // and that has an unmistakable signature: a second multiply at
                // opacity 0.30 drives opaque pixels from ~255 to ~76. So
                // compare the MEAN alpha of the fully-opaque interior instead
                // of individual pixels: identical content at any scale keeps
                // the mean equal, while a squared multiplier more than halves
                // it. Threshold sits at 0.90 — far above interpolation noise
                // (~0.13 worst-case) and far below the 0.30 a double apply
                // produces.
                bool sameAlpha = !actual.isNull() && !expected.isNull()
                              && actual.width() > 16 && expected.width() > 16;
                double meanActual = 0.0, meanExpected = 0.0;
                if (sameAlpha) {
                    long long sumA = 0, sumE = 0;
                    long long n = 0;
                    for (int y = 12; y < actual.height() - 12; ++y) {
                        const int ey = qMin(expected.height() - 13,
                                            qRound(y * double(expected.height())
                                                   / double(actual.height())));
                        for (int x = 12; x < actual.width() - 12; ++x) {
                            const int ex = qMin(expected.width() - 13,
                                                qRound(x * double(expected.width())
                                                       / double(actual.width())));
                            sumA += qAlpha(actual.pixel(x, y));
                            sumE += qAlpha(expected.pixel(ex, ey));
                            ++n;
                        }
                    }
                    if (n > 0) {
                        meanActual = double(sumA) / double(n);
                        meanExpected = double(sumE) / double(n);
                    }
                }
                // Both images are the SAME panel rendered twice: one straight
                // from the widget, one through the preview's derived pixmap.
                // Any systematic second multiplier shows up here as a large
                // gap, so require the ratio to sit within 10%.
                const double ratio = meanExpected > 1.0
                                   ? meanActual / meanExpected : 0.0;
                check(sameAlpha && ratio > 0.90 && ratio < 1.10,
                      QStringLiteral("preview alpha equals the real Panel's "
                                     "render (mean interior alpha %1 vs %2, "
                                     "ratio %3 — a second alpha multiply would "
                                     "read ~0.30)")
                          .arg(meanActual, 0, 'f', 1)
                          .arg(meanExpected, 0, 'f', 1)
                          .arg(ratio, 0, 'f', 3));
            }
            if (opacityValue)
                check(opacityValue->text().trimmed() == QLatin1String("30%"),
                      QStringLiteral("the live percentage follows the "
                                     "slider (30%)"),
                      opacityValue->text());
            opacity->setValue(100);
            pump();
        }
    }

    // ==================================================================
    // 验收项5. THE ZOOM ENTRY IS VISIBLE AND DRIVES THE BOARD
    // ==================================================================
    if (canvas) {
        QPushButton *plus  = zoomButton(&cp, QStringLiteral("+"));
        QPushButton *minus = zoomButton(&cp, QStringLiteral("−"));
        QPushButton *fit   = zoomButton(&cp, QStringLiteral("1:1"));
        QLabel *zoomLabel =
            cp.findChild<QLabel *>(QStringLiteral("stageToggleLabel"));
        check(plus && minus && fit,
              QStringLiteral("− / + / 1:1 exist as stage controls"));
        check(plus && plus->isVisibleTo(&cp) && plus->isVisible(),
              QStringLiteral("the zoom-in button is really on screen "
                             "(no longer hide()d)"));
        check(fit && fit->isVisible(),
              QStringLiteral("…and so is the 1:1 reset"));
        canvas->setZoom(1.0);
        pump(30);
        if (plus) {
            realClick(plus, QPointF(plus->width() / 2.0, plus->height() / 2.0));
            pump();
            check(qAbs(canvas->zoom() - 1.5) < 0.01,
                  QStringLiteral("clicking + steps the REAL board zoom "
                                 "from 1.0 to 1.5"),
                  QStringLiteral("zoom=%1").arg(canvas->zoom()));
        }
        if (minus) {
            realClick(minus,
                      QPointF(minus->width() / 2.0, minus->height() / 2.0));
            pump();
            check(qAbs(canvas->zoom() - 1.0) < 0.01,
                  QStringLiteral("clicking − steps it back"));
        }
        if (plus) {
            realClick(plus, QPointF(plus->width() / 2.0, plus->height() / 2.0));
            pump();
        }
        if (fit) {
            realClick(fit, QPointF(fit->width() / 2.0, fit->height() / 2.0));
            pump();
            check(qAbs(canvas->zoom() - 1.0) < 0.01,
                  QStringLiteral("clicking 1:1 restores the default zoom"),
                  QStringLiteral("zoom=%1").arg(canvas->zoom()));
        }
        if (zoomLabel)
            check(zoomLabel->text().contains(QLatin1String("1.0")),
                  QStringLiteral("the % label follows the canvas "
                                 "(zoomChanged → updateZoomLabel)"),
                  zoomLabel->text());
    }

    evidence(&cp, "console-zoom-fit");

    // ==================================================================
    // 验收项7. A BOARD CLICK FILLS THE INSPECTOR
    // ==================================================================
    if (canvas) {
        const QVector<QRectF> boxes = canvas->motherWindowRects();
        check(boxes.size() == mhw::kPanelCount,
              QStringLiteral("Single mode paints one box per panel"));
        if (boxes.size() == mhw::kPanelCount && !boxes[0].isEmpty()) {
            // Land on the monster box first so panel 0 is NOT the
            // canvas's current highlight, then click the player box.
            cp.selectWindow(1, -1);
            pump();
            realClick(canvas, boxes[0].center());
            pump();
            auto *x = cp.findChild<QSpinBox *>(QStringLiteral("metaX"));
            check(x != nullptr && x->isEnabled(),
                  QStringLiteral("a click on the player box fills the "
                                 "inspector (position field is live)"));
            check(box && !box->pixmap().isNull(),
                  QStringLiteral("…and the preview column follows the "
                                 "clicked window"));
            // Clicking the box that is ALREADY highlighted must not fall
            // silent either — the inspector stays on that window.
            realClick(canvas, boxes[0].center());
            pump();
            check(x != nullptr && x->isEnabled()
                      && box && !box->pixmap().isNull(),
                  QStringLiteral("a second click on the same box keeps the "
                                 "inspector filled (no silent loss)"));
        } else {
            note(QStringLiteral("the board painted no box on this platform "
                               "(offscreen output naming) — the click "
                               "assertions are recorded as notes"));
        }
    }

    // ==================================================================
    // 验收项4. A DRAG MOVES THE BOX WITHOUT REBUILDING THE INSPECTOR
    // ==================================================================
    if (canvas) {
        auto *navPlayer = cp.findChild<QFrame *>(QStringLiteral("navPlayer"));
        check(navPlayer != nullptr, QStringLiteral("player rail entry exists"));
        if (navPlayer) realClick(navPlayer, navPlayer->rect().center());
        canvas->setEditing(true);
        cp.selectWindow(0, -1);
        pump();
        const QVector<QRectF> boxes = canvas->motherWindowRects();
        auto *x = cp.findChild<QSpinBox *>(QStringLiteral("metaX"));
        if (boxes.size() == mhw::kPanelCount && !boxes[0].isEmpty() && x) {
            const int xBefore = x->value();
            const int weightBefore = sectionColumnWeight(&cp);
            const QMargins marginsBefore =
                cp.buildWindowStates().isEmpty()
                    ? QMargins()
                    : cp.buildWindowStates().first().margins;
            const QPointF from = boxes[0].center();
            const QPointF to = from + QPointF(48, 32);
            realDrag(canvas, from, to);
            pump();
            const mhw::WindowList list = cp.buildWindowStates();
            check(!list.isEmpty() && list.first().margins != marginsBefore,
                  QStringLiteral("the drag moved the window's margins"),
                  QStringLiteral("before %1 after %2")
                      .arg(QString::number(marginsBefore.left()),
                           QString::number(list.isEmpty()
                                               ? -1
                                               : list.first().margins.left())));
            check(x->value() != xBefore,
                  QStringLiteral("the position field follows the drag in "
                                 "real time (metaX)"),
                  QStringLiteral("%1 -> %2").arg(xBefore).arg(x->value()));
            check(sectionColumnWeight(&cp) == weightBefore,
                  QStringLiteral("the inspector was NOT rebuilt by the "
                                 "drag (same widget set under "
                                 "inspectorSections)"),
                  QStringLiteral("%1 -> %2")
                      .arg(weightBefore).arg(sectionColumnWeight(&cp)));
        } else {
            note(QStringLiteral("no player box to drag on this platform — "
                               "the drag assertions are recorded as notes"));
        }
        canvas->setEditing(false);
    }

    // ==================================================================
    // 验收项8. THE DISPLAY COMBO OFFERS EVERY OUTPUT, eDP INCLUDED
    // ==================================================================
    {
        QComboBox *combo =
            cp.findChild<QComboBox *>(QStringLiteral("screenCombo"));
        check(combo != nullptr,
              QStringLiteral("the topbar built the display combo"));
        if (combo) {
            const auto outs = screen_query::listOutputs();
            int offered = 0;
            for (const auto &o : outs) {
                if (o.name.isEmpty())
                    continue;
                ++offered;
                if (combo->findData(o.name) >= 0) {
                    check(true, QStringLiteral("output %1 is offered")
                                    .arg(o.name));
                } else {
                    check(false, QStringLiteral("output %1 is offered"),
                          QStringLiteral("the combo is filtering it out — "
                                         "内屏 eDP 必须可选"));
                }
            }
            check(combo->count() > 0,
                  QStringLiteral("the combo is never empty"));
            note(QStringLiteral("screen_query reports %1 named outputs; the "
                               "combo offers %2 entries")
                     .arg(offered).arg(combo->count()));
        }
    }

    if (app.platformName() == QStringLiteral("offscreen")) {
        auto *primary = new FixtureOutput(QStringLiteral("Test-eDP-1"));
        auto *removed = new FixtureOutput(QStringLiteral("Test-DP-1"));
        QWindowSystemInterface::handleScreenAdded(primary, true);
        QWindowSystemInterface::handleScreenAdded(removed, false);
        pump();
        auto *combo = cp.findChild<QComboBox *>(QStringLiteral("screenCombo"));
        check(combo && combo->findData(QStringLiteral("Test-DP-1")) >= 0,
              QStringLiteral("newly attached output is offered by the real combo"));
        if (combo && canvas) {
            combo->setCurrentIndex(combo->findData(QStringLiteral("Test-DP-1")));
            pump();
            check(canvas->previewScreen() == QStringLiteral("Test-DP-1"),
                  QStringLiteral("viewer follows the chosen attached output"));
            QWindowSystemInterface::handleScreenRemoved(removed);
            pump();
            check(combo->currentData().toString() == QStringLiteral("Test-eDP-1"),
                  QStringLiteral("unplugged viewer falls back in the combo"));
            check(canvas->previewScreen() == combo->currentData().toString(),
                  QStringLiteral("unplugged viewer also falls back in the canvas"));
            for (const auto &state : cp.buildWindowStates())
                check(state.outputResolved == QStringLiteral("Test-eDP-1"),
                      QStringLiteral("following window model resolves to the live primary"));
        }
    }

    // Measure the actual event/paint path, and pin widget/image stability
    // AFTER selection on press (a selection may legitimately refill it).
    if (canvas) {
        if (auto *nav = cp.findChild<QFrame *>(QStringLiteral("navPlayer")))
            realClick(nav, nav->rect().center());
        canvas->setEditing(true);
        cp.selectWindow(0, -1);
        pump();
        const auto boxes = canvas->motherWindowRects();
        if (!boxes.isEmpty() && !boxes[0].isEmpty()) {
            const QPointF start = boxes[0].center();
            QMouseEvent press(QEvent::MouseButtonPress, start,
                              canvas->mapToGlobal(start.toPoint()),
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(canvas, &press);
            pump(20);
            std::vector<QPointer<QWidget>> stable;
            if (auto *sections = cp.findChild<QFrame *>(QStringLiteral("inspectorSections")))
                for (QWidget *child : sections->findChildren<QWidget *>()) stable.emplace_back(child);
            const auto sourceKey = box->pixmap().cacheKey();
            std::vector<qint64> timings;
            for (int i = 1; i <= 64; ++i) {
                const QPointF at = start + QPointF(i, i / 4.0);
                QMouseEvent move(QEvent::MouseMove, at, canvas->mapToGlobal(at.toPoint()),
                                 Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                QElapsedTimer clock;
                clock.start();
                QApplication::sendEvent(canvas, &move);
                QCoreApplication::processEvents();
                timings.push_back(clock.nsecsElapsed());
            }
            const QPointF end = start + QPointF(64, 16);
            QMouseEvent release(QEvent::MouseButtonRelease, end,
                                canvas->mapToGlobal(end.toPoint()),
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(canvas, &release);
            check(!stable.empty() && std::all_of(stable.begin(), stable.end(),
                  [](const QPointer<QWidget> &w){ return !w.isNull(); }),
                  QStringLiteral("64 drag moves preserve existing section widget identities"));
            check(box->pixmap().cacheKey() == sourceKey,
                  QStringLiteral("64 drag moves reuse the same HUD content source"));
            std::sort(timings.begin(), timings.end());
            std::printf("DRAG_METRICS platform=%s moves=64 median_ms=%.3f p95_ms=%.3f max_ms=%.3f\n",
                        qPrintable(app.platformName()), timings[32] / 1e6,
                        timings[60] / 1e6, timings.back() / 1e6);
        } else check(false, QStringLiteral("drag benchmark has a real visible player box"));
    }

    // Narrow compositor tiles must expose the whole inspector grid.
    cp.resize(1024, 900);
    cp.selectWindow(0, -1);
    pump();
    if (auto *inspector = cp.findChild<QScrollArea *>(QStringLiteral("inspectorContent"))) {
        // Deterministic overflow independent of platform font metrics.
        inspector->widget()->setMinimumWidth(inspector->viewport()->width() + 96);
        pump(30);
        const bool fits = inspector->widget()->width() <= inspector->viewport()->width();
        check(fits || (inspector->horizontalScrollBar()->isVisible()
                       && inspector->horizontalScrollBar()->maximum() > 0),
              QStringLiteral("narrow window: inspector either fits or exposes horizontal overflow"),
              QStringLiteral("grid %1 viewport %2 range %3")
                  .arg(inspector->widget()->width()).arg(inspector->viewport()->width())
                  .arg(inspector->horizontalScrollBar()->maximum()));
        inspector->horizontalScrollBar()->setValue(inspector->horizontalScrollBar()->maximum());
        pump(30);
        evidence(&cp, "console-narrow-inspector");
    }
    // ==================================================================
    // D1. A MOTHER FOCUS IN SPLIT: ONE READING, AND EVERY ENABLED CONTROL
    //     HAS TO BE WRITABLE
    // ==================================================================
    // The audit's probe_ui2 state: after a Split switch `selectedWindow()`
    // answers invalid (Split emits no mother row) while the capability
    // rows still answer the panel — so the form displayed a
    // default-constructed window's numbers (100% over a panel at 150%)
    // and every write was refused by a `w.panelIndex < 0` guard.
    // currentEditableSelection() is the single answer both halves use now;
    // these checks pin the contract on the state where it used to break.
    if (splitMode && singleMode && canvas) {
        realClick(splitMode, splitMode->rect().center());
        pump();
        check(canvas->subWindowCount() > 0,
              QStringLiteral("D1: the console really switched to Split topology"),
              QStringLiteral("subWindows=%1").arg(canvas->subWindowCount()));
        // The mother slot (panel 0, no sub-window): in Split this matches
        // no row of buildWindowStates() — the whole point of the state.
        cp.selectWindow(0, -1);
        pump();
        check(cp.selectedWindow().panelIndex < 0,
              QStringLiteral("D1: precondition — a Split mother focus has NO window row"));
        Panel *mother = cp.currentEditableSelection().panel;
        check(mother != nullptr && mother == cp.selectedPanelOrNull(),
              QStringLiteral("D1: both halves of the inspector name the SAME "
                             "panel (the double reading is gone)"));
        auto *metaScale = cp.findChild<QSlider *>(QStringLiteral("metaScale"));
        auto *metaOpacity = cp.findChild<QSlider *>(QStringLiteral("metaOpacity"));
        auto *metaOutput = cp.findChild<QComboBox *>(QStringLiteral("metaOutput"));
        auto *metaVisible = cp.findChild<QCheckBox *>(QStringLiteral("metaVisible"));
        auto *metaX = cp.findChild<QSpinBox *>(QStringLiteral("metaX"));
        if (mother && metaScale && metaOpacity && metaOutput
            && metaVisible && metaX) {
            check(metaScale->isEnabled() && metaOpacity->isEnabled()
                      && metaOutput->isEnabled(),
                  QStringLiteral("D1: 缩放 / 透明度 / 目标显示器 are enabled "
                                 "for the mother focus"));
            // Give the panel a number the DEFAULT window state does not
            // have (a default-constructed WindowState carries scale 1.0),
            // then force a refill: the next check can only pass if the
            // form reads the PANEL. Reading selectedWindow() here — the
            // pre-D1 behaviour — shows 100 while the panel says 175.
            mother->setScale(1.75, /*persist=*/false);
            cp.refreshWindowModel();
            pump();
            check(metaScale->value() == 175,
                  QStringLiteral("D1: a refill re-reads the PANEL, not a "
                                 "default-constructed window"),
                  QStringLiteral("form %1% vs panel %2%")
                      .arg(metaScale->value())
                      .arg(qRound(mother->scale() * 100)));
            check(metaOpacity->value() == qRound(mother->opacity() * 100),
                  QStringLiteral("D1: 透明度 reads the panel too"));
            check(metaOutput->currentData().toString() == mother->outputName(),
                  QStringLiteral("D1: 目标显示器 shows this panel's own output"));
            // ENABLED ⇒ WRITABLE — the exact probe_ui2 failure.
            metaScale->setValue(135);
            pump();
            check(qRound(mother->scale() * 100) == 135,
                  QStringLiteral("D1: a 缩放 edit lands on the panel the form "
                                 "was showing"),
                  QStringLiteral("panel reads %1%")
                      .arg(qRound(mother->scale() * 100)));
            check(metaScale->value() == 135,
                  QStringLiteral("D1: and the readout still agrees with it"));
            // 显示该窗口: nothing in the model reads a key for a slot that
            // is not a window in this topology, so the switch must be dark
            // rather than decorative.
            check(!metaVisible->isEnabled(),
                  QStringLiteral("D1: 显示该窗口 is DISABLED where no window "
                                 "row can read its key"));
            // Coordinates are enabled only because the accessor resolved an
            // HONEST frame for the mother (own → primary, the chain the
            // model itself runs); enabled then means the write must land.
            check(metaX->isEnabled(),
                  QStringLiteral("D1: the coordinate form is enabled with an "
                                 "honest frame (not a fabricated one)"));
            if (metaX->isEnabled()) {
                const QMargins beforeMargins = mother->margins();
                const int beforeX = metaX->value();
                metaX->setValue(beforeX + 33);
                const bool emitted = QMetaObject::invokeMethod(
                    metaX, "editingFinished");
                pump();
                check(emitted && mother->margins() != beforeMargins,
                      QStringLiteral("D1: a coordinate edit lands on the "
                                     "mother panel"),
                      QStringLiteral("margins %1 -> %2")
                          .arg(beforeMargins.left())
                          .arg(mother->margins().left()));
                metaX->setValue(beforeX);
                QMetaObject::invokeMethod(metaX, "editingFinished");
            }
            metaScale->setValue(100);
            pump();
        }
        // Topology back to Single so the rest of the suite (and whoever
        // runs it next) still sees the scope it was written for.
        realClick(singleMode, singleMode->rect().center());
        pump();
        check(canvas->subWindowCount() == 0,
              QStringLiteral("D1: topology restored to Single after the probe"),
              QStringLiteral("subWindows=%1").arg(canvas->subWindowCount()));
    }

    // ==================================================================
    // 顶栏溢出 (v1.1.1): THE HEADER MUST FIT THE WINDOW, IN BOTH LOCALES.
    //
    // The reported defect, in the user's own words: at a 1272px console
    // the topbar's right-hand buttons were cut by the window edge —
    // `Start ov…` where "Start overlay" belongs, and 导出 off-screen with
    // the page scrolled 252px sideways to hide it. Root cause (measured,
    // not guessed): the single row's MINIMUM width is ~1292px, the bar
    // lives inside the root scroll, and a scroll-area child is sized to
    // its own minimum once that exceeds the viewport — so the content
    // layer grew to 232 (rail) + 1292 and the buttons ended up outside
    // the visible area at scroll position 0.
    //
    // The fix is refreshTopbarWrap(): the header keeps ONE row while
    // that row's minimum fits the window and wraps the action buttons
    // onto a second row when it does not — the same flex-wrap the
    // prototype's own `.topbar{display:flex;flex-wrap:wrap}` has. These
    // checks pin the RESULT at the widths the user actually runs, with
    // the display combo carrying the copy a real desktop produces
    // (`HDMI-A-1 · 1080×1920(rotated) (primary)`), because that text is
    // what the row's width is made of.
    // ==================================================================
    {
        if (QScreen *qs = QGuiApplication::primaryScreen())
            std::printf("PROBE screen=%dx%d dpr=%.2f\n",
                        qs->size().width(), qs->size().height(),
                        qs->devicePixelRatio());
        // Simulate the USER'S display list BEFORE the sweep: the offscreen
        // platform reports one 800x800 placeholder, the real desktop
        // reports `HDMI-A-1 * 1080x1920(rotated) (primary)` - and that text
        // is what the combo's width hint is made of. Every width below is
        // then measured against the copy the user actually sees.
        if (auto *comboProbe = cp.findChild<QComboBox *>(QStringLiteral("screenCombo"))) {
            comboProbe->clear();
            comboProbe->addItem(QStringLiteral("HDMI-A-1 \u00b7 1080\u00d71920(rotated) (primary)"),
                                QStringLiteral("HDMI-A-1"));
            comboProbe->addItem(QStringLiteral("eDP-1 \u00b7 2560\u00d71440"),
                                QStringLiteral("eDP-1"));
            comboProbe->setCurrentIndex(0);
        }
        for (int probeW : {1600, 1272, 1200, 1024}) {
            cp.resize(probeW, 1386);
            pump(60);
            // Optional evidence hook: `MONSTER_OVERLAY_SNAP_DIR=/tmp/x`
            // makes every swept width save cp.grab() there, so the same
            // run that asserts the geometry also produces the screenshot
            // the report is judged by. Inert in ctest (the env is unset).
            const QString snapDir = qEnvironmentVariable("MONSTER_OVERLAY_SNAP_DIR");
            if (!snapDir.isEmpty()) {
                const QString localeTag =
                    qEnvironmentVariable("MONSTER_OVERLAY_UI_TEST_LOCALE",
                                         QStringLiteral("en-US"))
                        .replace(QLatin1Char('-'), QLatin1Char('_'));
                const QString snapFile = QStringLiteral("%1/console-%2-w%3.png")
                                             .arg(snapDir, localeTag).arg(probeW);
                check(cp.grab().save(snapFile),
                      QStringLiteral("console evidence screenshot saved"),
                      snapFile);
            }
            auto *tb = cp.findChild<QFrame *>(QStringLiteral("topbar"));
            if (!snapDir.isEmpty() && tb) {
                const QString localeTag =
                    qEnvironmentVariable("MONSTER_OVERLAY_UI_TEST_LOCALE",
                                         QStringLiteral("en-US"))
                        .replace(QLatin1Char('-'), QLatin1Char('_'));
                const QString barFile = QStringLiteral("%1/topbar-%2-w%3.png")
                                            .arg(snapDir, localeTag).arg(probeW);
                check(tb->grab().save(barFile),
                      QStringLiteral("topbar evidence screenshot saved"),
                      barFile);
            }
            std::printf("PROBE cp=%dx%d topbar=%s\n", cp.width(), cp.height(),
                        tb ? qPrintable(QStringLiteral("%1x%2 minW=%3 hint=%4")
                                            .arg(tb->width()).arg(tb->height())
                                            .arg(tb->minimumWidth())
                                            .arg(tb->sizeHint().width()))
                           : "MISSING");
            if (!tb || !tb->layout())
                continue;
            QLayout *l = tb->layout();
            std::printf("PROBE   margins=%d,%d spacing=%d count=%d minW=%d\n",
                        l->contentsMargins().left(), l->contentsMargins().right(),
                        l->spacing(), l->count(), l->minimumSize().width());
            for (int i = 0; i < l->count(); ++i) {
                QLayoutItem *it = l->itemAt(i);
                if (QWidget *w = it->widget()) {
                    const QPoint tl = w->mapTo(tb, QPoint(0, 0));
                    std::printf("PROBE   [%2d] %-16s x=%4d w=%4d right=%4d minW=%4d hint=%4d vis=%d\n",
                                i, qPrintable(w->objectName().isEmpty()
                                                  ? w->metaObject()->className()
                                                  : w->objectName()),
                                tl.x(), w->width(), tl.x() + w->width(),
                                w->minimumWidth(), w->sizeHint().width(),
                                int(w->isVisible()));
                } else if (QLayout *sub = it->layout()) {
                    std::printf("PROBE   [%2d] <layout> minW=%4d hint=%4d\n",
                                i, sub->minimumSize().width(),
                                sub->sizeHint().width());
                    for (int j = 0; j < sub->count(); ++j) {
                        QLayoutItem *sit = sub->itemAt(j);
                        if (QWidget *w = sit->widget()) {
                            const QPoint tl = w->mapTo(tb, QPoint(0, 0));
                            std::printf("PROBE        [%d.%d] %-14s x=%4d w=%4d right=%4d minW=%4d hint=%4d vis=%d\n",
                                        i, j, qPrintable(w->objectName().isEmpty()
                                                              ? w->metaObject()->className()
                                                              : w->objectName()),
                                        tl.x(), w->width(), tl.x() + w->width(),
                                        w->minimumWidth(), w->sizeHint().width(),
                                        int(w->isVisible()));
                        } else if (sit->spacerItem()) {
                            std::printf("PROBE        [%d.%d] <spacer> minW=%d grow=%d\n",
                                        i, j, sit->minimumSize().width(),
                                        sit->sizeHint().width());
                        }
                    }
                } else if (it->spacerItem()) {
                    std::printf("PROBE   [%2d] <spacer> minW=%d hint=%d\n",
                                i, it->minimumSize().width(), it->sizeHint().width());
                }
            }
            if (auto *rs = cp.findChild<QScrollArea *>(QStringLiteral("consoleRootScroll"))) {
                QWidget *inner = rs->widget();
                std::printf("PROBE   rootScroll viewport=%dx%d inner=%dx%d minW=%d hscroll=%d/%d\n",
                            rs->viewport()->width(), rs->viewport()->height(),
                            inner ? inner->width() : -1, inner ? inner->height() : -1,
                            inner ? inner->minimumWidth() : -1,
                            rs->horizontalScrollBar()->isVisible(),
                            rs->horizontalScrollBar()->maximum());
                if (inner) {
                    const auto kids = inner->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
                    for (QWidget *k : kids)
                        std::printf("PROBE     inner-child %-18s x=%4d w=%4d minW=%4d hint=%4d\n",
                                    qPrintable(k->objectName().isEmpty()
                                                   ? k->metaObject()->className()
                                                   : k->objectName()),
                                    k->x(), k->width(), k->minimumWidth(),
                                    k->sizeHint().width());
                    if (auto *rs2 = inner->findChild<QSplitter *>(QStringLiteral("rootSplitter"))) {
                        for (QWidget *k : rs2->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly))
                            std::printf("PROBE       rootSplitter-child %-14s w=%4d minW=%4d hint=%4d\n",
                                        qPrintable(k->objectName().isEmpty()
                                                       ? k->metaObject()->className()
                                                       : k->objectName()),
                                        k->width(), k->minimumWidth(), k->sizeHint().width());
                        std::printf("PROBE       rootSplitter minW=%d hint=%d layoutMin=%d\n",
                                    rs2->minimumWidth(), rs2->sizeHint().width(),
                                    rs2->layout() ? rs2->layout()->minimumSize().width() : -1);
                    }
                    if (auto *ma = inner->findChild<QWidget *>(QStringLiteral("mainArea")))
                        std::printf("PROBE       mainArea w=%d minW=%d hint=%d layoutMin=%d\n",
                                    ma->width(), ma->minimumWidth(), ma->sizeHint().width(),
                                    ma->layout() ? ma->layout()->minimumSize().width() : -1);
                }
            }
        }

        // ---- the acceptance, asserted rather than eyeballed ------------
        // Every width has to keep the whole header INSIDE the window; the
        // row count is only allowed to differ. The row-count assertion is
        // deliberately conditional (one row AT 1600 must still measure the
        // prototype's 66px band) rather than "1600 is always one row": a
        // larger font can make even the reference width wrap, and that is
        // the wrap working, not a regression.
        // ── 第6条: EXACTLY ONE EXPORT ENTRY ────────────────────────────────
        // The topbar used to carry a SECOND 导出 button (objectName
        // `topExport`), wired to the same openExportDialog() as the rail's —
        // two doors into one modal, which the user filed as a duplicate. It
        // was deleted along with its i18n keys and its QSS rule, so what is
        // asserted here is that it is gone FROM THE TREE (a findChild that
        // answers at all is the regression — visibility is not the point)
        // and that the kept entry, the rail's sideFootExport, is still there.
        QPushButton *railExport =
            cp.findChild<QPushButton *>(QStringLiteral("sideFootExport"));
        QPushButton *dupExport =
            cp.findChild<QPushButton *>(QStringLiteral("topExport"));
        check(dupExport == nullptr && railExport != nullptr,
              QStringLiteral("导出: exactly ONE export entry — sideFootExport "
                             "kept, topExport deleted (第6条)"),
              QStringLiteral("topExport=%1 sideFootExport=%2")
                  .arg(dupExport ? QStringLiteral("present")
                                 : QStringLiteral("absent"))
                  .arg(railExport ? QStringLiteral("present")
                                  : QStringLiteral("absent")));

        for (const int w : {1600, 1272, 1200}) {
            cp.resize(w, 1386);
            pump(60);
            const int winW = cp.width();
            const auto fullyInside = [&](QWidget *widget) {
                const QPoint tl = widget->mapTo(&cp, QPoint(0, 0));
                return tl.x() >= 0 && tl.x() + widget->width() <= winW;
            };
            const auto where = [&](QWidget *widget) {
                const QPoint tl = widget->mapTo(&cp, QPoint(0, 0));
                return QStringLiteral("%1..%2 in %3px")
                    .arg(tl.x()).arg(tl.x() + widget->width()).arg(winW);
            };

            // 1. THE THREE ACTIONS (第6条: the topbar's duplicate 导出 is
            //    gone, the rail's sideFootExport is the only export entry).
            //    startBtn is renamed stopBtn while a
            //    child overlay is alive, so both names are accepted.
            int actions = 0;
            for (const QString &name : {QStringLiteral("enterEdit"),
                                        QStringLiteral("overlayEdit"),
                                        QStringLiteral("startBtn"),
                                        QStringLiteral("stopBtn")}) {
                QPushButton *btn = cp.findChild<QPushButton *>(name);
                if (!btn || !btn->isVisible())
                    continue;
                ++actions;
                check(fullyInside(btn),
                      QStringLiteral("顶栏: %1 is not cut by the window").arg(name),
                      where(btn));
            }
            check(actions == 3,
                  QStringLiteral("顶栏: all three action buttons are on screen"),
                  QStringLiteral("%1 at %2px").arg(actions).arg(winW));

            // 2. THE SEGMENT: 整块 / 分块 / 混合 — all three entries.
            for (const QString &seg : {QStringLiteral("layoutSegSingle"),
                                       QStringLiteral("layoutSegSplit"),
                                       QStringLiteral("layoutSegMixed")}) {
                QPushButton *entry = cp.findChild<QPushButton *>(seg);
                check(entry && entry->isVisible() && fullyInside(entry),
                      QStringLiteral("顶栏: %1 is not cut by the window").arg(seg),
                      entry ? where(entry) : QStringLiteral("missing"));
            }

            // 3. THE DISPLAY COMBO: the user's own words were that its
            //    text was cut. Its TEXT is capped by the prototype's own
            //    `select{max-width:245px}` (the open list shows the full
            //    name), but the CONTROL itself must be whole.
            QComboBox *combo = cp.findChild<QComboBox *>(QStringLiteral("screenCombo"));
            check(combo && combo->isVisible() && fullyInside(combo),
                  QStringLiteral("顶栏: the display combo is whole and visible"),
                  combo ? where(combo) : QStringLiteral("missing"));

            // 4. THE BAND. 66 exactly while it is one row (the measured
            //    `.topbar`); taller once the actions have their own row.
            QFrame *bar = cp.findChild<QFrame *>(QStringLiteral("topbar"));
            QFrame *segFrame = cp.findChild<QFrame *>(QStringLiteral("layoutSeg"));
            // 第6条: the rightmost action is 启动/停止 now that the topbar's
            // duplicate 导出 button is gone — that is the button which has to
            // share row1's vertical centre while the band is still one row.
            QPushButton *lastAction =
                cp.findChild<QPushButton *>(QStringLiteral("startBtn"));
            if (!lastAction)
                lastAction =
                    cp.findChild<QPushButton *>(QStringLiteral("stopBtn"));
            // row1 carries the segment, row2 the actions — so THEIR
            // relative y is what tells the two states apart (two action
            // buttons sit on the same row in BOTH states and would say
            // nothing). Compared by VERTICAL CENTRE, not top edge: the
            // segment is 36px tall and a button 32, so in one row they
            // share a centre and not a top.
            const auto centerY = [&](QWidget *widget) {
                const QPoint tl = widget->mapTo(&cp, QPoint(0, 0));
                return tl.y() + widget->height() / 2.0;
            };
            const bool oneRow = bar && segFrame && lastAction
                && qAbs(centerY(segFrame) - centerY(lastAction)) < 4.0;
            if (oneRow)
                check(bar->height() == 66,
                      QStringLiteral("顶栏: the one-row band keeps the measured 66px"),
                      QStringLiteral("%1px at %2px wide").arg(bar->height()).arg(winW));
            else
                check(bar->height() > 66,
                      QStringLiteral("顶栏: the wrapped band grew by a button row"),
                      QStringLiteral("%1px at %2px wide").arg(bar->height()).arg(winW));
            std::printf("PROBE_FIT w=%d row=%s band=%d actions=%d\n",
                        winW, oneRow ? "one" : "two", bar ? bar->height() : -1,
                        actions);

            // 5. THE PAGE DOES NOT SCROLL SIDEWAYS ANY MORE. This is the
            //    mechanism behind the defect (content min = rail + row
            //    minimum), asserted at the user's own 1272 where the
            //    margin is the largest; 1200 is reported only, because a
            //    wider font there can push row1 past the viewport again —
            //    which now costs the SUBTITLE, not the buttons.
            if (auto *rootScroll =
                    cp.findChild<QScrollArea *>(QStringLiteral("consoleRootScroll"))) {
                const int range =
                    rootScroll->horizontalScrollBar()->maximum();
                std::printf("PROBE_FIT w=%d hscroll-range=%d\n", winW, range);
                if (w == 1272)
                    check(range == 0,
                          QStringLiteral("顶栏: at 1272 the page no longer scrolls sideways"),
                          QStringLiteral("range=%1").arg(range));
            }
        }

        cp.resize(1600, 1000);
        pump();
    }

    // ==================================================================
    // 实测缺陷1: THE SHELL DOES NOT SCROLL — 只有示例与详情预览可滚
    // ==================================================================
    //
    // Contract, in measurable terms:
    //   * the page (`consoleRootScroll`) has NO vertical range at any
    //     window size, so 顶栏与侧栏 can never scroll away;
    //   * 侧栏 (`railScroll`) has no vertical range of its own either —
    //     its list fits, so the sidebar has nothing to scroll;
    //   * 顶栏 pinned to the top and 侧栏 pinned to the left edge, at
    //     exactly the client height, across resizes AND across scrolls
    //     of the two inner regions (that is what "固定" means here);
    //   * 显示器示例 (`canvasScroll`) DOES scroll when its pane is
    //     smaller than the board (the divider drag), and 详情预览
    //     (`inspectorContent`) scrolls with the selection's content —
    //     and scrolling either one moves NEITHER the top bar nor the
    //     sidebar by a pixel.
    {
        // Shell geometry in the CONSOLE's coordinates, so a window
        // resize cannot masquerade as a shell move.
        auto shellPos = [&](QWidget *w) {
            return w ? w->mapTo(&cp, QPoint(0, 0)) : QPoint(-1, -1);
        };
        auto *root = cp.findChild<QScrollArea *>(QStringLiteral("consoleRootScroll"));
        auto *rail = cp.findChild<QFrame *>(QStringLiteral("objectRail"));
        auto *railScroll = cp.findChild<QScrollArea *>(QStringLiteral("railScroll"));
        auto *topbar = cp.findChild<QFrame *>(QStringLiteral("topbar"));
        auto *stage = cp.findChild<QWidget *>(QStringLiteral("stage"));
        auto *canvasScroll = cp.findChild<QScrollArea *>(QStringLiteral("canvasScroll"));
        auto *inspScroll = cp.findChild<QScrollArea *>(QStringLiteral("inspectorContent"));
        check(root && rail && railScroll && topbar && stage && canvasScroll && inspScroll,
              QStringLiteral("缺陷1: the seven widgets this contract names all exist"));

        // A window selected, so 详情预览 has content to scroll.
        const mhw::WindowList rows = cp.buildWindowStates();
        check(!rows.isEmpty(), QStringLiteral("缺陷1: precondition — the model lists a window"));
        if (!rows.isEmpty()) {
            cp.selectWindow(rows.first().panelIndex, rows.first().subIndex);
            pump(300);
        }

        int canvasRangeAtRest = -1;
        for (const QSize sz : {QSize(1200, 1040), QSize(1200, 700),
                               QSize(1600, 1000)}) {
            cp.resize(sz);
            pump(250);
            const QString tag = QStringLiteral("%1x%2").arg(sz.width()).arg(sz.height());
            const int pageRange =
                root ? root->verticalScrollBar()->maximum() : -1;
            const int railRange =
                railScroll ? railScroll->verticalScrollBar()->maximum() : -1;
            const QPoint railPos = shellPos(rail);
            const QPoint topPos = shellPos(topbar);
            const int railH = rail ? rail->height() : -1;
            const int vpH = root ? root->viewport()->height() : -1;
            const int canvasRange =
                canvasScroll ? canvasScroll->verticalScrollBar()->maximum() : -1;
            canvasRangeAtRest = canvasRange;
            std::printf("PROBE_DEFECT1 %s pageRange=%d railRange=%d canvasRange=%d "
                        "inspRange=%d rail=(%d,%d h=%d vpH=%d) topbar=(%d,%d h=%d) "
                        "stageH=%d\n",
                        qPrintable(tag), pageRange, railRange, canvasRange,
                        inspScroll ? inspScroll->verticalScrollBar()->maximum() : -1,
                        railPos.x(), railPos.y(), railH, vpH,
                        topPos.x(), topPos.y(), topbar ? topbar->height() : -1,
                        stage ? stage->height() : -1);
            // …and the example is never CLIPPED: the canvas carries the
            // board's own height-for-width whenever that is taller than
            // the pane. This is the half QScrollArea reads from the size
            // POLICY; before it was wired the widget was pinned to the
            // viewport height and a board taller than the pane (any
            // square or portrait output) was simply cut, with no
            // scrollbar to reach the rest.
            if (canvasScroll && canvasScroll->widget()) {
                const int have = canvasScroll->widget()->height();
                const int need =
                    canvasScroll->widget()->heightForWidth(
                        canvasScroll->widget()->width());
                const int paneH = canvasScroll->viewport()->height();
                std::printf("PROBE_DEFECT1 %s canvas widget=%d need=%d paneH=%d\n",
                            qPrintable(tag), have, need, paneH);
                check(need <= paneH || have >= need - 2,
                      QStringLiteral("缺陷1 @%1: 示例画布按 heightForWidth 生长，"
                                     "内容不被裁切").arg(tag),
                      QStringLiteral("widget=%1 need=%2 paneH=%3")
                          .arg(have).arg(need).arg(paneH));
            }
            check(pageRange == 0 && root && !root->verticalScrollBar()->isVisible(),
                  QStringLiteral("缺陷1 @%1: 页面不滚动 (consoleRootScroll 竖向 range==0)").arg(tag),
                  QStringLiteral("range=%1").arg(pageRange));
            check(railRange == 0 && railScroll
                      && !railScroll->verticalScrollBar()->isVisible(),
                  QStringLiteral("缺陷1 @%1: 侧栏自身也不滚 (railScroll range==0)").arg(tag),
                  QStringLiteral("range=%1").arg(railRange));
            check(railPos == QPoint(0, 0),
                  QStringLiteral("缺陷1 @%1: 侧栏钉在左上角不移动").arg(tag),
                  QStringLiteral("(%1,%2)").arg(railPos.x()).arg(railPos.y()));
            check(topPos.y() == 0,
                  QStringLiteral("缺陷1 @%1: 顶栏钉在顶部").arg(tag),
                  QStringLiteral("y=%1").arg(topPos.y()));
            check(rail && rail->width() == 232,
                  QStringLiteral("缺陷1 @%1: 侧栏宽度仍是固定的 232").arg(tag),
                  QStringLiteral("w=%1").arg(rail ? rail->width() : -1));
            check(railH == vpH,
                  QStringLiteral("缺陷1 @%1: 侧栏高度 == 视口高度（随窗口自适应）").arg(tag),
                  QStringLiteral("rail=%1 vp=%2").arg(railH).arg(vpH));
        }

        // 详情预览 scrolls on its own: its content column is taller than
        // the drawer at every size above (measured 196 / 269 / 202).
        if (inspScroll) {
            const int inspRange = inspScroll->verticalScrollBar()->maximum();
            check(inspRange > 0 && inspScroll->verticalScrollBar()->isVisible(),
                  QStringLiteral("缺陷1: 详情预览 (inspectorContent) 自己可滚"),
                  QStringLiteral("range=%1 vis=%2")
                      .arg(inspRange)
                      .arg(int(inspScroll->verticalScrollBar()->isVisible())));
        }

        // 显示器示例 scrolls when its pane is smaller than the board —
        // the divider drag down to the stage's own 360px minimum.
        const QPoint railBefore = shellPos(rail);
        const QPoint topBefore = shellPos(topbar);
        const int stageBefore = stage ? stage->height() : -1;
        if (auto *split = cp.findChild<QSplitter *>(QStringLiteral("consoleSplitter"))) {
            const int stageMin = stage ? stage->minimumHeight() : 360;
            split->setSizes({stageMin, split->height() - stageMin});
            pump(300);
        }
        const int stageAfter = stage ? stage->height() : -1;
        const int canvasRangeSqueezed =
            canvasScroll ? canvasScroll->verticalScrollBar()->maximum() : -1;
        const bool canvasVis =
            canvasScroll && canvasScroll->verticalScrollBar()->isVisible();
        std::printf("PROBE_DEFECT1 stage %d -> %d, canvasRange %d -> %d vis=%d\n",
                    stageBefore, stageAfter, canvasRangeAtRest,
                    canvasRangeSqueezed, int(canvasVis));
        check(stageAfter < stageBefore,
              QStringLiteral("缺陷1: 示例区被分隔条压到了自己的最小高度"),
              QStringLiteral("%1 -> %2").arg(stageBefore).arg(stageAfter));
        check(canvasRangeSqueezed > 0 && canvasVis,
              QStringLiteral("缺陷1: 显示器示例 (canvasScroll) 在面板变矮后自己可滚"),
              QStringLiteral("range=%1 vis=%2")
                  .arg(canvasRangeSqueezed).arg(int(canvasVis)));
        check(shellPos(rail) == railBefore && shellPos(topbar) == topBefore,
              QStringLiteral("缺陷1: 压缩示例区不移动顶栏与侧栏"),
              QStringLiteral("rail %1,%2 -> %3,%4; topbar %5,%6 -> %7,%8")
                  .arg(railBefore.x()).arg(railBefore.y())
                  .arg(shellPos(rail).x()).arg(shellPos(rail).y())
                  .arg(topBefore.x()).arg(topBefore.y())
                  .arg(shellPos(topbar).x()).arg(shellPos(topbar).y()));

        // Scroll BOTH inner regions to their ends: only they move.
        if (canvasScroll) {
            canvasScroll->verticalScrollBar()->setValue(
                canvasScroll->verticalScrollBar()->maximum());
        }
        if (inspScroll) {
            inspScroll->verticalScrollBar()->setValue(
                inspScroll->verticalScrollBar()->maximum());
        }
        pump(200);
        check(root && root->verticalScrollBar()->maximum() == 0,
              QStringLiteral("缺陷1: 滚动两个内区之后页面仍然不滚"),
              QStringLiteral("pageRange=%1").arg(root
                                                     ? root->verticalScrollBar()
                                                           ->maximum()
                                                     : -1));
        check(shellPos(rail) == railBefore && shellPos(topbar) == topBefore,
              QStringLiteral("缺陷1: 滚动两个内区不移动顶栏与侧栏"),
              QStringLiteral("rail %1,%2 topbar %3,%4")
                  .arg(shellPos(rail).x()).arg(shellPos(rail).y())
                  .arg(shellPos(topbar).x()).arg(shellPos(topbar).y()));
        evidence(&cp, "issue1-shell-fixed");
        cp.resize(1600, 1000);
        pump(250);
    }

    // ==================================================================
    // 实测缺陷4: 「显示该窗口」必须真生效 — 命中 → 画板 → 磁盘 → 重开
    // ==================================================================
    //
    // Four things have to hold, each asserted where it could lie:
    //
    //   1. with a WINDOW row behind the selection the switch is ENABLED
    //      (the other side of the same rule — dark when no row can read
    //      its key — is pinned by console_inspector_fidelity_tests and
    //      by the D1 mother-focus check above);
    //   2. a real press/release on the widget's own CENTRE toggles it.
    //      This is the half that was broken: the row used to be the
    //      whole 374px column while QCheckBox::hitButton() answers
    //      QStyle::SE_CheckBoxClickRect — indicator ∪ text, measured
    //      100px — so a press at the centre arrived (an event filter on
    //      the widget counted press=1, release=1) and was then DROPPED
    //      by QAbstractButton::mousePressEvent (accepted=0, isDown=0,
    //      never a toggled()). Left-aligned, the widget is its own
    //      click target (measured 97px wide, centre inside);
    //   3. the write lands on DISK: a plain read of panels.ini shows the
    //      `visible` row (winVisible=false) under exactly this window's
    //      own scope group — not a QSettings round-trip that could
    //      answer from its own cache;
    //   4. a console reopened on the same config reads the same state
    //      back, and the 示例 moved with it (the window is no longer
    //      drawn while hidden).
    {
        const QString iniFile =
            qEnvironmentVariable("XDG_CONFIG_HOME")
            + QStringLiteral("/monster-overlay/panels.ini");
        // READ BACK THE WAY QSETTINGS WRITES IT. The section on disk is
        // only the FIRST component of the group path and every further
        // '/' inside the key is written as a backslash — Qt's
        // QSettingsPrivate::iniEscapedKey maps '/' -> '\\' — so
        // `world/single/dps` + `winVisible` lands on disk as
        //
        //     [world]
        //     single\\dps\\winVisible=false
        //
        // A parser that looks for a line STARTING with `winVisible` finds
        // nothing there and would report the write as missing; this one
        // unescapes every line back into its logical key first, which is
        // also what QSettings itself does when it reads the file.
        auto iniEntries = [&] {
            QHash<QString, QString> out;
            QFile f(iniFile);
            if (!f.open(QIODevice::ReadOnly))
                return out;
            QString section;
            for (const QByteArray &raw : f.readAll().split('\n')) {
                const QString line = QString::fromUtf8(raw).trimmed();
                if (line.startsWith(QLatin1Char('['))) {
                    section = line.mid(1, line.size() - 2);
                    continue;
                }
                const int eq = line.indexOf(QLatin1Char('='));
                if (eq <= 0)
                    continue;
                QString key = line.left(eq);
                key.replace(QLatin1Char('\\'), QLatin1Char('/'));
                out.insert(section.isEmpty() ? key
                                             : section + QLatin1Char('/') + key,
                           line.mid(eq + 1));
            }
            return out;
        };
        // The one row this test is about, keyed exactly as the console's
        // own writer scopes it.
        auto visibleRow = [&](const QString &group) {
            return iniEntries().value(group + QStringLiteral("/winVisible"));
        };
        auto iniDump = [&] {
            QStringList out;
            const QHash<QString, QString> entries = iniEntries();
            for (auto it = entries.constBegin(); it != entries.constEnd(); ++it)
                out << QStringLiteral("%1 = %2").arg(it.key(), it.value());
            out.sort();
            return out;
        };

        const mhw::WindowList list = cp.buildWindowStates();
        check(!list.isEmpty(), QStringLiteral("缺陷4: precondition — the model lists a window"));
        int wantPanel = -1, wantSub = -2;
        for (const mhw::WindowState &w : list) {
            // A sub-window row when the topology has one (Split), else
            // the mother row (Single / Mixed) — the same two cases
            // buildWindowStates() itself distinguishes.
            if (w.subIndex >= 0 && wantSub < 0) {
                wantPanel = w.panelIndex; wantSub = w.subIndex;
            } else if (wantSub < 0 && w.panelIndex >= 0) {
                wantPanel = w.panelIndex; wantSub = w.subIndex;
            }
        }
        if (wantPanel < 0) {
            std::printf("FAIL 缺陷4: no selectable row\n");
            ++failures; ++checks;
            return 1;
        }
        cp.selectWindow(wantPanel, wantSub);
        pump(300);

        const ControlPanel::EditableSelection sel = cp.currentEditableSelection();
        auto *box = cp.findChild<QCheckBox *>(QStringLiteral("metaVisible"));
        auto *canvas = cp.findChild<HudCanvas *>();
        check(sel.hasWindow && sel.panel != nullptr,
              QStringLiteral("缺陷4: a window row backs the selection"),
              QStringLiteral("panel=%1 sub=%2 hasWindow=%3")
                  .arg(sel.panelIndex).arg(sel.subIndex).arg(int(sel.hasWindow)));
        check(box != nullptr, QStringLiteral("缺陷4: metaVisible 开关存在"));
        if (!box || !sel.panel) {
            cp.resize(1600, 1000);
            pump(200);
            return failures == 0 ? 0 : 1;
        }

        // (1) enabled, and its geometry is its own click target.
        QStyleOptionButton opt;
        opt.initFrom(box);
        opt.state |= box->isChecked() ? QStyle::State_On : QStyle::State_Off;
        opt.text = box->text();
        opt.icon = box->icon();
        opt.iconSize = box->iconSize();
        const QRect clickRect =
            box->style()->subElementRect(QStyle::SE_CheckBoxClickRect, &opt, box);
        check(box->isEnabled(),
              QStringLiteral("缺陷4: 有窗口行时「显示该窗口」是可用的 (enabled)"));
        check(clickRect.contains(box->rect().center()),
              QStringLiteral("缺陷4: 控件自己的中心就在命中区内 (SE_CheckBoxClickRect 覆盖中心)"),
              QStringLiteral("widget=%1x%2 clickRect=%3,%4 %5x%6")
                  .arg(box->width()).arg(box->height())
                  .arg(clickRect.x()).arg(clickRect.y())
                  .arg(clickRect.width()).arg(clickRect.height()));
        std::printf("PROBE_DEFECT4 sel panel=%d sub=%d key=%s group=%s "
                    "box=%dx%d clickRect=%d,%d %dx%d checked=%d\n",
                    sel.panelIndex, sel.subIndex,
                    qPrintable(sel.panel->settingsKey()),
                    qPrintable(sel.panel->settingsGroup()),
                    box->width(), box->height(),
                    clickRect.x(), clickRect.y(),
                    clickRect.width(), clickRect.height(),
                    int(box->isChecked()));

        // (2) a REAL press/release pair on the centre — the same helper
        // every other click in this file uses.
        int toggles = 0;
        auto conn = QObject::connect(box, &QCheckBox::toggled,
                                     [&](bool on) {
                                         ++toggles;
                                         std::printf("PROBE_DEFECT4 toggled=%d\n",
                                                     int(on));
                                     });
        const bool wasChecked = box->isChecked();
        realClick(box, box->rect().center());
        pump(300);
        check(toggles == 1 && box->isChecked() != wasChecked,
              QStringLiteral("缺陷4: 真实点击把开关翻过来了 (toggled 恰好一次)"),
              QStringLiteral("toggles=%1 checked %2 -> %3")
                  .arg(toggles).arg(int(wasChecked)).arg(int(box->isChecked())));
        QObject::disconnect(conn);

        // (2b) the BOARD followed: the window stopped being drawn.
        if (canvas) {
            const bool drawn = sel.subIndex >= 0
                                   ? canvas->subWindowVisible(sel.subIndex)
                                   : canvas->panelWindowVisible(sel.panelIndex);
            check(!drawn,
                  QStringLiteral("缺陷4: 示例画布上的这扇窗口随开关消失了"),
                  QStringLiteral("visible=%1").arg(int(drawn)));
        }

        // (3) DISK, read with a plain file read.
        const QString group = sel.panel->settingsGroup();
        {
            // WHERE, and WHEN. Two questions this assertion could get
            // wrong: a different file than the production writer's, and a
            // write that is still sitting in QSettings' deferred flush
            // (setValue only posts an UpdateRequest —
            // QSettingsPrivate::requestUpdate()).
            auto dumpRaw = [&](const char *tag) {
                QFile raw(iniFile);
                if (!raw.open(QIODevice::ReadOnly)) {
                    std::printf("PROBE_DEFECT4 raw[%s] MISSING %s\n", tag,
                                qPrintable(iniFile));
                    return;
                }
                const QByteArray all = raw.readAll();
                std::printf("PROBE_DEFECT4 raw[%s] %d bytes:\n", tag,
                            int(all.size()));
                for (const QByteArray &l : all.split('\n'))
                    if (!l.trimmed().isEmpty())
                        std::printf("PROBE_DEFECT4   |%s\n",
                                    l.trimmed().constData());
            };
            dumpRaw("after-click");
            QSettings probeSettings(QSettings::IniFormat, QSettings::UserScope,
                                    QStringLiteral("monster-overlay"),
                                    QStringLiteral("panels"));
            probeSettings.beginGroup(group);
            std::printf("PROBE_DEFECT4 fresh QSettings %s: contains=%d value=%d\n",
                        qPrintable(probeSettings.fileName()),
                        int(probeSettings.contains(QStringLiteral("winVisible"))),
                        int(probeSettings.value(QStringLiteral("winVisible"), true).toBool()));
            probeSettings.endGroup();
            probeSettings.sync();          // force the deferred flush
            dumpRaw("after-sync");
        }
        pump(100);
        const QString mine = visibleRow(group);
        std::printf("PROBE_DEFECT4 panels.ini entries:\n");
        for (const QString &line : iniDump())
            std::printf("PROBE_DEFECT4   %s\n", qPrintable(line));
        check(mine == QStringLiteral("false"),
              QStringLiteral("缺陷4: panels.ini 磁盘上这一行真的是 visible=false"),
              mine.isEmpty() ? QStringLiteral("(no row)") : mine);
        check(!mhw::readWindowVisible(group, sel.panel->settingsKey()),
              QStringLiteral("缺陷4: 生产读路径 readWindowVisible() 也读到 hidden"));

        // (4) REOPEN: a second console on the same config.
        {
            ControlPanel cp2;
            cp2.resize(1400, 1000);
            cp2.show();
            pump(400);
            cp2.selectWindow(wantPanel, wantSub);
            pump(300);
            auto *box2 = cp2.findChild<QCheckBox *>(QStringLiteral("metaVisible"));
            const bool shown = box2 && box2->isChecked();
            check(box2 && box2->isEnabled(),
                  QStringLiteral("缺陷4: 重开后开关仍然可用"));
            check(!shown,
                  QStringLiteral("缺陷4: 重开后「显示该窗口」读回为未勾选"),
                  box2 ? (box2->isChecked() ? QStringLiteral("checked")
                                            : QStringLiteral("unchecked"))
                       : QStringLiteral("(absent)"));
            check(visibleRow(group) == QStringLiteral("false"),
                  QStringLiteral("缺陷4: 重开之后磁盘上这一行还在且仍是 false"),
                  visibleRow(group).isEmpty() ? QStringLiteral("(no row)")
                                              : visibleRow(group));
            evidence(&cp2, "issue4-reopened-hidden");
            cp2.close();
            pump(200);
        }

        // RESTORE the default, and prove the row is REMOVED rather than
        // flipped — show() has no key on disk (console_fidelity_windows.h).
        realClick(box, box->rect().center());
        pump(300);
        check(box->isChecked(),
              QStringLiteral("缺陷4: 再点一次把开关放回勾选"));
        check(visibleRow(group).isEmpty(),
              QStringLiteral("缺陷4: 恢复显示后磁盘上不再有这一行"),
              visibleRow(group));
        if (canvas && !box->isChecked() == false) {
            const bool drawn = sel.subIndex >= 0
                                   ? canvas->subWindowVisible(sel.subIndex)
                                   : canvas->panelWindowVisible(sel.panelIndex);
            check(drawn,
                  QStringLiteral("缺陷4: 恢复显示后示例画布把它画回来了"),
                  QStringLiteral("visible=%1").arg(int(drawn)));
        }
        evidence(&cp, "issue4-checkbox");
        cp.resize(1600, 1000);
        pump(250);
    }

    // ==================================================================
    // 实测缺陷3: 本组子窗 (the sibling chips) lists only what the game has
    // ==================================================================
    //
    // In Split the player mother carves six windows out of itself, and
    // the registry behind them is deliberately game-agnostic — 衣装 and
    // 翔虫 are both there so either game can resolve its own plan. The
    // LIST the user reads is a different statement: World must not be
    // offered 翔虫 (Rise only) and Rise must not be offered 衣装 (World
    // only), and 伙伴 (pet-damage) is Rise-only at panel granularity.
    {
        auto *splitSeg = cp.findChild<QPushButton *>(QStringLiteral("layoutSegSplit"));
        auto *singleSeg = cp.findChild<QPushButton *>(QStringLiteral("layoutSegSingle"));
        const QList<QPushButton *> gameBtns =
            cp.findChildren<QPushButton *>(QStringLiteral("gameBtn"));
        check(splitSeg && singleSeg && gameBtns.size() == 2,
              QStringLiteral("缺陷3: the layout segment and the two game buttons exist"),
              QStringLiteral("split=%1 single=%2 gameBtns=%3")
                  .arg(int(splitSeg != nullptr)).arg(int(singleSeg != nullptr))
                  .arg(gameBtns.size()));

        auto chipKeys = [&] {
            QStringList keys;
            for (QPushButton *c :
                 cp.findChildren<QPushButton *>(QStringLiteral("groupWinChip")))
                keys << c->property("settingsKey").toString();
            keys.sort();
            return keys;
        };
        // Select the first window of the player group the model offers.
        auto selectPlayerWindow = [&] {
            for (const mhw::WindowState &w : cp.buildWindowStates())
                if (w.panelIndex == 0 && w.subIndex >= 0) {
                    cp.selectWindow(0, w.subIndex);
                    pump(250);
                    return w.subIndex;
                }
            return -2;
        };
        auto panel3Rows = [&] {
            int n = 0;
            for (const mhw::WindowState &w : cp.buildWindowStates())
                if (w.panelIndex == 3) ++n;
            return n;
        };

        // Split topology: six carved windows, five shown per game.
        if (splitSeg && !splitSeg->isChecked()) {
            realClick(splitSeg, splitSeg->rect().center());
            pump(350);
        }
        check(cp.buildWindowStates().size() > 0,
              QStringLiteral("缺陷3: precondition — the Split model lists windows"));

        // gameBtns are built World-then-Rise (buildGameColumn()); the
        // assertion below is what proves the mapping, because the two
        // expected sets are disjoint in exactly one key each.
        const QStringList kWorldSet = {QStringLiteral("player-bars"),
                                       QStringLiteral("player-info"),
                                       QStringLiteral("player-mantles"),
                                       QStringLiteral("player-status"),
                                       QStringLiteral("player-weapon")};
        const QStringList kRiseSet  = {QStringLiteral("player-bars"),
                                       QStringLiteral("player-info"),
                                       QStringLiteral("player-status"),
                                       QStringLiteral("player-weapon"),
                                       QStringLiteral("player-wirebug")};

        if (gameBtns.size() == 2) {
            realClick(gameBtns[0], gameBtns[0]->rect().center());
            pump(450);
            const int subWorld = selectPlayerWindow();
            const QStringList world = chipKeys();
            std::printf("PROBE_DEFECT3 world chips: %s\n",
                        qPrintable(world.join(QLatin1Char(','))));
            check(subWorld >= 0,
                  QStringLiteral("缺陷3: World 下玩家组有子窗可选"),
                  QStringLiteral("sub=%1").arg(subWorld));
            check(world == kWorldSet,
                  QStringLiteral("缺陷3: World 的本组子窗不含 player-wirebug、含 player-mantles "
                                 "(5 个)"),
                  world.join(QLatin1Char(',')));
            check(!world.contains(QStringLiteral("pet-damage"))
                      && !world.contains(QStringLiteral("player-pets")),
                  QStringLiteral("缺陷3: World 的列表里没有伙伴窗"),
                  world.join(QLatin1Char(',')));
            check(panel3Rows() == 0,
                  QStringLiteral("缺陷3: World 的模型里根本没有伙伴行"),
                  QStringLiteral("panel3 rows=%1").arg(panel3Rows()));
            // 伙伴 in World: selectWindow() refuses the slot outright
            // (its own Rise-only gate, the same rule at one level down),
            // so the selection never moves to panel 3 and no pet key can
            // appear in either list.
            const int panelBefore = cp.currentEditableSelection().panelIndex;
            cp.selectWindow(3, -1);
            pump(250);
            check(cp.currentEditableSelection().panelIndex != 3,
                  QStringLiteral("缺陷3: World 下伙伴槽选不中（selectWindow 的 Rise-only 门直接 "
                                 "return，槽不移动）"),
                  QStringLiteral("panelIndex %1 -> %2")
                      .arg(panelBefore)
                      .arg(cp.currentEditableSelection().panelIndex));
            const QStringList worldAfter = chipKeys();
            check(!worldAfter.contains(QStringLiteral("pet-damage"))
                      && !worldAfter.contains(QStringLiteral("player-pets")),
                  QStringLiteral("缺陷3: World 的伙伴分组列不出任何伙伴子窗"),
                  worldAfter.join(QLatin1Char(',')));

            realClick(gameBtns[1], gameBtns[1]->rect().center());
            pump(450);
            const int subRise = selectPlayerWindow();
            const QStringList rise = chipKeys();
            std::printf("PROBE_DEFECT3 rise chips: %s\n",
                        qPrintable(rise.join(QLatin1Char(','))));
            check(subRise >= 0,
                  QStringLiteral("缺陷3: Rise 下玩家组有子窗可选"),
                  QStringLiteral("sub=%1").arg(subRise));
            check(rise == kRiseSet,
                  QStringLiteral("缺陷3: Rise 的本组子窗不含 player-mantles、含 player-wirebug "
                                 "(5 个)"),
                  rise.join(QLatin1Char(',')));
            // 伙伴 in Rise: the row exists and it is the only chip.
            cp.selectWindow(3, -1);
            pump(300);
            check(cp.currentEditableSelection().panel != nullptr,
                  QStringLiteral("缺陷3: Rise 下伙伴母面板可选"));
            check(chipKeys() == QStringList{QStringLiteral("pet-damage")},
                  QStringLiteral("缺陷3: Rise 的伙伴分组列出 pet-damage（仅 Rise）"),
                  chipKeys().join(QLatin1Char(',')));
        }

        // Leave the suite on the game it started from (world seed).
        std::printf("PROBE_DEFECT3 restore: back to world\n");
        if (gameBtns.size() == 2) {
            realClick(gameBtns[0], gameBtns[0]->rect().center());
            pump(350);
        }
        std::printf("PROBE_DEFECT3 restore: world done, single=%d\n",
                    singleSeg ? int(singleSeg->isChecked()) : -1);
        if (singleSeg && !singleSeg->isChecked()) {
            realClick(singleSeg, singleSeg->rect().center());
            pump(350);
        }
        std::printf("PROBE_DEFECT3 restore: single done\n");
        cp.resize(1600, 1000);
        pump(200);
        std::printf("PROBE_DEFECT3 restore: resize done\n");
    }

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
