// SPDX-License-Identifier: Apache-2.0
#include "ui/viewmodel/console_layout_store.h"

#include <QByteArray>
#include <QString>
#include <QtGlobal>

#include <QSettings>

namespace mhw {

namespace {

// Every key this class touches, spelled exactly as the seven original
// call sites spelled them. Grouped by the site that used them so a
// future rename can be done as a rename and not as an "improvement".
//
// Original spellings, for the record (control_panel.cpp @ fb76f53):
//   "ui/geometry", "ui/windowState"  — ctor restore + close save
//   "ui/leftSplitter"                — ctor restore + splitterMoved save
//   "ui/zoom"                        — ctor restore + close save
//   "ui/splitter", "ui/stageHeight"  — ctor restore + splitterMoved save
const char kGeometry[]     = "ui/geometry";
const char kWindowState[]  = "ui/windowState";
const char kLeftSplitter[] = "ui/leftSplitter";
const char kSplitter[]     = "ui/splitter";
const char kStageHeight[]  = "ui/stageHeight";
const char kZoom[]         = "ui/zoom";

// The one object this translation unit needs from the settings library,
// opened at the identity the instance is bound to. A stack object per
// accessor call is the original semantics: each of the seven call sites
// let its settings object flush at scope exit, so a caller never
// observes a half-written group and a long-lived handle is never left
// dangling in a widget's member list.
//
// The name is intentionally generic — the header does not know, or need
// to know, that this exists.
struct SettingsRef {
    QSettings settings;
    SettingsRef(bool useApplicationIdentity,
                const QString &organization,
                const QString &application)
        : settings(useApplicationIdentity
                       ? QSettings()
                       : QSettings(organization, application))
    {
    }

    QSettings *operator->() { return &settings; }
};

} // namespace

ConsoleLayoutStore::ConsoleLayoutStore() = default;

ConsoleLayoutStore::ConsoleLayoutStore(const QString &organization,
                                       const QString &application)
    : organization_(organization),
      application_(application),
      useApplicationIdentity_(false)
{
}

ConsoleLayoutStore::WindowFrame ConsoleLayoutStore::windowFrame() const
{
    SettingsRef s(useApplicationIdentity_, organization_, application_);
    // geometry + windowState are read by one block, exactly as the ctor
    // path read them: both keys, one settings object, no default.
    WindowFrame f;
    f.geometry    = s->value(QLatin1String(kGeometry)).toByteArray();
    f.windowState = s->value(QLatin1String(kWindowState)).toByteArray();
    return f;
}

qreal ConsoleLayoutStore::zoom() const
{
    SettingsRef s(useApplicationIdentity_, organization_, application_);
    // Default 2.0: the zoom level the v0.7.5 controls start the canvas at.
    return s->value(QLatin1String(kZoom), kConsoleZoomDefault).toDouble();
}

QByteArray ConsoleLayoutStore::leftSplitterState() const
{
    SettingsRef s(useApplicationIdentity_, organization_, application_);
    return s->value(QLatin1String(kLeftSplitter)).toByteArray();
}

ConsoleLayoutStore::SplitterLayout ConsoleLayoutStore::splitterLayout() const
{
    SettingsRef s(useApplicationIdentity_, organization_, application_);
    // The struct member already carries the documented default, so a
    // missing `ui/stageHeight` yields 570 without repeating the literal.
    SplitterLayout l;
    l.splitterState = s->value(QLatin1String(kSplitter)).toByteArray();
    l.stageHeight   = s->value(QLatin1String(kStageHeight),
                               kConsoleStageHeightDefault).toInt();
    return l;
}

void ConsoleLayoutStore::saveWindowFrame(const QByteArray &geometry,
                                         qreal zoom,
                                         const QByteArray &windowState)
{
    SettingsRef s(useApplicationIdentity_, organization_, application_);
    s->setValue(QLatin1String(kGeometry), geometry);
    s->setValue(QLatin1String(kZoom), zoom);
    s->setValue(QLatin1String(kWindowState), windowState);
}

void ConsoleLayoutStore::saveLeftSplitterState(const QByteArray &state)
{
    SettingsRef s(useApplicationIdentity_, organization_, application_);
    s->setValue(QLatin1String(kLeftSplitter), state);
}

void ConsoleLayoutStore::saveSplitterLayout(const QByteArray &splitterState,
                                            int stageHeight)
{
    SettingsRef s(useApplicationIdentity_, organization_, application_);
    s->setValue(QLatin1String(kSplitter), splitterState);
    s->setValue(QLatin1String(kStageHeight), stageHeight);
}

} // namespace mhw
