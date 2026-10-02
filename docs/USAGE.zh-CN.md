# 使用指南

`monster-overlay`（面板）、`monster-control`（控制台）、`monster-doctor`（诊断）。
环境要求与安装方式见 [../README.zh-CN.md](../README.zh-CN.md)。

## 1. 三个程序

| 程序 | 作用 |
|---|---|
| `monster-overlay` | 绘制 layer-shell 面板，读取游戏进程 |
| `monster-control` | 控制台：启动覆盖层、切换面板/分区、调整位置、选择显示器与界面语言 |
| `monster-doctor` | 一次性只读诊断报告（见 §8） |

## 2. 首次运行

```bash
sudo ./install-deps.sh      # Arch 上只需执行一次：Qt 6 + layer-shell-qt
./monster-control           # 打开控制台
```

先启动游戏并进入主菜单之后，在控制台点 **START**。控制台会保存开关状态，
以 `--mask-*` / `--no-*` 参数加上 `--game=<world|rise>` 拉起 `monster-overlay`
并隐藏自己，面板随即浮在游戏画面上。当覆盖层自己按下 `Esc` 退出时，控制台
重新显示。如果已经有一个在运行，再点 START 会被拒绝，而不是启动第二个。

## 3. 控制台

| 元素 | 作用 |
|---|---|
| 面板卡片（玩家 / 怪物 / 伤害 / 随从） | 总开关加各分区开关，切换时预览实时更新 |
| `START` / `STOP` | 启动 / 停止 `monster-overlay`；运行时控制台隐藏，退出后自动回来 |
| `ENTER EDIT` | 与 START 相同，但附带 `--edit` |
| 预览画布 | 点击选中面板，拖动移动，方向键微调 10 px（Shift 为 50 px）；`Ctrl`+滚轮缩放的是画布本身，不是面板 |
| 显示器选择器 | 面板显示在哪块输出上 |
| `中文 | EN` 按钮 | 切换界面语言，覆盖层约 1 秒内跟随 |
| 主题按钮 | 控制台浅色 / 深色主题 |
| `WORLD` / `RISE` 按钮 | 覆盖层针对哪一款游戏启动；运行中点击可热切换目标游戏 |
| 检测徽标 | 每 5 秒扫描 `/proc` 并指出当前运行的 Monster Hunter 进程（绿色 = 与选择一致，橙色 = 不一致，灰色 = 无） |
| `1` `2` `3` `4` | 选中玩家 / 怪物 / 伤害 / 随从卡片 |

随从轨道卡片、它的检查器页面和预览图块**只在 Rise 模式存在**——World 没有对应
数据源，所以选择 World 时这些会被移除，而不是显示一张空卡片。

## 4. 调整面板位置（编辑模式）

```bash
./monster-overlay --edit        # 无需运行游戏
```

| 操作 | 效果 |
|---|---|
| `←` `↑` `↓` `→` | 微调 10 px（Shift 为 50 px）——仅编辑模式 |
| 鼠标滚轮 | 缩放 0.5× – 3×——仅编辑模式 |
| `Space` | 折叠面板为小方块——编辑模式和运行模式都生效 |
| `Esc` | 退出覆盖层并把焦点还给控制台——编辑模式和运行模式都生效 |

**必须先点击面板**才能用键盘操作（layer-shell 的交互性会从 None 变为
OnDemand，合成器才被要求激活该 surface）。在覆盖层里方向键是唯一的移动手段，
拖动只存在于控制台的预览画布上。

**没有 `Ctrl+S`。** 位置一变就立刻落盘：每次微调和滚轮都会马上写入
`~/.config/monster-overlay/panels.ini`，所以收到 SIGTERM 或崩溃都不会丢位置。
退出编辑模式（`Esc`，或带 `--edit` 退出）时会再写一遍全部四个面板，这是唯一
一次整体写入。运行模式下方向键和滚轮都被忽略，因此打怪时面板不会漂移。

## 5. 面板内容

| 面板 | 显示 |
|---|---|
| 玩家 | 名称、生命 / 耐力、锋利度、武器、操虫棍（Rise）、披风（World）、异常状态、食技能 |
| 怪物 | 当前目标、生命与部位伤害、异常状态、愤怒计时 / 耐力（Rise）、冠饰大小 |
| 伤害 | 每人伤害与 DPS；Rise 下由 REFramework 生产器供给（见 §10） |
| 随从 | 按主人分组的随从伤害——仅 Rise；World 模式下轨道卡片、选项和浮动窗口都不显示 |

