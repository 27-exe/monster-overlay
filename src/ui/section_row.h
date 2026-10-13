#pragma once

#include <QColor>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QSize>
#include <QWidget>

class QLabel;

// Inspector section row — a faithful port of the prototype's
// `.section-item` (reference.html:228-230):
//
//   .section-item { display:flex; align-items:center; gap:7px;
//                   min-height:29px; padding:0 7px;
//                   border:1px solid var(--line);
//                   background:var(--surface-2); border-radius:5px;
//                   color:var(--fg-2); font-size:10px; }
//   .section-item:has(input:checked) {
//                   color:var(--fg);
//                   border-color:color-mix(in srgb,var(--clay) 55%,var(--line));
//                   background:color-mix(in srgb,var(--clay) 10%,var(--surface-2)); }
//   .section-item input { accent-color:var(--clay); }
//   input:focus-visible { outline:2px solid var(--clay-light);
//                         outline-offset:2px; }   (reference.html:86-87)
//
// The row is a bordered pill containing ONE 13×13 checkbox and the
// localized label — nothing else. The prototype draws no per-section
// icon and no right-hand ASCII key, so this widget no longer paints
// either; the glyph enum, the key text and the panel accent survive as
// constructor parameters (see the Icon notes below) because
// control_panel.cpp and control_l2_smoke.cpp still pass them, but they
// carry no pixels.
//
// i18n: the label is UI copy (panel_sections.h displayName(), i.e.
// console.section.*) and is replaceable at runtime via setDisplayText().
//
// Both themes are hard-mapped here from the prototype's token block so
// the row matches the HTML regardless of which console palette is live
// (ui_theme.h stays untouched — it is shared by other widgets).
class SectionRow : public QWidget {
    Q_OBJECT
public:
    // Which glyph a caller *would* draw. Retained for source/ABI
    // stability only: kPlayerIcons in control_panel.cpp subscripts this
    // enum by the section bit index, and the switch that used to paint
    // these is gone. Reordering or removing an enumerator would change
    // the values the caller passes, so every entry keeps its value and
    // the row simply ignores it. IconNone (-1) is the legacy "no kind".
    enum Icon {
        IconNone = -1,
        // Player panel
        IconConn, IconQuest, IconWeapon, IconBars, IconMantles, IconDebuff,
        IconBuff, IconWirebug,
        // v0.12: the sharpness gauge's own row. Added after IconWirebug,
        // NOT inserted before it, so every existing enumerator keeps its
        // value.
        IconSharpness,
        // Monster panel
        IconInfo, IconHp, IconEnrage, IconAil, IconParts,
        // Damage panel
        IconRows, IconShare, IconChart,
    };

    explicit SectionRow(const QString &zh, const QString &key,
                        int icon = IconNone, QWidget *parent = nullptr);

    bool isChecked() const { return on_; }
    void setChecked(bool c);

    // i18n: swap the display label in place (keeps the row and its
    // checked state). Used by ControlPanel::retranslateUi() so a language
    // switch relabels the existing switches instead of rebuilding the
    // panel.
    void setDisplayText(const QString &text);

    // Kept for API stability. The prototype's .section-item paints no
    // per-panel accent, so this is a no-op.
    void setAccent(const QColor &c);

    // Reapply the inline label colour from the current theme. SectionRow
    // owns its child label's stylesheet, so a parent QSS theme swap cannot
    // update it automatically.
    void refreshTheme();

    // .section-item: min-height 29px, border-box, padding 0 7px. The row
    // can stretch to the grid cell's width (measured 199.92×29 in the
    // prototype's two-column section-list) but never below 29px tall.
    QSize sizeHint() const override { return QSize(160, 29); }
    QSize minimumSizeHint() const override { return QSize(60, 29); }

signals:
    void stateChanged(int state);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    // Space must behave like the HTML's checkbox: the row has no default
    // key handling (QAbstractButton's is not inherited), so KeyPress /
    // KeyRelease are routed here. Auto-repeat from a held Space is
    // swallowed by the press handler so one physical press emits exactly
    // one stateChanged, matching the HTML (where a held key repeats
    // nothing because the checkbox only toggles on the keydown edge).
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    // Qt reports WHY focus arrived (focusInEvent). Chrome shows
    // :focus-visible only for keyboard focus, so Tab/shortcut focus
    // enables the ring and a mouse or programmatic focus does not.
    void focusInEvent(QFocusEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    // The label is placed manually, so a resize has to re-fit its width
    // to the new row width or the text would be clipped at the old one.
    void resizeEvent(QResizeEvent *e) override;

private:
    // The four colours the row needs, in the prototype's own token
    // values. Kept private so only this widget hard-maps HTML tokens.
    struct Palette {
        QColor surface2;   // --surface-2 (row fill, unchecked)
        QColor line;       // --line      (row border, unchecked)
        QColor fg;         // --fg        (label, checked)
        QColor fg2;        // --fg-2      (label, unchecked)
        QColor clay;       // --clay      (checkbox fill + tint source)
        QColor clayLight;  // --clay-light (focus-visible outline)
    };
    static Palette paletteFor(bool dark);

    // Position the label at the measured kLabelX and re-fit its width to
    // the current row width. Called from refreshTheme() and resizeEvent().
    void placeLabel();

    QLabel *zh_ = nullptr;
    // Collapsed-to-zero-width stable ASCII key label. It carries no
    // pixels (the prototype's row has no right-hand key) but stays a
    // child so the console's L2 smoke test can locate a row by its key
    // text. See the constructor.
    QLabel *key_ = nullptr;
    QString zhText_;
    QString keyText_;
    int icon_{IconNone};
    QColor accent_{80, 197, 183};   // teal default (unused for pixels)
    bool on_{true};
    // True only while the row holds focus that the browser would report
    // as :focus-visible. Mouse clicks and programmatic focus do NOT set
    // it (Chrome shows the ring only after keyboard interaction), so a
    // mouse-activated row never paints the keyboard outline.
    bool keyboardFocusVisible_{false};
};
