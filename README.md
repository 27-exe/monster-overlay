# MHW Linux Overlay

Linux-native、仅支持 **Monster Hunter: World** 的无边框透明悬浮窗原型。参考 HunterPie 的 Apache-2.0 源码与 `MonsterHunterWorld.421810.map`，但不运行 HunterPie、WPF 或 Windows DLL 注入器。

## 当前能力

- Qt 6 + KDE `layer-shell-qt`：Wayland `overlay` 层、透明背景、无边框、鼠标穿透、不抢键盘焦点。
- 自动发现 Proton 下的 `MonsterHunterWorld.exe` Linux PID。
- 解析 HunterPie legacy `.map` 的 `Address` / `Offset` 项。
- 通过 `/proc/<pid>/mem` 只读读取（无写内存、无注入）。
- MVP 数据：
  - 大型怪物列表、ID、内部名、HP、耐力、愤怒时间；
  - 玩家 HP / 耐力；
  - 任务 ID、星级、状态、剩余时间、猫车次数；
  - 队员名、武器、MR、游戏自带统计伤害。

## 构建

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## 运行

```bash
# 真实读取；请先启动 MHW
./build/mhw-overlay

# 只看悬浮窗布局，不读游戏
./build/mhw-overlay --demo

# 指定地址表
./build/mhw-overlay --map /path/to/MonsterHunterWorld.421810.map
```

## 关键限制：Yama ptrace

本机 `kernel.yama.ptrace_scope=1`。这意味着一个普通的独立进程默认不能读同用户但非子进程的 MHW。程序会明确显示权限错误，不会尝试 sudo 或改系统配置。

当前原型已经验证了**本进程自读** (`process_vm_readv` + `/proc/pid/mem` fallback)，但还没有在 MHW 正在运行时验证跨 Proton 进程读取；这一步必须等你实际启动一局 MHW 后做 contract test。产品化时推荐用一个**极小的、有边界的 launcher/reader helper**处理进程关系/权限，而不是让整个 UI 以 root 运行。调试阶段也可以让 Steam 启动 overlay，使 reader 成为同一启动树的一部分；具体路径要在真机启动 MHW 后做 contract test 决定。

## 数据来源与许可

参考：<https://github.com/HunterPie/HunterPie>（Apache-2.0）。仓库保留其地址表与怪物 schema 的上游许可说明。原型阶段只内置当前 MHW `421810` 地址表。