伤害面板有两件事，在读它的行之前值得先知道。

**有人掉线不会让其他人的行重新排序。** 每位猎人在整场任务中一直占着自己认领的那
一行。World 模式下，中途掉线的猎人会把自己的行留在面板上：名字、伤害和 DPS 都停在
掉线那一刻——只是数字不再变化，这一行本身既不会置灰，也没有任何别的标记。仍在任务
中的猎人各行其位、继续累计，不会有谁的数字被贴到别人名下。同名猎人重连时，该行会以
新的基线继续。（Rise 的行由生产器每帧上报的内容重新生成。）

**行内武器槽里的灰色 `?` 表示「这一行没有武器数据」** —— 覆盖层没能解析出该猎人的
武器。它不是图标损坏或缺失，也不是面板还在加载时的占位。某些队伍组合（例如房主 + 3
位猎人 + 1 位盟友）会遇到这种情况。看到它应当去查这一行，而不是当成一张没出现的图。

## 6. 命令行

### `monster-overlay`

| 参数 | 含义 |
|---|---|
| `-m`, `--map <path>` | 地址映射文件；覆盖运行时搜索顺序 |
| `--game auto\|world\|rise` | 读取哪款游戏（默认 `auto`） |
| `--poll <ms>` | 轮询间隔，默认 250 |
| `--locale <code>` | 本次运行的界面语言（`zh-CN`、`en-US`） |
| `--edit` | 编辑模式（见 §4） |
| `--mask-player\|monster\|damage\|pets <hex32>` | 各分区可见性；由控制台传入 |
| `--no-player\|no-monster\|no-damage\|no-pets` | 隐藏整个面板 |
| `--output-player\|monster\|damage\|pets <name>` | 把某个面板固定到指定 `QScreen`（留空 = 跟随系统主屏）；由控制台传入 |
| `--version`, `--help` | |

`--game auto` 会扫描 `/proc` 查找运行中的 Monster Hunter 进程；`--game`
为 `world` / `rise` 则强制使用指定 reader。`--no-pets` 只对 Rise 有意义：
World 无论带不带该参数都不会挂载随从伤害面板。

### `monster-control`

| 参数 | 含义 |
|---|---|
| `--locale <code>` | 本次运行的界面语言 |
| `--light` | 浅色主题 |
| `--snap <file>` | 把控制台渲染成 PNG 后退出 |
| `--print-locale` | 打印最终语言及其来源，然后退出 |
| `--print-screen-info` | 打印检测到的显示器，然后退出 |
| `--version` / `--help`，`-v` / `-h` | 打印版本 / 用法后退出（不打开窗口） |

### `monster-doctor`

| 参数 | 含义 |
|---|---|
| `--world` / `--rise` | 把所有小节限定到某一款游戏 |
| `--out <file>` | 报告路径（默认 `./monster-doctor-<时间戳>.txt`） |
| `--seconds <n>` | 覆盖层状态检查的持续秒数，默认 6，接受 0–60 |
| `--no-overlay-run` | 跳过该检查（离线机器用） |
| `--version` | 打印版本后退出 |

## 7. 会写入哪些文件

| 路径 | 写入方 | 内容 |
|---|---|---|
| `~/.config/monster-overlay/monster-overlay.conf` | 控制台 | `player=` / `monster=` / `damage=` / `pets=` 掩码（hex32），选定语言后写入 `locale=`；无关行会被保留 |
| `~/.config/monster-overlay/panels.ini` | 覆盖层 | 各面板几何信息：每个面板一组（`player`、`monster`、`dps`、`pet-damage`），含 `ml`/`mt`/`mr`/`mb` 边距、`scale`、`opacity`、`bgAlpha`、`outputName`、`visible` |
| `~/monster-doctor-<时间戳>.txt` | doctor | 诊断报告 |

控制台还会把自己的窗口几何、窗口状态、分栏宽度和画布缩放保存在 Qt 默认
设置文件里（身份名为 `monster-control`）；覆盖层不读其中任何一项。

覆盖层**从不写入** conf 文件——它只在启动时读 `locale=`，运行期间轮询该文件的
mtime，因此切换控制台的语言按钮能在约一秒内作用于正在运行的覆盖层，无需任何 IPC。

