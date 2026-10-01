#!/usr/bin/env python3
"""Regenerate the Graphviz renders in docs/uml/ from the canonical sources.

Every fact below was measured against the tree this file lives in.  Class
names, method signatures, include paths, CMake line numbers and the
static-initialiser registration path were read out of the headers/sources
themselves; nothing here is copied from docs/ARCHITECTURE.md without being
checked against the code.  Line numbers drift as the code moves — re-check
them against the tree before trusting a box that cites one.

    python3 docs/uml/make_diagrams.py            # render all *.png
    python3 docs/uml/make_diagrams.py --check    # warn on drift vs sources

The *.puml files in this directory are hand-maintained equivalents of the
same diagrams for PlantUML (GitHub/IDE).  This script owns the *.dot sources
and the *.png renders only.
"""

from __future__ import annotations

import re

import argparse
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent  # repository root, derived from this file's location

FONT = "Helvetica"
DPI = "150"

# ── palette ───────────────────────────────────────────────────────────────
C_CORE = "#BFDBFE"      # monster-core        (kernel, Qt6::Core only)
C_GAME = "#BBF7D0"      # game-rise / game-world (peers, one game each)
C_UI = "#FED7AA"        # mhw-ui              (GUI layer)
C_EXE = "#DDD6FE"       # executable + glue translation units
C_DATA = "#FDE68A"      # per-game data / registry rows
C_SNAP = "#F3F4F6"      # snapshot value types (game-agnostic)
C_ENUM = "#FECACA"      # GameId / enums
C_EDGE = "#374151"
C_NOTE = "#FFFBEB"

GRAPH_BASE = (
    'graph [bgcolor="white", pad="0.35", splines=polyline, nodesep="0.4", '
    'ranksep="0.55", fontname="%s", fontsize=13];\n' % FONT
)
NODE_BASE = (
    'node [fontname="%s", fontsize=11, shape=box, style="rounded,filled", '
    'color="#9CA3AF", fontcolor="#111827", margin="0.14,0.09"];\n' % FONT
)
EDGE_BASE = (
    'edge [fontname="%s", fontsize=10, color="%s", fontcolor="#374151"];\n'
    % (FONT, C_EDGE)
)


# ── helpers ───────────────────────────────────────────────────────────────
def esc(text: str) -> str:
    # Already-escaped entities pass through; only bare & / < / > are encoded.
    text = re.sub(r"&(?!(?:amp|lt|gt|quot|apos|#\d+|#x[0-9A-Fa-f]+);)", "&amp;", text)
    return text.replace("<", "&lt;").replace(">", "&gt;")


def html_class(
    name: str,
    members=None,
    stereotype=None,
    fillcolor=C_SNAP,
    colspan=1,
    member_align="left",
) -> str:
    """One C++-style class box: name / stereotype / members compartments."""
    rows = [f'<TR><TD BGCOLOR="{fillcolor}" COLSPAN="{colspan}"><B>{esc(name)}</B></TD></TR>']
    if stereotype:
        rows.append(
            '<TR><TD COLSPAN="%d"><FONT POINT-SIZE="9" COLOR="#4B5563">'
            "<I>&lt;&lt;%s&gt;&gt;</I></FONT></TD></TR>" % (colspan, esc(stereotype))
        )
    for m in members or []:
        if not m:
            rows.append(
                f'<TR><TD COLSPAN="{colspan}"><FONT POINT-SIZE="4">&#160;</FONT></TD></TR>'
            )
            continue
        rows.append(
            '<TR><TD ALIGN="%s" BALIGN="LEFT"><FONT POINT-SIZE="10">%s</FONT></TD></TR>'
            % (member_align, esc(m))
        )
    return '<<TABLE BORDER="0" CELLBORDER="1" CELLSPACING="0" CELLPADDING="4">' + "".join(rows) + "</TABLE>>"


def plain_note(text: str, title: str | None = None, width=None) -> str:
    body = ""
    if title:
        body += f'<TR><TD BGCOLOR="{C_NOTE}" ALIGN="LEFT"><B>{esc(title)}</B></TD></TR>'
    for line in text.strip("\n").split("\n"):
        body += '<TR><TD BGCOLOR="%s" ALIGN="LEFT" BALIGN="LEFT"><FONT POINT-SIZE="10">%s</FONT></TD></TR>' % (
            C_NOTE,
            esc(line),
        )
    return '<<TABLE BORDER="1" CELLBORDER="0" CELLSPACING="0" CELLPADDING="4" COLOR="#D97706">' + body + "</TABLE>>"


def digraph(name: str, body: str) -> str:
    return f"digraph {name} {{\n{GRAPH_BASE}{NODE_BASE}{EDGE_BASE}\n{body}}}\n"


def run_dot(dot_path: Path) -> Path:
    png = dot_path.with_suffix(".png")
    subprocess.run(
        ["dot", f"-Tpng", f"-Gdpi={DPI}", str(dot_path), "-o", str(png)],
        check=True,
    )
    return png


