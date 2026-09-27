// SPDX-License-Identifier: Apache-2.0
#pragma once

// ConsoleLayoutStore — the window-layout persistence the control console
// used to open directly, inline, in seven places inside ControlPanel.
//
// Everything the console remembers about ITS OWN window lives in one
// semantic domain: the `ui/*` group of keys in the application's
// settings file (window geometry, main-window state, the left and main
// splitters, the canvas zoom, the stage height). Those seven call sites
// each opened the settings backend, read or wrote one or two keys, and
// let it flush on scope exit — but they were the only persistence in the
// console with no seam: there was no way to drive the round-trip from a
// plain logic test, and the key spellings lived in a 2900-line widget
// file, next to the widget code that used them.
//
// Hard rule, same as DamageViewModel / MonsterViewModel /
// riseReframeworkStatus: this header and its implementation carry NO
// widget dependency — only QtCore (QByteArray / QString / qreal). The
// view reads what this class publishes and applies it to its own
// widgets; nothing here knows a QSplitter or a QMainWindow exists.
//
// The settings backend is deliberately ABSENT from this header, not
// merely undocumented: it is not included, not forward-declared and not
// named in any signature. Every accessor opens the backend inside its
// own .cpp definition, so a consumer that includes this header sees
// QByteArray / qreal / int and nothing else, and a name search for the
// settings type over this file comes back empty by construction rather
// than by manual discipline.
//
// Every key spelling, every default and every read/write timing is the
// original: this class is a move, not a rewrite. The single addition is
// the second constructor, which binds an explicit (organization,
// application) pair so a test can point the round-trip at a throwaway
// file; the default constructor keeps honoring whatever the host
// application installed — exactly what the console did before.

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace mhw {

// Default for `ui/zoom`: the preview canvas opens at 2.0x (v0.7.5 zoom
// controls). Named so a test can assert the contract instead of
// re-typing the literal.
constexpr qreal kConsoleZoomDefault = 2.0;

// Default for `ui/stageHeight`: the stage pane height used until the
// user has dragged the main splitter at least once.
constexpr int kConsoleStageHeightDefault = 570;

class ConsoleLayoutStore {
public:
    // Value types. Grouped exactly as the original call sites grouped
    // their keys, so each accessor below reads or writes both members in
    // the same settings object the site used before.

    // `ui/geometry` + `ui/windowState` — restored by the constructor,
    // written back by the console's close path.
    struct WindowFrame {
        QByteArray geometry;
        QByteArray windowState;
    };

    // `ui/splitter` + `ui/stageHeight` — also restored by the
    // constructor, and written back together on every main-splitter move.
    struct SplitterLayout {
        QByteArray splitterState;
        int stageHeight{kConsoleStageHeightDefault};
    };

    // Bound to the host application's settings identity — the behaviour
    // the console had before this class existed.
    ConsoleLayoutStore();

    // Bound to an explicit (organization, application) pair, so a test
    // can isolate the round-trip inside a QTemporaryDir instead of
    // racing the developer's real config file.
    explicit ConsoleLayoutStore(const QString &organization,
                                const QString &application);

    // The type owns no backend handle — only two strings plus a flag —
    // so it stays copyable and comparable, which is also what makes it
    // safe for the console to create a fresh instance per call site
    // (its old pattern) or share one across them.
    ConsoleLayoutStore(const ConsoleLayoutStore &) = default;
    ConsoleLayoutStore &operator=(const ConsoleLayoutStore &) = default;

    // ---- reads -----------------------------------------------------------
    //
    // Each returns exactly the stored value; an absent key yields the
    // default documented on the matching constant. An empty byte array
    // is NOT distinguished from an absent key here — the view keeps its
    // original `if (!state.isEmpty())` guard, because only it knows
    // whether restoring this particular widget is meaningful.

    [[nodiscard]] WindowFrame windowFrame() const;
    [[nodiscard]] qreal zoom() const;
    [[nodiscard]] QByteArray leftSplitterState() const;
    [[nodiscard]] SplitterLayout splitterLayout() const;

    // ---- writes ----------------------------------------------------------

    // `ui/geometry` + `ui/zoom` + `ui/windowState` — the console's close
    // path. `zoom` is the live canvas zoom (1.0 when there is no canvas).
    void saveWindowFrame(const QByteArray &geometry, qreal zoom,
                         const QByteArray &windowState);

    // `ui/leftSplitter` — written on every left-splitter move.
    void saveLeftSplitterState(const QByteArray &state);

    // `ui/splitter` + `ui/stageHeight` — written on every main-splitter
    // move. `stageHeight` is the plain int the console keeps so it does
    // not have to decode the splitter byte array.
    void saveSplitterLayout(const QByteArray &splitterState, int stageHeight);

private:
    // Empty (and useApplicationIdentity_ true) when the instance was
    // default-constructed, in which case the host application's own
    // identity is used.
    QString organization_;
    QString application_;
    bool    useApplicationIdentity_{true};
};

} // namespace mhw