## 8. 诊断

```bash
./monster-doctor            # 离线时不到一秒，报告写入 ./monster-doctor-<时间戳>.txt
```

报告是一个扁平文本文件，每个小节回答一个问题，顺序为：主机、三个可执行文件
的版本、地址映射（覆盖层使用的搜索目录列表，并标出其中哪些目录含 World / Rise
的 `.map`，外加开发树回退路径）、游戏进程、读取结论（决定面板能否工作的那一条，
附带确切 errno 和读取被拒时的补救办法）、游戏运行时（Proton 版本、容器、
Steam 启动选项）、一次覆盖层状态检查（默认 `--seconds 6`，所以离线的几项数值
要加上这几秒）、以及隐私说明。

可以直接把它附到 issue——`~` 下的路径会显示为 `~`，不会附加到任何东西，
也不会读取或修改任何数据，不收集环境变量、Steam 账号信息或游戏内存。

## 9. 界面语言的确定顺序

`--locale` 参数 → conf 里的 `locale=` 行（仅当点击 `中文 | EN` 按钮时才写入）
→ 检测到的系统语言（依次 `LC_ALL`、`LC_MESSAGES`、`LANG`，然后 `LANGUAGE`；
`zh*` 归为 `zh-CN`，其余包括 `C` 和未设置一律归为 `en-US`）。保存面板开关
不会写这一行，所以未改过设置的安装会一直跟随桌面语言。

覆盖层在轮询循环中重读 conf 的 mtime，该行变化时原地重新加载，无需重启。
控制台点击语言按钮时立即应用到自己的已注册控件。

## 10. Rise 实时伤害（REFramework）

Rise 不把实时伤害暴露给内存读取，它由游戏内一小段 REFramework Lua 脚本
产生。REFramework 1.5.9.1 把 Lua 文件访问限制在 `reframework/data`，因此
生产器在该目录下交替写两份快照（`mhr_damage_a.json` / `mhr_damage_b.json`），
覆盖层消费其中最新且序号有效的一份。控制台替你管理这条链路——选择 Rise 游戏，
然后在左侧轨道打开 **REFramework** 卡片（World 模式下不显示该卡片）。

卡片头显示覆盖层认为 Rise 安装在哪里：

| 头部行 | 含义 |
|---|---|
| `STEAM INSTALL: <dir>` | 在某个 Steam 库中找到了 `MonsterHunterRise.exe` |
| `RISE INSTALL NOT FOUND IN STEAM LIBRARIES` | 没有库包含该游戏；按钮保持禁用 |
| `RISE INSTALL IS NOT VALID: <dir>` | 找到了路径但不可用 |

接着是一行状态，按以下优先级：

| 状态行 | 含义 |
|---|---|
| `STATUS: REFRAMEWORK NOT INSTALLED` | 核心缺失（上方头部行仍会显示安装目录） |
| `STATUS: INSTALLATION INCOMPLETE — REINSTALL RECOMMENDED` | 清单文件无效，或 Lua 与自带副本不一致 |
| `STATUS: LUA NOT INSTALLED (no damage tracking)` | 核心正常，缺少生产器 |
| `STATUS: READY` | 两者都已安装且匹配 |

任何时候还可能出现另外两行：运行着 Monster Hunter 进程时的 `CLOSE THE GAME
BEFORE CHANGING REFRAMEWORK FILES`，以及上一次操作的结果（成功/失败及其细节）。

| 按钮 | 效果 |
|---|---|
| `INSTALL REFRAMEWORK + LUA` | 询问一次后，安装固定版本的 REFramework 及覆盖层的 Lua。优先使用本包内自带的压缩包（可离线）；若缺失则下载指定的上游版本并校验 SHA-256，之后才写入任何文件 |
| `REMOVE LUA` | 只移除覆盖层托管的 Lua |
| `REMOVE REFRAMEWORK + LUA` | 移除覆盖层 Lua 以及安装器清单记录的全部文件；外部安装的 REFramework 核心会保留，状态行会说明这一点 |
| `Fix menu popup` / `Restore default (popup every launch)` | 把 REFramework 的 `RememberMenuState` 设为 true / false，写入所有检测到的配置位置，并为每个文件保留第一份副本为 `.pre-monster-overlay` |

控制台强制执行并在卡片上逐条报告的规则：