# ══════════════════════════════════════════════════════════════════════════
# 1 — Layer / package diagram
# ══════════════════════════════════════════════════════════════════════════
LAYERS = r'''
  labelloc="t";
  label=<<B>1. 四库分层与依赖方向</B><BR/><FONT POINT-SIZE="10">measured: CMakeLists.txt:41 / :89 / :143 / :199, target_link_libraries at :75 / :131 / :176 / :257-265</FONT><BR/> >;

  subgraph cluster_core {
    label=<<B>monster-core</B><BR/><FONT POINT-SIZE="9">Qt6::Core only · CMakeLists.txt:41</FONT>>;
    fontsize=12; style="rounded,filled"; fillcolor="#EFF6FF"; color="#3B82F6";
    c_snap [label=<GameSnapshot<BR/><FONT POINT-SIZE="9">game_snapshot.h:14</FONT>>, fillcolor="#DBEAFE"];
    c_reader [label=<IGameReader<BR/><FONT POINT-SIZE="9">game_reader.h:39</FONT>>, fillcolor="#DBEAFE"];
    c_zone [label=<ZoneRef<BR/><FONT POINT-SIZE="9">zone_id.h:54</FONT>>, fillcolor="#DBEAFE"];
    c_types [label=<GameId · Snapshots<BR/><FONT POINT-SIZE="9">monster_types.h</FONT>>, fillcolor="#DBEAFE"];
    c_tables [label=<<B>game_tables registry</B><BR/><FONT POINT-SIZE="9">game_tables.cpp — holds pointers,</FONT><BR/><FONT POINT-SIZE="9">names no game's table</FONT>>, fillcolor="#FDE68A"];
    c_caps [label=<<B>game_capabilities</B><BR/><FONT POINT-SIZE="9">kTraits[2] + 3 queries</FONT>>, fillcolor="#FDE68A"];
    c_support [label=<string_table · game_detector · game_profile<BR/>steam_game_locator · map_paths · process_memory<BR/><FONT POINT-SIZE="9">6 support TU — process_memory default args name<BR/>"monsterhunterworld.exe" (process_memory.h:64,:119)</FONT>>, fillcolor="#DBEAFE"];
  }

  subgraph cluster_rise {
    label=<<B>game-rise</B><BR/><FONT POINT-SIZE="9">CMakeLists.txt:89 · 28 sources</FONT>>;
    fontsize=12; style="rounded,filled"; fillcolor="#F0FDF4"; color="#22C55E";
    r_reader [label=<MhrReader<BR/><FONT POINT-SIZE="9">mhr_reader.{h,cpp} + _sharpness + _quest</FONT>>, fillcolor="#DCFCE7"];
    r_dmg [label=<RiseDamageReader<BR/>rise_damage_reader.cpp<BR/><FONT POINT-SIZE="9">REFramework JSON feed</FONT>>, fillcolor="#DCFCE7"];
    r_zones [label=<RiseZone vocabulary<BR/><FONT POINT-SIZE="9">rise_zones.h</FONT>>, fillcolor="#DCFCE7"];
    r_reg [label=<<B>registerRiseTables()</B><BR/><FONT POINT-SIZE="9">part_schemas.cpp:118 — run by static</FONT><BR/><FONT POINT-SIZE="9">initialiser at :131</FONT>>, fillcolor="#FDE68A"];
    r_rf [label=<REFramework<BR/><FONT POINT-SIZE="9">installer / config / fetcher</FONT>>, fillcolor="#DCFCE7"];
  }

  subgraph cluster_world {
    label=<<B>game-world</B><BR/><FONT POINT-SIZE="9">CMakeLists.txt:143 · 11 sources</FONT>>;
    fontsize=12; style="rounded,filled"; fillcolor="#F0FDF4"; color="#22C55E";
    w_reader [label=<MhwReader<BR/><FONT POINT-SIZE="9">world_reader.{h,cpp} + monster/player/quest<BR/>_reader.cpp</FONT>>, fillcolor="#DCFCE7"];
    w_zones [label=<WorldZone vocabulary<BR/><FONT POINT-SIZE="9">world_types.{h,cpp}</FONT>>, fillcolor="#DCFCE7"];
    w_reg [label=<<B>registerWorldTables()</B><BR/><FONT POINT-SIZE="9">monster/data/part_schemas_data.cpp:1000</FONT><BR/><FONT POINT-SIZE="9">— static initialiser at :1011</FONT>>, fillcolor="#FDE68A"];
    w_meta [label=<target_selector.cpp<BR/><FONT POINT-SIZE="9">shared target-pick rule</FONT><BR/><FONT POINT-SIZE="9">(compiled into game-world)</FONT>>, fillcolor="#DCFCE7"];
  }

  subgraph cluster_ui {
    label=<<B>mhw-ui</B><BR/><FONT POINT-SIZE="9">CMakeLists.txt:199 · 56 sources, compiled once</FONT>>;
    fontsize=12; style="rounded,filled"; fillcolor="#FFF7ED"; color="#F97316";
    u_panels [label=<Panel (QMainWindow)<BR/><FONT POINT-SIZE="9">panel_player / _monster / _damage /<BR/>_pet_damage .cpp</FONT>>, fillcolor="#FFEDD5"];
    u_vm [label=<viewmodel/<BR/><FONT POINT-SIZE="9">player / monster / damage</FONT><BR/><FONT POINT-SIZE="9">pure transforms, no QWidget</FONT>>, fillcolor="#FFEDD5"];
    u_icon [label=<icon.cpp<BR/><FONT POINT-SIZE="9">qrc probe: QFile::exists(":/…")</FONT><BR/><FONT POINT-SIZE="9">icon.cpp:232 · no id list</FONT>>, fillcolor="#FFEDD5"];
  }

  subgraph cluster_exe {
    label=<<B>monster-overlay (executable)</B><BR/><FONT POINT-SIZE="9">CMakeLists.txt:308</FONT>>;
    fontsize=12; style="rounded,filled"; fillcolor="#F5F3FF"; color="#8B5CF6";
    e_main [label=<main.cpp<BR/><FONT POINT-SIZE="9">assembly + QTimer poll loop</FONT>>, fillcolor="#EDE9FE"];
    e_factory [label=<<B>reader_factory.cpp</B><BR/><FONT POINT-SIZE="9">switch(game) → make_unique&lt;MhwReader</FONT><BR/><FONT POINT-SIZE="9">&#160;&#160;&#160;or MhrReader&gt;</FONT><BR/><FONT POINT-SIZE="9">the ONE TU naming both readers — :24</FONT>>, fillcolor="#EDE9FE"];
    e_desc [label=<reader_descriptors.cpp<BR/><FONT POINT-SIZE="9">mapFileName / mapSelection per game</FONT>>, fillcolor="#EDE9FE"];
  }

  // dependency edges (library → library, ltail/lhead draw cluster to cluster)
  e_main  -> e_factory [ltail=cluster_exe];
  e_factory -> r_reader [ltail=cluster_exe, lhead=cluster_rise, label="includes\nrise/mhr_reader.h:19"];
  e_factory -> w_reader [ltail=cluster_exe, lhead=cluster_world, label="includes\nworld/world_reader.h:20"];
  u_panels -> r_reader [ltail=cluster_ui, lhead=cluster_rise, label="12 ui includes of\nrise/ or world/ headers", style=dashed];
  u_panels -> c_snap  [ltail=cluster_ui, lhead=cluster_core, label="consumes\nGameSnapshot"];

  r_reader -> c_snap  [ltail=cluster_rise, lhead=cluster_core, label="produces"];
  w_reader -> c_snap  [ltail=cluster_world, lhead=cluster_core, label="produces"];
  r_reader -> c_reader [ltail=cluster_rise, lhead=cluster_core, style=dashed];
  w_reader -> c_reader [ltail=cluster_world, lhead=cluster_core, style=dashed];
  r_reg -> c_tables [ltail=cluster_rise, lhead=cluster_core, label="registerCrownThresholds /\nregisterCaptureRules /\nregisterAilmentStyles", color="#B45309", fontcolor="#B45309", penwidth=1.8];
  w_reg -> c_tables [ltail=cluster_world, lhead=cluster_core, color="#B45309", fontcolor="#B45309", penwidth=1.8];

  u_panels -> c_tables [ltail=cluster_ui, lhead=cluster_core, label="crownThresholdsFor /\nailmentStyle", color="#B45309", fontcolor="#B45309", style=dashed];
  u_panels -> c_caps  [ltail=cluster_ui, lhead=cluster_core, label="gameDisplayName /\nstaminaDisplayScale", style=dashed];

  c_support -> c_snap [style=invis];
  r_dmg -> r_reader [style=invis];
  r_zones -> r_reg [style=invis];
  w_zones -> w_reg [style=invis];
  u_vm -> u_icon [style=invis];
  e_desc -> e_main [style=invis];

  note_a [shape=note, fillcolor="#FFFBEB", color="#D97706", label=<
    <TABLE BORDER="0" CELLBORDER="0" CELLSPACING="0" CELLPADDING="2"><TR><TD ALIGN="LEFT" BALIGN="LEFT"><FONT POINT-SIZE="10">
    <B>硬边界（nm 实测，scripts/assert_lib_purity.sh build 全 PASS）：</B><BR ALIGN="LEFT"/>
    · libmonster-core.a 无任何游戏命名符号（0）<BR ALIGN="LEFT"/>
    · libgame-rise.a 对 World 符号的未定义引用 = 0；game-world 对 Rise = 0<BR ALIGN="LEFT"/>
    · libmhw-ui.a 无 MhwReader | MhrReader | readMonsters | readParty 符号<BR ALIGN="LEFT"/>
    · 反向 include=0：core/ rise/ world/ 下没有文件 include ui/<BR ALIGN="LEFT"/>
    · qrc 不进任何库，只在 exe target（CMakeLists.txt:318-319）<BR ALIGN="LEFT"/>
    </FONT></TD></TR></TABLE>>];
  note_a -> c_tables [style=invis];

'''


