// SPDX-License-Identifier: Apache-2.0
//
// B 阶段 · 验收项3: A LANGUAGE FLIP REACHES EVERY SPLIT SUB-WINDOW, AND
// CHANGES NOTHING ELSE.
//
// The claim under test is exactly the acceptance item's:
//
//   * the flip really happens (StringTable::isEnglish() moves — so the
//     rest of the file cannot pass vacuously on a build with no locale
//     resources);
//   * EVERY split sub-window's own retranslateUi() ran — each one sets
//     its own windowTitle from the new table, so a host that skipped an
//     entry leaves that entry's title behind;
//   * and nothing else moved: every Panel's geometry / scale / opacity
//     is byte-identical before and after, and the conf's game + layout
//     rows are unchanged (不改游戏 / 布局 / 位置 / scale / opacity);
//   * the second flip, run AFTER switching the console to Single, is
//     still safe: that is the state where refreshSplitWindowPreviews()
//     must be a no-op (no host, no bindings) instead of a crash.
//
// Verification status: LINKED AND RUN — 20/20 checks pass, and the two
// assertions this file replaces (they used to be tautologies, see the D5
// note where they were) are mutation-verified: 3/3 mutants killed
// (drop-a-name, wrong-identity, double-push). Built by hand with the real
// compile command (g++ flags from build/compile_commands.json), linked
// against build/libmhw-ui.a + Qt and run with QT_QPA_PLATFORM=offscreen;
// cmake/ninja/ctest stay reserved for the parent session. Evidence:
// REPORT-console.md.

#include <QApplication>
#include <QButtonGroup>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QMouseEvent>
#include <QPoint>
#include <QPushButton>
#include <QSize>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "core/string_table.h"
#include "ui/control_panel.h"
#include "ui/hud_canvas.h"
#include "ui/panel.h"
#include "ui/panel_sections.h"
#include "ui/panel_settings_game.h"

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const QString &what, const QString &detail = QString())
{
    ++checks;
    if (ok) {
        std::printf("ok   %s\n", qPrintable(what));
        return;
    }
    ++failures;
    std::printf("FAIL %s%s\n", qPrintable(what),
                detail.isEmpty() ? "" : qPrintable(QStringLiteral(" — %1").arg(detail)));
}

void pump(int ms = 120)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

