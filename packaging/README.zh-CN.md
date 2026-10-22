# Monster Overlay v1.1.5

Linux 原生 HUD 覆盖层,支持 **怪物猎人:世界** 与 **怪物猎人:崛起** ——
玩家/怪物/伤害面板绘制在游戏之上(Wayland layer-shell),从进程外部读取
游戏内存。

## 1. 环境要求

- x86_64 Linux,Wayland 会话 + 支持 layer-shell 的合成器(KDE Plasma、
  Hyprland、niri、sway 等)。**不支持 X11。**
- **Steam + GE-Proton 10-34**(推荐;Proton 11 系列同样可用)
- **Qt 6.12**(Core/Gui/Widgets/Svg)、**glibc 2.34+** 与 `layer-shell-qt`。
  C++ 运行时须提供 **CXXABI_1.3.15**（GCC 14 或更高版本的 libstdc++）。
  旧版 Qt 或 libstdc++ 会在加载时报缺少符号。`./install-deps.sh` 仅处理 Arch 的
  运行依赖。

读取游戏内存通常开箱可用:Steam 会把游戏放进由你拥有的 user namespace,
满足内核的 ptrace 检查。万一读取被拒,覆盖层会自己报出原因,两种解法:
`sudo setcap cap_sys_ptrace+ep ./monster-overlay`(仅对该二进制生效,
文件被替换后失效)或 `sudo sysctl kernel.yama.ptrace_scope=0`(临时)。

**世界范畴不需要额外依赖。**崛起的伤害面板另需 `reframework/autorun/` 内随包发布的
REFramework Lua producer,由控制台代为安装(见第 2 节)。

## 2. 启动

```bash
tar -xzf monster-overlay-v1.1.5-linux-x86_64.tar.gz
cd monster-overlay-v1.1.5-linux-x86_64
./install-deps.sh      # 一次性:装 Qt 6 + layer-shell-qt
./monster-control      # 控制台
```

先启动游戏(进到主菜单之后),再在控制台顶栏点 **启动悬浮窗**。想额外把
二进制装到 `~/.local/bin`、地图装到 `~/.local/share/monster-overlay/data`,
跑 `./install.sh`。

界面首次运行跟随桌面语言(默认英文;系统 locale 为 `zh*` 时用中文)。
在 **设置 → 语言** 中修改后会记住选择,之后以保存值为准。

### 控制台

一个原生窗口:左侧是游戏与分组导航,中间是显示器总览,右侧是三列检查器,
点哪里就描述哪里。

- **三种布局。***整块* 把每个面板画成一块;*分块* 让每个分区组独立成窗;
  *混合* 逐个窗口决定独立还是并回所属主面板。选择按「游戏 × 布局」分别
  保存,两个游戏互不覆盖。
- **编辑布局** 在控制台画布上移动窗口;**在悬浮窗中编辑** 会真的启动悬浮窗,
  在游戏画面上拖动。
- **重置布局** 把当前分组恢复为出厂默认;**全部重置** 覆盖两个游戏、三种布局
  (会先确认);**导入布局包** 按文件声明的作用域覆盖;**导出布局** 把当前这
  一套写成文本。改动在关闭控制台时写入 —— 没有单独的保存按钮。
- 预览里画的是演示内容:控制台不读取游戏内存,只有进程检测是真的。

### 崛起伤害面板(REFramework)

打开 **设置 → 崛起环境 · REFramework 与脚本**。该节会显示安装状态,并可代为
安装 Lua producer。崛起的实时伤害无法从外部内存读取,因此由 producer 在
游戏内采样并写入 `reframework/data/`,覆盖层取最新的一份快照。

**修复菜单弹窗**按钮让 REFramework 不再每次启动都弹出菜单 —— 只改动一项
设置,并保留 `.pre-monster-overlay` 副本供手动还原;**还原默认**则改回原状。

## 3. 出问题了?跑 `monster-doctor`

```bash
./monster-doctor       # 生成 ./monster-doctor-<时间戳>.txt
```

一条命令,只读、不需要权限、约 15 秒。报告记录:地址表从哪里找到、能否看到
游戏进程(及其镜像基址)、reader 的判定结果(含具体 errno)、当前游戏使用的
Proton 版本,以及覆盖层自身在 stderr 上打印的状态行。把该文件附到 GitHub
issue/discussion 即可。

不收集任何隐私:home 下的路径显示为 `~`,不含环境变量、Steam 账号信息、
游戏内存内容或其他进程的命令行。