# ══════════════════════════════════════════════════════════════════════════
# 2 — Core contract classes
# ══════════════════════════════════════════════════════════════════════════
def contracts() -> str:
    igame = html_class(
        "IGameReader",
        [
            "+~IGameReader() = default",
            "+poll() : GameSnapshot  {pure}",
            "+mapPath() : const QString&amp;  {pure}",
            "",
            "typedef GameReaderPtr =",
            "  unique_ptr&lt;IGameReader&gt;",
        ],
        stereotype="abstract · core/game_reader.h:39",
        fillcolor="#DBEAFE",
    )
    snap = html_class(
        "GameSnapshot",
        [
            "+game : GameId",
            "+attached : bool",
            "+pid : qint64",
            "+imageBase : uintptr_t",
            "+status : QString",
            "+zone : ZoneRef",
            "+monsters : QVector&lt;MonsterSnapshot&gt;",
            "+player : PlayerSnapshot",
            "+party : QVector&lt;PartyMemberSnapshot&gt;",
            "+quest : QuestSnapshot",
            "+playerCount : int",
            "+isMultiplayer : bool",
        ],
        stereotype="struct · core/game_snapshot.h:14 · 14 fields",
        fillcolor=C_SNAP,
    )
    zone = html_class(
        "ZoneRef",
        [
            "+game : GameId {World}",
            "+id : int {kZoneUnknownId = -1}",
            "",
            "+operator== / !=  (constexpr)",
            "isUnknownZone(z)  (constexpr)",
            "",
            "NOT the same question as",
            "hasZoneName() — that lives",
            "on the game side",
        ],
        stereotype="struct · core/zone_id.h:54",
        fillcolor="#FEF3C7",
    )
    gameid = html_class(
        "GameId",
        ["enum class {", "    World,", "    Rise", "}"],
        stereotype="enum · monster/monster_types.h:18",
        fillcolor=C_ENUM,
    )
    mhw = html_class(
        "MhwReader",
        [
            "+selectLoadableMap(QStringList) : QString  {static}",
            "+poll() : GameSnapshot  override",
            "+mapPath() : const QString&amp;  override",
            "",
            "findGamePid / selectLoadableMap",
            "stay World-side",
        ],
        stereotype="class final : IGameReader · world/world_reader.h:17",
        fillcolor="#DCFCE7",
    )
    mhr = html_class(
        "MhrReader",
        [
            "+findBestMap(QStringList) : QString  {static}",
            "+poll() : GameSnapshot  override",
            "+mapPath() : const QString&amp;  override",
            "",
            "findRisePid / readZone",
            "stay Rise-side",
        ],
        stereotype="class final : IGameReader · rise/mhr_reader.h:546",
        fillcolor="#DCFCE7",
    )
    monster = html_class(
        "MonsterSnapshot",
        [
            "+address : uintptr_t",
            "+id : int",
            "+game : GameId",
            "+health / maxHealth : float",
            "+stamina / size / enrage* : float",
            "+parts : QVector&lt;PartSnapshot&gt;",
            "+ailments : QVector&lt;…&gt;",
            "+qurio* (Rise only)",
        ],
        stereotype="struct · monster_types.h:106",
        fillcolor=C_SNAP,
    )
    part = html_class(
        "PartSnapshot",
        [
            "+partType : PartType",
            "    {Flinch, Severable, Breakable}",
            "+health / maxHealth  (sever layer)",
            "+breakHealth / breakMaxHealth",
            "+flinch / maxFlinch",
            "+isSeverable / isBreakable",
            "+isBroken / isPartSevered",
            "+tenderize{Duration,MaxDuration}",
        ],
        stereotype="struct · monster_types.h:35",
        fillcolor=C_SNAP,
    )
    external = html_class(
        "PlayerSnapshot / QuestSnapshot",
        [
            "player_types.h · quest_types.h",
            "header-only: no .cpp, not in",
            "any add_library — contract paper",
            "shared by both games",
        ],
        stereotype="shared vocabulary",
        fillcolor="#E5E7EB",
    )
    panel = html_class(
        "Panel : QMainWindow",
        [
            "+update(const GameSnapshot&amp;)",
            "#paintPanel(QPainter&amp;)  {pure}",
            "#onSnapshot(const GameSnapshot&amp;)",
            "+retranslateUi()",
        ],
        stereotype="abstract base · ui/panel.h:32",
        fillcolor="#FFEDD5",
    )

    body = f'''
  labelloc="t";
  label=<<B>2. 核心契约类</B><BR/><FONT POINT-SIZE="10">final / override / pure-virtual measured in the headers; field list is the contract surface, not every field</FONT>>;

  igame [label={igame}];
  mhw  [label={mhw}];
  mhr  [label={mhr}];
  snap [label={snap}];
  zone [label={zone}];
  gameid [label={gameid}];
  monster [label={monster}];
  part [label={part}];
  external [label={external}];
  panel [label={panel}];

  mhw -> igame [arrowhead=onormal, label="&lt;&lt;final&gt;&gt;"];
  mhr -> igame [arrowhead=onormal, label="&lt;&lt;final&gt;&gt;"];
  snap -> zone [arrowhead=vee, label="zone"];
  snap -> monster [arrowhead=vee, label="monsters"];
  snap -> external [arrowhead=vee, style=dashed];
  snap -> gameid [arrowhead=vee, label="game"];
  zone -> gameid [arrowhead=vee, style=dashed];
  monster -> part [arrowhead=vee, label="parts"];
  monster -> gameid [arrowhead=vee, style=dashed, label="game"];
  panel -> snap [arrowhead=vee, style=dashed, label="update()"];
  igame -> snap [arrowhead=vee, label="poll() returns"];
'''
    return body


