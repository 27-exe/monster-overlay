#include "overlay_window.h"

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
    setWindowTitle(QStringLiteral("MHW Linux Overlay"));
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
    status_ = makeLabel(QStringLiteral("MHW Linux Overlay"), 10, QFont::DemiBold);
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
    QFont font(QStringLiteral("Noto Sans CJK SC"));
    font.setPointSize(pointSize);
    font.setWeight(static_cast<QFont::Weight>(weight));
    label->setFont(font);
    return label;
}

QLabel *OverlayWindow::makeContextLabel()
{
    auto *label = new QLabel(container_);
    QFont font(QStringLiteral("Noto Sans CJK SC"));
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
    layer->setScope(QStringLiteral("mhw-linux-overlay"));
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
        context_->setText(QStringLiteral("未连接"));
        quest_->setText(QStringLiteral("只读原型 · 当前未连接游戏"));
        player_->clear();
        party_->clear();
        monsters_->clear();
        abnormalities_->clear();
        equipment_->clear();
        lastZone_ = mhw::Zone::Unknown;
        adjustSize();
        return;
    }

    QString contextText;
    if (mhw::isHuntingZone(snapshot.zone)) {
        if (snapshot.quest.active)
            contextText = QStringLiteral("狩猎 · %1").arg(QString::fromUtf8(mhw::zoneName(snapshot.zone)));
        else
            contextText = QStringLiteral("狩猎场内 · 未开始任务 · %1").arg(QString::fromUtf8(mhw::zoneName(snapshot.zone)));
    } else if (mhw::isPeaceZone(snapshot.zone)) {
        contextText = QStringLiteral("营地 · %1").arg(QString::fromUtf8(mhw::zoneName(snapshot.zone)));
    } else if (snapshot.zone == mhw::Zone::MainMenu) {
        contextText = QStringLiteral("主菜单");
    } else if (snapshot.zone == mhw::Zone::TrainingArea) {
        contextText = QStringLiteral("训练区");
    } else {
        contextText = QStringLiteral("未知场景 · id=%1").arg(static_cast<int>(snapshot.zone));
    }
    context_->setText(contextText);
    lastZone_ = snapshot.zone;

    if (mhw::isHuntingZone(snapshot.zone)) {
        if (snapshot.quest.active) {
            quest_->setText(QStringLiteral("任务 %1 · ★%2 · 剩余 %3 · 猫车 %4/%5")
                                .arg(snapshot.quest.id)
                                .arg(snapshot.quest.stars % 10)
                                .arg(seconds(snapshot.quest.timeLeftSeconds))
                                .arg(snapshot.quest.deaths)
                                .arg(snapshot.quest.maxDeaths));
        } else {
            quest_->setText(QStringLiteral("未在任务中"));
        }
    } else {
        quest_->clear();
    }

    if (snapshot.player.valid) {
        player_->setText(QStringLiteral("猎人  HP %1/%2 (%3)   ST %4/%5")
                             .arg(snapshot.player.health, 0, 'f', 0)
                             .arg(snapshot.player.maxHealth, 0, 'f', 0)
                             .arg(percentage(snapshot.player.health, snapshot.player.maxHealth))
                             .arg(snapshot.player.stamina, 0, 'f', 0)
                             .arg(snapshot.player.maxStamina, 0, 'f', 0));
    } else {
        player_->clear();
    }

    if (mhw::isHuntingZone(snapshot.zone) || snapshot.zone == mhw::Zone::TrainingArea) {
        QStringList partyLines;
        for (const auto &member : snapshot.party) {
            partyLines << QStringLiteral("%1  MR %2  伤害 %3")
                              .arg(member.name)
                              .arg(member.masterRank)
                              .arg(member.damage);
        }
        party_->setText(partyLines.isEmpty() ? QStringLiteral("无队员") : partyLines.join(QLatin1Char('\n')));
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
                    suffix = QStringLiteral("  🔥%1s").arg(remain, 0, 'f', 0);
            }
            // Total HP: in multiplayer the per-layer Health field on the
            // client is frozen, but total HP IS synced. Show the ratio
            // only when it actually moves (i.e. local client has fresh
            // data, == solo or first tick after switching quest).
            const bool totalLive = monster.health > 0.0F
                                && monster.health < monster.maxHealth;
            const QString totalLine = totalLive
                ? QStringLiteral("HP  %1 / %2   %3%4")
                      .arg(monster.health, 0, 'f', 0)
                      .arg(monster.maxHealth, 0, 'f', 0)
                      .arg(percentage(monster.health, monster.maxHealth))
                      .arg(suffix)
                : QStringLiteral("HP  -- / %1   (sync-locked)%2")
                      .arg(monster.maxHealth, 0, 'f', 0)
                      .arg(suffix);
            QString main = QStringLiteral("%1 [ID %2]\n%3")
                                .arg(monster.internalName)
                                .arg(monster.id)
                                .arg(totalLine);
            if (!monster.parts.isEmpty()) {
                QStringList partLines;
                for (const auto &p : monster.parts) {
                    const QString name = p.name.isEmpty()
                        ? QStringLiteral("Part[%1]").arg(p.index)
                        : p.name;

                    QStringList values;
                    if (p.isSeverable) {
                        // HunterPie Severable template: separate "Sever" bar.
                        // We read its live table, so label it as cutting
                        // progress rather than pretending it is a major break.
                        values << QStringLiteral("    切断 %1/%2  %3")
                            .arg(p.health, 0, 'f', 0)
                            .arg(p.maxHealth, 0, 'f', 0)
                            .arg(percentage(p.health, p.maxHealth));
                    } else {
                        values << QStringLiteral("    硬直 %1/%2  %3 · 小硬直计数 %4%5")
                            .arg(p.flinch, 0, 'f', 0)
                            .arg(p.maxFlinch, 0, 'f', 0)
                            .arg(percentage(p.flinch, p.maxFlinch))
                            .arg(p.counter)
                            .arg(multi ? QStringLiteral("  (mp)") : QString());

                        if (p.isBreakable) {
                            // HunterPie Breakable template's second (orange)
                            // bar. Its values are the exact HunterPie
                            // UpdateBreakableData transformation; they are
                            // break-stage progress, not a claimed number of
                            // completed major breaks.
                            values << QStringLiteral("    破坏进度 %1/%2  %3")
                                .arg(p.health, 0, 'f', 0)
                                .arg(p.maxHealth, 0, 'f', 0)
                                .arg(percentage(p.health, p.maxHealth));
                        }
                    }
                    partLines << QStringLiteral("  %1\n%2")
                                  .arg(name, -10)
                                  .arg(values.join(QLatin1Char('\n')));
                }
                main += QLatin1Char('\n') + partLines.join(QLatin1Char('\n'));
            }
            monsterLines << main;
        }
        monsters_->setText(monsterLines.isEmpty() ? QStringLiteral("等待大型怪物数据") : monsterLines.join(QStringLiteral("\n\n")));
    } else {
        monsters_->clear();
    }

    abnormalities_->setText(QString());
    equipment_->setText(QString());
    adjustSize();
}

void OverlayWindow::renderDemo()
{
    status_->setText(QStringLiteral("DEMO · KDE Wayland layer-shell overlay"));
    context_->setText(QStringLiteral("狩猎 · 古代树森林"));
    quest_->setText(QStringLiteral("任务 66801 · ★6 · 剩余 41:37 · 猫车 0/3"));
    player_->setText(QStringLiteral("猎人  HP 172/200 (86.0%)   ST 130/150"));
    party_->setText(QStringLiteral("A27exe  MR 214  伤害 12840\nHunter  MR 83  伤害 9061"));
    monsters_->setText(QStringLiteral("em\\em100_00 [ID 100]\nHP  14280 / 20880   68.4%  🔥74s\n  头部\n    硬直 3105/3105  100.0%  · 硬直次数 0\n  尾巴\n    硬直 200/200   100.0%  · 硬直次数 0 (mp)"));
    abnormalities_->setText(QString());
    equipment_->setText(QString());
    lastZone_ = mhw::Zone::AncientForest;
    adjustSize();
}
