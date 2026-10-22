// SPDX-License-Identifier: Apache-2.0
//
// Mixed 布局的逐窗口「独立 / 合并」开关 + `layout.windows.<game>=` 持久化
// + 首次进入 Mixed 的作用域迁移，三条契约同一个文件。
//
// ─── 合同 ───────────────────────────────────────────────────────────────
//
// 1. 显式选择 Mixed 才播种作用域。mhw::ensureMixedPanelSettingsScope()
//    把 <game>/<fromMode>/<window> 里已有的窗口参数复制进
//    <game>/mixed/<window>（只补缺失键，已有值是权威）。触发点只能是
//    用户真的选了 Mixed：只读载入（applyStartupLayoutMode）和任何刷新
//    路径都绝不允许播种，否则「打开控制台看一眼」就会改 panels.ini。
//
// 2. 十一个开关就是 window_layout_plan.h 的注册表本身。开关、状态标注
//    （独立窗口 / 合并显示）、全部独立 / 全部合并两个快捷操作全部按
//    kWindowLayoutRows 的顺序来，不在 UI 里维护第二份 key → (parent,row)
//    映射。未勾选 = 该行回到母面板：retainedMask() 把那一位还给母面板。
//
// 3. 保存写一行 `layout.windows.<game>=`，且只写当前游戏的那一行：
//    另一游戏的行、locale= 行、未知键、注释逐字保留；连续两次保存字节
//    完全相同；Single / Split 不写这一行（这两种模式从不读它）。
//
// ─── 必须杀掉的变异（逐条实测，结果见 REPORT） ─────────────────────────
//
//   R1. saveMaskToDisk() 把 plan 行写成两个游戏都写 / 写错游戏
//                                -> RED  §4.1 / §4.2（rise 行被改写）
//   R2. saveMaskToDisk() 不先删旧 plan 行（set 变成 pile）
//                                -> RED  §4.1b / §4.4（同前缀行数 > 1，
//                                          二次保存字节不同）
//   R3. setMixedWindowDetached() 里 setDetached(key, !on)
//                                -> RED  §3.2 / §3.5（勾选态与计划相反；
//                                          未勾选的行没有回到母面板）
//   R4. 删掉 applyLayoutChoice() 里的 ensureMixedPanelSettingsScope()
//                                -> RED  §1.3 / §1.4（mixed 域为空）
//   R5. applyLayoutChoice() 去掉 `mode == Mixed` 保护
//                                -> RED  §1.4（mixed 域的键集合变了）
//   R6. 删掉 refill 里对 mixedGroupHost_ 的 removeWidget()
//                                -> RED  §2.5（块被 purge 删掉后重建，
//                                          对象身份变了）
//   R7. layoutComboIndexFor() 退回 `mode == Split ? 1 : 0`
//                                -> RED  §1.6 / §2.1（conf 读回的索引是 0）
//
// 不杀的，说明原因而不是只列杀掉的：
//
//   S1. 删掉 syncLayoutSegmentToMode() 里的 refreshMixedGroupBlock() ——
//       本文件的可见性断言都发生在 refreshWindowModel() 之后，那条路自己
//       就会刷新；这个调用服务的是「模式提交但选择没变」的 UI 路径，属于
//       保守冗余。本文件不假装覆盖它。
//
// ─── 隔离 ───────────────────────────────────────────────────────────────
//
// HOME 与 XDG_CONFIG_HOME 都指向 QTemporaryDir，QSettings::setPath() 在
// 第一个 QSettings 之前调用，`game=world` 在第一个 ControlPanel 之前写入
// （否则 detectGame() 扫到本机在跑的怪猎进程就切到 rise，所有 world 断言
// 会在开发机上假红）。CMakeLists 注册需要 RESOURCE_LOCK "panels-ini"：
// §1 要往 panels.ini 的 world/single/player 写一个播种键。

#pragma GCC diagnostic ignored "-Wtemplate-body"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>            // §7 的画布像素差分
#include <QLabel>
#include <QPixmap>           // §7 的画布 grab()
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstdio>
#include <string>

// ─── 访问接缝 ───────────────────────────────────────────────────────────
//
// 本文件断言的全是私有面：mixedPlanFor()、refreshWindowModel()、
// saveMaskToDisk 的效果、layoutCombo_ / layoutApplyBtn_。
// 打开 access specifier 是这个仓库的既有拼法（console_scope_persistence_
// tests.cpp 有同名注释）：access 在 Itanium ABI 里是纯编译期属性，不改
// 符号名、vtable 槽位或成员偏移，本 TU 与按正常 access 编译的 libmhw-ui
// 仍然对同一个对象布局。
//
// Qt 与 libstdc++ 的头文件必须在宏定义之前先解析一次（否则 libstdc++ 的
// <sstream> 会以两种 access 实例化并被 GCC 拒绝），所以顺序是固定的：
// 先 Qt，再宏，再项目头。
#define private public
#define protected public
#include "ui/panel_settings_game.h"
#include "ui/control_panel.h"
// v1.1.3 §6.4d: the test reads the canvas's sub-window binding count, so
// the complete type has to be here — control_panel.h only forward-
// declares HudCanvas. Inside the access-macro block with the other
// project headers (the file's own ordering note applies to it too: Qt is
// parsed above, before `#define private public`).
#include "ui/hud_canvas.h"
#include "ui/toggle_chip.h"          // §6.8 直接拨总开关（ToggleChip 只是
                                     // 画出来的开关，click() 不存在）
#include "ui/viewmodel/console_text_helpers.h"
#include "ui/window_layout_plan.h"
#undef private
#undef protected

namespace
{
int checks = 0, failures = 0;

void check(bool ok, const char *what)
{
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("  FAIL  %s\n", what);
    } else {
        std::printf("PASS: %s\n", what);
    }
}

void check(bool ok, const char *what, const QString &detail)
{
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("  FAIL  %s [%s]\n", what, qPrintable(detail));
    } else {
        std::printf("PASS: %s [%s]\n", what, qPrintable(detail));
    }
}

// v1.1.5: THE SAVE, WITHOUT ITS BUTTON. The rail's 保存 was removed — it
// performed saveMaskToDisk() + saveAllSplitAndBaseWindows(), byte for byte
// the write closeEvent() performs on every exit (and saveBeforeLaunch()
// before every launch), so the button was a second door to a room with one
// entrance. These assertions are about the WRITE (which rows land in the
// conf, idempotently, without touching the others), not about the door, so
// they drive the two functions the button and closeEvent share.
void saveLikeCloseEvent(ControlPanel &cp)
{
    cp.saveMaskToDisk();
    cp.saveAllSplitAndBaseWindows();
}

void checkQ(const QString &got, const QString &want, const char *what)
{
    ++checks;
    if (got != want) {
        ++failures;
        std::printf("  FAIL  %s: got \"%s\" want \"%s\"\n", what,
                    qPrintable(got.isEmpty() ? QStringLiteral("(empty)") : got),
                    qPrintable(want.isEmpty() ? QStringLiteral("(empty)")
                                              : want));
    } else {
        std::printf("PASS: %s\n", what);
    }
}

void checkU32(uint32_t got, uint32_t want, const char *what)
{
    ++checks;
    if (got != want) {
        ++failures;
        std::printf("  FAIL  %s: got 0x%08x want 0x%08x\n", what, got, want);
    } else {
        std::printf("PASS: %s [0x%08x]\n", what, got);
    }
}

const char *kConfRel = "monster-overlay/monster-overlay.conf";
const char *kIniRel  = "monster-overlay/panels.ini";

QString confPath(const QString &home)
{
    return home + QLatin1Char('/') + QLatin1String(kConfRel);
}

QString iniPath(const QString &home)
{
    return home + QLatin1Char('/') + QLatin1String(kIniRel);
}

// 整文件覆写。每一节都先重写 conf：上一个 ControlPanel 析构时可能已经按
// closeEvent 的老规矩改过文件了，节与节之间不能共享状态。
bool writeConf(const QString &home, const QString &text)
{
    QDir().mkpath(QFileInfo(confPath(home)).absolutePath());
    QFile f(confPath(home));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        std::printf("  FAIL  cannot write conf\n");
        ++failures;
        return false;
    }
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    out << text;
    out.flush();
    return true;
}

QByteArray readConf(const QString &home)
{
    QFile f(confPath(home));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return f.readAll();
}

// 某一前缀的全部行的值，按文件顺序。返回值个数 > 1 就是「堆」而不是
// 「集合」——这正是 layout= 行当年烂掉的形状。
QStringList confValues(const QByteArray &text, const QByteArray &prefix)
{
    QStringList out;
    const QList<QByteArray> lines = text.split('\n');
    for (const QByteArray &line : lines) {
        const QByteArray t = line.trimmed();
        if (t.startsWith(prefix))
            out.append(QString::fromUtf8(t.mid(prefix.size())));
    }
    return out;
}

void pumpEvents()
{
    for (int i = 0; i < 100; ++i)
        QCoreApplication::processEvents();
}

// 混合块里的控件按 settingsKey 属性找，而不是按 findChildren 的返回顺序。
QCheckBox *toggleFor(ControlPanel &cp, const QString &key)
{
    const auto boxes =
        cp.findChildren<QCheckBox *>(QStringLiteral("mixedWinToggle"));
    for (QCheckBox *box : boxes)
        if (box->property("settingsKey").toString() == key)
            return box;
    return nullptr;
}

QLabel *stateLabelFor(ControlPanel &cp, const QString &key)
{
    const auto labels =
        cp.findChildren<QLabel *>(QStringLiteral("mixedWinState"));
    for (QLabel *lab : labels)
        if (lab->property("settingsKey").toString() == key)
            return lab;
    return nullptr;
}

QLabel *captionFor(ControlPanel &cp)
{
    const auto labels = cp.findChildren<QLabel *>();
    for (QLabel *lab : labels)
        if (lab->property("mixedGroupCaption").toBool())
            return lab;
    return nullptr;
}

QPushButton *namedButton(ControlPanel &cp, const char *name)
{
    const auto buttons = cp.findChildren<QPushButton *>();
    for (QPushButton *btn : buttons)
        if (btn->objectName() == QLatin1String(name))
            return btn;
    return nullptr;
}