# ══════════════════════════════════════════════════════════════════════════
# 3 — Registry + capability tables
# ══════════════════════════════════════════════════════════════════════════
REGISTRY = r'''
  labelloc="t";
  label=<<B>3. 注册表与能力表：谁在什么时候注册</B><BR/><FONT POINT-SIZE="10">registration runs as a STATIC INITIALISER inside the data TU — not in a reader constructor (measured: no register* call in world_reader.cpp / mhr_reader.cpp)</FONT>>;

  subgraph cluster_core {
    label=<<B>monster-core</B><BR/><FONT POINT-SIZE="9">core/game_tables.cpp · core/game_capabilities.cpp</FONT>>;
    style="rounded,filled"; fillcolor="#EFF6FF"; color="#3B82F6"; fontsize=12;

    reg_api [label=<<B>registration API (3)</B><BR/>registerCrownThresholds(GameId, const QHash&amp;)<BR/>registerCaptureRules(GameId, const QHash&lt;int,int&gt;*, const QSet&amp;)<BR/>registerAilmentStyles(GameId, const QHash&lt;int,AilmentStyle&gt;&amp;)<BR/><FONT POINT-SIZE="9">game_tables.h:140,141,108</FONT>>, fillcolor="#BFDBFE"];
    qry_api [label=<<B>query API</B><BR/>crownThresholdsFor(GameId, int) → const array*<BR/>captureThresholdFor(GameId, int) → optional&lt;int&gt;<BR/>ailmentStyle(GameId, int) → AilmentStyle<BR/>crownTablesFor / captureTablesFor<BR/><FONT POINT-SIZE="9">game_tables.h:122,123,102</FONT>>, fillcolor="#BFDBFE"];

    slots [label=<<B>static Tables slots</B><BR/>Tables world; Tables rise;<BR/><FONT POINT-SIZE="9">game_tables.cpp:33-34</FONT><BR/>hasCrowns · crowns<BR/>hasCapture · captureThresholds · notCapturable<BR/>hasAilments · ailmentStyles<BR/><FONT POINT-SIZE="9">:16-24 — stores POINTERS, no game's data</FONT>>, fillcolor="#FDE68A"];

    switch_tt [label=<tableFor(GameId)<BR/><FONT POINT-SIZE="9">game_tables.cpp:26-42</FONT><BR/>switch, no default,<BR/><B>return world;</B> after<BR/>→ new GameId = -Wswitch>, fillcolor="#E0E7FF"];

    caps [label=<<B>game_capabilities</B><BR/>kTraits[] — one row per game<BR/><FONT POINT-SIZE="9">game_capabilities.cpp:26-37</FONT><BR/>{displayNameKey, displayNameAscii,<BR/> normalisedPartGauges, staminaDivisor}<BR/>static_assert(std::size(kTraits) == 2)  :50<BR/>→ compares to the literal 2, so it<BR/><B>cannot</B> fail when an enumerator is added>, fillcolor="#FDE68A"];
    caps_q [label=<<B>queries</B><BR/>gameDisplayName(GameId)<BR/>usesNormalisedPartGauges(GameId)<BR/>staminaDisplayScale(GameId)<BR/><FONT POINT-SIZE="9">game_capabilities.h:56,64,82</FONT>>, fillcolor="#BFDBFE"];
    caps_sw [label=<gameTraits(GameId)<BR/><FONT POINT-SIZE="9">game_capabilities.cpp:61</FONT><BR/>switch, no default,<BR/><B>__builtin_unreachable()</B><BR/>→ new GameId = -Wswitch>, fillcolor="#E0E7FF"];
  }

  subgraph cluster_world {
    label=<<B>game-world data TU</B><BR/><FONT POINT-SIZE="9">src/monster/data/part_schemas_data.cpp</FONT>>;
    style="rounded,filled"; fillcolor="#F0FDF4"; color="#22C55E";
    w_init [label=<<B>RegisterWorldTablesAtStartup</B><BR/><FONT POINT-SIZE="9">anonymous namespace, :1010-1013</FONT><BR/>ctor runs before main()>, fillcolor="#BBF7D0"];
    w_reg [label=<<B>registerWorldTables()</B>  :1000<BR/>registerCrownThresholds(World, kCrownThresholds)<BR/>registerCaptureRules(World, &amp;kMonsterCaptureThresholds,<BR/>                          kNoNotCapturable())<BR/>registerAilmentStyles(World, kWorldAilmentStyles)>, fillcolor="#FDE68A"];
  }

  subgraph cluster_rise {
    label=<<B>game-rise data TU</B><BR/><FONT POINT-SIZE="9">src/rise/part_schemas.cpp</FONT>>;
    style="rounded,filled"; fillcolor="#F0FDF4"; color="#22C55E";
    r_init [label=<<B>RegisterRiseTablesAtStartup</B><BR/><FONT POINT-SIZE="9">anonymous namespace, :129-133</FONT><BR/>ctor runs before main()>, fillcolor="#BBF7D0"];
    r_reg [label=<<B>registerRiseTables()</B>  :118<BR/>registerCrownThresholds(Rise, kRiseCrownThresholds)<BR/>registerCaptureRules(Rise, <B>nullptr</B>,<BR/>                          kRiseNotCapturableMonsterIds)<BR/>registerAilmentStyles(Rise, kRiseAilmentStyles)>, fillcolor="#FDE68A"];
  }

  callers [label=<<B>callers (UI / readers / tests)</B><BR/>panel_monster.cpp:1125 crown chip · ailment cards<BR/>ui panel_player stamina · console display name<BR/>tests: rise_monster_metadata_tests.cpp:35-36,<BR/>rise_monster_name_tests.cpp:59-60,<BR/>ailment_style_tests.cpp:43-44 call registerWorldTables()/<BR/>registerRiseTables() explicitly>, fillcolor="#F3F4F6"];

  w_init -> w_reg [label="ctor()"];
  r_init -> r_reg [label="ctor()"];
  w_reg -> reg_api [label="registers World rows", color="#B45309", fontcolor="#B45309", penwidth=1.6];
  r_reg -> reg_api [label="registers Rise rows", color="#B45309", fontcolor="#B45309", penwidth=1.6];
  reg_api -> switch_tt [label="writes the slot for (game)"];
  switch_tt -> slots [label="&amp;world / &amp;rise"];
  qry_api -> switch_tt;
  callers -> qry_api;
  callers -> caps_q;
  caps_q -> caps_sw;
  caps_sw -> caps;

  note_a [shape=plaintext, label=<<TABLE BORDER="1" CELLBORDER="0" CELLSPACING="0" CELLPADDING="5" COLOR="#D97706" BGCOLOR="#FFFBEB"><TR><TD ALIGN="LEFT" BALIGN="LEFT"><FONT POINT-SIZE="10">
  <B>注：为什么是静态初始化器而不是 reader 构造函数</B><BR ALIGN="LEFT"/>
  · 实测：world_reader.cpp / mhr_reader.cpp 里没有任何 register* 调用（grep，HEAD dbbac03）<BR ALIGN="LEFT"/>
  · 放进 reader 构造函数会让 edit-mode demo 面板（不构造 reader）丢掉 crown 芯片，<BR ALIGN="LEFT"/>
  &#160;&#160;all_monster.png 像素差 730（ARCHITECTURE.md §3.4.1 记录的实测）<BR ALIGN="LEFT"/>
  · 跨 TU 初始化顺序未指定，但安全：注册表只存指针；同一 TU 内先构造被取地址的表<BR ALIGN="LEFT"/>
  <BR ALIGN="LEFT"/>
  <B>注：注册表的查询语义（两台游戏答案不同，不是同一查询写两遍）</B><BR ALIGN="LEFT"/>
  · World 查阈值表（Elder Dragons 的 Capture=0 在表内）；Rise 查不可捕获集合，成员答 0<BR ALIGN="LEFT"/>
  · Rise 注册 nullptr 阈值表 — 空表会把「无数据」和「有数据但都不匹配」混为一谈<BR ALIGN="LEFT"/>
  · nullptr / nullopt = 「这款游戏没有这项静态数据」，永远不是「退回另一款游戏」<BR ALIGN="LEFT"/>
  · 重复注册同一游戏是替换，不是累积（game_tables.h:127）<BR ALIGN="LEFT"/>
  </FONT></TD></TR></TABLE>>];
  note_a -> slots [style=invis];
'''


