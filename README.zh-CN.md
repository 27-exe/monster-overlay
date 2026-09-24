# Monster Overlay

[English](README.md) · **简体中文**

**怪物猎人:世界**与**怪物猎人:崛起**的 Wayland HUD 覆盖层。通过从外部读取
游戏进程的内存,把玩家 / 怪物 / 伤害面板绘制在游戏画面上 —— 不注入 DLL、
不修改游戏文件。

![任务中](assets/screenshots/03-in-quest.png)
![控制台](assets/screenshots/01-control-console.png)

## 已知问题

- **暂不支持《怪物猎人:荒野》。**本仓库当前没有荒野分支 —— 荒野风评
  不好,目前没有让我们想购买的理由,手里没有游戏自然也无法承诺适配。
  如果未来 DLC 让游戏重新变得值得入手,我们可能会从本代码基线重新评估。

## 环境要求

- x86_64 Linux,Wayland 会话 + 支持 layer-shell 的合成器(KDE Plasma、
  Hyprland、niri、sway 等)
- Qt 6.10 或更高(Widgets、Svg)与 `layer-shell-qt` —— Arch 上执行
  `sudo ./install-deps.sh` 一次装好
- Steam,推荐 **GE-Proton 10-34**(其他 Proton 9+ 也可)
- 不需要 mod,默认不需要任何特殊权限。世界范畴零额外依赖;崛起的伤害
  统计另需随包发布的 REFramework Lua producer,由控制台代为安装

## 安装

```bash
tar -xzf monster-overlay-v0.9.1-linux-x86_64.tar.gz
cd monster-overlay-v0.9.1
sudo ./install-deps.sh      # 一次性:Qt 6 + layer-shell-qt
./install.sh                # 可选:装到 ~/.local/bin 与 XDG 数据目录
```

## 启动

1. 启动游戏,进到主菜单之后。
2. 启动控制台并点 **START**:

```bash
./monster-control           # 跑过 install.sh 后直接 `monster-control`
```

三块面板会浮在游戏之上。面板与其子项的开关、位置、所在屏幕,都在控制台里设置。

### 快捷键(悬浮窗)

控制台启动悬浮窗后会自动隐藏,面板成为焦点窗口。此时:

| 按键 | 作用 |
|---|---|
| `Esc` | 关闭悬浮窗,焦点回到控制台 |
| `Space` | 把悬浮窗暂时折叠成一个小色块 —— 再按一次恢复 |
| 方向键 | 在布局模式中,微调焦点面板 10 px(`Shift` + 方向键 = 50 px) |

方向键微调属于布局模式(控制台可进入,或直接 `./monster-overlay --edit`)。
完整的布局模式速查 —— 点击聚焦、滚轮缩放、`Ctrl+S` 保存 —— 见
[docs/USAGE.md §4](docs/USAGE.md)。

## 面板内容

| 面板 | 显示 |
|---|---|
| 玩家 | 名称、血量 / 耐力、斩味、武器、翔虫(崛起)、衣装(世界)、异常状态、猫饭技能 |
| 怪物 | 当前目标、血量与部位破坏、异常状态、愤怒 / 体力(崛起)、体型金冠 |
| 伤害 | 每位玩家的伤害与 DPS —— 世界直接读取;崛起需用随包发布的 REFramework Lua producer(见 [docs/USAGE.md](docs/USAGE.md)) |

## 语言

首次运行跟随系统语言 —— `zh*` 用中文,其他一律英文。控制台的 `中文 | EN`
可即时切换(悬浮窗约 1 秒内跟随),并记住你的选择;保存面板开关**不会**改写
语言选择。

## 读取被拒绝时

Steam 会把游戏放进一个由你拥有的 user namespace,所以覆盖层通常**不需要
任何额外权限**。万一读取被拒绝,面板会给出 errno 与两种解法:

```bash
sudo setcap cap_sys_ptrace+ep ./monster-overlay   # 只作用于该二进制
sudo sysctl kernel.yama.ptrace_scope=0            # 临时,系统级
```

## 面板不显示?跑 `monster-doctor`

```bash
./monster-doctor            # 生成 monster-doctor-<时间戳>.txt
```

只读、无需权限、约 15 秒。它会记录:地址表从哪里找到的、游戏进程是否可见及
装载基址、读取判定与确切 errno、游戏实际使用的 Proton 与启动参数、以及覆盖层
自己的状态输出。把这个文件附到 issue 里即可:家目录路径折叠为 `~`,不采集环境
变量、Steam 账号信息与游戏内存内容。

## 从源码构建

```bash
cmake -S . -B build
cmake --build build -j"$(nproc)"
ctest --test-dir build          # 17 个套件中 7 个需要游戏在跑,会跳过
./build/monster-overlay         # 在仓库根目录运行:地图从 data/ 解析
```

需要 CMake ≥ 3.25、C++20 编译器、Qt 6.10+ 与 `layer-shell-qt`。

## 许可

Apache-2.0,见 [LICENSE](LICENSE)。派生自
[HunterPie v2](https://github.com/HunterPie/HunterPie);地址表与改编的图标
数据署名见 [assets/NOTICE](assets/NOTICE)。

完整使用说明:[docs/USAGE.md](docs/USAGE.md)。
