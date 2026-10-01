# monster-overlay 架构 UML 图

所有类名、方法签名、库边界、行号都从源码实测，不照抄 `docs/ARCHITECTURE.md`；
凡本文与代码不一致处，见文末「与 ARCHITECTURE.md 的差异」。

## 图目录

| # | 图 | 讲什么 | 权威源码位置 |
|---|---|---|---|
| 1 | `01-layers` | **四库分层与依赖方向**。core 不依赖任何游戏的硬边界，两个游戏库平级、互不知晓，mhw-ui 单向向上依赖，reader_factory 是「契约×实现」的唯一交汇 TU | `CMakeLists.txt:41/:89/:143/:199`（add_library）、`:75/:131/:176/:257-265`（target_link_libraries） |
| 2 | `02-contracts` | **核心契约类**：`IGameReader` 的两个纯虚、`GameSnapshot` 的字段面、`ZoneRef{game,id}`、`MonsterSnapshot`/`PartSnapshot`，以及两个 `final` reader | `src/core/game_reader.h:39`、`game_snapshot.h:14`、`core/zone_id.h:54`、`monster/monster_types.h`、`src/world/world_reader.h:17`、`src/rise/mhr_reader.h:546` |
| 3 | `03-registry` | **注册表与能力表**：3 个 register / 3 个 query、`Tables{world,rise}` 槽位、`kTraits[]` 能力行，以及**谁在什么时候注册**——静态初始化器，不是 reader 构造函数 | `src/core/game_tables.cpp:16-42`、`core/game_capabilities.cpp:26-37,61`；`src/monster/data/part_schemas_data.cpp:1000,1011`；`src/rise/part_schemas.cpp:118,131` |
| 4 | `04-dataflow` | **主数据流**：`main.cpp` → 一次 reader 构造 → 每个 tick `reader->poll()` → `GameSnapshot` → Panel `update()` → `paintPanel()`；标出 World/Rise 的真实分叉点 | `src/main.cpp:392/458/476-485/505/527/593/663`、`src/world/world_reader.cpp:215`、`src/rise/mhr_reader.cpp:1311`、`src/ui/panel_player.cpp:539`、`src/ui/viewmodel/player_view_model.cpp` |
| 5 | `05-static-registration` | **静态注册时机**：`Register…TablesAtStartup` 在 `main()` 之前的 loader 阶段执行，为何必须在第一次 poll / 第一次 crown 查询之前，跨 TU 顺序未指定为何仍然安全 | `part_schemas_data.cpp:1010-1013`、`part_schemas.cpp:129-133`、`core/game_tables.cpp:33-34` |
| 6 | `06-damage-arbitration` | **`riseDamageReaders` 三槽位仲裁**：数据源取「活得最久的」（sequence 大、平手 timestampMs 大），报错源取「最有信息量且非 Missing 的」，以及为何失败时仍投递 invalid snapshot | `src/main.cpp:398-417/534-621`、`src/rise/rise_damage_reader.cpp`、`src/rise/rise_damage_roster.cpp:84` |

## 本目录文件

**渲染图（PNG，150 dpi，直接看）**

```
01-layers.png   02-contracts.png   03-registry.png
04-dataflow.png 05-static-registration.png 06-damage-arbitration.png
```

**PlantUML 源码**（`.puml`，GitHub / JetBrains / VS Code 插件可直接内联渲染）

```
01-layers.puml  02-contracts.puml  03-registry.puml
04-dataflow.puml 05-static-registration.puml
```

**Graphviz 源码**（`.dot`，与 PNG 一一对应，可 `dot -Tsvg` 自行改）

```
01-layers.dot 02-contracts.dot 03-registry.dot
04-dataflow.dot 05-static-registration.dot 06-damage-arbitration.dot
```

**Mermaid 源码**（`.mmd`，GitHub Markdown / mermaid.live 直接渲染）

```
06-damage-arbitration.mmd
```

**工具脚本**

```
make_diagrams.py   重新生成全部 .dot 与 .png（python3 docs/uml/make_diagrams.py）
check_dot.py       校验 .dot 里 HTML label 的尖括号配平
fix_labels.py      把 label=TEXT<TAG/> 修成 label=<TEXT<TAG/>> 的半自动修复器
```

## 为什么不是统一一种格式

这套图刻意混用了三种格式，因为每种格式在这一套图里的位置不同：