# ══════════════════════════════════════════════════════════════════════════
# 4 — Main data-flow sequence
# ══════════════════════════════════════════════════════════════════════════
SEQUENCE = r'''
  labelloc="t";
  label=<<B>4. 主数据流：一个 poll tick</B><BR/><FONT POINT-SIZE="10">line numbers measured at HEAD dbbac03; both games share one code path except where marked</FONT>>;

  subgraph cluster_setup {
    label=<<B>startup (once)</B>>; style="rounded,dashed"; color="#8B5CF6";
    s1 [label=<<B>1</B> main.cpp:392<BR/>createReader(gameId,<BR/>explicitMap, dataDirs)>, fillcolor="#EDE9FE"];
    s2 [label=<<B>2</B> reader_factory.cpp:24<BR/>switch(game) → one reader>, fillcolor="#EDE9FE"];
    s3w [label=<<B>3a  World</B><BR/>make_unique&lt;MhwReader&gt;(<BR/>&#160;&#160;selectLoadableMap(<BR/>&#160;&#160;&#160;&#160;worldMapCandidates(…))))<BR/><FONT POINT-SIZE="9">factory :35-40</FONT>>, fillcolor="#DCFCE7"];
    s3r [label=<<B>3b  Rise</B><BR/>make_unique&lt;MhrReader&gt;(<BR/>&#160;&#160;findBestMap(<BR/>&#160;&#160;&#160;&#160;versionedMapCandidates(…))))<BR/><FONT POINT-SIZE="9">factory :41-48</FONT>>, fillcolor="#DCFCE7"];
    s1 -> s2;
    s2 -> s3w [label="GameId::World"];
    s2 -> s3r [label="GameId::Rise"];
  }

  subgraph cluster_tick {
    label=<<B>QTimer tick (main.cpp:458-668, pollMs period)</B>>; style="rounded,dashed"; color="#6B7280";
    t0 [label=<QTimer::timeout<BR/><FONT POINT-SIZE="9">main.cpp:458</FONT>>, fillcolor="#EDE9FE"];
    t1 [label=<<B>4</B> try { snap = reader-&gt;poll() }<BR/>catch (…) → skip frame<BR/><FONT POINT-SIZE="9">main.cpp:476-485 — exception</FONT><BR/><FONT POINT-SIZE="9">barrier around whole tick</FONT>>, fillcolor="#EDE9FE"];
    t2w [label=<<B>5a  World</B><BR/>MhwReader::poll()<BR/><FONT POINT-SIZE="9">world_reader.cpp:215</FONT><BR/>zone = ZoneRef{World,<BR/>&#160;&#160;(int)worldZone} :225<br/>>, fillcolor="#DCFCE7"];
    t2r [label=<<B>5b  Rise</B><BR/>MhrReader::poll()<BR/><FONT POINT-SIZE="9">mhr_reader.cpp:1311</FONT><BR/>zone = ZoneRef{Rise,<BR/>&#160;&#160;rawId} :1320<br/>>, fillcolor="#DCFCE7"];
    snap [label=<<B>GameSnapshot</B><BR/><FONT POINT-SIZE="9">the only type that crosses</FONT><BR/><FONT POINT-SIZE="9">reader → UI</FONT>>, fillcolor="#F3F4F6"];
    t3 [label=<<B>6</B> playerPanel.update(snap)<BR/><FONT POINT-SIZE="9">main.cpp:505 → panel_player.cpp:539</FONT><BR/>mirrors 9 fields, then<BR/>PlayerViewModel::resolveIdentity(snap) :566<br/>>, fillcolor="#FFEDD5"];
    t4 [label=<<B>7</B> selectMonsterTarget(<BR/>&#160;&#160;snap.monsters, displayedAddr)<BR/><FONT POINT-SIZE="9">target_selector.cpp — compiled</FONT><BR/><FONT POINT-SIZE="9">into game-world</FONT><BR/>→ monsterPanel.update(<BR/>&#160;&#160;&#160;snap.monsters[selected]) :527<br/>>, fillcolor="#FFEDD5"];
    t5w [label=<<B>8a  World</B><BR/>visibleDamageParty(snap.party,<BR/>&#160;&#160;showOtherMembers) filter<BR/>→ damagePanel.update(snap) :663<BR/><FONT POINT-SIZE="9">petDamagePanel force-disabled</FONT>>, fillcolor="#FFEDD5"];
    t5r [label=<<B>8b  Rise</B><BR/>3× RiseDamageReader::update()<BR/> arbitration (see diagram 6)<BR/>→ enrichRiseDamageSnapshot(dmg, snap)<BR/><FONT POINT-SIZE="9">rise_damage_roster.cpp:84</FONT><BR/>→ damagePanel.updateRiseDamage(dmg) :593<BR/>→ petDamagePanel.updateRiseDamage(dmg) :595<br/>>, fillcolor="#FFEDD5"];
    t6 [label=<<B>9</B> setVisible(panelEnabled()<BR/>&#160;&#160;&amp;&amp; hasVisibleContent())<BR/>triggerUpdate() → paintEvent<BR/>→ paintPanel(QPainter&amp;) :142<br/>>, fillcolor="#FFEDD5"];

    t0 -> t1;
    t1 -> t2w [label="World reader"];
    t1 -> t2r [label="Rise reader"];
    t2w -> snap; t2r -> snap;
    snap -> t3 [label="shared"];
    snap -> t4;
    snap -> t5w [label="World"];
    snap -> t5r [label="Rise"];
    t3 -> t6; t4 -> t6; t5w -> t6; t5r -> t6;
  }

  s3w -> t2w [style=dashed, label="same object,\npolled each tick"];
  s3r -> t2r [style=dashed, label="same object,\npolled each tick"];

  note [shape=plaintext, rank=sink, margin="0.6,0.45", label=<<TABLE BORDER="1" CELLBORDER="0" CELLSPACING="0" CELLPADDING="5" COLOR="#D97706" BGCOLOR="#FFFBEB"><TR><TD ALIGN="LEFT" BALIGN="LEFT"><FONT POINT-SIZE="10">
  <B>两条路径的差异只有这些（其余逐行相同）：</B><BR ALIGN="LEFT"/>
  · 取 map：World 逐候选试到能解析（FirstLoadable）；Rise 先探测运行中游戏（BestWithProbe）<BR ALIGN="LEFT"/>
  · 区域：World 传原始 int32（只过滤 0..1000）；Rise 还会产出 -2 LoadingScreen / 199 CharSelection<BR ALIGN="LEFT"/>
  · DamagePanel 的源：World = 本帧 snap.party；Rise = REFramework JSON feed（out-of-band，3 个文件槽）<BR ALIGN="LEFT"/>
  · PetDamagePanel：只有 Rise 会构造并推送<BR ALIGN="LEFT"/>
  · 残留的 isRise（main.cpp:305）只为这两项 Rise 独有功能存在，按 §3.5 的理由不做成能力查询<BR ALIGN="LEFT"/>
  <BR ALIGN="LEFT"/>
  <B>edit/demo 模式：</B>skipUpdate(Panel) = editMode &amp;&amp; panel.demoPrimed()（main.cpp:496）——<BR ALIGN="LEFT"/>
  &#160;&#160;只 triggerUpdate()，不投递 reader 快照，避免把 demo 状态擦掉<BR ALIGN="LEFT"/>
  </FONT></TD></TR></TABLE>>];
  note -> t0 [style=invis];
'''


