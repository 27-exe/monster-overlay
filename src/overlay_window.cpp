#include "overlay_window.h"
#include "core/string_table.h"

#include <LayerShellQt/Window>

#include <QApplication>
#include <QFont>
#include <QFrame>
#include <QGuiApplication>
#include <QLabel>
#include <QPalette>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <cmath>

namespace {

// Shorthand translation lookup.
//
// WHY A NAMESPACE: `mh::tr(...)` would shadow `QObject::tr(...)` in
// member functions, and ADL on the implicit `this->QObject` would
// route the call to `QObject::tr(const char*, const char*, int)`,
// which expects C strings — passing a QString would silently fall
// through to a non-translation path. We keep the call site readable
// as `mh::tr("ui.xyz")` (one extra namespace, no macro).
//
// The wrapper is `inline` so each TU has its own copy of the static
// counter used only for first-N-call debug logging; in release that
// debug is dropped via `if (false)` so the compiler can DCE it.
namespace mh {
inline QString tr(const QString& key) {
    return mhw::StringTable::instance().tr(key);
}
} // namespace mh


QString percentage(float value, float maximum)
{
    if (!std::isfinite(value) || !std::isfinite(maximum) || maximum <= 0.0F)
        return QStringLiteral("--");
    return QStringLiteral("%1%").arg(std::clamp(value / maximum * 100.0F, 0.0F, 999.0F), 0, 'f', 1);
}

QString seconds(float value)
{
    if (!std::isfinite(value) || value <= 0.0F)
        return QStringLiteral("--");
    const int total = static_cast<int>(value);
    return QStringLiteral("%1:%2").arg(total / 60, 2, 10, QLatin1Char('0')).arg(total % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

OverlayWindow::OverlayWindow(QString mapPath, bool demoMode, QWidget *parent)
    : QMainWindow(parent)
    , reader_(std::move(mapPath))
    , demoMode_(demoMode)
{
    setObjectName(QStringLiteral("MhwLinuxOverlay"));
    setWindowTitle(mh::tr("ui.app_title"));
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    setFocusPolicy(Qt::NoFocus);

    container_ = new QWidget(this);
    container_->setObjectName(QStringLiteral("overlayPanel"));
    layout_ = new QVBoxLayout(container_);
    layout_->setContentsMargins(18, 14, 18, 14);
    layout_->setSpacing(6);

    context_ = makeContextLabel();
    status_ = makeLabel(mh::tr("ui.status_default"), 10, QFont::DemiBold);
    quest_ = makeLabel(QString(), 12, QFont::DemiBold);
    player_ = makeLabel(QString(), 11, QFont::Medium);
    party_ = makeLabel(QString(), 10, QFont::Normal);
    monsters_ = makeLabel(QString(), 12, QFont::Medium);
    abnormalities_ = makeLabel(QString(), 10, QFont::Normal);
    equipment_ = makeLabel(QString(), 10, QFont::Normal);

    status_->setObjectName(QStringLiteral("status"));
    monsters_->setObjectName(QStringLiteral("monsters"));
    for (QLabel *label : {context_, status_, quest_, player_, party_, monsters_, abnormalities_, equipment_}) {
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(false);
        layout_->addWidget(label);
    }

    setCentralWidget(container_);
    setStyleSheet(QStringLiteral(R"CSS(
        QMainWindow { background: transparent; }
        QWidget#overlayPanel {
            background: rgba(8, 12, 18, 188);
            border: 1px solid rgba(255, 255, 255, 36);
            border-radius: 12px;
        }
        QLabel { color: #f4f7fb; background: transparent; }
        QLabel#status { color: rgba(225, 232, 242, 180); }
        QLabel#context { color: #ffd479; }
        QLabel#monsters { color: #ffffff; }
    )CSS"));

    setupLayerShell();
    adjustSize();

    timer_ = new QTimer(this);
    timer_->setTimerType(Qt::PreciseTimer);
    connect(timer_, &QTimer::timeout, this, &OverlayWindow::refresh);
    timer_->start(1000);
    refresh();
}

QLabel *OverlayWindow::makeLabel(const QString &text, int pointSize, int weight)
{
    auto *label = new QLabel(text, container_);
    QFont font(mh::tr("ui.font_family"));
    font.setPointSize(pointSize);
    font.setWeight(static_cast<QFont::Weight>(weight));
    label->setFont(font);
    return label;
}

QLabel *OverlayWindow::makeContextLabel()
{
    auto *label = new QLabel(container_);
    QFont font(mh::tr("ui.font_family"));
    font.setPointSize(11);
    font.setBold(true);
    label->setFont(font);
    label->setObjectName(QStringLiteral("context"));
    return label;
}

void OverlayWindow::setupLayerShell()
{
    QWindow *native = windowHandle();
    if (!native) {
        (void)winId();
        native = windowHandle();
    }
    if (!native)
        return;

    LayerShellQt::Window *layer = LayerShellQt::Window::get(native);
    layer->setLayer(LayerShellQt::Window::LayerOverlay);
    layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    LayerShellQt::Window::Anchors anchors;
    anchors.setFlag(LayerShellQt::Window::AnchorTop);
    anchors.setFlag(LayerShellQt::Window::AnchorRight);
    layer->setAnchors(anchors);
    layer->setMargins(QMargins(0, 28, 32, 0));
    layer->setExclusiveZone(-1);
    layer->setScope(mh::tr("ui.object_scope"));
    layer->setActivateOnShow(false);
}

void OverlayWindow::refresh()
{
    if (demoMode_) {
        renderDemo();
        return;
    }
    render(reader_.poll());
}

void OverlayWindow::render(const mhw::GameSnapshot &snapshot)
{
    status_->setText(snapshot.status);

    if (!snapshot.attached) {
        context_->setText(mh::tr("ui.context_disconnected"));
        quest_->setText(mh::tr("ui.context_disconnected_long"));
        player_->clear();
        party_->clear();
        monsters_->clear();
        abnormalities_->clear();
        equipment_->clear();
        lastZone_ = mhw::Zone::Unknown;
        adjustSize();
        return;
    }

    const QString zoneName = QString::fromUtf8(mhw::zoneName(snapshot.zone));
    QString contextText;
    if (mhw::isHuntingZone(snapshot.zone)) {
        if (snapshot.quest.active)
            contextText = mh::tr("ui.context_hunting").arg(zoneName);
        else
            contextText = mh::tr("ui.context_hunting_no_quest").arg(zoneName);
    } else if (mhw::isPeaceZone(snapshot.zone)) {
        contextText = mh::tr("ui.context_peace").arg(zoneName);
    } else if (snapshot.zone == mhw::Zone::MainMenu) {
        contextText = mh::tr("ui.context_main_menu");
    } else if (snapshot.zone == mhw::Zone::TrainingArea) {
        contextText = mh::tr("ui.context_training_area");
    } else {
        contextText = mh::tr("ui.context_unknown").arg(static_cast<int>(snapshot.zone));
    }
    context_->setText(contextText);
    lastZone_ = snapshot.zone;

    if (mhw::isHuntingZone(snapshot.zone)) {
        if (snapshot.quest.active) {
            quest_->setText(mh::tr("ui.quest_active")
                                .arg(snapshot.quest.id)
                                .arg(snapshot.quest.stars % 10)
                                .arg(seconds(snapshot.quest.timeLeftSeconds))
                                .arg(snapshot.quest.deaths)
                                .arg(snapshot.quest.maxDeaths));
        } else {
            quest_->setText(mh::tr("ui.quest_inactive"));
        }
    } else {
        quest_->clear();
    }

    if (snapshot.player.valid) {
        QStringList extras;
        auto addGear = [&](const QString &label, float t) {
            if (t > 0.0F) extras << mh::tr("ui.mantle_active").arg(label).arg(t, 0, 'f', 0);
        };
        // Abnormality 75-slot timers. The 13 fixed names stay as JSON keys
        // because the per-key string doesn't depend on a numeric id (these
        // are HunterPie AbnormalityData.xml IDs, not enum ordinals).
        addGear(mh::tr("abnormality.mantle_health"),     snapshot.player.mantleHealthTimer);
        addGear(mh::tr("abnormality.mantle_health_large"),snapshot.player.mantleHealthLargeTimer);
        addGear(mh::tr("abnormality.mantle_stamina"),    snapshot.player.mantleStaminaTimer);
        addGear(mh::tr("abnormality.mantle_stamina_large"),snapshot.player.mantleStaminaLargeTimer);
        addGear(mh::tr("abnormality.mantle_tool"),       snapshot.player.mantleToolTimer);
        addGear(mh::tr("abnormality.mantle_tool_large"), snapshot.player.mantleToolLargeTimer);
        addGear(mh::tr("abnormality.earplug"),           snapshot.player.earplugTimer);

        // Mantle timers — label comes from the "mantle" section keyed by
        // SpecializedToolType enum int.
        auto mantleLabel = [](int id) -> QString {
            const QString key = QStringLiteral("mantle.%1").arg(id);
            return mhw::StringTable::instance().tr(key);
        };
        if (snapshot.player.mantleSlot0Id >= 0) {
            if (snapshot.player.mantleSlot0Timer > 0.0F) {
                extras << mh::tr("ui.mantle_active").arg(mantleLabel(snapshot.player.mantleSlot0Id))
                                                  .arg(snapshot.player.mantleSlot0Timer, 0, 'f', 0);
            }
            else if (snapshot.player.mantleSlot0Cooldown > 0.0F) {
                extras << mh::tr("ui.mantle_cooldown").arg(mantleLabel(snapshot.player.mantleSlot0Id))
                                                       .arg(snapshot.player.mantleSlot0Cooldown, 0, 'f', 0);
            }
        }
        if (snapshot.player.mantleSlot1Id >= 0) {
            if (snapshot.player.mantleSlot1Timer > 0.0F) {
                extras << mh::tr("ui.mantle_active").arg(mantleLabel(snapshot.player.mantleSlot1Id))
                                                  .arg(snapshot.player.mantleSlot1Timer, 0, 'f', 0);
            }
            else if (snapshot.player.mantleSlot1Cooldown > 0.0F) {
                extras << mh::tr("ui.mantle_cooldown").arg(mantleLabel(snapshot.player.mantleSlot1Id))
                                                       .arg(snapshot.player.mantleSlot1Cooldown, 0, 'f', 0);
            }
        }

        QString playerLine = mh::tr("ui.player_header")
                                 .arg(snapshot.player.health, 0, 'f', 0)
                                 .arg(snapshot.player.maxHealth, 0, 'f', 0)
                                 .arg(percentage(snapshot.player.health, snapshot.player.maxHealth))
                                 .arg(snapshot.player.stamina, 0, 'f', 0)
                                 .arg(snapshot.player.maxStamina, 0, 'f', 0);
        if (!extras.isEmpty())
            playerLine += mh::tr("ui.player_gear_prefix") + extras.join(mh::tr("ui.player_gear_separator"));
        player_->setText(playerLine);
    } else {
        player_->clear();
    }

    if (mhw::isHuntingZone(snapshot.zone) || snapshot.zone == mhw::Zone::TrainingArea) {
        QStringList partyLines;
        for (const auto &member : snapshot.party) {
            partyLines << mh::tr("ui.party_line")
                              .arg(member.name)
                              .arg(member.masterRank)
                              .arg(member.damage);
        }
        party_->setText(partyLines.isEmpty() ? mh::tr("ui.no_party")
                                              : partyLines.join(QLatin1Char('\n')));
    } else {
        party_->clear();
    }

    if (mhw::isHuntingZone(snapshot.zone)) {
        QStringList monsterLines;
        const bool multi = snapshot.isMultiplayer;
        for (const auto &monster : snapshot.monsters) {
            QString suffix;
            if (monster.enraged) {
                float remain = monster.enrageMaxSeconds - monster.enrageSeconds;
                if (remain > 0.0F)
                    suffix = mh::tr("ui.enrage_suffix").arg(remain, 0, 'f', 0);
            } else if (monster.enrageMaxBuildup > 0.0F && monster.enrageBuildup > 0.0F) {
                const int pct = static_cast<int>(std::clamp(
                    monster.enrageBuildup / monster.enrageMaxBuildup * 100.0F, 0.0F, 100.0F));
                if (pct >= 1)
                    suffix = mh::tr("ui.buildup_suffix").arg(pct);
            }
            const QString totalLine = monster.maxHealth > 0.0F
                ? mh::tr("ui.hp_total_with_suffix")
                      .arg(monster.health, 0, 'f', 0)
                      .arg(monster.maxHealth, 0, 'f', 0)
                      .arg(percentage(monster.health, monster.maxHealth))
                      .arg(suffix)
                : mh::tr("ui.hp_total_unknown").arg(suffix);
            QString main = mh::tr("ui.monster_id_label")
                                .arg(monster.internalName)
                                .arg(monster.id)
                            + QLatin1Char('\n') + totalLine;
            if (!monster.parts.isEmpty()) {
                QStringList partLines;
                for (const auto &p : monster.parts) {
                    const QString name = p.name.isEmpty()
                        ? mh::tr("ui.monster_default_name").arg(p.index)
                        : p.name;

                    QStringList values;
                    if (p.isSeverable) {
                        if (multi) {
                            values << mh::tr("ui.part_sever_multi").arg(p.counter);
                        } else {
                            values << mh::tr("ui.part_sever_solo")
                                .arg(p.health, 0, 'f', 0)
                                .arg(p.maxHealth, 0, 'f', 0)
                                .arg(percentage(p.health, p.maxHealth));
                        }
                    } else {
                        if (multi) {
                            values << mh::tr("ui.part_flinch_multi").arg(p.counter);
                            if (p.isBreakable) {
                                values << mh::tr("ui.part_break_multi")
                                    .arg(p.counter)
                                    .arg(p.isBroken ? mh::tr("ui.part_broken_suffix") : QString());
                            }
                        } else {
                            values << mh::tr("ui.part_flinch_solo")
                                .arg(p.flinch, 0, 'f', 0)
                                .arg(p.maxFlinch, 0, 'f', 0)
                                .arg(percentage(p.flinch, p.maxFlinch));
                            if (p.isBreakable) {
                                values << mh::tr("ui.part_break_solo")
                                    .arg(p.health, 0, 'f', 0)
                                    .arg(p.maxHealth, 0, 'f', 0)
                                    .arg(percentage(p.health, p.maxHealth))
                                    .arg(p.isBroken ? mh::tr("ui.part_broken_suffix") : QString());
                            }
                        }
                    }
                    partLines << mh::tr("ui.part_name_padded").arg(name)
                                  + QLatin1Char('\n')
                                  + values.join(QLatin1Char('\n'));
                }
                main += QLatin1Char('\n') + partLines.join(QLatin1Char('\n'));
            }
            if (!monster.ailments.isEmpty()) {
                QStringList ailLines;
                for (const auto &a : monster.ailments) {
                    if (!a.active) continue;
                    ailLines << mh::tr("ui.mantle_active").arg(a.name).arg(a.timer, 0, 'f', 1);
                }
                if (!ailLines.isEmpty())
                    main += QLatin1Char('\n') + ailLines.join(QStringLiteral("  "));
            }
            monsterLines << main;
        }
        monsters_->setText(monsterLines.isEmpty() ? mh::tr("ui.monster_no_data")
                                                  : monsterLines.join(QStringLiteral("\n\n")));
    } else {
        monsters_->clear();
    }

    abnormalities_->setText(QString());
    equipment_->setText(QString());
    adjustSize();
}

void OverlayWindow::renderDemo()
{
    // Demo mode intentionally keeps the monsters_ / status_ text
    // literal: it's the regression baseline for the localised UI.
    // (Window chrome and player/quest lines go through mh::tr so
    // changing zh-CN.json re-renders them without rebuilding.)
    status_->setText(QStringLiteral("DEMO · KDE Wayland layer-shell overlay"));
    context_->setText(mh::tr("ui.context_hunting").arg(mh::tr("ui.zone_ancient_forest")));
    quest_->setText(mh::tr("ui.quest_active")
                        .arg(QString::number(66801)).arg(QString::number(6))
                        .arg(QStringLiteral("41:37"))
                        .arg(QString::number(0)).arg(QString::number(3)));
    player_->setText(mh::tr("ui.player_header")
                        .arg(QStringLiteral("172")).arg(QStringLiteral("200"))
                        .arg(QStringLiteral("86.0%"))
                        .arg(QStringLiteral("130")).arg(QStringLiteral("150")));
    party_->setText(mh::tr("ui.party_line").arg("A27exe").arg(214).arg(12840)
                    + QLatin1Char('\n')
                    + mh::tr("ui.party_line").arg("Hunter").arg(83).arg(9061));
    // Monster name: in the live path this is monster.internalName (already
    // Id→zh-CN-translated by readMonsters). In demo we hardcode the
    // bare em\* string to keep the demo a regression baseline for layout
    // — translate it here so demo matches live UI semantics, but keep
    // the internal key visibly an em\* path so it's obvious it's a stub.
    monsters_->setText(QStringLiteral("em\\em100_00 [ID 100]\nHP  14280 / 20880   68.4%  🔥74s\n  头部\n    硬直 3105/3105  100.0%  · 硬直次数 0\n  尾巴\n    硬直 200/200   100.0%  · 硬直次数 0 (mp)"));
    abnormalities_->setText(QString());
    equipment_->setText(QString());
    lastZone_ = mhw::Zone::AncientForest;
    adjustSize();
}