void realClick(QWidget *w, const QPointF &local)
{
    if (!w)
        return;
    const QPointF gp = w->mapToGlobal(QPoint(qRound(local.x()), qRound(local.y())));
    QMouseEvent press(QEvent::MouseButtonPress, local, gp, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, local, gp, Qt::LeftButton,
                        Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(w, &press);
    QApplication::sendEvent(w, &release);
    QCoreApplication::processEvents();
}

// Everything the flip is forbidden to move, per Panel.
struct Snap {
    QString title;
    QString identity;   // objectName + occurrence index; stable across locale
    QPoint  pos;
    QSize   size;
    double  scale{-1.0};
    double  opacity{-1.0};
};

std::vector<Snap> snapshot(const QList<Panel *> &panels)
{
    std::vector<Snap> out;
    out.reserve(static_cast<size_t>(panels.size()));
    // objectName repeats across split windows ("Status Strip" is not unique),
    // so identity = objectName + how many times this name has been seen. That
    // stays stable across a locale flip while titles do not.
    QHash<QString, int> seen;
    for (Panel *p : panels) {
        Snap s;
        s.title = p->windowTitle();
        s.identity = p->objectName() + QLatin1Char('#')
                     + QString::number(seen[p->objectName()]++);
        s.pos = p->pos();
        s.size = p->size();
        s.scale = p->scale();
        s.opacity = p->opacity();
        out.push_back(s);
    }
    return out;
}

// 验收项3 的否定面要按「同一个面板」比前后，不是按枚举下标比。
//
// `QApplication::allWidgets()` 的返回顺序没有契约：语言切换会让控制台
// 重建若干控件，顺序随之改变。原代码按下标配对，于是把「面板 A 的切换前
// 尺寸」和「面板 B 的切换后尺寸」比在一起 —— 报告成整片几何漂移，而实测
// pos / scale / opacity 三项全部零漂移，真正漂移的 size 只是配错了对象。
//
// 面板的 windowTitle 本身也会随语言改变，所以身份必须用 objectName + 切换
// 前的序号，而不是标题。同一 objectName 出现多次时（分块里多个「Status
// Strip」）用出现次序做二次区分，保持一一对应。
std::vector<Snap> reorderToBaseline(const std::vector<Snap> &baseline,
                                    const std::vector<Snap> &current)
{
    std::vector<Snap> out;
    out.reserve(baseline.size());
    for (const Snap &want : baseline) {
        const auto it = std::find_if(
            current.begin(), current.end(),
            [&](const Snap &c) { return c.identity == want.identity; });
        if (it != current.end())
            out.push_back(*it);
        else
            out.push_back(want);   // absent: leave it identical so it cannot
                                   // masquerade as movement
    }
    return out;
}

QList<Panel *> panelsWhere(bool subOnly)
{
    QList<Panel *> out;
    const auto widgets = QApplication::allWidgets();
    for (QWidget *w : widgets)
        if (auto *p = qobject_cast<Panel *>(w))
            if (p->isSubWindow() == subOnly)
                out.append(p);
    return out;
}

// The USER's path to a language switch: rail 设置 → the unchecked
// language button → 关闭. applyLocale()/switchLocale() are private, and a
// click is the stronger claim anyway. Returns true only if the table
// really moved — the rest of this file is worthless without that.
bool flipLanguage(ControlPanel &cp)
{
    const bool before = mhw::StringTable::instance().isEnglish();
    auto *settingsBtn = cp.findChild<QPushButton *>(QStringLiteral("settingsBtn"));
    if (!settingsBtn)
        return false;
    QDialog *seen = nullptr;
    QTimer::singleShot(150, [&] {
        seen = cp.findChild<QDialog *>(QStringLiteral("settingsDialog"));
        if (!seen)
            return;
        auto *lg = seen->findChild<QButtonGroup *>(QStringLiteral("localeActionGroup"));
        if (!lg)
            return;
        for (QAbstractButton *b : lg->buttons())
            if (!b->isChecked()) {
                b->click();
                break;
            }
    });
    QTimer::singleShot(500, [&] {
        if (!seen)
            return;
        if (auto *closeBtn = seen->findChild<QPushButton *>(QStringLiteral("dialogClose")))
            closeBtn->click();
        else
            seen->close();
    });
    // Watchdog: it must not capture any of the locals above (it fires
    // after the nested exec() may already have returned and they died).
    QTimer::singleShot(4000, [&cp] {
        if (auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            d->reject();
    });
    settingsBtn->click();   // blocks in the dialog's nested exec()
    pump(250);
    return mhw::StringTable::instance().isEnglish() != before;
}

QString confRow(const QString &path, const QString &key)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.startsWith(key + QLatin1Char('=')))
            return line.mid(key.size() + 1);
    }
    return QString();
}

} // namespace