# ══════════════════════════════════════════════════════════════════════════
# 5 — Static registration timing
# ══════════════════════════════════════════════════════════════════════════
STATIC_REG = r'''
  labelloc="t";
  label=<<B>5. 静态注册时机：为什么必须在第一次 poll 之前</B><BR/><FONT POINT-SIZE="10">the registration is a static initialiser in the data TU, executed by the runtime loader before main()</FONT>>;

  subgraph cluster_pre {
    label=<<B>before main() — dynamic-loader phase</B><BR/><FONT POINT-SIZE="9">order between the two TUs is UNSPECIFIED; that is safe here</FONT>>;
    style="rounded,dashed"; color="#B45309";

    wtu [label=<<B>game-world TU</B><BR/>src/monster/data/<BR/>part_schemas_data.cpp<br/>>, fillcolor="#DCFCE7"];
    rtu [label=<<B>game-rise TU</B><BR/>src/rise/part_schemas.cpp<br/>>, fillcolor="#DCFCE7"];
    wobj [label=<<B>kRegisterWorldTablesAtStartup</B><BR/><FONT POINT-SIZE="9">anonymous-namespace object, :1011-1012</FONT><BR/><FONT POINT-SIZE="9">defined AFTER every table it hands over</FONT>>, fillcolor="#FDE68A"];
    robj [label=<<B>kRegisterRiseTablesAtStartup</B><BR/><FONT POINT-SIZE="9">anonymous-namespace object, :131-132</FONT><BR/><FONT POINT-SIZE="9">defined AFTER every table it hands over</FONT>>, fillcolor="#FDE68A"];
    wfn [label=<<B>registerWorldTables()</B> :1000<BR/>→ registerCrownThresholds(World, kCrownThresholds)<BR/>→ registerCaptureRules(World, &amp;kMonsterCaptureThresholds,<BR/>&#160;&#160;&#160;&#160;kNoNotCapturable())<BR/>→ registerAilmentStyles(World, kWorldAilmentStyles)>, fillcolor="#FDE68A"];
    rfn [label=<<B>registerRiseTables()</B> :118<BR/>→ registerCrownThresholds(Rise, kRiseCrownThresholds)<BR/>→ registerCaptureRules(Rise, nullptr,<BR/>&#160;&#160;&#160;&#160;kRiseNotCapturableMonsterIds)<BR/>→ registerAilmentStyles(Rise, kRiseAilmentStyles)>, fillcolor="#FDE68A"];
    core [label=<<B>core registry slots</B><BR/>Tables world; Tables rise;<BR/><FONT POINT-SIZE="9">game_tables.cpp:33-34</FONT><BR/>hasCrowns / hasCapture /<BR/>hasAilments set true,<BR/>pointers stored<br/>>, fillcolor="#BFDBFE"];
  }

  subgraph cluster_main {
    label=<<B>main() and each tick</B>>; style="rounded,dashed"; color="#6B7280";
    m1 [label=<main.cpp:392<BR/>createReader(gameId, explicitMap,<BR/>&#160;&#160;dataDirs)<BR/>>, fillcolor="#EDE9FE"];
    m2 [label=<reader ctor<BR/><FONT POINT-SIZE="9">MhwReader :132 / MhrReader :166</FONT><BR/><FONT POINT-SIZE="9"><B>no register* call here</B></FONT><BR/><FONT POINT-SIZE="9">(measured: grep = 0 hits)</FONT>>, fillcolor="#DCFCE7"];
    m3 [label=<first reader-&gt;poll()<BR/><FONT POINT-SIZE="9">main.cpp:478</FONT>>, fillcolor="#EDE9FE"];
    m4 [label=<first crown / ailment query<BR/><FONT POINT-SIZE="9">panel_monster.cpp:1125 etc.</FONT>>, fillcolor="#FFEDD5"];
  }

  subgraph cluster_test {
    label=<<B>tests (no reader, no qrc)</B>>; style="rounded,dashed"; color="#6B7280";
    x1 [label=<test init calls<BR/><B>registerWorldTables()</B><BR/><B>registerRiseTables()</B><BR/>explicitly<BR/><FONT POINT-SIZE="9">rise_monster_metadata_tests.cpp:35-36</FONT><BR/><FONT POINT-SIZE="9">rise_monster_name_tests.cpp:59-60</FONT><BR/><FONT POINT-SIZE="9">ailment_style_tests.cpp:43-44</FONT>>, fillcolor="#F3F4F6"];
  }

  wtu -> wobj; wobj -> wfn [label="ctor body"];
  rtu -> robj; robj -> rfn [label="ctor body"];
  wfn -> core [color="#B45309", fontcolor="#B45309", penwidth=1.6, label="pointer handover"];
  rfn -> core [color="#B45309", fontcolor="#B45309", penwidth=1.6, label="pointer handover"];
  core -> m3 [style=dashed, label="already populated"];
  m1 -> m2 [label="constructs"];
  m2 -> m3;
  m3 -> m4;
  x1 -> core [style=dashed, color="#6B7280"];

  note [shape=plaintext, label=<<TABLE BORDER="1" CELLBORDER="0" CELLSPACING="0" CELLPADDING="5" COLOR="#D97706" BGCOLOR="#FFFBEB"><TR><TD ALIGN="LEFT" BALIGN="LEFT"><FONT POINT-SIZE="10">
  <B>顺序约束（源码事实，不是推断）：</B><BR ALIGN="LEFT"/>
  · 同一个 TU 内：初始化器自上而下执行，所以静态对象写在所有表定义之后——<BR ALIGN="LEFT"/>
  &#160;&#160;ctor 里取地址时表已构造（part_schemas_data.cpp:1007 表 vs :1011 对象）<BR ALIGN="LEFT"/>
  · 跨 TU 的顺序未指定，也不影响：注册表只存指针，任一顺序都得到同样的槽位内容<BR ALIGN="LEFT"/>
  · 好处是显式的：edit-mode demo 面板不构造 reader，静态初始化照样把 World 数据注册进去<BR ALIGN="LEFT"/>
  &#160;&#160;（放进 reader 构造函数的话，crown 芯片会静默消失，all_monster.png 差 730 像素）<BR ALIGN="LEFT"/>
  · 目标文件侧的确认：nm -C build/libgame-world.a 有 mhw::registerWorldTables()，<BR ALIGN="LEFT"/>
  &#160;&#160;libgame-rise.a 有 mhw::registerRiseTables()；两者都在各自的静态 init 里被调用<BR ALIGN="LEFT"/>
  </FONT></TD></TR></TABLE>>];
  note -> wtu [style=invis];
'''