- **PlantUML 源码**是这堆图里最有长期价值的产物：可维护、可进
  GitHub/IDE、可加进 CI。图 1–5 用它表达（分层、类图、注册表、时序都合适）。
  本机没装 PlantUML 也能写它——它是给人读的源码，合成 PNG 用下面那条。
- **实际渲染出的图片**用 Graphviz `dot`，因为它是这几种里本机最常见的一个。
  图 1、3、5、6 里 HTML-like label 塞进了大量源码事实（行号、签名、开关兜底类型），`dot` 的
  HTML label 表达力够了；时序图（4）`dot` 表达吃力，所以 PNG 版是**按时序分层
  重画的流程图**，与 04 的 PlantUML 时序图内容一致、形态不同。
- **图 6 额外给了 Mermaid**：PlantUML 的生命周期时序带 loop/alt 嵌套在这个图里
  读起来不如 Mermaid 直观，`.mmd` 是 GitHub 原生渲染的补充件。

如果你要换渲染器，`.puml` 是唯一需要人手改的；`.dot` / `.mmd` 都可以由
`make_diagrams.py` 重新生成。

## 六张图的共同约定

- 每条边、每个类名都带 `file:line`；**行号会随代码漂移**，改代码后重新生成。
- 图上不写「解耦良好」「架构清晰」这类评价，只写可复核的事实。
- 标注为「measured」的都是 grep / `nm` / 读头文件得出的结论；
  少数引自 `docs/ARCHITECTURE.md` 的实测记录（如 730 像素差异），已在 note 里点名出处。
- 颜色约定：蓝=core，绿=游戏库，橙=UI，紫=可执行/装配，黄=数据表与注册表。

## 与 ARCHITECTURE.md 的差异

按「以代码为准」处理，图上均写的是实测结论：

1. **`registerRiseTables()` / `registerWorldTables()` 的静态初始化器位置**：
   文档 §3.4.1 说 `:1011` / `:131`，实测 `part_schemas_data.cpp:1010-1013`
   与 `part_schemas.cpp:129-133`（对象定义在 `:1011` / `:131`，函数在 `:1000` / `:118`）。
   文档的单个行号指向的是对象行，函数行是另一处——图上两处都标了。
2. **`src/rise/part_schemas.cpp` 行数**：文档 §1 目录树写 96 行，实测 135 行。
3. **`src/ui/` 文件数**：文档 §1 写 41 文件 / 11839 行，实测 56 个 `.h`+`.cpp` /
   14273 行。
4. **`src/core/game_tables.h` 顶部注释说「each reader's constructor registers its own tables」**：
   与 `game_tables.h:45` 的注释以及两个数据 TU 的实际机制矛盾；
   实测 `world_reader.cpp` / `mhr_reader.cpp` 里 **0 处** register 调用，
   所以那条注释（`game_tables.h:45`）是过时的。图 3、图 5 都按「静态初始化器」画，
   并把这条列为反证。
5. **`GameSnapshot` 字段数**：文档 §3.1 说「14 个字段」并列出 12 个。
   实测结构体是 12 个字段（`game`/`attached`/`pid`/`imageBase`/`status`/`zone`/
   `monsters`/`player`/`party`/`quest`/`playerCount`/`isMultiplayer`）。
   数不出第 13、14 个，疑似把 `#include` 头文件里的类型算进去了。图上按 12 画。
6. **`IGameReader` 的方法数**一致（2 个纯虚），文档 §3.2 无异议；
   但文档没写 `GameReaderPtr` 这个 using 别名，图 2 补上了（`game_reader.h:54`）。
7. **`src/core/game_capabilities.cpp` 的 `static_assert` 位置**：文档 §7.1 表格写
   `:50`，实测在 `:50` 附近（49–51 区间含注释），图 3 标 `:50`。
   结论本身（「比较的是字面量，加枚举值不可能失败」）与代码注释完全一致，无需修正。

重新生成：`cd docs/uml && python3 make_diagrams.py`
校验：`python3 -c "import glob,subprocess,sys; sys.exit(subprocess.run(['dot','-Tsvg','']+glob.glob('*.dot')+['-o','/dev/null']).returncode)"`
（`check_dot.py *.dot` 只查单行 label 的尖括号配平，多行 `<TABLE>` note 会误报——
真正的判据是 `dot -Tsvg` 全部退出 0。）