// 注册表的 11 个拼写，按 kWindowLayoutRows 的实际顺序钉死。这不是从
// 注册表读出来的（读出来就成了自己证明自己），是任务里那 11 个精确拼写。
QStringList registryKeys()
{
    return {
        QStringLiteral("player-status"),   QStringLiteral("player-mantles"),
        QStringLiteral("player-wirebug"),  QStringLiteral("player-bars"),
        QStringLiteral("player-weapon"),   QStringLiteral("player-info"),
        QStringLiteral("damage-share"),    QStringLiteral("damage-chart"),
        QStringLiteral("damage-rows"),
        QStringLiteral("monster-identity"),
        QStringLiteral("monster-conditions"),
    };
}

// 母面板的原 mask。main.cpp 实测 player 母面板在 player-bars 独立时是
// fffffff7，反推原值 0xffffffff；player-bars 认领的是 0x8 那一位 —— 下面
// 先钉死这个位，再用它算期望值，registry 的 tableRow 一旦和
// kPlayerSplitTargets 的位漂移，§3.1 就会先红。
const uint32_t kPlayerMother = 0xffffffffu;
const uint32_t kBarsBit      = 0x00000008u;

QString mixedGroupOf(mhw::GameId game)
{
    return mhw::panelSettingsScopeFor(
        game, mhw::PlayerSection::PlayerLayoutMode::Mixed, QString());
}

QString singleGroupOf(mhw::GameId game)
{
    return mhw::panelSettingsScopeFor(
        game, mhw::PlayerSection::PlayerLayoutMode::Single, QString());
}

bool iniHas(const QString &home, const QString &group, const QString &key)
{
    QSettings s(iniPath(home), QSettings::IniFormat);
    s.beginGroup(group);
    return s.contains(key);
}

QString iniValue(const QString &home, const QString &group, const QString &key)
{
    QSettings s(iniPath(home), QSettings::IniFormat);
    s.beginGroup(group);
    return s.value(key).toString();
}

void setIniValue(const QString &home, const QString &group,
                 const QString &key, const QVariant &value)
{
    QSettings s(iniPath(home), QSettings::IniFormat);
    s.beginGroup(group);
    s.setValue(key, value);
    s.sync();
}

// 把下拉框拨到某个模式并点「保存并应用」——和用户一模一样的两步。
// 点击必须发生在控件使能之后：apply 只有在「选中的 != 生效的」时才亮。
bool pickAndApply(ControlPanel &cp, int comboIndex)
{
    cp.layoutCombo_->setCurrentIndex(comboIndex);
    if (!cp.layoutApplyBtn_ || !cp.layoutApplyBtn_->isEnabled())
        return false;
    cp.layoutApplyBtn_->click();
    pumpEvents();
    return true;
}

// 11 个开关的勾选态 / 标注 与某个 plan 是否一致。返回不一致的项数。
int mismatches(ControlPanel &cp, const mhw::WindowLayoutPlan &plan)
{
    int bad = 0;
    const QStringList keys = registryKeys();
    for (const QString &key : keys) {
        const int idx = mhw::windowLayoutRowIndexByKey(key);
        if (idx < 0)
            continue;
        const bool want = plan.detached(mhw::kWindowLayoutRows[idx].parentSlot,
                                        mhw::kWindowLayoutRows[idx].tableRow);
        QCheckBox *box = toggleFor(cp, key);
        QLabel *state = stateLabelFor(cp, key);
        if (!box || box->isChecked() != want)
            ++bad;
        const QString wantState = mhw::consoleText(
            want ? QStringLiteral("console.layout.detached")
                 : QStringLiteral("console.layout.merged"));
        if (!state || state->text() != wantState)
            ++bad;
    }
    return bad;
}
// 两个 WindowList 是否逐行逐字段相同。§6 用它证明 Single / Split 的计数
// 模型与画板列表零差异（硬要求：这两模式的行为一个字节都不能变）——
// 只比 size 会被「同样多但换了一行」骗过去，所以逐字段比。
bool sameWindowList(const mhw::WindowList &a, const mhw::WindowList &b)
{
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < a.size(); ++i) {
        const mhw::WindowState &x = a[static_cast<size_t>(i)];
        const mhw::WindowState &y = b[static_cast<size_t>(i)];
        if (x.panelIndex != y.panelIndex || x.subIndex != y.subIndex
            || x.key != y.key || x.title != y.title
            || x.margins != y.margins || x.scale != y.scale
            || x.opacity != y.opacity || x.outputOwn != y.outputOwn
            || x.outputResolved != y.outputResolved
            || x.primaryOutput != y.primaryOutput
            || x.panelEnabled != y.panelEnabled
            || x.visiblePersisted != y.visiblePersisted
            || x.sectionMask != y.sectionMask
            || x.sectionsOn != y.sectionsOn
            || x.sectionsTotal != y.sectionsTotal)
            return false;
    }
    return true;
}

// 计数模型里某个母面板的行数（母行 + 该组的独立窗口行）。
int rowsOfPanel(const mhw::WindowList &list, int panel)
{
    int n = 0;
    for (const mhw::WindowState &w : list)
        if (w.panelIndex == panel)
            ++n;
    return n;
}

// ── §7: 画布像素差分与窗口集合 ─────────────────────────────────────────
//
// 不比源码、不比字符串：§7 的每条断言要么读画布这一帧 paintEvent 真的
// 写下的矩形（subWindowRects / motherWindowRects，hud_canvas.h 明说它们
// 来自同一次绘制，未画出来的框留空矩形），要么直接比两张 grab() 位图。
int differingPixels(const QImage &a, const QImage &b)
{
    if (a.size() != b.size())
        return -1;
    const QImage x = a.convertToFormat(QImage::Format_ARGB32);
    const QImage y = b.convertToFormat(QImage::Format_ARGB32);
    int n = 0;
    for (int r = 0; r < x.height(); ++r) {
        const QRgb *px = reinterpret_cast<const QRgb *>(x.constScanLine(r));
        const QRgb *py = reinterpret_cast<const QRgb *>(y.constScanLine(r));
        for (int c = 0; c < x.width(); ++c)
            if (px[c] != py[c])
                ++n;
    }
    return n;
}

// 一个列表里 sub 行（或母行）的 key，排序后返回 —— 集合比较不该把
// 「宿主先画子窗再画母面板」这种顺序当成契约。
QStringList keysOf(const mhw::WindowList &list, bool subRows)
{
    QStringList out;
    for (const mhw::WindowState &w : list)
        if (subRows == (w.subIndex >= 0))
            out.append(w.key);
    out.sort();
    return out;
}

// 画完再读：矩形是 paintEvent 写的，grab() 一次把这一帧坐实。
int paintedSubBoxes(const HudCanvas *cv)
{
    int n = 0;
    const QVector<QRectF> rects = cv->subWindowRects();
    for (const QRectF &r : rects)
        if (!r.isEmpty())
            ++n;
    return n;
}

// 母面板画了几块（空矩形 = 这一帧没画它）。
int paintedMotherBoxes(const HudCanvas *cv)
{
    int n = 0;
    const QVector<QRectF> rects = cv->motherWindowRects();
    for (const QRectF &r : rects)
        if (!r.isEmpty())
            ++n;
    return n;
}
} // namespace