# ══════════════════════════════════════════════════════════════════════════
# 6 — riseDamageReaders arbitration
# ══════════════════════════════════════════════════════════════════════════
ARBITRATION = r'''
  labelloc="t";
  label=<<B>6. riseDamageReaders 三槽位仲裁（每个 tick，main.cpp:534-621）</B><BR/><FONT POINT-SIZE="10">the most subtle logic in the tree: which of three JSON feeds wins, and which failure gets reported</FONT>>;

  start [label=<tick &amp;&amp; isRise<BR/><FONT POINT-SIZE="9">main.cpp:534</FONT>>, fillcolor="#EDE9FE"];
  guard [label=<skipUpdate(damagePanel)<BR/>|| skipUpdate(petDamagePanel)<BR/><FONT POINT-SIZE="9">main.cpp:537-539</FONT>>, shape=diamond, fillcolor="#FEF9C3", height=1.0, width=2.4];
  init [label=<newestReader = nullptr<BR/>diagnosticReader =<BR/>&#160;&#160;riseDamageReaders.front().get()<BR/><FONT POINT-SIZE="9">:553-554 — slot A starts as</FONT><BR/><FONT POINT-SIZE="9">the diagnostic candidate</FONT>>, fillcolor="#F3F4F6"];

  loop [label=<for each of 3 readers:<BR/><FONT POINT-SIZE="9">[0] reframework/data/mhr_damage_a.json</FONT><BR/><FONT POINT-SIZE="9">[1] reframework/data/mhr_damage_b.json</FONT><BR/><FONT POINT-SIZE="9">[2] /tmp/mhr_damage.json (legacy)</FONT><BR/><FONT POINT-SIZE="9">main.cpp:404-417</FONT>>, fillcolor="#F3F4F6"];

  upd [label=<reader.update() ?<BR/><FONT POINT-SIZE="9">rise_damage_reader.cpp — false when</FONT><BR/><FONT POINT-SIZE="9">missing / &gt;1 MiB / symlink / unparsable /</FONT><BR/><FONT POINT-SIZE="9">unsupported version / older than 5 s</FONT>>, shape=diamond, fillcolor="#FEF9C3", height=1.3, width=3.2];

  cmp [label=<sequence &gt; current.seq ?<BR/>|| (sequence == &amp;&amp;<BR/>&#160;&#160;&#160;timestampMs &gt;)<BR/><FONT POINT-SIZE="9">:558-564</FONT>>, shape=diamond, fillcolor="#FEF9C3", height=1.2, width=2.6];
  take [label=<<B>newestReader</B><BR/>= &amp;reader<br/>>, fillcolor="#DCFCE7"];
  contender [label=<peer holds a real error ?<BR/><FONT POINT-SIZE="9">:574-577 — replaces only when</FONT><BR/><FONT POINT-SIZE="9">current candidate is Error::Missing</FONT>>, shape=diamond, fillcolor="#FEF9C3", height=1.2, width=3.0];
  swap [label=<diagnosticReader<BR/>&#160;&#160;= &amp;reader<BR/><FONT POINT-SIZE="9">first real error sticks;</FONT><BR/><FONT POINT-SIZE="9">slot A wins ties</FONT>>, fillcolor="#FEE2E2"];

  decide [label=<newestReader != nullptr ?<BR/><FONT POINT-SIZE="9">:582</FONT>>, shape=diamond, fillcolor="#FEF9C3", height=1.0, width=2.6];

  ok [label=<<B>feed available</B><BR/>lastRiseDamageFeedError = None<BR/>damageSnapshot = newestReader-&gt;snapshot()<BR/><B>enrichRiseDamageSnapshot(dmg, snap)</B><BR/><FONT POINT-SIZE="9">rise_damage_roster.cpp:84 — merges the</FONT><BR/><FONT POINT-SIZE="9">GameSnapshot into the damage roster</FONT><BR/>damagePanel.updateRiseDamage(dmg) :593<BR/>petDamagePanel.updateRiseDamage(dmg) :595<br/>>, fillcolor="#DCFCE7"];

  lost [label=<<B>feed lost</B><BR/>diagnosticReader-&gt;lastError() /<BR/>&#160;&#160;lastErrorText() → qInfo / qWarning<BR/><FONT POINT-SIZE="9">:597-611 — severity depends on the</FONT><BR/><FONT POINT-SIZE="9">error kind, Missing logs as info</FONT><BR/>deliver <B>invalid</B> RiseDamageSnapshot<BR/><FONT POINT-SIZE="9">:615 — panels clear old quest data</FONT><BR/><FONT POINT-SIZE="9">instead of freezing the last chart</FONT>>, fillcolor="#FEE2E2"];

  vis [label=<<B>damagePanel.setVisible(</B><BR/>&#160;&#160;panelEnabled() &amp;&amp; hasVisibleContent())<BR/>petDamagePanel likewise :627-641<BR/>triggerUpdate() → paintPanel()>, fillcolor="#FFEDD5"];
  done [label="end of tick", fillcolor="#E5E7EB"];
  skipn [label=<no feed read this tick<BR/><FONT POINT-SIZE="9">panels keep their demo state</FONT>>, fillcolor="#E5E7EB"];

  start -> guard;
  guard -> skipn [label="true"];
  guard -> init [label="false"];
  init -> loop;
  loop -> upd;
  upd -> cmp [label="true"];
  upd -> contender [label="false"];
  cmp -> take [label="yes"];
  cmp -> loop [label="no — keep current", style=dashed];
  take -> loop [label="next slot", style=dashed];
  contender -> swap [label="yes"];
  contender -> loop [label="no", style=dashed];
  swap -> loop [label="next slot", style=dashed];
  loop -> decide [label="after 3rd slot"];
  decide -> ok [label="yes"];
  decide -> lost [label="no"];
  ok -> vis;
  lost -> vis;
  vis -> done;

  note [shape=plaintext, label=<<TABLE BORDER="1" CELLBORDER="0" CELLSPACING="0" CELLPADDING="5" COLOR="#D97706" BGCOLOR="#FFFBEB"><TR><TD ALIGN="LEFT" BALIGN="LEFT"><FONT POINT-SIZE="10">
  <B>选择规则（按代码，不是按意图）：</B><BR ALIGN="LEFT"/>
  · 数据源：活得最久的（sequence 最大，相同则 timestampMs 最大）——不是第一个更新的<BR ALIGN="LEFT"/>
  · 报错源：从 slot A 出发，只有当前候选是 Missing 且这个槽有真实错误时才替换；<BR ALIGN="LEFT"/>
  &#160;&#160;所以「两个同样有信息的错误」里 A 赢（producer 先写 A）<BR ALIGN="LEFT"/>
  · 三个槽位是一起构造、永不为 null 的（main.cpp:549-552 的注释说明 front() 依赖这一点）<BR ALIGN="LEFT"/>
  · REFramework 1.5.9.1 没有 rename，producer 只能交替写两个槽——所以读三个、取最新<BR ALIGN="LEFT"/>
  · update() 失败也照样推进循环，不会提前退出<BR ALIGN="LEFT"/>
  </FONT></TD></TR></TABLE>>];
  note -> start [style=invis];
'''