- 安装或卸载前必须先关闭游戏；
- 手工改过的覆盖层 Lua **绝不覆盖**——安装会停下并提示「Lua 与自带副本不一致，
  请先移除」（用移除按钮清掉后，安装按钮即可工作）；
- 托管清单无效时，安装直接拒绝，而不是删除它不认识的文件；
- 已有操作进行中时，第二次操作会被拒绝，而不是叠加在后面。

**安装后需要启动或重启游戏**才会加载 Lua 生产器。挂接成功后，
`reframework_revision.txt` 和两份生产器文件（位于游戏目录的
`reframework/data/` 下）会出现在 `dinput8.dll` 旁边。

### 哪些伤害计入、那个随从是谁的

Lua 生产器按攻击者种类对每个伤害事件分类（Player / Companion / Pet /
Palico / Palamute / Unknown）。覆盖层再用 reader 已有的队伍名册判断一行意味着
什么：

- **Companion**（其他猎人的角色）或主人是其他猎人的随从，只有在开启
  `other players` 时才显示——那不是你的伤害；
- 自己的随从在开启 `local pets` 时显示；
- 生产器已把同一主人的所有随从聚合成一个 actor，所以随从行是**按主人**的
  ——不需要区分猫/狗，行标签用的是主人的名字而非动物的名字；
- 随从行的宠物标签保留为 `<follower> · pet`，以免被误读为该随队猎人自己的伤害；
- `Unknown` 攻击者永不显示。

归属判定在 `src/rise/rise_damage_roster.cpp` 中按以下顺序进行：

1. actor 自身的 `ownerEntityIndex`，与名册匹配；
2. 无主人的随从流，仅在**实测过**的布局中用原始实体索引解析——**`4`** =
   本地猎人的第二只随从，`5`/`6` = 队伍存在随队布局时的随队槽位 0 / 1 的随从；
3. `5`–`9` 且**没有**实测随队布局时，回落到历史的 `owner = id − 5` 连接
   （队友队伍）；
4. 仍未解析的保持为明确的通用行——不猜测，也绝不静默丢弃。

这些条件的门槛刻意很窄：第 2 步只在名册恰好只有一名人类猎人**且**该猎人是
本地时才生效，第 3 步只在没有随队布局时生效。无法证明属于某位具名猎人的流，
会保留生产器给出的标签。

## 11. 故障排查

| 现象 | 检查 |
|---|---|
| 面板空白 / 未连接 | 运行 `monster-doctor`，其 `read verdict` 小节会指出原因 |
| 内存读取被拒 | 面板会原样打印两种补救办法（对覆盖层二进制执行 `setcap cap_sys_ptrace+ep`，或临时 `sysctl kernel.yama.ptrace_scope=0`） |
| 面板出现在错误的屏幕 | 用控制台的显示器选择器；或用命令行 `--output-*` |
| Rise：伤害面板一直空白 | 从 REFramework 卡片安装生产器（见 §10）；安装时必须已关闭游戏 |
| Rise：安装成功后伤害仍空白 | 在 `re2_framework_log.txt` 中查生产器的 `loaded` 和 `hooked` 行，再看 `reframework/data/` 下是否有 `mhr_damage_{a,b}.json` |
| 手工改过的 Lua 阻止安装 | 卡片拒绝覆盖它；先移除 Lua，再重新安装 |
| 读取了错误的游戏 | `--game world` / `--game rise` 强制指定 reader；默认 `auto` 扫描 `/proc` |
| 伤害面板的行数比本局猎人多 | 每位猎人在整场任务中只占一行并保留该行；中途离开的猎人会留下自己那行冻结的数据。只有在面板没能看到任务结束的情况下，上一批人的行才会留下——回到集会区域（或重启游戏）后，下一局面板会重建这些行。如果仍能复现，请报告该局的队伍组合 |

## 12. 快捷键速查

**编辑模式**（`--edit`，或控制台的布局模式）：方向键微调 10 px（Shift 为
50 px），滚轮缩放 0.5×–3×。

**运行与编辑模式均可**：`Space` 折叠获得焦点的面板，`Esc` 退出覆盖层并把焦点
还给控制台。

**控制台**：`1`/`2`/`3`/`4` 选中玩家 / 怪物 / 伤害 / 随从卡片，`Ctrl`+滚轮
缩放预览画布，`←→↑↓` 在画布中移动选中面板（10 px，Shift 为 50 px）。