int main(int argc, char *argv[])
{
    // ── 隔离 ────────────────────────────────────────────────────────────
    QTemporaryDir home;
    if (!home.isValid()) {
        std::fprintf(stderr, "FAIL: could not create a scratch HOME\n");
        return 2;
    }
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());

    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("monster-overlay"));
    app.setApplicationName(QStringLiteral("monster-overlay"));

    // 钉住游戏轴（同 console_scope_persistence_tests）：写在 QApplication
    // 之后，因为这两个名字就是文件路径的一半。
    {
        QSettings gameScope;
        gameScope.setValue(QStringLiteral("game"), QStringLiteral("world"));
        gameScope.sync();
    }

    std::printf("== console_mixed_layout_tests ==\n");
    std::printf("(逐窗口 独立/合并 + layout.windows.<game> 持久化 + "
                "首次进入 Mixed 的作用域迁移)\n\n");

    using Mode = mhw::PlayerSection::PlayerLayoutMode;
    const QStringList keys = registryKeys();

    // ═══ §1 首次进入 Mixed 的作用域迁移 ═════════════════════════════════
    std::printf("-- §1 显式进入 Mixed 才播种作用域 --\n");
    {
        // 播种键写进 single 域。mixed 域故意一个键都不写：§1.0 断言的是
        // 「构造完之后它仍然一个键都没有」，用 setValue(QVariant()) 去
        // "清空" 会凭空造出一个空键，正好把这条断言的前提毁掉。
        setIniValue(home.path(),
                    singleGroupOf(mhw::GameId::World)
                        + QStringLiteral("/player"),
                    QStringLiteral("minWidth"), 321);
        writeConf(home.path(), QStringLiteral("layout=single\n"));

        ControlPanel cp;
        cp.show();
        pumpEvents();

        check(!iniHas(home.path(),
                      mixedGroupOf(mhw::GameId::World)
                          + QStringLiteral("/player"),
                      QStringLiteral("minWidth")),
              "§1.0 只读载入不播种：world/mixed/player 下没有 minWidth");
        check(cp.mixedGroupHost_ != nullptr,
              "§1.1 分组列里的混合块被建出来了（列存在）");
        if (cp.mixedGroupHost_)
            check(cp.mixedGroupHost_->isHidden(),
                  "§1.1b Single 模式下混合块隐藏（Single 没有可编辑的 plan）");

        const bool applied = pickAndApply(cp, ControlPanel::kLayoutComboMixedIndex);
        check(applied, "§1.2 选中 Mixed 后「保存并应用」按钮使能并被点击");
        check(cp.layoutModeInForce() == Mode::Mixed,
              "§1.2b 生效模式变成 Mixed");
        if (cp.mixedGroupHost_)
            check(!cp.mixedGroupHost_->isHidden(),
                  "§1.2c 进入 Mixed 后混合块可见");

        const QString mixedPlayer = mixedGroupOf(mhw::GameId::World)
                                    + QStringLiteral("/player");
        const QString singlePlayer = singleGroupOf(mhw::GameId::World)
                                     + QStringLiteral("/player");
        const QString riseMixedPlayer =
            mixedGroupOf(mhw::GameId::Rise) + QStringLiteral("/player");

        check(iniHas(home.path(), mixedPlayer, QStringLiteral("minWidth")),
              "§1.3 显式进入 Mixed 后 world/mixed/player/minWidth 被播种");
        checkQ(iniValue(home.path(), mixedPlayer, QStringLiteral("minWidth")),
               QStringLiteral("321"), "§1.3b 播种值 == single 域的 321");
        checkQ(iniValue(home.path(), singlePlayer, QStringLiteral("minWidth")),
               QStringLiteral("321"), "§1.3c 源域只读：world/single/player 仍是 321");
        check(!iniHas(home.path(), riseMixedPlayer, QStringLiteral("minWidth")),
              "§1.4 另一个游戏（rise/mixed/player）既没被读也没被写");

        // 已存在的 mixed 值是权威的：改掉它，再 Single→Mixed 一次，
        // copy-missing-only 不许把它盖回去。
        setIniValue(home.path(), mixedPlayer, QStringLiteral("minWidth"), 999);
        check(pickAndApply(cp, ControlPanel::kLayoutComboSingleIndex),
              "§1.5 回到 Single（按钮使能并被点击）");
        check(cp.layoutModeInForce() == Mode::Single, "§1.5b 生效 Single");
        check(pickAndApply(cp, ControlPanel::kLayoutComboMixedIndex),
              "§1.5c 再次显式进入 Mixed");
        checkQ(iniValue(home.path(), mixedPlayer, QStringLiteral("minWidth")),
               QStringLiteral("999"),
               "§1.5d 已存在的 mixed 值没被二次播种覆盖（999 保留）");

        // 下拉框两个方向。
        check(cp.layoutCombo_->currentIndex() == ControlPanel::kLayoutComboMixedIndex,
              "§1.6 混合生效时下拉框停在 Mixed（不是整块）");
        cp.layoutCombo_->setCurrentIndex(ControlPanel::kLayoutComboSingleIndex);
        check(cp.layoutComboMode() == Mode::Single,
              "§1.6b 下拉框第 0 项 → Single");
        cp.layoutCombo_->setCurrentIndex(ControlPanel::kLayoutComboSplitIndex);
        check(cp.layoutComboMode() == Mode::Split,
              "§1.6c 下拉框第 1 项 → Split");
        cp.layoutCombo_->setCurrentIndex(ControlPanel::kLayoutComboMixedIndex);
        check(cp.layoutComboMode() == Mode::Mixed,
              "§1.6d 下拉框第 2 项 → Mixed");

        // §1.7 反向断言：只有「显式进 Mixed」才播种。前置是给 single 域
        //     一个 mixed 域没有的键；把 applyLayoutChoice() 里的
        //     `mode == Mixed` 守卫拿掉（任何模式切换都播种），Single →
        //     Split 这一步就会把 777 复制进 mixed，这条断言立刻变红。
        setIniValue(home.path(), singlePlayer, QStringLiteral("minWidthSeed"),
                    777);
        check(!iniHas(home.path(), mixedPlayer, QStringLiteral("minWidthSeed")),
              "§1.7 前置：mixed 域里没有 minWidthSeed");
        check(pickAndApply(cp, ControlPanel::kLayoutComboSingleIndex),
              "§1.7b Mixed → Single（按钮使能并被点击）");
        check(pickAndApply(cp, ControlPanel::kLayoutComboSplitIndex),
              "§1.7c Single → Split（按钮使能并被点击）");
        check(cp.layoutModeInForce() == Mode::Split, "§1.7d 生效 Split");
        check(!iniHas(home.path(), mixedPlayer, QStringLiteral("minWidthSeed")),
              "§1.7e 进入 Split 没有播种 mixed 域（播种只属于显式进 Mixed）");

        cp.close();
        pumpEvents();
    }

    // ═══ §2 Mixed 载入：映射、注册表、两游戏互不干扰 ═════════════════════
    std::printf("\n-- §2 Mixed 载入后的控件与映射 --\n");
    {
        // 两个游戏故意给不同的列表：一个共用 plan 的实现在下面两处立刻
        // 相互矛盾。
        writeConf(home.path(),
                  QStringLiteral("layout=mixed\n"
                                 "locale=zh-CN\n"
                                 "future.key=42\n"
                                 "layout.windows.world=player-bars,damage-share\n"
                                 "layout.windows.rise=damage-rows\n"));

        ControlPanel cp;
        cp.show();
        pumpEvents();

        check(cp.layoutModeInForce() == Mode::Mixed,
              "§2.1 conf 的 layout=mixed 读回 Mixed");
        check(cp.layoutCombo_->currentIndex() == ControlPanel::kLayoutComboMixedIndex,
              "§2.1b 下拉框显示 Mixed 而不是整块");
        check(cp.layoutComboMode() == Mode::Mixed,
              "§2.1c 下拉框 → 模式的反向映射同样是 Mixed");
        if (cp.layoutMixedBtn_)
            check(cp.layoutMixedBtn_->isChecked(),
                  "§2.1d 分段控件亮在 Mixed 上");
        if (cp.layoutSingleBtn_)
            check(!cp.layoutSingleBtn_->isChecked(),
                  "§2.1e 分段控件没有亮在整块上");

        // §2.2 十一个开关 == 注册表（拼写 + 顺序 + 数量）。
        check(mhw::kWindowLayoutRowCount == 11,
              "§2.2 注册表恰好 11 行",
              QString::number(mhw::kWindowLayoutRowCount));
        const auto toggles =
            cp.findChildren<QCheckBox *>(QStringLiteral("mixedWinToggle"));
        check(toggles.size() == 11,
              "§2.2b 分组列里有 11 个「独立/合并」开关",
              QString::number(toggles.size()));
        QStringList memberKeys;
        for (QCheckBox *box : cp.mixedGroupToggles_)
            memberKeys.append(box->property("settingsKey").toString());
        check(memberKeys == keys,
              "§2.2c 开关顺序与 11 个拼写 == window_layout_plan.h 注册表",
              memberKeys.join(QLatin1Char(',')));
        check(cp.mixedGroupStateLabels_.size() == 11,
              "§2.2d 11 个 独立窗口/合并显示 状态标注",
              QString::number(cp.mixedGroupStateLabels_.size()));

        // §2.3 块长在「窗口分组」列里，标题 / 说明 / 两个快捷键都在。
        check(cp.mixedGroupHost_ != nullptr &&
                  cp.mixedGroupHost_->parentWidget() == cp.windowGroupCol_,
              "§2.3 混合块挂在 windowGroupCol_（窗口分组列）下");
        QLabel *cap = captionFor(cp);
        check(cap != nullptr, "§2.3b 分组标题存在");
        if (cap)
            checkQ(cap->text(),
                   mhw::consoleText(QStringLiteral("console.layout.group_title")),
                   "§2.3c 标题用 console.layout.group_title");
        // 实测缺陷4 (user-8): 「下面的文案也没必要留」 —— 那段自定义模式
        // 说明整段删掉了，所以 mixedGroupHint 根本不再被创建。KEY 没有
        // 变成死键：它还是「混合」段按钮的 tooltip（console.layout.
        // custom_hint 在 copy_semantics 的键表里，仍有一处真实使用）。
        const auto hints =
            cp.findChildren<QLabel *>(QStringLiteral("mixedGroupHint"));
        check(hints.isEmpty(),
              "§2.3d 底部的自定义模式说明已删除（实测缺陷4）");
        auto *mixedSegBtn = cp.findChild<QPushButton *>(
            QStringLiteral("layoutSegMixed"));
        check(mixedSegBtn != nullptr
                  && mixedSegBtn->toolTip().contains(mhw::consoleText(
                      QStringLiteral("console.layout.custom_hint"))),
              "§2.3e console.layout.custom_hint 仍然是「混合」按钮的 "
              "tooltip（key 未失效）",
              mixedSegBtn ? mixedSegBtn->toolTip() : QString());
        QPushButton *detachAllBtn = namedButton(cp, "mixedDetachAll");
        QPushButton *mergeAllBtn = namedButton(cp, "mixedMergeAll");
        check(detachAllBtn != nullptr && mergeAllBtn != nullptr,
              "§2.3f 全部独立 / 全部合并两个按钮都在");
        if (detachAllBtn)
            checkQ(detachAllBtn->text(),
                   mhw::consoleText(QStringLiteral("console.layout.detach_all")),
                   "§2.3g 「全部独立」按钮用 console.layout.detach_all（全部独立）");
        if (mergeAllBtn)
            checkQ(mergeAllBtn->text(),
                   mhw::consoleText(QStringLiteral("console.layout.merge_all")),
                   "§2.3h 「全部合并」按钮用 console.layout.merge_all（全部合并）");

        // §2.4 勾选态来自 conf 的 world 行；rise 行是另一个游戏的。
        const QStringList worldKeys =
            cp.mixedPlanFor(mhw::GameId::World).detachedKeys();
        const QStringList riseKeys =
            cp.mixedPlanFor(mhw::GameId::Rise).detachedKeys();
        check(worldKeys == (QStringList{QStringLiteral("player-bars"),
                                        QStringLiteral("damage-share")}),
              "§2.4 world 的 plan == conf 的 layout.windows.world",
              worldKeys.join(QLatin1Char(',')));
        check(riseKeys == (QStringList{QStringLiteral("damage-rows")}),
              "§2.4b rise 的 plan == conf 的 layout.windows.rise（两游戏的 plan "
              "互不干扰）",
              riseKeys.join(QLatin1Char(',')));
        const int bad = mismatches(cp, cp.mixedPlanFor(mhw::GameId::World));
        check(bad == 0,
              "§2.4c 11 个开关的勾选态与 独立窗口/合并显示 标注全部来自 "
              "world 的 plan",
              QString::number(bad) + QStringLiteral(" 处不一致"));

        const QString det =
            mhw::consoleText(QStringLiteral("console.layout.detached"));
        const QString mer =
            mhw::consoleText(QStringLiteral("console.layout.merged"));
        check(!det.isEmpty() && !mer.isEmpty() && det != mer,
              "§2.4d detached / merged 两个标注都取得到且互不相同",
              det + QStringLiteral(" / ") + mer);

        // §2.5 只建一次。冷启动没有选择（分组列整个是空的，块是靠
        //     buildWindowInspectorFace 的第一份布局就位的），所以先点
        //     导航卡建立选择，再点一个兄弟窗口 chip 触发真正的 refill：
        //     两次之后块与第一个开关的身份都不许变。
        QFrame *hostBefore = cp.mixedGroupHost_;
        QCheckBox *firstBefore =
            cp.mixedGroupToggles_.isEmpty() ? nullptr
                                            : cp.mixedGroupToggles_.first();
        check(hostBefore != nullptr && firstBefore != nullptr,
              "§2.5 冷启动（无选择）时块与 11 个开关已经存在");
        // selectPanel() 只改选择状态，模型刷新是各导航路径自己的收尾，
        // 这里显式做一次 —— 和控制台真实路径一样，然后点 chip 走
        // selectWindow() → deferred refreshWindowModel() 这条用户路径，
        // §2.5c/§2.5d 断言的就是那一次真 refill 之后的身份。
        cp.selectPanel(0);
        cp.refreshWindowModel();
        pumpEvents();
        const auto chips =
            cp.findChildren<QPushButton *>(QStringLiteral("groupWinChip"));
        check(!chips.isEmpty(),
              "§2.5b 建立选择后分组列里出现兄弟窗口 chip",
              QString::number(chips.size()));
        if (!chips.isEmpty())
            chips.first()->click();
        pumpEvents();
        check(cp.mixedGroupHost_ == hostBefore,
              "§2.5c 分组列 refill 后混合块还是同一个对象（不被 purge 删除）");
        check(!cp.mixedGroupToggles_.isEmpty() &&
                  cp.mixedGroupToggles_.first() == firstBefore,
              "§2.5d refill 后第一个开关还是同一个对象");
        check(cp.mixedGroupHost_ != nullptr && !cp.mixedGroupHost_->isHidden(),
              "§2.5e Mixed 下混合块的可见标志是「显示」");
        if (cp.mixedGroupHost_)
            check(cp.mixedGroupHost_->isVisible(),
                  "§2.5f 有选择时混合块真的在屏幕上可见");
        check(cp.findChildren<QCheckBox *>(QStringLiteral("mixedWinToggle"))
                      .size() == 11,
              "§2.5g refill 后仍然是 11 个开关");

        cp.close();
        pumpEvents();
    }

    // ═══ §3 逐窗口切换：未勾选 = 回到母面板 ══════════════════════════════
    std::printf("\n-- §3 开关把行放回母面板 --\n");
    {
        writeConf(home.path(), QStringLiteral("layout=mixed\n"));
        ControlPanel cp;
        cp.show();
        pumpEvents();

        const bool blockVisible =
            cp.mixedGroupHost_ != nullptr && !cp.mixedGroupHost_->isHidden();
        check(blockVisible, "§3.0 混合块在 Mixed 下可见（前置条件）");
        if (!blockVisible) {
            // 后面每一节都在读这块，没块就没意义 —— 直接失败，不留假绿。
            cp.close();
            std::printf("\nRESULT: FAIL (%d checks, %d failures)\n",
                        checks, failures);
            return 1;
        }

        const QString det =
            mhw::consoleText(QStringLiteral("console.layout.detached"));
        const QString mer =
            mhw::consoleText(QStringLiteral("console.layout.merged"));
        QPushButton *detachAllBtn = namedButton(cp, "mixedDetachAll");
        QPushButton *mergeAllBtn = namedButton(cp, "mixedMergeAll");

        // §3.1 先钉死 player-bars 那一位，registry 的 tableRow 与位表一旦
        //      漂移，这里先红，而不是让 §3.4 的期望值一起跟着漂。
        const int barsIdx = mhw::windowLayoutRowIndexByKey(QStringLiteral("player-bars"));
        check(barsIdx >= 0, "§3.1 player-bars 在注册表里");
        const int barsParent = mhw::kWindowLayoutRows[barsIdx].parentSlot;
        const int barsRow = mhw::kWindowLayoutRows[barsIdx].tableRow;
        checkU32(mhw::windowLayoutRowMask(barsParent, barsRow), kBarsBit,
                 "§3.1b player-bars 认领 0x8 那一位");

        mhw::WindowLayoutPlan &plan = cp.mixedPlanFor(mhw::GameId::World);

        // §3.2 没有 conf 行 = 混合模式自己的可见回退：全部独立。
        check(plan == mhw::WindowLayoutPlan::allDetached(),
              "§3.2 无 conf 行时默认全独立（与 overlay 的可见回退一致）");
        check(plan.detachedKeys() == keys,
              "§3.2b 默认 plan 覆盖全部 11 行",
              plan.detachedKeys().join(QLatin1Char(',')));
        check(mismatches(cp, plan) == 0,
              "§3.2c 11 个开关全部勾选、标注全部是「独立窗口」");

        // §3.3 点一个勾着的开关 → 变合并，标注跟着变。
        QCheckBox *barsBox = toggleFor(cp, QStringLiteral("player-bars"));
        check(barsBox != nullptr && barsBox->isChecked(),
              "§3.3 player-bars 开关存在且勾选");
        if (barsBox && barsBox->isChecked()) {
            barsBox->click();
            pumpEvents();
            check(!plan.detached(barsParent, barsRow),
                  "§3.3b 点击后该行离开 plan（合并）");
            QLabel *state = stateLabelFor(cp, QStringLiteral("player-bars"));
            check(state != nullptr && state->text() == mer,
                  "§3.3c 该行标注变成「合并显示」");
        }

        // §3.4 全部合并 → 每一行都回到母面板。这就是「未勾选窗口回到母
        //      面板」的正面断言：retainedMask 把分块位全部还给母面板。
        check(mergeAllBtn != nullptr, "§3.4 「全部合并」按钮存在");
        if (mergeAllBtn) {
            mergeAllBtn->click();
            pumpEvents();
        }
        check(plan.detachedKeys().isEmpty(),
              "§3.4b 全部合并后 plan 为空（显式空 = 全部并回母面板）");
        checkU32(plan.retainedMask(barsParent, kPlayerMother), kPlayerMother,
                 "§3.4c player 母面板收回全部 6 位（0xffffffff）");
        checkU32(plan.retainedMask(mhw::kLayoutParentMonster, kPlayerMother),
                 kPlayerMother,
                 "§3.4d monster 母面板不受影响（仍是原 mask）");
        check(mismatches(cp, plan) == 0,
              "§3.4e 11 个开关全部不勾选、标注全部是「合并显示」");

        // §3.5 只勾一个 → 只有那一位离开母面板，而且正是 main.cpp 实测的
        //      fffffff7；其余两个母面板纹丝不动。
        QCheckBox *barsBox2 = toggleFor(cp, QStringLiteral("player-bars"));
        check(barsBox2 != nullptr && !barsBox2->isChecked(),
              "§3.5 全部合并后 player-bars 开关未勾选");
        if (barsBox2 && !barsBox2->isChecked()) {
            barsBox2->click();
            pumpEvents();
        }
        check(plan.detached(barsParent, barsRow),
              "§3.5b 点击后 player-bars 回到 plan（独立）");
        checkU32(plan.retainedMask(barsParent, kPlayerMother),
                 kPlayerMother & ~kBarsBit,
                 "§3.5c player 母面板只交出 player-bars 那一位 "
                 "(0xfffffff7，main.cpp 实测值)");
        checkU32(plan.retainedMask(mhw::kLayoutParentMonster, kPlayerMother),
                 kPlayerMother, "§3.5d monster 母面板仍不受影响");
        check(mismatches(cp, plan) == 0,
              "§3.5e 11 个开关与 plan 仍然一致");

        // §3.6 全部独立快捷键 + 拼写规则（不做大小写折叠）。
        check(detachAllBtn != nullptr, "§3.6 「全部独立」按钮存在");
        if (detachAllBtn) {
            detachAllBtn->click();
            pumpEvents();
        }
        check(plan.detachedKeys() == keys,
              "§3.6b 全部独立后 plan == 全部 11 行",
              plan.detachedKeys().join(QLatin1Char(',')));
        {
            int notDetached = 0;
            for (const QString &key : keys) {
                QLabel *lab = stateLabelFor(cp, key);
                if (!lab || lab->text() != det)
                    ++notDetached;
            }
            check(notDetached == 0,
                  "§3.6c 全部独立后 11 个标注全部是「独立窗口」",
                  QString::number(notDetached) + QStringLiteral(" 处不是"));
        }
        check(!cp.setMixedWindowDetached(QStringLiteral("Player-Bars"), true),
              "§3.6d 拼写不折叠：\"Player-Bars\" 不是注册表键，写入被拒");
        check(!cp.setMixedWindowDetached(QStringLiteral("player-armor"), true),
              "§3.6e 未知键 \"player-armor\" 同样被拒且不落盘");

        cp.close();
        pumpEvents();
    }

    // ═══ §4 持久化：只写当前游戏那一行，幂等，未知行保留 ═════════════════
    std::printf("\n-- §4 保存写 layout.windows.<game> 行 --\n");
    {
        const QString seed = QStringLiteral(
            "layout=mixed\n"
            "locale=zh-CN\n"
            "future.key=42\n"
            "layout.windows.world=player-bars,damage-share\n"
            "layout.windows.rise=damage-rows\n");
        writeConf(home.path(), seed);

        ControlPanel cp;
        cp.show();
        pumpEvents();

        // 改动 world 的 plan：把 player-bars 收回母面板 → {damage-share}。
        QCheckBox *barsBox = toggleFor(cp, QStringLiteral("player-bars"));
        check(barsBox != nullptr && barsBox->isChecked(),
              "§4.0 conf 的 world 行使 player-bars 勾选");
        if (barsBox && barsBox->isChecked()) {
            barsBox->click();
            pumpEvents();
        }

        // v1.1.5: the rail's 保存 button is gone — closeEvent is the user
        // path now, and it performs exactly this write. The assertions
        // below are about the write, so they drive it directly.
        saveLikeCloseEvent(cp);   // 保存 = 落盘（与关闭控制台同一写入口）
        pumpEvents();
        QByteArray once = readConf(home.path());
        check(!once.isEmpty(), "§4.0c 保存后 conf 可读");

        const QStringList worldRows =
            confValues(once, QByteArray("layout.windows.world="));
        const QStringList riseRows =
            confValues(once, QByteArray("layout.windows.rise="));
        check(worldRows.size() == 1,
              "§4.1 layout.windows.world 恰好一行（set 而不是 pile）",
              QString::number(worldRows.size()) + QStringLiteral(" 行"));
        checkQ(worldRows.value(0), QStringLiteral("damage-share"),
               "§4.1b 写出的是当前 world plan {damage-share}");
        check(riseRows.size() == 1,
              "§4.1c 保存 world 时 rise 行仍在，且只有一行",
              QString::number(riseRows.size()) + QStringLiteral(" 行"));
        checkQ(riseRows.value(0), QStringLiteral("damage-rows"),
               "§4.1d rise 行逐字未被改动（还是 conf 里的 damage-rows）");

        const QStringList localeRows = confValues(once, QByteArray("locale="));
        check(localeRows.size() == 1 && localeRows.value(0) == QStringLiteral("zh-CN"),
              "§4.2 locale= 行原样保留",
              localeRows.join(QLatin1Char('|')));
        check(confValues(once, QByteArray("future.key=")).size() == 1,
              "§4.3 未知键 future.key=42 原样保留");
        check(confValues(once, QByteArray("layout=")).size() == 1 &&
                  confValues(once, QByteArray("layout=")).value(0)
                      == QStringLiteral("mixed"),
              "§4.3b layout= 行仍在（Mixed 生效）");

        // §4.4 幂等：同一状态下再保存一次，字节完全相同。
        saveLikeCloseEvent(cp);
        pumpEvents();
        const QByteArray twice = readConf(home.path());
        check(twice == once, "§4.4 连续两次保存，conf 字节完全相同（幂等）");
        check(confValues(twice, QByteArray("layout.windows.world=")).size() == 1,
              "§4.4b 第二次保存后 world 行仍只有一行");

        // §4.5 行与内存的回环：overlay 用同一个解析器读回同一份计划。
        const mhw::WindowLayoutPlan reread = mhw::resolveWindowLayoutPlan(
            mhw::GameId::World, Mode::Mixed, QString::fromUtf8(twice));
        check(reread == cp.mixedPlanFor(mhw::GameId::World),
              "§4.5 resolveWindowLayoutPlan(读回的文件) == 控制台内存里的 plan");
        const mhw::WindowLayoutPlan riseReread = mhw::resolveWindowLayoutPlan(
            mhw::GameId::Rise, Mode::Mixed, QString::fromUtf8(twice));
        check(riseReread == cp.mixedPlanFor(mhw::GameId::Rise),
              "§4.5b rise 行的回环同样一致（没有被 world 的保存污染）");

        // §4.6 离开 Mixed 再保存：plan 行不被重写也不被删除（Single 从不
        //      读它，但它是 mixed 域的既存记录），rise 行仍然原样。
        check(pickAndApply(cp, ControlPanel::kLayoutComboSingleIndex),
              "§4.6 切回 Single 并应用");
        saveLikeCloseEvent(cp);
        pumpEvents();
        const QByteArray afterSingle = readConf(home.path());
        check(confValues(afterSingle, QByteArray("layout.windows.world="))
                      .value(0) == QStringLiteral("damage-share"),
              "§4.6b Single 下保存：world 的 plan 行原样保留（没有被删）");
        check(confValues(afterSingle, QByteArray("layout.windows.rise="))
                      .value(0) == QStringLiteral("damage-rows"),
              "§4.6c Single 下保存：rise 行仍然逐字未动");
        check(confValues(afterSingle, QByteArray("locale=")).value(0)
                  == QStringLiteral("zh-CN"),
              "§4.6d locale 行第三次保存后仍然原样");
        check(confValues(afterSingle, QByteArray("layout=")).isEmpty(),
              "§4.6e Single 仍不写 layout= 行（老契约没被破坏）");

        cp.close();
        pumpEvents();
    }

    // ═══ §5 Single 不写 plan 行；只读载入不碰文件 ═════════════════════════
    std::printf("\n-- §5 Single 不写 plan 行 / 只读载入不碰文件 --\n");
    {
        const QString seed = QStringLiteral("layout=single\n");
        writeConf(home.path(), seed);
        const QByteArray before = readConf(home.path());

        ControlPanel cp;
        cp.show();
        pumpEvents();
        const QByteArray afterLoad = readConf(home.path());
        check(afterLoad == before,
              "§5.1 只读载入（构造 + 刷新）一个字节都没写进 conf");
        check(confValues(afterLoad, QByteArray("layout.windows.")).isEmpty(),
              "§5.1b Single 的 conf 里没有任何 layout.windows. 行");

        saveLikeCloseEvent(cp);
        pumpEvents();
        const QByteArray afterSave = readConf(home.path());
        check(confValues(afterSave, QByteArray("layout.windows.")).isEmpty(),
              "§5.2 Single 下保存不写 plan 行（Single 从不读它）");

        // 进入 Mixed 再保存：plan 行这时才出现，值 = 混合模式的可见回退
        // （全部 11 行独立），且顺序是注册表顺序。
        check(pickAndApply(cp, ControlPanel::kLayoutComboMixedIndex),
              "§5.3 显式进入 Mixed 并应用");
        saveLikeCloseEvent(cp);
        pumpEvents();
        const QByteArray afterMixed = readConf(home.path());
        const QStringList rows =
            confValues(afterMixed, QByteArray("layout.windows.world="));
        check(rows.size() == 1, "§5.3b Mixed 下保存写出一行 world plan",
              QString::number(rows.size()) + QStringLiteral(" 行"));
        checkQ(rows.value(0), keys.join(QLatin1Char(',')),
               "§5.3c 值 = 全部 11 行、注册表顺序");
        check(confValues(afterMixed, QByteArray("layout.windows.rise=")).isEmpty(),
              "§5.3d 另一个游戏（rise）的行没有被凭空造出来");
        check(confValues(afterMixed, QByteArray("layout=")).value(0)
                  == QStringLiteral("mixed"),
              "§5.3e layout=mixed 同时写入");

        cp.close();
        pumpEvents();
    }

    // ═══ §6 计数模型：Mixed 不再把独立窗口折叠进母面板 ═══════════════════
    //
    // buildWindowStates() 回答的是「控制台画板能画哪几个窗口」。v1.1.3
    // 之前 Mixed 下它答 Single 的答案是有意的（预览宿主只在 Split 构建），
    // 拿它去数「这次运行有几个窗口」就会少算用户逐个独立出去的那些，所以
    // 新增了 buildWindowTopology() 作计数模型：Single / Split 原样返回
    // buildWindowStates()（硬要求：零变化），Mixed 下补上独立窗口、并
    // 按覆盖层的挂载门去掉不再挂载的母面板。
    //
    // v1.1.3 把缺口关掉了：Mixed 也按 plan 构建预览宿主，于是画板列表
    // 自己就带独立窗口（subIndex >= 0、真实几何），拓扑只需在宿主不在场
    // 时才补行。§6.4 起的断言随之改写——旧断言钉的正是那个已知缺口。
    std::printf("\n-- §6 buildWindowTopology()：计数反映真实拓扑 --\n");
    {
        // 前置：Mixed 生效，显式空 plan 行 = 全部并回母面板。
        writeConf(home.path(), QStringLiteral("layout=mixed\n"
                                              "layout.windows.world=\n"));
        ControlPanel cp;
        cp.show();
        pumpEvents();

        check(cp.layoutModeInForce() == Mode::Mixed,
              "§6.0 conf 的 layout=mixed 只读载入生效（前提）");
        const mhw::WindowList mergedStates = cp.buildWindowStates();
        const mhw::WindowList mergedTopo   = cp.buildWindowTopology();
        check(!mergedStates.isEmpty(),
              "§6.1 全部合并时画板列表非空（前提）");
        check(mergedTopo.size() == mergedStates.size(),
              "§6.2 全部合并：计数 == 画板列表（没有独立窗口时两者相同）");

        // ── 6.3 逐个独立一个窗口：画板列表 +1，宿主真的建出来了 ──
        //
        // v1.1.3 之前的断言在这里钉的是那个「已知缺口」：宿主只在 Split
        // 构建，所以画板列表纹丝不动（仍然是母面板那几行），独立窗口只能
        // 由 buildWindowTopology() 事后补。缺口关掉之后，同一条断言反过来
        // 就是修复的验收：画板模型自己多了那一行，而且它是宿主建出来的
        // sub 行（subIndex >= 0），画布拿到了对应绑定，计数于是不再需要
        // 补任何东西。
        check(cp.setMixedWindowDetached(QStringLiteral("player-bars"), true),
              "§6.3 把 player-bars 独立出去（合法 key）");
        cp.refreshWindowModel();
        pumpEvents();
        const mhw::WindowList detStates = cp.buildWindowStates();
        const mhw::WindowList detTopo   = cp.buildWindowTopology();
        check(detStates.size() == mergedStates.size() + 1,
              "§6.4 画板列表 +1：独立出去的窗口出现在画板模型里",
              QString("%1 -> %2").arg(mergedStates.size())
                  .arg(detStates.size()));
        const mhw::WindowState *boardSubRow = nullptr;
        for (const mhw::WindowState &w : detStates)
            if (w.key == QStringLiteral("player-bars") && w.subIndex >= 0)
                boardSubRow = &w;
        check(boardSubRow != nullptr,
              "§6.4b 画板列表里多出来的那一行是 sub 行（宿主建的），"
              "不是母面板行");
        if (boardSubRow)
            check(boardSubRow->panelIndex == mhw::kLayoutParentPlayer,
                  "§6.4c 它归属于 player 这一组");
        // 画布那一半：宿主把自己交给画布的绑定数 == 独立窗口数。这行
        // 断言之前永远是 0（宿主在 Mixed 下根本不存在），也就是父会话
        // 拍到的「画布头 chip 0 个窗口」的根因。
        check(cp.canvas_ && cp.canvas_->subWindowCount() == 1,
              "§6.4d 画布收到 1 个子窗绑定（= 独立窗口数）",
              cp.canvas_ ? QString::number(cp.canvas_->subWindowCount())
                         : QStringLiteral("no canvas"));
        // 宿主在场时，计数模型不再需要事后补行：两个列表逐行逐字段相同。
        check(sameWindowList(detTopo, detStates),
              "§6.4e 计数 == 画板列表（宿主在场，拓扑零补行）");
        check(detTopo.size() == mergedTopo.size() + 1,
              "§6.5 计数 +1：独立出去的窗口被算进来了",
              QString("%1 -> %2").arg(mergedTopo.size())
                  .arg(detTopo.size()));
        const mhw::WindowState *detachedRow = nullptr;
        for (const mhw::WindowState &w : detTopo)
            if (w.key == QStringLiteral("player-bars") && w.subIndex >= 0)
                detachedRow = &w;
        check(detachedRow != nullptr,
              "§6.6 计数里多出来的那行就是 player-bars，subIndex >= 0 "
              "（独立窗，不是母面板行）");
        if (detachedRow)
            check(detachedRow->panelIndex == mhw::kLayoutParentPlayer,
                  "§6.6b 它归属于 player 这一组");

        // ── 6.7 顶栏副标题与侧栏计数读的是同一个模型 ──
        // 顶栏副标题只有在「分组视图」里才带窗口数（总览副标题没有），
        // 所以先像用户那样点进 player 组。
        cp.selectPanel(mhw::kLayoutParentPlayer, true);
        cp.refreshNavCounts();
        cp.refreshTopbarTitle();
        pumpEvents();
        const int playerWindows =
            rowsOfPanel(detTopo, mhw::kLayoutParentPlayer);
        check(playerWindows == 2,
              "§6.7 player 组 = 母面板 + player-bars = 2 窗",
              QString::number(playerWindows));
        if (cp.topbarSubtitle_)
            checkQ(cp.topbarSubtitle_->text(),
                   mhw::consoleText(
                       QStringLiteral("console.proto.top.sub.panel"))
                       .arg(playerWindows),
                   "§6.7b 顶栏副标题的 %1 就是这个窗口数（不再是面板名）");
        if (cp.navCounts_[mhw::kLayoutParentPlayer])
            checkQ(cp.navCounts_[mhw::kLayoutParentPlayer]->text(),
                   mhw::consoleText(
                       QStringLiteral("console.proto.wins.count"))
                       .arg(playerWindows),
                   "§6.7c 侧栏 player 行与顶栏说的是同一个数字");
        if (cp.navOverviewCount_)
            checkQ(cp.navOverviewCount_->text(),
                   mhw::consoleText(
                       QStringLiteral("console.proto.wins.count"))
                       .arg(detTopo.size()),
                   "§6.7d 总览行的总数 = 计数模型的全部行数");

        // ── 6.8 挂载门：交出全部行且保留掩码里没有 block 可画时，母面板
        //      不再挂载（main.cpp 的 planMotherPanelEnabled 的掩码半边）。
        //      计数必须跟着少一个，而 sibling 的母面板必须毫发无损。
        QPushButton *detachAllBtn = namedButton(cp, "mixedDetachAll");
        check(detachAllBtn != nullptr, "§6.8 「全部独立」按钮存在（前提）");
        if (detachAllBtn) {
            detachAllBtn->click();
            pumpEvents();
        }
        const mhw::WindowLayoutPlan &plan =
            cp.mixedPlanFor(mhw::GameId::World);
        // v1.1.4: the shortcut is scoped to the focused group. This block
        // is in the PLAYER group's view (§6.7 selected it), so 全部独立
        // detaches the player group's World rows — five, because 翔虫 is
        // Rise-only — and must leave the other two groups alone. The mount
        // gate below only needs "every row of THIS parent is detached",
        // which 5/5 is.
        int playerDetached = 0;
        int otherDetached = 0;
        for (int i = 0; i < mhw::kWindowLayoutRowCount; ++i) {
            const mhw::WindowLayoutRow &row = mhw::kWindowLayoutRows[i];
            const bool det = plan.detached(row.parentSlot, row.tableRow);
            if (!det)
                continue;
            if (row.parentSlot == mhw::kLayoutParentPlayer)
                ++playerDetached;
            else
                ++otherDetached;
        }
        check(playerDetached == 5,
              "§6.8b 全部独立：玩家组 5 行都在 plan 里（World 无翔虫）",
              QString::number(playerDetached));
        check(otherDetached == 0,
              "§6.8b2 怪物/伤害组未被这个快捷键改动",
              QString::number(otherDetached));
        // 三个面板的 mask 快照：关掉 player 的总开关只应该动 player 自己，
        // 另外两组必须原样（对照组的意义）。
        const uint32_t masksBefore[mhw::kLayoutParentCount] = {
            ControlPanel::maskFor(cp.ctl_[mhw::kLayoutParentPlayer]),
            ControlPanel::maskFor(cp.ctl_[mhw::kLayoutParentMonster]),
            ControlPanel::maskFor(cp.ctl_[mhw::kLayoutParentDamage]),
        };
        // player 的总开关关掉 → maskFor(player) == 0 → 保留掩码没有
        // block → 这个 parent 的母面板不挂载。直接拨控件状态（不点），
        // 因为 maskFor() 读的就是控件。
        if (cp.ctl_[mhw::kLayoutParentPlayer].master)
            cp.ctl_[mhw::kLayoutParentPlayer].master->setChecked(false);
        checkU32(ControlPanel::maskFor(
                     cp.ctl_[mhw::kLayoutParentPlayer]),
                 0u, "§6.8c player 总开关关掉后 maskFor == 0（前提）");
        checkU32(ControlPanel::maskFor(
                     cp.ctl_[mhw::kLayoutParentMonster]),
                 masksBefore[mhw::kLayoutParentMonster],
                 "§6.8f 另外两组的 mask 一个位都没动（对照）");

        int detachedFromPlayer = 0;
        int detachedFromMonster = 0;
        int detachedFromDamage = 0;
        for (int i = 0; i < mhw::kWindowLayoutRowCount; ++i) {
            const mhw::WindowLayoutRow &row =
                mhw::kWindowLayoutRows[static_cast<size_t>(i)];
            if (!plan.detached(row.parentSlot, row.tableRow))
                continue;
            if (row.parentSlot == mhw::kLayoutParentPlayer)
                ++detachedFromPlayer;
            if (row.parentSlot == mhw::kLayoutParentMonster)
                ++detachedFromMonster;
            if (row.parentSlot == mhw::kLayoutParentDamage)
                ++detachedFromDamage;
        }
        const auto motherMountsFor = [&plan](int panel,
                                             uint32_t mask) -> bool {
            if (panel == mhw::kLayoutParentPlayer)
                return mhw::PlayerSection::playerPanelHasBlocks(
                    plan.retainedMask(panel, mask));
            if (panel == mhw::kLayoutParentMonster)
                return mhw::MonsterSection::monsterPanelHasBlocks(
                    plan.retainedMask(panel, mask));
            return mhw::DamageSection::damagePanelHasBlocks(
                plan.retainedMask(panel, mask));
        };
        const int playerMounts = motherMountsFor(
            mhw::kLayoutParentPlayer,
            ControlPanel::maskFor(cp.ctl_[mhw::kLayoutParentPlayer]));
        check(playerMounts == 0,
              "§6.8d 前提：player 母面板交出全部行且保留掩码里没有 "
              "block，确实不该挂载");

        const mhw::WindowList gated = cp.buildWindowTopology();
        const int detachedPerPanel[mhw::kLayoutParentCount] = {
            detachedFromPlayer, detachedFromMonster, detachedFromDamage
        };
        for (int panel = 0; panel < mhw::kLayoutParentCount; ++panel) {
            const bool mounts =
                motherMountsFor(panel,
                                ControlPanel::maskFor(cp.ctl_[panel]));
            check(rowsOfPanel(gated, panel)
                      == (mounts ? 1 : 0) + detachedPerPanel[panel],
                  panel == mhw::kLayoutParentPlayer
                      ? "§6.8e 计数跟着挂载门走：不挂载的母面板不再占一个数"
                      : "§6.8g 对照组：另一组按自己的挂载门计数，独立窗口"
                        "照旧算",
                  QString("panel=%1 mounts=%2 rows=%3 det=%4")
                      .arg(panel).arg(mounts)
                      .arg(rowsOfPanel(gated, panel))
                      .arg(detachedPerPanel[panel]));
        }

        // ── 6.9 Single / Split 的计数模型 == 画板列表（逐行逐字段） ──
        check(pickAndApply(cp, ControlPanel::kLayoutComboSingleIndex),
              "§6.9 切到 Single（和用户一样的两步）");
        check(sameWindowList(cp.buildWindowTopology(),
                             cp.buildWindowStates()),
              "§6.9b Single：两个模型逐行逐字段完全相同（零变化）");
        check(pickAndApply(cp, ControlPanel::kLayoutComboSplitIndex),
              "§6.9c 切到 Split");
        const mhw::WindowList splitStates = cp.buildWindowStates();
        int splitSubRows = 0;
        for (const mhw::WindowState &w : splitStates)
            if (w.subIndex >= 0)
                ++splitSubRows;
        check(splitSubRows > 0,
              "§6.9d 前提：Split 的画板列表里真的有 sub 行（等值断言不空洞）",
              QString::number(splitSubRows));
        check(sameWindowList(cp.buildWindowTopology(), splitStates),
              "§6.9e Split：两个模型逐行逐字段完全相同（零变化）");

        cp.close();
        pumpEvents();
    }

    // ═══ §7 画布真的画出了独立窗口（v1.1.3 补完）══════════════════════
    //
    // §6 证明的是「模型/计数」对。这一节证明的是**画布这一帧**真的画了
    // 那些窗口 —— 用 paintEvent 亲自写下的矩形，不是源码匹配，也不是把
    // 画布的 grab() 前后两帧一减就宣布成功。
    //
    // 为什么必须查矩形而不是只看计数：模型可以算对而画布仍然什么都不画
    // （这正是 v1.1.3 之前的状态：buildWindowTopology() 数对了，画布头
    // chip 仍是「0 个窗口」，因为宿主根本没建）。
    std::printf("\n-- §7 画布在 Mixed 下真的画出独立窗口 --\n");
    {
        writeConf(home.path(),
                  QStringLiteral("layout=mixed\nlayout.windows.world=%1\n")
                      .arg(registryKeys().join(QLatin1Char(','))));
        ControlPanel cp;
        cp.show();
        pumpEvents();

        check(cp.layoutModeInForce() == Mode::Mixed,
              "§7.0 前提：Mixed 生效（全部 11 行独立）");
        check(cp.canvas_ != nullptr, "§7.1 控制台建出了画布");

        if (cp.canvas_) {
            cp.canvas_->update();
            pumpEvents();
            cp.canvas_->grab();      // 强制一次同步绘制

            const int subPainted = paintedSubBoxes(cp.canvas_);
            const int motherPainted = paintedMotherBoxes(cp.canvas_);
            check(subPainted > 0,
                  "§7.2 画布这一帧真的画出了至少一个独立窗口的框"
                  "（不是只有模型计数）",
                  QStringLiteral("painted sub=%1 mother=%2 subCount=%3")
                      .arg(subPainted).arg(motherPainted)
                      .arg(cp.canvas_->subWindowCount()));
            check(subPainted == cp.canvas_->subWindowCount(),
                  "§7.3 画出的子窗框数 == 画布自报的子窗数",
                  QStringLiteral("painted=%1 reported=%2")
                      .arg(subPainted).arg(cp.canvas_->subWindowCount()));
            check(keysOf(cp.buildWindowStates(), true)
                      == keysOf(cp.buildWindowTopology(), true),
                  "§7.4 Mixed 有宿主时：画板列表的 sub 行 == 拓扑模型的 sub 行"
                  "（计数模型不再需要事后补行）");

            // 像素对照：把一个窗口并回去，画布必须画出不同的像素。
            const int subBefore = paintedSubBoxes(cp.canvas_);
            const QImage shotBefore = cp.canvas_->grab()
                .toImage().convertToFormat(QImage::Format_ARGB32);
            check(cp.setMixedWindowDetached(QStringLiteral("damage-rows"), false),
                  "§7.5 把 damage-rows 合并回母面板（可见的变化）");
            pumpEvents();
            cp.canvas_->grab();
            const int subAfter = paintedSubBoxes(cp.canvas_);
            check(subBefore - subAfter == 1,
                  "§7.6 合并一个窗口后画布少画了正好一个框（逐个独立真的生效）",
                  QStringLiteral("before=%1 after=%2").arg(subBefore).arg(subAfter));

            const QImage shotAfter = cp.canvas_->grab()
                .toImage().convertToFormat(QImage::Format_ARGB32);
            const int diff = differingPixels(shotBefore, shotAfter);
            check(!shotBefore.isNull() && !shotAfter.isNull() && diff > 0,
                  "§7.7 两次画布 grab() 的像素确有差异（不是同一帧）",
                  QStringLiteral("differing pixels=%1").arg(diff));
        }

        // 负向对照：切回 Single，画布必须回到「只有母面板、没有子窗框」。
        check(pickAndApply(cp, ControlPanel::kLayoutComboSingleIndex),
              "§7.8 切回 Single");
        if (cp.canvas_) {
            cp.canvas_->grab();
            check(paintedSubBoxes(cp.canvas_) == 0,
                  "§7.9 Single：画布不再画任何子窗框（行为回到旧契约）",
                  QString::number(paintedSubBoxes(cp.canvas_)));
            check(paintedMotherBoxes(cp.canvas_) > 0,
                  "§7.10 Single：母面板照画（没有把画面清空）",
                  QString::number(paintedMotherBoxes(cp.canvas_)));
        }

        cp.close();
        pumpEvents();
    }
    // ═══ §8 实测缺陷3：窗口分组按当前游戏过滤 ════════════════════════════
    //
    // 注册表（window_layout_plan.h）是「两游戏共用的一张表」，所以它里面
    // 同时有 衣装(World 专属) 与 翔虫(Rise 专属) —— 计划要能按拼写解析，
    // 这没错。错的是把这张表原样列给用户：World 的控制台列出「翔虫」、
    // Rise 的控制台列出「衣装」，都是这局游戏根本没有的窗口，而分区总数
    // 早就被 panelSectionSchema() 按游戏掩掉了，那一行的计数永远是 0/0。
    //
    // 断言两游戏各自的 key 集合，且第 11 行（注册表身份）一行不少。
    std::printf("\n-- §8 窗口分组只列当前游戏有的窗口 --\n");
    {
        writeConf(home.path(), QStringLiteral("layout=mixed\n"));
        ControlPanel cp;
        cp.show();
        pumpEvents();
        cp.refreshWindowModel();
        pumpEvents();

        const QStringList reg = registryKeys();
        check(reg.size() == 11, "§8.0 注册表仍是 11 行（前置）",
              QString::number(reg.size()));
        const auto toggles =
            cp.findChildren<QCheckBox *>(QStringLiteral("mixedWinToggle"));
        check(toggles.size() == 11,
              "§8.1 分组列里仍是 11 个开关（隐藏行不删行）",
              QString::number(toggles.size()));

        // 行的「显示 / 隐藏」用 isHidden() 而不是 isVisible()：前者只读
        // 这一行自己有没有被 hide()，不受祖先（比如无选择时被藏起来的
        // 三列网格）影响 —— 这里断言的正是 refreshMixedGroupBlock() 的
        // 那一次 setVisible(inGame)，不是它有没有被祖先显示出来。
        auto listedKeys = [&] {
            QStringList keys;
            for (QCheckBox *box : cp.findChildren<QCheckBox *>(
                     QStringLiteral("mixedWinToggle"))) {
                const QWidget *row = box->parentWidget();
                if (row && !row->isHidden())
                    keys << box->property("settingsKey").toString();
            }
            keys.sort();
            return keys;
        };
        auto expect = [](QStringList keys, const QString &drop) {
            keys.removeAll(drop);
            keys.sort();
            return keys;
        };

        // ── World：不列 翔虫，伙伴也不在（注册表本来就没有伙伴行） ──
        QStringList worldWant = expect(reg, QStringLiteral("player-wirebug"));
        const QStringList worldGot = listedKeys();
        std::printf("  world 列出: %s\n", qPrintable(worldGot.join(',')));
        check(worldGot == worldWant,
              "§8.2 World 的窗口分组 = 注册表去掉 player-wirebug（10 行）",
              worldGot.join(','));
        check(worldGot.contains(QStringLiteral("player-mantles")),
              "§8.3 World 仍列 衣装（World 专属，必须在）",
              worldGot.join(','));
        check(!worldGot.contains(QStringLiteral("pet-damage"))
                  && !worldGot.contains(QStringLiteral("player-pets")),
              "§8.4 World 的分组里没有伙伴窗（伙伴仅 Rise）",
              worldGot.join(','));

        // ── Rise：不列 衣装 ──
        cp.switchGame(mhw::GameId::Rise);
        pumpEvents();
        cp.refreshWindowModel();
        pumpEvents();
        QStringList riseWant = expect(reg, QStringLiteral("player-mantles"));
        const QStringList riseGot = listedKeys();
        std::printf("  rise  列出: %s\n", qPrintable(riseGot.join(',')));
        check(riseGot == riseWant,
              "§8.5 Rise 的窗口分组 = 注册表去掉 player-mantles（10 行）",
              riseGot.join(','));
        check(!riseGot.contains(QStringLiteral("player-mantles")),
              "§8.6 Rise 的分组里没有 衣装", riseGot.join(','));
        check(riseGot.contains(QStringLiteral("player-wirebug")),
              "§8.7 Rise 仍列 翔虫（Rise 专属，必须在）", riseGot.join(','));

        // 切回 World：同一份块，随游戏来回（不是一次性隐藏）。
        cp.switchGame(mhw::GameId::World);
        pumpEvents();
        cp.refreshWindowModel();
        pumpEvents();
        check(listedKeys() == worldWant,
              "§8.8 切回 World 后分组列表复原（过滤跟着游戏走）",
              listedKeys().join(','));

        cp.close();
        pumpEvents();
    }

    // ═══ §9 实测缺陷6：Mixed(全独立) → 整块，覆盖层必须仍有窗口 ════════
    //
    // 用户实测：Mixed 里把 11 个窗口全设成独立，然后点「整块」，两块显示器
    // 上一个窗口都没有。§7.10 只证明控制台画布仍画母面板，证明不了**下一次
    // 覆盖层启动读到的 conf**：若切模式时把 Mixed 的 retained 掩码（全独立
    // 时是 0 / 0x20 / 0x8）当成用户掩码写回，覆盖层拿到 single + 空掩码，
    // 就是「两个显示器都空」。这一节断言字节，不再靠画布推断。
    std::printf("\n-- §9 Mixed 全独立 → 整块：conf 的掩码不能被 Mixed 污染 --\n");
    {
        const QString allDetached = registryKeys().join(QLatin1Char(','));
        writeConf(home.path(),
                  QStringLiteral("player=1ff\nmonster=3f\ndamage=f\npets=3\n"
                                 "maskVersion=1\nlayout=mixed\n"
                                 "layout.windows.world=%1\n")
                      .arg(allDetached));
        ControlPanel cp;
        cp.show();
        pumpEvents();
        cp.refreshWindowModel();
        pumpEvents();

        check(cp.layoutModeInForce() == Mode::Mixed,
              "§9.0 前提：起始是 Mixed（11 行全独立）");
        // v1.1.5: the conf detaches all eleven rows, but this console is on
        // WORLD, and the preview host does not mount a row the game does
        // not have (翔虫 is Rise-only) — the same filter buildWindowStates()
        // reads its sub rows from. The board therefore lists ten, and the
        // count agrees with what is painted. The intent of this
        // precondition — "the Mixed model really is in all-detached state"
        // — is unchanged; only the game-absent row is absent from it.
        check(keysOf(cp.buildWindowStates(), true).size() == 10,
              "§9.1 前提：Mixed 模型是全部独立行的子窗（World 无翔虫，10 个）",
              QString::number(keysOf(cp.buildWindowStates(), true).size()));

        check(pickAndApply(cp, ControlPanel::kLayoutComboSingleIndex),
              "§9.2 切到整块（按钮使能并被点击）");
        check(cp.layoutModeInForce() == Mode::Single,
              "§9.3 生效模式 = Single");

        const mhw::WindowList rows = cp.buildWindowStates();
        const QStringList motherKeys = keysOf(rows, false);
        // World has no pets panel, so Single's four-slot loop emits three
        // mothers here: player, monster, damage.
        check(rows.size() == 3,
              "§9.4 整块后模型是三个母面板（World 无伙伴）",
              QStringLiteral("%1 [%2]").arg(rows.size())
                  .arg(motherKeys.join(',')));
        int withSections = 0;
        for (const mhw::WindowState &w : rows)
            if (w.sectionsOn > 0)
                ++withSections;
        check(withSections == 3,
              "§9.5 三个母面板都还有分区可画（掩码没有被 Mixed retained 残留）",
              QString::number(withSections));
        if (cp.canvas_) {
            cp.canvas_->grab();
            check(paintedMotherBoxes(cp.canvas_) == 3,
                  "§9.5b 画布这一帧真的画出三个母面板（不是只有模型）",
                  QString::number(paintedMotherBoxes(cp.canvas_)));
        }

        // 下一次覆盖层启动读的就是这个文件。掩码被污染的话，这里先红。
        const QByteArray conf = readConf(home.path());
        std::printf("  conf after Mixed -> Single:\n%s\n", conf.constData());
        // Single writes NO `layout=` row on purpose: absent and
        // `layout=single` resolve identically, and writing the default
        // would look like a deliberate choice in every user's file.
        check(confValues(conf, "layout=").isEmpty(),
              "§9.6 conf 里没有 layout= 行（Single 是解析器的默认）",
              confValues(conf, "layout=").join(QLatin1Char('|')));
        check(confValues(conf, "layout.windows.world=")
                  == QStringList{allDetached},
              "§9.6b layout.windows.world= 原样保留（下次进 Mixed 还能用）",
              confValues(conf, "layout.windows.world=").join(QLatin1Char('|')));
        check(confValues(conf, "player=")
                  == QStringList{QStringLiteral("1ff")},
              "§9.7 player 掩码仍是 1ff（没被 Mixed retained 覆盖）",
              confValues(conf, "player=").join(QLatin1Char('|')));
        check(confValues(conf, "monster=")
                  == QStringList{QStringLiteral("3f")},
              "§9.8 monster 掩码仍是 3f",
              confValues(conf, "monster=").join(QLatin1Char('|')));
        check(confValues(conf, "damage=")
                  == QStringList{QStringLiteral("f")},
              "§9.9 damage 掩码仍是 f",
              confValues(conf, "damage=").join(QLatin1Char('|')));

        cp.close();
        pumpEvents();
    }

    // ═══ §10 实测缺陷5：全部独立 / 全部合并只作用于当前组 ═══════════════
    //
    // 用户实测：焦点在玩家组时点「全部独立」，怪物组和伤害组也被一起改了。
    // 这一节把两个快捷按钮写成「当前列出的这些行」而不是「整个注册表」：
    // 有焦点时只动该组，无焦点时仍是当前游戏的全部行（列表本来就这么显示）。
    std::printf("\n-- §10 全部独立/全部合并只作用于当前组 --\n");
    {
        writeConf(home.path(),
                  QStringLiteral("layout=mixed\nlayout.windows.world=\n"));
        ControlPanel cp;
        cp.show();
        pumpEvents();
        cp.refreshWindowModel();
        pumpEvents();

        check(cp.layoutModeInForce() == Mode::Mixed, "§10.0 前提：Mixed 生效");
        check(keysOf(cp.buildWindowStates(), true).isEmpty(),
              "§10.1 前提：显式空行 = 全部合并，没有独立窗口");

        auto detachedCount = [&cp](int parent) {
            const mhw::WindowLayoutPlan &plan =
                cp.mixedPlanFor(mhw::GameId::World);
            int n = 0;
            for (int i = 0; i < mhw::kWindowLayoutRowCount; ++i)
                if (mhw::kWindowLayoutRows[i].parentSlot == parent
                    && plan.detached(mhw::kWindowLayoutRows[i].parentSlot,
                                     mhw::kWindowLayoutRows[i].tableRow))
                    ++n;
            return n;
        };
        auto *detachAll = cp.findChild<QPushButton *>(
            QStringLiteral("mixedDetachAll"));
        auto *mergeAll = cp.findChild<QPushButton *>(
            QStringLiteral("mixedMergeAll"));
        check(detachAll && mergeAll, "§10.2 两个快捷按钮存在");

        if (detachAll && mergeAll) {
            // (A) 焦点在玩家母面板 → 全部独立：只有玩家组 5 行离开。
            cp.selectWindow(0, -1);
            pumpEvents();
            detachAll->click();
            pumpEvents();
            std::printf("  player focus + detach all: player=%d monster=%d damage=%d\n",
                        detachedCount(0), detachedCount(1), detachedCount(2));
            check(detachedCount(0) == 5,
                  "§10.3 玩家组 5 行全部独立（World 无翔虫）",
                  QString::number(detachedCount(0)));
            check(detachedCount(1) == 0 && detachedCount(2) == 0,
                  "§10.4 怪物组与伤害组一行都没动",
                  QStringLiteral("monster=%1 damage=%2")
                      .arg(detachedCount(1)).arg(detachedCount(2)));

            // (B) 焦点在怪物母面板 → 全部独立：怪物组 2 行离开，而玩家组
            //     保持上一轮的 5 行（这一对断言是有内容的：整表赋值会把
            //     玩家组也改掉）。
            cp.selectWindow(1, -1);
            pumpEvents();
            detachAll->click();
            pumpEvents();
            std::printf("  monster focus + detach all: player=%d monster=%d damage=%d\n",
                        detachedCount(0), detachedCount(1), detachedCount(2));
            check(detachedCount(1) == 2,
                  "§10.5 怪物组 2 行全部独立",
                  QString::number(detachedCount(1)));
            check(detachedCount(0) == 5 && detachedCount(2) == 0,
                  "§10.6 玩家组保留上一轮的 5 行、伤害组没动",
                  QStringLiteral("player=%1 damage=%2")
                      .arg(detachedCount(0)).arg(detachedCount(2)));

            // (C) 焦点在玩家子窗 → 全部合并：只把玩家组并回去。此处怪物组
            //     是独立的，所以「怪物组仍是 2」是**有内容**的断言——
            //     整表清空同样会把它清成 0，§10.7 会当场变红。
            int playerSub = -1;
            for (const mhw::WindowState &w : cp.buildWindowStates())
                if (w.panelIndex == 0 && w.subIndex >= 0) {
                    playerSub = w.subIndex;
                    break;
                }
            check(playerSub >= 0, "§10.7 前提：玩家组有子窗可聚焦");
            if (playerSub >= 0) {
                cp.selectWindow(0, playerSub);
                pumpEvents();
                mergeAll->click();
                pumpEvents();
                std::printf("  player sub focus + merge all: player=%d monster=%d damage=%d\n",
                            detachedCount(0), detachedCount(1), detachedCount(2));
                check(detachedCount(0) == 0,
                      "§10.8 玩家组全部并回", QString::number(detachedCount(0)));
                check(detachedCount(1) == 2 && detachedCount(2) == 0,
                      "§10.9 怪物组保持独立（合并只作用于当前组）",
                      QStringLiteral("monster=%1 damage=%2")
                          .arg(detachedCount(1)).arg(detachedCount(2)));
            }

            // (D) 焦点在伤害母面板 → 全部独立：伤害组 3 行离开，怪物组 2 行
            //     保持独立（整表赋值会把两者一起改掉，这里先红）。
            cp.selectWindow(2, -1);
            pumpEvents();
            detachAll->click();
            pumpEvents();
            std::printf("  damage focus + detach all: player=%d monster=%d damage=%d\n",
                        detachedCount(0), detachedCount(1), detachedCount(2));
            check(detachedCount(2) == 3,
                  "§10.10 伤害组 3 行全部独立",
                  QString::number(detachedCount(2)));
            check(detachedCount(0) == 0 && detachedCount(1) == 2,
                  "§10.11 玩家组没动、怪物组仍是 2 行",
                  QStringLiteral("player=%1 monster=%2")
                      .arg(detachedCount(0)).arg(detachedCount(1)));
        }

        cp.close();
        pumpEvents();
    }

    // ═══ §11 侧栏换组后，列表与快捷按钮说的是同一组 ═════════════════════
    //
    // 审计发现（lane-audit）：selectPanel() 只改了 selectedPanel_ 与
    // panelSlotChosen_，从不重建分组列，于是「画布点玩家 → 侧栏点伤害 →
    // 全部独立」这条序列里，用户看着玩家那几行，按钮却按伤害焦点写。
    // 第 5 条把按钮收窄到当前组之后，这条缝就从「无害」变成「有害」，所以
    // 它必须和第 5 条一起有断言：列表可见的那几行 == 按钮会改的那几行。
    std::printf("\n-- §11 侧栏换组后，列表与快捷按钮说的是同一组 --\n");
    {
        writeConf(home.path(),
                  QStringLiteral("layout=mixed\nlayout.windows.world=\n"));
        ControlPanel cp;
        cp.show();
        pumpEvents();
        cp.refreshWindowModel();
        pumpEvents();

        auto listedKeys = [&cp] {
            QStringList keys;
            for (QCheckBox *box : cp.findChildren<QCheckBox *>(
                     QStringLiteral("mixedWinToggle"))) {
                const QWidget *row = box->parentWidget();
                if (row && !row->isHidden())
                    keys << box->property("settingsKey").toString();
            }
            keys.sort();
            return keys;
        };
        auto detachedCount = [&cp](int parent) {
            const mhw::WindowLayoutPlan &plan =
                cp.mixedPlanFor(mhw::GameId::World);
            int n = 0;
            for (int i = 0; i < mhw::kWindowLayoutRowCount; ++i)
                if (mhw::kWindowLayoutRows[i].parentSlot == parent
                    && plan.detached(mhw::kWindowLayoutRows[i].parentSlot,
                                     mhw::kWindowLayoutRows[i].tableRow))
                    ++n;
            return n;
        };

        const QStringList playerWant = {
            QStringLiteral("player-bars"), QStringLiteral("player-info"),
            QStringLiteral("player-mantles"), QStringLiteral("player-status"),
            QStringLiteral("player-weapon")};
        const QStringList damageWant = {
            QStringLiteral("damage-chart"), QStringLiteral("damage-rows"),
            QStringLiteral("damage-share")};

        cp.selectWindow(0, -1);
        pumpEvents();
        check(listedKeys() == playerWant,
              "§11.1 玩家组焦点下，分组列列出玩家 5 行（World 无翔虫）",
              listedKeys().join(QLatin1Char(',')));

        // 侧栏换组：这是一个「换了焦点」的动作，列表必须跟着换。
        cp.selectPanel(mhw::kLayoutParentDamage, true);
        pumpEvents();
        check(listedKeys() == damageWant,
              "§11.2 侧栏切到伤害组后，分组列立刻换成伤害 3 行"
              "（修复前会停在玩家那 5 行）",
              listedKeys().join(QLatin1Char(',')));

        auto *detachAll = cp.findChild<QPushButton *>(
            QStringLiteral("mixedDetachAll"));
        check(detachAll != nullptr, "§11.3 「全部独立」按钮存在");
        if (detachAll) {
            detachAll->click();
            pumpEvents();
            std::printf("  damage focus + detach all: player=%d monster=%d damage=%d\n",
                        detachedCount(0), detachedCount(1), detachedCount(2));
            check(detachedCount(2) == 3,
                  "§11.4 按钮写的是列表正在显示的那一组（伤害 3 行）",
                  QString::number(detachedCount(2)));
            check(detachedCount(0) == 0 && detachedCount(1) == 0,
                  "§11.5 屏幕上没列出的两组一行都没动",
                  QStringLiteral("player=%1 monster=%2")
                      .arg(detachedCount(0)).arg(detachedCount(1)));
        }

        cp.close();
        pumpEvents();
    }

    std::printf("\nRESULT: %s (%d checks, %d failures)\n",
                failures ? "FAIL" : "OK", checks, failures);
    return failures ? 1 : 0;
}