DIAGRAMS = {
    "01-layers.dot": LAYERS,
    "02-contracts.dot": contracts(),
    "03-registry.dot": REGISTRY,
    "04-dataflow.dot": SEQUENCE,
    "05-static-registration.dot": STATIC_REG,
    "06-damage-arbitration.dot": ARBITRATION,
}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="only verify sources exist")
    args = ap.parse_args()

    if not (REPO / "CMakeLists.txt").exists():
        print(f"error: repo not found at {REPO}", file=sys.stderr)
        return 1

    for name, body in DIAGRAMS.items():
        path = HERE / name
        path.write_text(digraph("g" + name.split(".")[0].replace("-", "_"), body))
        png = run_dot(path)
        print(f"{png.relative_to(REPO)}  ({png.stat().st_size // 1024} KiB)")

    if args.check:
        for rel in [
            "src/core/game_reader.h",
            "src/core/game_snapshot.h",
            "src/core/game_tables.cpp",
            "src/core/game_capabilities.cpp",
            "src/reader_factory.cpp",
            "src/reader_descriptors.cpp",
            "src/rise/part_schemas.cpp",
            "src/monster/data/part_schemas_data.cpp",
            "src/main.cpp",
            "src/ui/panel_player.cpp",
            "src/rise/rise_damage_reader.h",
        ]:
            assert (REPO / rel).exists(), rel
        print("sources present")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