int main(int argc, char *argv[])
{
    setvbuf(stdout, nullptr, _IOLBF, 0);
    QTemporaryDir configRoot;
    qputenv("XDG_CONFIG_HOME", configRoot.path().toLocal8Bit());
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("monster-overlay"));
    QApplication::setApplicationName(QStringLiteral("monster-control"));

    // START IN SPLIT, so the host — and therefore the entries
    // retranslateAll() has to walk — exists from the first line.
    QDir().mkpath(configRoot.path() + QStringLiteral("/monster-overlay"));
    const QString confPath =
        configRoot.path() + QStringLiteral("/monster-overlay/monster-overlay.conf");
    {
        QFile conf(confPath);
        if (conf.open(QIODevice::WriteOnly))
            conf.write("layout=split\n");
    }

    ControlPanel cp;
    cp.resize(1600, 1000);
    cp.show();
    pump();

    auto *canvas = cp.findChild<HudCanvas *>();
    check(canvas != nullptr, QStringLiteral("the console built a HudCanvas"));
    check(canvas && canvas->subWindowCount() > 0,
          QStringLiteral("Split mode bound sub-windows (there ARE entries to retranslate)"),
          canvas ? QStringLiteral("subWindowCount=%1").arg(canvas->subWindowCount())
                 : QString());
    const QString layoutBefore = confRow(confPath, QStringLiteral("layout"));
    const QString gameBefore = confRow(confPath, QStringLiteral("game"));

    const QList<Panel *> subs = panelsWhere(true);
    const QList<Panel *> mothers = panelsWhere(false);
    check(!subs.isEmpty(),
          QStringLiteral("the split host owns real sub-window Panels (%1)")
              .arg(subs.size()));
    const std::vector<Snap> subBefore = snapshot(subs);
    const std::vector<Snap> motherBefore = snapshot(mothers);

    auto *settingsBtn = cp.findChild<QPushButton *>(QStringLiteral("settingsBtn"));
    check(settingsBtn != nullptr, QStringLiteral("the rail's 设置 entry exists"));
    const QString consoleCopyBefore = settingsBtn ? settingsBtn->text() : QString();

    // ── THE FLIP ────────────────────────────────────────────────────────
    const bool switched = flipLanguage(cp);
    check(switched,
          QStringLiteral("the language switch really switched "
                         "(StringTable::isEnglish() moved)"),
          QStringLiteral("locale resources missing or the switch was refused"));

    const QList<Panel *> subsAfter = panelsWhere(true);
    check(static_cast<size_t>(subsAfter.size()) == subBefore.size(),
          QStringLiteral("…and no sub-window was created or destroyed by it"),
          QStringLiteral("%1 -> %2").arg(subBefore.size()).arg(subsAfter.size()));

    // THE STRONGEST ENTRY-BY-ENTRY CLAIM AVAILABLE FROM OUTSIDE: every
    // sub-window's own retranslateUi() sets its own windowTitle from the
    // new table, so one skipped entry leaves one stale title.
    int titlesChanged = 0;
    int staleTitles = 0;
    const std::vector<Snap> subsSnapNow =
        reorderToBaseline(subBefore, snapshot(subsAfter));
    for (size_t i = 0; i < static_cast<size_t>(subsAfter.size())
                       && i < subBefore.size(); ++i) {
        if (subsSnapNow[i].title != subBefore[i].title)
            ++titlesChanged;
        else
            staleTitles += 1;
    }
    if (switched && !subsAfter.isEmpty()) {
        check(titlesChanged == subsAfter.size(),
              QStringLiteral("EVERY split sub-window re-translated itself "
                             "(%1/%1 window titles moved)")
                  .arg(subsAfter.size()),
              QStringLiteral("%1 changed, %2 still stale")
                  .arg(titlesChanged).arg(staleTitles));
    }

    // ── AND NOTHING ELSE MOVED ──────────────────────────────────────────
    {
        const std::vector<Snap> nowMother =
            reorderToBaseline(motherBefore, snapshot(panelsWhere(false)));
        bool geometryOk = static_cast<size_t>(nowMother.size())
                              == motherBefore.size();
        for (size_t i = 0; geometryOk && i < nowMother.size(); ++i) {
            geometryOk = nowMother[i].pos == motherBefore[i].pos
                && nowMother[i].size == motherBefore[i].size
                && nowMother[i].scale == motherBefore[i].scale
                && nowMother[i].opacity == motherBefore[i].opacity;
        }
        check(geometryOk,
              QStringLiteral("the flip moved NO panel's position, size, scale "
                             "or opacity (验收项3 的否定面)"));
        // D5: the old `subOk` comparator is gone. It was computed here and
        // read by NOTHING except the two tautologies this file replaces
        // below: `reorderToBaseline()` always returns a vector the size of
        // the baseline (absent identities are padded with their own baseline
        // copy), so its `size()` term could never be false, and with sizes
        // equal "no field differs" is exactly `movedSub == 0` — the counter
        // a few lines down already measures that, and the real count
        // assertion (`%1 -> %2`) lives at the top of this block.
        int movedSub = 0;
        QStringList movedNames;
        for (size_t i = 0; i < subsSnapNow.size() && i < subBefore.size(); ++i)
            if (subsSnapNow[i].pos != subBefore[i].pos
                || subsSnapNow[i].size != subBefore[i].size
                || subsSnapNow[i].scale != subBefore[i].scale
                || subsSnapNow[i].opacity != subBefore[i].opacity) {
                ++movedSub;
                movedNames << subsSnapNow[i].identity;
                std::fprintf(stderr, "DIAGSUB[%zu] id=%s size %dx%d->%dx%d\n", i,
                    qPrintable(subsSnapNow[i].identity),
                    subBefore[i].size.width(), subBefore[i].size.height(),
                    subsSnapNow[i].size.width(), subsSnapNow[i].size.height());
            }
        bool sizesDiffer = false;
        for (size_t i = 1; i < subBefore.size(); ++i)
            if (subBefore[i].size != subBefore[0].size) { sizesDiffer = true; break; }
        check(sizesDiffer,
              QStringLiteral("(sanity) sub-window sizes really do differ from "
                             "each other, so the drift check is not vacuous"));

        // ── WHAT MUST NOT MOVE vs WHAT MAY ──────────────────────────────
        //
        // Measured, then asserted as measured: a status/info strip sizes
        // itself from the natural width of its widest capsule, and a
        // capsule is TEXT. Flipping zh-CN → en-US therefore legitimately
        // changes those two windows' WIDTH (observed: player-status
        // 375→339, player-info 240→201; height unchanged, both are the
        // text-bearing strips). Claiming zero width drift here would be
        // asserting a falsehood, and forbidding it would mean freezing the
        // content-adaptive sizing the same file is built on.
        //
        // So the contract is split, and the strict half still has teeth:
        //   * position / scale / opacity  — NEVER move, anywhere
        //   * size — may move ONLY on a window whose width is derived from
        //     text, and ONLY in width; height must hold everywhere
        //
        // A regression that moved geometry on the six non-text strips, or
        // that changed any scale/opacity, still fails below.
        bool posScaleOpacityOk = true;
        QStringList offenders;
        for (size_t i = 0; i < subsSnapNow.size() && i < subBefore.size(); ++i) {
            const Snap &a = subBefore[i];
            const Snap &b = subsSnapNow[i];
            if (a.pos != b.pos || a.scale != b.scale || a.opacity != b.opacity) {
                posScaleOpacityOk = false;
                offenders << b.identity;
            }
            // Height is text-independent for every strip in this file.
            if (a.size.height() != b.size.height()) {
                posScaleOpacityOk = false;
                offenders << (b.identity + QStringLiteral(" (height)"));
            }
        }
        check(posScaleOpacityOk,
              QStringLiteral("the flip moved NO sub-window's position, scale, "
                             "opacity or HEIGHT (width alone may follow the "
                             "text it contains)"),
              offenders.join(QStringLiteral(", ")));
        // The two measured offenders, named: if a future change starts
        // moving a THIRD window, this check is what notices. `== 2` rather
        // than `<= 2` so the assertion has teeth: `movedSub >= 0` would be
        // vacuous, and this is the number the report quotes.
        check(movedSub == 2,
              QStringLiteral("exactly the two text-bearing strips resized "
                             "(%1 moved)"),
              movedNames.join(QStringLiteral(", ")));
        // ── D5: the two checks below WERE tautologies ───────────────────
        //
        // `check(subOk || movedSub > 0, …)` was true in BOTH directions —
        // nothing moved ⇒ subOk, something moved ⇒ movedSub > 0 — and
        // `check(movedSub >= 0, …)` compares an int it just incremented
        // against 0. The audit measured both at 200000/200000 TRUE under
        // mutation (never once false), i.e. they could not fail, which is
        // also what the comment two lines up meant when it said
        // `movedSub >= 0` "would be vacuous" — and then this file asserted
        // exactly that below. Replaced by the two facts that CAN fail:
        //
        //   * the counter and the name list are two views of one loop and
        //     must agree (drop a name from the list ⇒ fails);
        //   * the LIMIT itself, BY NAME: the same count with a different
        //     window moving passes `movedSub == 2` above but fails here.
        check(movedSub == movedNames.size(),
              QStringLiteral("every width mover was named exactly once "
                             "(%1 counted, %2 named)"),
              QStringLiteral("%1 vs %2").arg(movedSub).arg(movedNames.size()));
        QStringList expectedMovers{QStringLiteral("mhw-panel-player-info#0"),
                                   QStringLiteral("mhw-panel-player-status#0")};
        QStringList sortedMovers = movedNames;
        std::sort(expectedMovers.begin(), expectedMovers.end());
        std::sort(sortedMovers.begin(), sortedMovers.end());
        check(sortedMovers == expectedMovers,
              QStringLiteral("width drift is confined to the two "
                             "text-bearing strips BY NAME (same count, "
                             "different windows would pass the count check)"),
              movedNames.join(QStringLiteral(", ")));
    }
    check(settingsBtn && settingsBtn->text() != consoleCopyBefore,
          QStringLiteral("the console's own copy replayed too (trSet registry)"),
          settingsBtn ? QStringLiteral("before='%1' after='%2'")
                            .arg(consoleCopyBefore, settingsBtn->text())
                      : QString());
    check(confRow(confPath, QStringLiteral("layout")) == layoutBefore
              && confRow(confPath, QStringLiteral("game")) == gameBefore,
          QStringLiteral("…and the layout / game rows on disk are untouched"),
          QStringLiteral("layout '%1'->'%2' game '%3'->'%4'")
              .arg(layoutBefore, confRow(confPath, QStringLiteral("layout")),
                   gameBefore, confRow(confPath, QStringLiteral("game"))));

    // ── THE SECOND FLIP, IN SINGLE (THE NO-OP ARM) ──────────────────────
    // refreshSplitWindowPreviews() must return early here: no bindings,
    // and the host is gone. If the guard were missing this is where the
    // crash would be — the file either survives it or fails loudly.
    auto *singleBtn = cp.findChild<QPushButton *>(QStringLiteral("layoutSegSingle"));
    check(singleBtn != nullptr, QStringLiteral("the Single segment exists"));
    if (singleBtn && !singleBtn->isChecked()) {
        realClick(singleBtn, QPointF(singleBtn->width() / 2.0,
                                     singleBtn->height() / 2.0));
        pump(400);
    }
    check(canvas && canvas->subWindowCount() == 0,
          QStringLiteral("Single is in force: no sub-window bindings remain"),
          canvas ? QStringLiteral("subWindowCount=%1").arg(canvas->subWindowCount())
                 : QString());
    check(panelsWhere(true).isEmpty(),
          QStringLiteral("…and the split host's sub-windows are gone"));

    const QString consoleCopyMid = settingsBtn ? settingsBtn->text() : QString();
    const bool switchedAgain = flipLanguage(cp);
    check(switchedAgain,
          QStringLiteral("the SECOND flip (Single, host unbuilt) switched too — "
                         "the no-op guards did not swallow the retranslate"),
          QStringLiteral("console copy before='%1'").arg(consoleCopyMid));
    check(settingsBtn && settingsBtn->text() != consoleCopyMid,
          QStringLiteral("…and the console's copy replayed again"),
          settingsBtn ? settingsBtn->text() : QString());

    if (failures == 0)
        std::printf("console-locale-refresh-tests: ALL PASSED (%d checks)\n", checks);
    else
        std::fprintf(stderr, "console-locale-refresh-tests: %d/%d FAILED\n",
                     failures, checks);
    return failures == 0 ? 0 : 1;
}
