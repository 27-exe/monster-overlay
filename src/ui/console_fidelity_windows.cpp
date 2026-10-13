#include "console_fidelity_windows.h"

#include "ui/panel.h"
#include "ui/panel_settings_game.h"

#include <QGuiApplication>
#include <QScreen>
#include <QSettings>

namespace mhw {

QString windowVisibleSettingsKey()
{
    // `winVisible`, not `visible`: `visible` is a Qt property name on
    // every QWidget and the console already had a deleted `visible`
    // key once (panel.cpp removed the old fake one). A distinct
    // spelling keeps the new key from being mistaken for the QWidget
    // state it deliberately is NOT — see the header.
    return QStringLiteral("winVisible");
}

namespace {

// The same handle panel.cpp's free function returns, spelled locally
// because that one is TU-local on purpose (it is the file that owns the
// sync-on-read contract). Identical construction — IniFormat, UserScope,
// org "monster-overlay", file "panels" — so both open the same file, and
// the sync() call keeps the read honest against the overlay process.
QSettings &panelSettings()
{
    thread_local QSettings s(QSettings::IniFormat, QSettings::UserScope,
                             QStringLiteral("monster-overlay"),
                             QStringLiteral("panels"));
    s.sync();
    return s;
}

} // namespace

bool readWindowVisible(const QString &scope, const QString &key)
{
    Q_UNUSED(key);
    if (scope.isEmpty())
        return true;   // no scope: the honest default is "shown"
    QSettings &s = panelSettings();
    s.beginGroup(scope);
    // THE VALUE MEANS "VISIBLE", AND IT DEFAULTS TO TRUE. writeWindowVisible()
    // below writes `winVisible=false` for a hide and REMOVES the row for a
    // show, so the only two states this key can be in are "absent" (visible)
    // and "false" (hidden) — and `true` is the answer for everything else.
    //
    // The first spelling of this read was `hidden = value(key,false).toBool();
    // return !hidden`, which made the two halves disagree: the writer stored
    // a false and the reader negated it back to visible, so a window the user
    // had hidden read back as shown and the inspector checkbox never stuck.
    // One polarity, spelled once: the stored value IS the answer.
    const bool visible = s.value(windowVisibleSettingsKey(), true).toBool();
    s.endGroup();
    return visible;
}

void writeWindowVisible(const QString &scope, const QString &key, bool on)
{
    Q_UNUSED(key);
    if (scope.isEmpty())
        return;
    QSettings &s = panelSettings();
    s.beginGroup(scope);
    if (on) {
        // Showing a window that was never hidden removes nothing — the
        // key is absent either way, and removing a key the user never
        // had would be a write for its own sake. Only a real hide adds
        // a row, so the "no spurious key" rule holds.
        if (s.contains(windowVisibleSettingsKey()))
            s.remove(windowVisibleSettingsKey());
    } else {
        s.setValue(windowVisibleSettingsKey(), false);
    }
    s.endGroup();
}

bool windowOnDisplay(const WindowState &w, const QString &screenName)
{
    // Three ANDed facts, each one spelled here and nowhere else. The
    // order is cheapest-first (a QString compare is the expensive one)
    // and every arm is a plain read — this is a pure function over the
    // model, which is why a test can build any combination of the three
    // without touching QSettings.
    return w.panelEnabled && w.visiblePersisted
           && w.outputResolved == screenName;
}

QString resolveWindowOutput(const QString &own, const QString &parentOutput,
                            const QString &primaryOutput)
{
    // own → parent → primary. Each arm is tested for emptiness rather
    // than for "looks like a real output name", because empty is the
    // only "not set" spelling the rest of this codebase uses and
    // guessing at a name's shape is how a hardcoded DP-1 gets in.
    if (!own.isEmpty())
        return own;
    if (!parentOutput.isEmpty())
        return parentOutput;
    return primaryOutput;
}

QString displayLabel(const QString &outputName,
                     const QList<screen_query::OutputInfo> &screens)
{
    if (outputName.isEmpty())
        return QString();
    for (const screen_query::OutputInfo &o : screens) {
        if (o.name != outputName)
            continue;
        // The prototype's `DP-1 · 2560×1440`. The size is Qt's LOGICAL
        // geometry size — the same space every other coordinate in this
        // console is in — so a rotated monitor prints its rotated size
        // rather than the panel mode.
        const QSize sz = o.geometry.size();
        if (sz.isEmpty())
            return o.name;
        return QStringLiteral("%1 · %2×%3").arg(o.name).arg(sz.width()).arg(sz.height());
    }
    // Not in the list (unplugged since the pick was made): the bare
    // name is still the right answer, because it is what the window is
    // aimed at and inventing a size would be a guess.
    return outputName;
}

} // namespace mhw
