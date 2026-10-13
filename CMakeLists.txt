cmake_minimum_required(VERSION 3.25)
project(monster_overlay VERSION 1.0.0 LANGUAGES CXX)

# Strip the build machine's absolute paths out of every compiled TU. Without
# this the binaries embed CMAKE_SOURCE_DIR via __FILE__ and the wiring test's
# MONSTER_SOURCE_DIR, so (a) a stranger reading `strings` sees the maintainer's
# home directory and (b) a checkout at a different path can fail to build.
# This is the same class of defect as the v0.9.0 release, which hardcoded a
# local path into the shipped binaries.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Widgets Svg Network)
find_package(LayerShellQt REQUIRED)
find_package(Threads REQUIRED)

# Core library: the game-agnostic kernel.
#
# S3 (library split). This library used to carry both games' data tables and
# the Rise reader alongside the kernel, so `libmonster-core.a` exported 60
# game-specific symbols of 140 — 43% — even after S1-S6 cut the header-level
# dependency. The registry in core/game_tables.{h,cpp} is what makes this
# split possible: it holds pointers, never a game's data, so moving each
# game's tables into its own library does not turn core around to depend on
# the games.
#
# What may live here:
#   * src/core/**               the contracts (snapshot, reader, zone, tables)
#   * src/monster/monster_types.h  the vocabulary the contracts are written in
#   * src/player/ src/quest/    the two shared structural vocabularies
# What may NOT live here: any concrete game's reader, data table, types, or
# offset constant. Both of those now live in game-rise / game-world, and the
# nm assertion in scripts/assert_lib_purity.sh keeps them out.
# ---------------------------------------------------------------------------
# The producer Lua, staged beside the binaries that hash it.
#
# RiseReFrameworkManager decides the overlay Lua is Modified when
# fileSha256(luaSourcePath(applicationDir)) disagrees with the copy already
# installed in the game directory, and it says so as a REFUSAL to install —
# 「悬浮窗 Lua 与打包副本不一致」. luaSourcePath() resolves to
# <applicationDir>/reframework/autorun/mhr-overlay-damage.lua, i.e. inside
# the build tree.
#
# Nothing was putting it there. A single `rm -rf build` therefore left the
# console reporting a corrupted Lua for a file that was byte-identical to
# the installed one, and damage tracking silently stopped. The source is
# tracked at reframework/mhr-overlay-damage.lua; this stages it, so the
# build tree cannot drift from the thing the hash is compared against.
set(_MONS_OVERLAY_LUA_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/reframework/mhr-overlay-damage.lua")
set(_MONS_OVERLAY_LUA_DST_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/reframework/autorun")
add_custom_command(
    OUTPUT "${_MONS_OVERLAY_LUA_DST_DIR}/mhr-overlay-damage.lua"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_MONS_OVERLAY_LUA_DST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_MONS_OVERLAY_LUA_SRC}"
            "${_MONS_OVERLAY_LUA_DST_DIR}/mhr-overlay-damage.lua"
    DEPENDS "${_MONS_OVERLAY_LUA_SRC}"
    COMMENT "Staging the Rise producer Lua for the hash-based install gate")
add_custom_target(monster-overlay-lua ALL
    DEPENDS "${_MONS_OVERLAY_LUA_DST_DIR}/mhr-overlay-damage.lua")


add_library(monster-core STATIC
    src/core/string_table.h
    src/core/string_table.cpp
    src/core/game_detector.h
    src/core/game_detector.cpp
    src/core/game_profile.h
    src/core/game_profile.cpp
    src/core/steam_game_locator.h
    src/core/steam_game_locator.cpp
    src/core/map_paths.h
    src/core/map_paths.cpp
    src/core/process_memory.h
    src/core/process_memory.cpp
    # The game-keyed metadata registry (S3). It is here, not on a game side,
    # because it is the thing that no longer has to know any game's data —
    # it only holds pointers handed to it by registerWorldTables() /
    # registerRiseTables(), which live next to the data they own.
    src/core/game_tables.h
    src/core/game_tables.cpp
    # Per-game traits the UI asks as questions instead of branching on
    # GameId (S4). Deliberately narrow: the game-specific FEATURES stay as
    # `if (game == GameId::Rise)` at their call sites, because a feature only
    # one game has is honestly written as a feature check. What moves here is
    # the per-game DATA (display name) and the behaviours that happen to be
    # identical for every game today.
    src/core/game_capabilities.h
    src/core/game_capabilities.cpp
    # The reader contract main() polls through. Two virtuals, no game
    # knowledge: the alternative was a std::function wrapping two unrelated
    # reader types, which type-erased the difference while leaving the
    # wiring — which reader, which map, which lambda — game-specific.
    src/core/game_reader.h
)
target_include_directories(monster-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(monster-core PUBLIC Qt6::Core Threads::Threads)
target_compile_options(monster-core PRIVATE -Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# game-rise: everything Monster Hunter Rise (S3 library split).
#
# The reader, its five per-domain TUs, the generated name tables, the
# REFramework installer and its config overlay, and the Rise static metadata
# (crowns / not-capturable set / ailment palette) that registerRiseTables()
# hands to core's registry via a static initialiser.
#
# It is the Rise half of the game-keyed architecture: core holds the query
# contract, this library holds the answers and is the only place that knows
# a Rise offset. Nothing here may include a world/ header.
add_library(game-rise STATIC
    src/rise/rise_zones.h
    src/rise/rise_install_locator.h
    src/rise/rise_install_locator.cpp
    src/rise/mhr_types.h
    src/rise/mhr_reader.h
    src/rise/mhr_reader.cpp
    src/rise/mhr_reader_sharpness.cpp
    src/rise/mhr_reader_quest.cpp
    src/rise/mhr_part_names.h
    src/rise/mhr_part_names.cpp
    src/rise/data/mhr_part_names_data.cpp
    src/rise/mhr_monster_names.h
    src/rise/mhr_monster_names.cpp
    src/rise/data/mhr_monster_names_data.cpp
    src/rise/mhr_abnormalities.h
    src/rise/mhr_abnormalities.cpp
    src/rise/rise_damage_types.h
    src/rise/rise_damage_reader.h
    src/rise/rise_damage_reader.cpp
    src/rise/rise_damage_roster.h
    src/rise/rise_damage_roster.cpp
    # Rise-only REFramework installer (network fetch + checksum + install of
    # the REFramework runtime payload). It is Rise behaviour, not a shared
    # contract, so it lives under src/rise/ rather than src/core/.
    src/rise/reframework/rise_reframework_manager.h
    src/rise/reframework/rise_reframework_manager.cpp
    # The REFramework user-config (re2_fw_config.txt) overlay — the INI
    # rewriter, its candidate-path resolution and the menu-state query/apply.
    # Its 5 entry points are Rise-adjacent but REFramework-config specific and
    # carry no dependency on the installer class, so they are their own TU.
    src/rise/reframework/reframework_config.h
    src/rise/reframework/reframework_config.cpp
    src/rise/reframework/reframework_fetcher.h
    src/rise/reframework/reframework_fetcher.cpp
    # Rise static metadata: crown thresholds, the not-capturable set and the
    # ailment palette, plus the registerRiseTables() that hands them to
    # core/game_tables.h. Defined beside the data, because the registry must
    # never name a game's table.
    src/rise/part_schemas.cpp
)
target_include_directories(game-rise PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(game-rise PUBLIC Qt6::Core monster-core)
target_compile_options(game-rise PRIVATE -Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# game-world: everything Monster Hunter World (S3 library split).
#
# Renamed from `mhw-reader`, which said "the reader" while holding exactly one
# game's reader — the name was how a second game's assets ended up in the same
# archive in the first place. It now also carries the World vocabulary
# (world_types.{h,cpp}) and the World data tables that registerWorldTables()
# hands to core's registry, so the library is "everything World" rather than
# "the reader that happened to be the only one".
add_library(game-world STATIC
    src/world/world_types.h
    src/world/world_types.cpp
    src/world/world_reader.h
    src/world/world_reader.cpp
    src/world/monster_reader.cpp
    # World monster display names: the two id-key tables plus the one lookup
    # that reads them, moved out of monster_reader.cpp's monsterDisplayName() so
    # that the reader TU carries reader logic only. It is the DATA+logic pair
    # the Rise equivalent splits across src/rise/data/mhr_monster_names_data.cpp
    # + src/rise/mhr_monster_names.cpp; see the header comment in the file for
    # why one TU is the honest alignment here (no generator to protect). It needs
    # only monster-core (StringTable), so it belongs in this library and not in
    # monster-core: world data stays out of the core → world-free direction.
    src/world/monster_names.cpp
    src/world/quest_reader.cpp
    src/world/player_reader.cpp
    src/world/world_severable_scan.h
    # The shared target-pick rule (highest HP with a lock, else the matching
    # address, else the first alive). Both readers need it; it is game-agnostic
    # in the "snapshot in, index out" sense and is listed here only because it
    # is not a core contract. Moving it into monster-core would be equally
    # defensible.
    src/monster/target_selector.cpp
    # World monster DATA tables (kPartSchemas / kAilmentNames / kAilmentNamesEn /
    # kCrownThresholds / kMonsterCaptureThresholds) plus the registerWorldTables()
    # that hands them to core/game_tables.h — their own translation unit; the
    # lookup code that reads them lives elsewhere. scripts/gen_schema.py rewrites
    # THIS file in place. Named src/monster/data/ for its generator, not for
    # ownership: every table in it is a World table.
    src/monster/data/part_schemas_data.cpp
)
target_include_directories(game-world PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(game-world PUBLIC Qt6::Core monster-core)
target_compile_options(game-world PRIVATE -Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# mhw-ui: the whole GUI layer, compiled exactly once.
#
# Every src/ui/*.cpp used to be listed by the 12 targets that draw panels,
# which meant the UI layer was compiled 12x over per build (6 core files —
# formatters/icon/panel + the three panels that every GUI target renders —
# literally 12 compiles each, 111 src/ui compiles in total) and any target
# could silently drift by picking its own subset. The same argument reached
# the reader layer in e513226; this is the UI half of that sink.
#
# Link this instead of listing UI sources. The private QtGui headers that
# screen_query.cpp needs (qpa/qplatformscreen.h, for the backend-native
# physical screen size) are PRIVATE to this library, so no consumer has to
# carry them.
#
# src/ui is self-contained w.r.t. the other layers: it includes only
# monster-core / game-world headers (core/…, player/…, quest/…, world/…,
# rise/…), never the reverse. That is what makes it safe to compile once
# and link 12 ways.
# ---------------------------------------------------------------------------
add_library(mhw-ui STATIC
    src/ui/control_panel.cpp
    src/ui/control_panel.h
    # v1.0.0 fidelity port (root scroll): the shell's page-scroll
    # container. One QScrollArea subclass whose only job is to keep the
    # content layer at its own natural height (read back from the
    # layout, never hardcoded) so a root taller than the window scrolls
    # the way reference.html's does — including the 15px of width the
    # page's scrollbar costs, which the QSS already spells.
    src/ui/console_root_scroll.cpp
    src/ui/console_root_scroll.h
    # v1.0.0 fidelity port: the console's window model — the prototype's
    # `state.scopes[...].windows[]` as a derived view over the live
    # Panels, plus the per-window visibility key and the
    # own→parent→primary output chain. A separate TU on purpose:
    # control_panel.cpp is 6,900 lines and the model is read by three
    # surfaces (canvas, inspector, export), so it gets its own file
    # rather than being one more block in the biggest one.
    src/ui/console_fidelity_windows.cpp
    src/ui/console_fidelity_windows.h
    src/ui/control_panel_styles.cpp
    src/ui/control_panel_styles.h
    src/ui/rise_reframework_card.cpp
    src/ui/rise_reframework_card.h
    src/ui/formatters.cpp
    src/ui/formatters.h
    src/ui/viewmodel/console_layout_store.cpp
    src/ui/viewmodel/console_layout_store.h
    src/ui/viewmodel/console_text_helpers.cpp
    src/ui/viewmodel/console_text_helpers.h
    src/ui/hud_canvas.cpp
    src/ui/hud_canvas.h
    src/ui/icon.cpp
    src/ui/icon.h
    src/ui/panel.cpp
    src/ui/panel.h
    src/ui/panel_accent.h
    src/ui/panel_damage.cpp
    src/ui/panel_damage.h
    # v0.12 third wave: the DPS panel's two width-hungry blocks as their
    # own windows. The draw code is HOISTED into a shared header rather
    # than copied, so the panel and the two windows paint the same bytes
    # from one definition — the alternative is a second spelling that
    # drifts a pixel and makes all_damage.png disagree with the window.
    src/ui/panel_damage_blocks.h
    src/ui/panel_damage_split_share.cpp
    src/ui/panel_damage_split_share.h
    src/ui/panel_damage_split_chart.cpp
    src/ui/panel_damage_split_chart.h
    # v0.14: the DPS panel's ROWS block as its own window - the per-player
    # name / damage / DPS list. It was the one block the split had no home
    # for: Rows and OtherMembers are what DamagePanel keeps in Split mode,
    # so the user's "the per-player part is missing" was the preview
    # showing the sub-windows only, with the rows living in a mother panel
    # the split canvas does not draw.
    #
    # It deliberately has NO row in kDamageSplitTargets. That table's
    # subscripts are the split-window index space (hud_canvas and its test
    # both index it directly), so a third row would shift what the existing
    # two mean. This window is preview-only for now; wiring it into the
    # live overlay needs a decision about whether DamagePanel should stop
    # drawing Rows, which is a mask change and not a rename.
    src/ui/panel_damage_split_rows.cpp
    src/ui/panel_damage_split_rows.h
    src/ui/viewmodel/damage_view_model.cpp
    src/ui/viewmodel/damage_view_model.h
    src/ui/viewmodel/monster_view_model.cpp
    src/ui/viewmodel/monster_view_model.h
    src/ui/viewmodel/overlay_process_controller.cpp
    src/ui/viewmodel/overlay_process_controller.h
    src/ui/viewmodel/rise_reframework_status.cpp
    src/ui/viewmodel/rise_reframework_status.h
    src/ui/panel_monster.cpp
    src/ui/panel_monster.h
    # v0.12 third wave: the monster panel's blocks as their own windows.
    # HP / Stamina / Enrage are three bars; Ail is a wrapped card row and
    # Parts the part grid, both answering the columns dial. Info and
    # Tenderize deliberately stay in the panel — see the NOT-in-table note
    # by kMonsterSplitTargets for why each one is not worth a window.
    src/ui/panel_monster_split_hp.cpp
    src/ui/panel_monster_split_hp.h
    src/ui/panel_monster_split_stamina.cpp
    src/ui/panel_monster_split_stamina.h
    src/ui/panel_monster_split_enrage.cpp
    src/ui/panel_monster_split_enrage.h
    src/ui/panel_monster_split_ail.cpp
    src/ui/panel_monster_split_ail.h
    src/ui/panel_monster_split_parts.cpp
    src/ui/panel_monster_split_parts.h
    # v0.13: the regrouped monster windows. The user's verdict on the
    # five-way split was「切太狠了」 — 图标/激怒/血量 belong together as the
    # glance, 异常/部位 belong together as the plan — so the table went
    # from five rows to two. The five single-block windows above are no
    # longer referenced by kMonsterSplitTargets; they stay listed here
    # because panel_monster_split_ail.{h,cpp} still owns the ail model that
    # the conditions window includes, and because deleting them is a
    # separate, deliberate step rather than a side effect of this one.
    #
    # Every block still has its own MonsterSection bit, so turning 激怒 off
    # removes the bar AND its gap from the identity window rather than
    # gating the whole window — which is what「还可选择性的关闭部分」asked
    # for.
    src/ui/panel_monster_split_identity.cpp
    src/ui/panel_monster_split_identity.h
    src/ui/panel_monster_split_conditions.cpp
    src/ui/panel_monster_split_conditions.h
    src/ui/panel_pet_damage.cpp
    src/ui/panel_pet_damage.h
    src/ui/panel_player.cpp
    src/ui/panel_player.h
    # v0.12 split: the player panel's status-pill strip as its own
    # layer-shell window. Purely additive — PlayerPanel is untouched, so
    # the single-panel mode stays byte-identical until the console opts in.
    src/ui/panel_player_split.cpp
    src/ui/panel_player_split.h
    # v0.12 split, second wave: the player panel's remaining low-coupling
    # blocks as their own layer-shell windows. Same contract as the strip
    # above — independent settingsKey, setChromeVisible(false),
    # isSubWindow() == true, hasContent() collapsing an empty window. Each
    # is per-game by construction (mantles World-only, wirebugs Rise-only),
    # so the wrong game's arm reads nothing and the window simply does not
    # mount.
    src/ui/panel_player_split_bars.cpp
    src/ui/panel_player_split_bars.h
    src/ui/panel_player_split_mantles.cpp
    src/ui/panel_player_split_mantles.h
    src/ui/panel_player_split_weapon.cpp
    src/ui/panel_player_split_weapon.h
    src/ui/panel_player_split_info.cpp
    src/ui/panel_player_split_info.h
    src/ui/panel_player_split_wirebug.cpp
    src/ui/panel_player_split_wirebug.h
    # The grid arithmetic every split window above shares: one definition of
    # "how wide is a row of N items", so the width a window reserves and the
    # slot width it draws into cannot drift apart again.
    src/ui/panel_sizing.h
    # v0.13: the per-game layout namespace. A panel's persisted section is
    # scoped to the game it was tuned for — [world/player-status] and
    # [rise/player-status] in the same file — because one flat namespace
    # made the two games' windows read each other's geometry, and a
    # preview showed a World-only mantle beside a Rise-only wirebug.
    src/ui/panel_settings_game.cpp
    src/ui/panel_settings_game.h
    src/ui/panel_player_sharpness.cpp
    src/ui/panel_player_sharpness.h
    src/ui/viewmodel/player_view_model.cpp
    src/ui/viewmodel/player_view_model.h
    src/ui/panel_player_metrics.h
    src/ui/panel_sections.h
    src/ui/panel_source.cpp
    src/ui/panel_source.h
    src/ui/rise_reframework_bridge.cpp
    src/ui/rise_reframework_bridge.h
    src/ui/screen_query.cpp
    src/ui/screen_query.h
    src/ui/section_count_bar.cpp
    src/ui/section_count_bar.h
    src/ui/section_row.cpp
    src/ui/section_row.h
    src/ui/toggle_chip.cpp
    src/ui/toggle_chip.h
    src/ui/ui_theme.cpp
    src/ui/ui_theme.h
)
target_include_directories(mhw-ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(mhw-ui PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
    # v1.0.0: the console's 检查更新 entry. It really asks GitHub for the
    # latest release (see ControlPanel::checkForUpdates) rather than
    # printing the prototype's fake 已是最新版本, and QNetworkAccessManager
    # is the only Qt API that can. Network is linked here rather than in a
    # helper TU because the request lives in control_panel.cpp itself —
    # which is the one file this wave owns.
    Qt6::Network
    LayerShellQt::Interface
    monster-core
    game-rise
    game-world
)
target_compile_options(mhw-ui PRIVATE -Wall -Wextra -Wpedantic)

# Private QtGui headers (qpa/qplatformscreen.h) live under
# <root>/QtGui/<ver>/QtGui/qpa/. Locate that include root and expose it
# to mhw-ui so screen_query.cpp can use QPlatformScreen::geometry() —
# the only public-ish way to get the backend-native screen size in
# physical pixels without guessing from QScreen::geometry() ×
# devicePixelRatio(). It stays PRIVATE to mhw-ui: a consumer only calls
# screen_query::detect()/listOutputs(), it does not include qpa headers
# itself, so no consumer needs this path.
#
# <root> is NOT the same everywhere, and a GLOB over one fixed layout
# used to hard-FAIL the configure step on every distro that packages Qt
# differently:
#
#   Arch Linux            /usr/include/qt6/QtGui/6.11.2/QtGui/qpa/
#   Fedora / openSUSE     /usr/include/qt6/QtGui/6.11.2/QtGui/qpa/
#   Debian / Ubuntu       /usr/include/<triplet>/qt6/QtGui/6.8.2/
#                                       QtGui/qpa/qplatformscreen.h
#                         (multiarch: the whole Qt6 tree is prefixed with
#                         the DEB_HOST_MULTIARCH triplet, e.g.
#                         /usr/include/x86_64-linux-gnu/qt6/)
#
# So instead of enumerating a directory tree we let find_path() probe the
# known roots. Both distro families must keep working, and the version
# directory is a single directory level below <root>/QtGui on both
# layouts, so a glob would not buy us anything find_path cannot do more
# portably.
set(_QT6_GUI_PRIVATE_PATH_SUFFIXES QtGui/${Qt6Gui_VERSION}/QtGui)
list(APPEND _QT6_GUI_PRIVATE_PATH_SUFFIXES QtGui/${Qt6Gui_MAJOR_MINOR}/QtGui)
list(REMOVE_DUPLICATES _QT6_GUI_PRIVATE_PATH_SUFFIXES)

# Candidate include roots. QT6_QPA_HEADERS_ROOT is the escape hatch for a
# locally-built / self-installed Qt (the usual reason the header is
# somewhere else entirely): pass either the install prefix or the
# "qt6" include root itself and both are probed.
set(_QT6_GUI_ROOT_CANDIDATES "")
if(QT6_QPA_HEADERS_ROOT)
    list(APPEND _QT6_GUI_ROOT_CANDIDATES
        "${QT6_QPA_HEADERS_ROOT}"
        "${QT6_QPA_HEADERS_ROOT}/include"
        "${QT6_QPA_HEADERS_ROOT}/include/qt6"
    )
endif()
list(APPEND _QT6_GUI_ROOT_CANDIDATES
    /usr/include/qt6
    /usr/local/include/qt6
)
file(GLOB _QT6_GUI_MULTIARCH_ROOTS
    LIST_DIRECTORIES true
    "/usr/include/*/qt6"
)
list(APPEND _QT6_GUI_ROOT_CANDIDATES ${_QT6_GUI_MULTIARCH_ROOTS})

find_path(_QT6_GUI_PRIVATE_INCLUDE_DIR
    NAMES qpa/qplatformscreen.h
    PATHS ${_QT6_GUI_ROOT_CANDIDATES}
    PATH_SUFFIXES ${_QT6_GUI_PRIVATE_PATH_SUFFIXES}
    DOC "Root holding the QtGui private qpa/ headers"
)

if(NOT _QT6_GUI_PRIVATE_INCLUDE_DIR)
    set(_searched "")
    foreach(_root IN LISTS _QT6_GUI_ROOT_CANDIDATES)
        foreach(_suf IN LISTS _QT6_GUI_PRIVATE_PATH_SUFFIXES)
            list(APPEND _searched "${_root}/${_suf}")
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES _searched)
    string(REPLACE ";" "\n    " _searched "${_searched}")
    message(FATAL_ERROR
        "Could not locate Qt6's private QtGui headers "
        "(qpa/qplatformscreen.h), which src/ui/screen_query.cpp compiles "
        "against. Qt ships them in a SEPARATE package from the normal "
        "devel headers.\n"
        "  Searched (each suffixed with <QtGui version>):\n"
        "    ${_searched}\n"
        "  Install the package that provides them:\n"
        "    Debian / Ubuntu   qt6-base-private-dev\n"
        "    Fedora            qt6-qtbase-private-devel\n"
        "    Arch              qt6-base (already includes them)\n"
        "  If Qt was built or installed by hand, re-run cmake with\n"
        "    -DQT6_QPA_HEADERS_ROOT=<prefix-or-qt6-include-dir>")
endif()
target_include_directories(mhw-ui PRIVATE
    ${_QT6_GUI_PRIVATE_INCLUDE_DIR}
)

# The UI layer is now one library. Keep the sink honest: a target that adds
# a src/ui/* source directly alongside `mhw-ui` would compile that file
# twice and risk duplicate symbols. pet-damage-panel-tests does exactly
# that for panel_pet_damage.cpp under MHW_PET_DAMAGE_DATA_TEST — it does
# not link mhw-ui, so it is not a duplicate. Anything else must link, not
# list. (The reader test target used to textually include panel_player.cpp
# for its wirebug-label / stamina-scale helpers; those helpers now live in
# the standalone inline src/ui/panel_player_metrics.h, see its header note.)
get_target_property(_mhw_ui_type mhw-ui TYPE)
if(NOT _mhw_ui_type STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "mhw-ui must be a STATIC library")
endif()

add_executable(monster-overlay
    src/main.cpp
    # The assembly point: the one translation unit that knows both the
    # IGameReader contract and every implementation of it. Above the core
    # contract and the games, so neither has to know the other exists.
    src/reader_factory.cpp
    src/reader_factory.h
    # Per-game reader configuration, split out so it names no concrete
    # reader and can be tested without the reader chain.
    src/reader_descriptors.cpp
    src/resources/resources.qrc
    assets/icons.qrc
)

target_link_libraries(monster-overlay PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
    LayerShellQt::Interface
    monster-core
    game-rise
    game-world
    mhw-ui
)

target_compile_definitions(monster-overlay PRIVATE
    MONSTER_VERSION="${PROJECT_VERSION}"
)

target_compile_options(monster-overlay PRIVATE -Wall -Wextra -Wpedantic)

# Offscreen snapshot of the player panel in edit/demo mode. Used for
# visual regression testing without launching the layer-shell overlay
# (which would steal keyboard focus and lock the user out of the
# desktop — see the note on offscreen testing in the project docs).
add_executable(snap-player-demo
    tests/snap_player_demo.cpp
    src/resources/resources.qrc
    assets/icons.qrc
)
target_link_libraries(snap-player-demo PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
    LayerShellQt::Interface
    monster-core
    game-world
    mhw-ui
)
target_include_directories(snap-player-demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(snap-player-demo PRIVATE -Wall -Wextra -Wpedantic)

add_executable(snap-all-demo
    tests/snap_all_demo.cpp
    src/resources/resources.qrc
    assets/icons.qrc
)
target_link_libraries(snap-all-demo PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
    LayerShellQt::Interface
    monster-core
    game-world
    mhw-ui
)
target_include_directories(snap-all-demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(snap-all-demo PRIVATE -Wall -Wextra -Wpedantic)

# v0.10.8: World multiplayer part-card layout probe. Renders the same
# synthetic World part data solo and multiplayer so the extra-row arithmetic
# (pcHasPrimaryGauge / pcGaugeExtraH) can be verified with a pixel diff
# instead of by reasoning about it. Diagnostic only — never built by
# `cmake --build .`, never registered as a CTest.
add_executable(snap-world-mp EXCLUDE_FROM_ALL
    tests/snap_world_mp.cpp
    src/resources/resources.qrc
    assets/icons.qrc
)
target_link_libraries(snap-world-mp PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
    LayerShellQt::Interface
    monster-core
    game-world
    mhw-ui
)
target_include_directories(snap-world-mp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(snap-world-mp PRIVATE -Wall -Wextra -Wpedantic)

# v0.10.10 off-screen probe: part-gauge FILL COLOUR acceptance (broken must
# be the #71717A Broken.Foreground grey, untouched #78909c, tenderize #f6a522
# and the Row-1 flinch blue #4B8EEE untouched). One card per scenario so
# every bar in the PNG is attributable. Diagnostic only — not built by
# `cmake --build .`, not registered as a CTest.
add_executable(snap-gauge-fill EXCLUDE_FROM_ALL
    tests/snap_gauge_fill.cpp
    src/resources/resources.qrc
    assets/icons.qrc
)
target_link_libraries(snap-gauge-fill PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
    LayerShellQt::Interface
    monster-core
    game-world
    mhw-ui
)
target_include_directories(snap-gauge-fill PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(snap-gauge-fill PRIVATE -Wall -Wextra -Wpedantic)

# Control console — standalone Qt window, no layer-shell. Reuses the same
# panel paint code as the live overlay to preview section toggles.
# The whole GUI layer now arrives via mhw-ui, so no src/ui/* source is
# listed here (and the qpa/screen_query private include lives on mhw-ui).
add_executable(monster-control
    src/main_control.cpp
    src/resources/resources.qrc
    assets/icons.qrc
)
target_link_libraries(monster-control PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
    LayerShellQt::Interface
    monster-core
    game-world
    mhw-ui
)
target_include_directories(monster-control PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# _QT6_GUI_PRIVATE_INCLUDE_DIR is deliberately NOT re-added here: the only
# consumer of qpa/qplatformscreen.h is screen_query.cpp, which mhw-ui owns
# (PRIVATE). monster-control includes no qpa headers of its own.
target_compile_definitions(monster-control PRIVATE
    MONSTER_VERSION="${PROJECT_VERSION}"
)
target_compile_options(monster-control PRIVATE -Wall -Wextra -Wpedantic)

# L2 smoke test for the control-panel mask persistence round-trip.
# Rebuilds an in-process ControlPanel, flips switches via the public
# SectionRow API (found by findChildren), lets the dtor save to disk,
# then re-opens and verifies the load applied the same state.
add_executable(monster-control-l2-smoke
    tests/control_l2_smoke.cpp
    src/resources/resources.qrc
    assets/icons.qrc
)
target_link_libraries(monster-control-l2-smoke PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
    LayerShellQt::Interface
    monster-core
    game-world
    mhw-ui
)
target_include_directories(monster-control-l2-smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# No _QT6_GUI_PRIVATE_INCLUDE_DIR: screen_query.cpp (the only qpa consumer)
# is inside mhw-ui and has it PRIVATE.
target_compile_options(monster-control-l2-smoke PRIVATE -Wall -Wextra -Wpedantic)

# Rise-only REFramework console smoke test: builds the full control console
# and verifies the Damage-inspector management card (visible with its three
# action buttons in Rise mode, hidden in World mode). Read-only against the
# Steam tree; config writes go to a redirected XDG_CONFIG_HOME.
add_executable(monster-control-rise-rf-smoke
    tests/control_reframework_smoke.cpp
    src/resources/resources.qrc
    assets/icons.qrc
)
target_link_libraries(monster-control-rise-rf-smoke PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
    LayerShellQt::Interface
    monster-core
    game-rise
    game-world
    mhw-ui
)
target_include_directories(monster-control-rise-rf-smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# No _QT6_GUI_PRIVATE_INCLUDE_DIR: see monster-control-l2-smoke above.
target_compile_options(monster-control-rise-rf-smoke PRIVATE -Wall -Wextra -Wpedantic)

# v0.5 P0 invariant test: Panel setters + HudCanvas toggle + hit-test.
# Builds without monster-control so it doesn't pull in the full GUI shell.
# The GUI layer now arrives whole via mhw-ui, so this target only contributes
# its own test source plus the non-UI helpers.
add_executable(monster-control-p0-test
    tests/control_panel_p0_test.cpp
    src/resources/resources.qrc
    assets/icons.qrc
)
target_link_libraries(monster-control-p0-test PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
    LayerShellQt::Interface
    monster-core
    game-world
    mhw-ui
)
target_include_directories(monster-control-p0-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# No _QT6_GUI_PRIVATE_INCLUDE_DIR: screen_query.cpp (the only qpa consumer)
# is inside mhw-ui and has it PRIVATE.
target_compile_options(monster-control-p0-test PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
if(BUILD_TESTING)
# Pixel-regression suite for the overlay panels: renders the panel code
# paths the snap_* diagnostics cover and asserts every pixel against the
# committed baselines in tests/baseline/. Added in v0.11 because the
# snap_* tools were a visual aid with no assertion behind them, so GUI
# refactors had nothing to fail on.
add_executable(snapshot-regression-tests
    tests/snapshot_regression_tests.cpp
    src/resources/resources.qrc
    assets/icons.qrc
)
target_link_libraries(snapshot-regression-tests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
    LayerShellQt::Interface
    monster-core
    game-world
    mhw-ui
)
target_include_directories(snapshot-regression-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(snapshot-regression-tests PRIVATE -Wall -Wextra -Wpedantic)
# Baseline PNGs live in the SOURCE tree; ctest runs this from build/, so
# resolve the directory at compile time instead of relative to cwd.
target_compile_definitions(snapshot-regression-tests PRIVATE
    MHW_BASELINE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/baseline")
add_test(NAME snapshot-regression-tests COMMAND snapshot-regression-tests)
# Must resolve its baselines through the source tree whatever the cwd is.
set_tests_properties(snapshot-regression-tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(world-severable-scan-tests
    tests/world_severable_scan_tests.cpp
)
    target_include_directories(world-severable-scan-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(world-severable-scan-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME world-severable-scan-tests COMMAND world-severable-scan-tests)

    # GUI smoke targets are built above because they share the complete control
    # console source set. Register them here so a normal CTest run cannot miss
    # persistence/preview regressions. Both use the offscreen QPA; the L2 test
    # owns a fixed temporary config path, so keep it serial under `ctest -j`.
    add_test(NAME monster-control-l2-smoke COMMAND monster-control-l2-smoke)
    add_test(NAME monster-control-p0-test COMMAND monster-control-p0-test)
    add_test(NAME monster-control-rise-rf-smoke COMMAND monster-control-rise-rf-smoke)
    set_tests_properties(monster-control-l2-smoke monster-control-p0-test
        monster-control-rise-rf-smoke
        PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    set_tests_properties(monster-control-l2-smoke PROPERTIES RUN_SERIAL TRUE)

    # Diagnostic snapshot tool for the MonsterPanel no-data state. It is
    # intentionally not a CTest: rendering is an offscreen visual aid, not a
    # deterministic assertion. Keep it available for explicit test-period
    # builds without pulling it into the default build.
    add_executable(snap-empty-state EXCLUDE_FROM_ALL
        tests/snap_empty_state.cpp
        src/resources/resources.qrc
        assets/icons.qrc
    )
    target_link_libraries(snap-empty-state PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
        LayerShellQt::Interface
        monster-core
        game-world
        mhw-ui
    )
    target_include_directories(snap-empty-state PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(snap-empty-state PRIVATE -Wall -Wextra -Wpedantic)

    # Diagnostic snapshot tool for the PlayerPanel no-data states (v0.9.1 P1):
    # waiting-for-exe / read-denied / map-missing / attached-but-empty, all of
    # which used to paint the same unexplained empty panel. Not a CTest — it
    # is a visual aid and needs a QPA platform.
    add_executable(snap-player-offline EXCLUDE_FROM_ALL
        tests/snap_player_offline.cpp
        src/resources/resources.qrc
        assets/icons.qrc
    )
    target_link_libraries(snap-player-offline PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
        LayerShellQt::Interface
        monster-core
        game-world
        mhw-ui
    )
    target_include_directories(snap-player-offline PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(snap-player-offline PRIVATE -Wall -Wextra -Wpedantic)

    add_executable(monster-reader-tests
        tests/reader_tests.cpp
        # qrc: the tests flip StringTable between zh-CN / en-US to assert
        # both columns of the data-name tables.
        src/resources/resources.qrc
    )
    target_link_libraries(monster-reader-tests PRIVATE
        Qt6::Core monster-core game-rise game-world)
    target_include_directories(monster-reader-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(monster-reader-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME monster-reader-tests COMMAND monster-reader-tests)

    add_executable(monster-probe
        tests/monster_probe.cpp
    )
    target_link_libraries(monster-probe PRIVATE Qt6::Core monster-core game-world)
    target_include_directories(monster-probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(monster-probe PRIVATE -Wall -Wno-unused-variable -Wno-unused-parameter)

    add_executable(monster-scan
        tests/monster_scan.cpp
    )
    target_link_libraries(monster-scan PRIVATE Qt6::Core monster-core game-world)
    target_include_directories(monster-scan PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(monster-scan PRIVATE -Wall -Wno-unused-variable -Wno-unused-parameter)

    add_executable(monster-probe-parts
        tests/monster_probe_parts.cpp
    )
    target_link_libraries(monster-probe-parts PRIVATE Qt6::Core monster-core game-world)
    target_include_directories(monster-probe-parts PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(monster-probe-parts PRIVATE -Wall -Wno-unused-variable -Wno-unused-parameter)

    add_executable(monster-probe-ailments
        tests/monster_probe_ailments.cpp
    )
    target_link_libraries(monster-probe-ailments PRIVATE Qt6::Core monster-core game-world)
    target_include_directories(monster-probe-ailments PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(monster-probe-ailments PRIVATE -Wall -Wno-unused-variable -Wno-unused-parameter)

    add_executable(monster-probe-ailments-watch
        tests/monster_probe_ailments_watch.cpp
    )
    target_link_libraries(monster-probe-ailments-watch PRIVATE Qt6::Core monster-core game-world)
    target_include_directories(monster-probe-ailments-watch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(monster-probe-ailments-watch PRIVATE -Wall -Wno-unused-variable -Wno-unused-parameter)

    add_executable(monster-probe-mantles
        tests/monster_probe_mantles.cpp
    )
    target_link_libraries(monster-probe-mantles PRIVATE Qt6::Core monster-core game-world)
    target_include_directories(monster-probe-mantles PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(monster-probe-mantles PRIVATE -Wall -Wno-unused-variable -Wno-unused-parameter)

    add_executable(monster-probe-mantles-wide
        tests/monster_probe_mantles_wide.cpp
    )
    target_link_libraries(monster-probe-mantles-wide PRIVATE Qt6::Core monster-core game-world)
    target_include_directories(monster-probe-mantles-wide PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(monster-probe-mantles-wide PRIVATE -Wall -Wno-unused-variable -Wno-unused-parameter)

    # Register every probe as a CTest target. The probe binaries read
    # /proc/<mhw pid>/mem at run time; if MHW isn't running they'll
    # print a diagnostic and exit with the documented "MHW not found"
    # code (1 for monster-probe, 3 for monster-probe-ailments[-watch]).
    # Mark them as SKIP so ctest still passes on a CI box without MHW.
    add_test(NAME monster-probe               COMMAND monster-probe)
    add_test(NAME monster-scan                COMMAND monster-scan)
    add_test(NAME monster-probe-parts         COMMAND monster-probe-parts --no-watch)
    add_test(NAME monster-probe-ailments      COMMAND monster-probe-ailments)
    add_test(NAME monster-probe-ailments-watch COMMAND monster-probe-ailments-watch /tmp/monster-probe-ailments-watch-test.log)
    add_test(NAME monster-probe-mantles       COMMAND monster-probe-mantles)
    add_test(NAME monster-probe-mantles-wide  COMMAND monster-probe-mantles-wide)
    set_tests_properties(monster-probe monster-scan monster-probe-parts monster-probe-ailments
        monster-probe-ailments-watch monster-probe-mantles monster-probe-mantles-wide
        PROPERTIES SKIP_RETURN_CODE "1;3")

    # Unit tests for Rise/World monster metadata isolation. These use only
    # static tables generated from HunterPie XML and never touch live game memory.
    # Does NOT link monster-core: this test asserts the pure static metadata
    # tables in monster_types.h that the data table TUs define (crowns, capture
    # thresholds, not-capturable ids), so those translation units stay listed.
    # Adding monster-core here would drag in the whole reader/UI chain for a
    # pure-table check and create a duplicate-symbol fight with the TU above.
    # Both table TUs are listed: monster/data/ holds the World tables, rise/ holds
    # crownThresholdsFor()'s kRiseCrownThresholds / kRiseNotCapturableMonsterIds.
    # game_tables.cpp is listed for the same reason: since S3 the two lookups
    # live in the registry rather than as inline functions in monster_types.h,
    # and this test never constructs a reader, so nothing registers the tables
    # for it. It links the registry TU directly instead of monster-core, which
    # would drag in the whole reader chain for a pure-table check.
    add_executable(rise-monster-metadata-tests
        tests/rise_monster_metadata_tests.cpp
        src/monster/data/part_schemas_data.cpp
        src/rise/part_schemas.cpp
        src/core/game_tables.cpp
        src/resources/resources.qrc
    )
    target_link_libraries(rise-monster-metadata-tests PRIVATE Qt6::Core)
    target_include_directories(rise-monster-metadata-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(rise-monster-metadata-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME rise-monster-metadata-tests COMMAND rise-monster-metadata-tests)

    # v0.8.4-r19 monster-identity: official Rise monster names (generated from
    # HunterPie's zh-cn.xml) + the Rise crown-size ratio contract. Pure static
    # tables/inline helpers — never touches a live game process.
    add_executable(rise-monster-name-tests
        tests/rise_monster_name_tests.cpp
        src/resources/resources.qrc
    )
    target_link_libraries(rise-monster-name-tests PRIVATE
        Qt6::Core monster-core game-rise game-world)
    target_include_directories(rise-monster-name-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(rise-monster-name-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME rise-monster-name-tests COMMAND rise-monster-name-tests)

    add_executable(rise-part-names-hp-tests
        tests/rise_part_names_hp_tests.cpp
        src/resources/resources.qrc
    )
    target_link_libraries(rise-part-names-hp-tests PRIVATE
        Qt6::Core monster-core game-rise)
    target_include_directories(rise-part-names-hp-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(rise-part-names-hp-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME rise-part-names-hp-tests COMMAND rise-part-names-hp-tests)

    # Unit tests for the core string table — independent of MHW.
    add_executable(monster-core-tests
        tests/string_table_tests.cpp
        src/core/string_table.h
    )
    target_link_libraries(monster-core-tests PRIVATE Qt6::Core monster-core)
    target_include_directories(monster-core-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(monster-core-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME monster-core-tests COMMAND monster-core-tests)

    # Game identity: the one place that names a game. QtCore-only, so the
    # vocabulary is testable without a game process or a window.
    add_executable(game-profile-tests
        tests/game_profile_tests.cpp
    )
    target_link_libraries(game-profile-tests PRIVATE Qt6::Core monster-core)
    target_include_directories(game-profile-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(game-profile-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME game-profile-tests COMMAND game-profile-tests)

    # S5: the reader descriptors are the one place that names every game, so
    # they are the place a new game could be forgotten. This pins the two
    # pure decisions per game. It links reader_descriptors.cpp, NOT
    # reader_factory.cpp: the descriptors are deliberately free of the
    # concrete reader types, so a data test does not drag in the reader chain.
    add_executable(reader-factory-tests
        tests/reader_factory_tests.cpp
        src/reader_descriptors.cpp
        src/reader_factory.cpp
    )
    # game-rise and game-world are needed because createReader() constructs a
    # concrete reader; the descriptors above are what this test is about, and
    # the two libraries are how it reaches them. Linking the archive, not the
    # .o files, so these are the same objects the executable links.
    target_link_libraries(reader-factory-tests PRIVATE
        Qt6::Core monster-core game-rise game-world)
    target_include_directories(reader-factory-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(reader-factory-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME reader-factory-tests COMMAND reader-factory-tests)

    # S6: the ailment palette lives in per-game tables because the two id
    # spaces are unrelated — poison is World 1 but Rise 4. This pins that
    # hazard directly, so a future "simplification" to one id-keyed table
    # fails here rather than silently mis-colouring every status card.
    add_executable(ailment-style-tests
        tests/ailment_style_tests.cpp
        src/core/game_tables.cpp
        src/monster/data/part_schemas_data.cpp
        src/rise/part_schemas.cpp
    )
    target_link_libraries(ailment-style-tests PRIVATE Qt6::Core)
    target_include_directories(ailment-style-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(ailment-style-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ailment-style-tests COMMAND ailment-style-tests)

    # Steam/Rise installation discovery and REFramework lifecycle tests are
    # QtCore-only and link the same production core library used by the
    # console. They never touch the real Steam or game directory.
    # steam-game-locator-tests now covers the Rise-specific wrapper too
    # (rise_install_locator.cpp), so it links game-rise in addition to the
    # kernel's generic Steam helper it tests first.
    add_executable(steam-game-locator-tests
        tests/steam_game_locator_tests.cpp
        src/rise/rise_install_locator.cpp
    )
    target_link_libraries(steam-game-locator-tests PRIVATE monster-core game-rise)
    target_include_directories(steam-game-locator-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(steam-game-locator-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME steam-game-locator-tests COMMAND steam-game-locator-tests)

    add_executable(rise-reframework-manager-tests
        tests/rise_reframework_manager_tests.cpp
        src/resources/resources.qrc
    )
    target_link_libraries(rise-reframework-manager-tests PRIVATE
        monster-core game-rise)
    target_include_directories(rise-reframework-manager-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(rise-reframework-manager-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME rise-reframework-manager-tests COMMAND rise-reframework-manager-tests)

    add_executable(reframework-fetcher-tests
        tests/reframework_fetcher_tests.cpp
        src/resources/resources.qrc
    )
    target_link_libraries(reframework-fetcher-tests PRIVATE monster-core game-rise)
    target_include_directories(reframework-fetcher-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(reframework-fetcher-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME reframework-fetcher-tests COMMAND reframework-fetcher-tests)

    # i18n locale plumbing: conf read/write (locale_conf.h), the overlay's
    # change-detection state machine (locale_sync.h) and the per-locale
    # multi-file StringTable loader. Compiles the real qrc so the shipped
    # locale files (zh-CN/ en-US/ directories) are exercised as packaged.
    # Does NOT link monster-core: it exercises the StringTable loader
    # (instance/load/tr) that string_table.cpp defines. Linking monster-core
    # would pull the entire reader chain into what is a header-only-plus-loader
    # i18n plumbing test.
    add_executable(monster-locale-tests
        tests/locale_sync_tests.cpp
        src/core/string_table.cpp
        src/core/string_table.h
        src/core/locale_conf.h
        src/core/locale_sync.h
        src/resources/resources.qrc
    )
    target_link_libraries(monster-locale-tests PRIVATE Qt6::Core)
    target_include_directories(monster-locale-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(monster-locale-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME monster-locale-tests COMMAND monster-locale-tests)

    # P0 release-path regression: the runtime data-file lookup must not
    # depend on the build machine's source tree. Every case builds its own
    # temporary tree, so this runs cleanly under a relocated release.
    add_executable(map-path-tests
        tests/map_paths_tests.cpp
    )
    target_link_libraries(map-path-tests PRIVATE Qt6::Core monster-core)
    target_include_directories(map-path-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(map-path-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME map-path-tests COMMAND map-path-tests)

    # ZoneRef is the game-agnostic zone contract that replaced a World-only
    # enum on GameSnapshot (see core/zone_id.h for why the id is an int and
    # not an enum). It is a pure header, so this target needs no library and
    # no Qt: linking monster-core here would make the test depend on the very
    # implementation it is meant to pin. What it asserts is the contract —
    # identity is (game, id), the default is unknown rather than a plausible
    # id 0, and isUnknownZone asks "was a zone read", not "does it have a
    # name" (that is hasZoneName, on the game side).
    add_executable(zone-id-tests
        tests/zone_id_tests.cpp
    )
    target_link_libraries(zone-id-tests PRIVATE Qt6::Core)
    target_include_directories(zone-id-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(zone-id-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME zone-id-tests COMMAND zone-id-tests)

    # zoneLabel() (src/ui/panel_player.cpp:124) is a per-game seam that no
    # test could reach: it sits in an anonymous namespace in a 1400-line UI
    # translation unit and returns a translated QString. This target pins the
    # dispatch instead — which vocabulary a given (game, id) consults, and that
    # an id no vocabulary names falls back by range rather than borrowing
    # another game's label. A new GameId would otherwise trip a -Wswitch here
    # that nothing would notice. The wording itself is covered by the snapshot
    # regression; this covers the decision.
    add_executable(zone-label-tests
        tests/zone_label_tests.cpp
    )
    target_link_libraries(zone-label-tests PRIVATE Qt6::Core)
    target_include_directories(zone-label-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(zone-label-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME zone-label-tests COMMAND zone-label-tests)

    # Rise damage ingestion and pet-panel behavior are independent targets so
    # failures identify the feed or rendering contract directly.
    add_executable(rise-damage-reader-tests
        tests/rise_damage_reader_tests.cpp
    )
    target_link_libraries(rise-damage-reader-tests PRIVATE
        Qt6::Core monster-core game-rise)
    target_include_directories(rise-damage-reader-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(rise-damage-reader-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME rise-damage-reader-tests COMMAND rise-damage-reader-tests)

    add_executable(rise-damage-lifecycle-tests
        tests/rise_damage_lifecycle_tests.cpp
    )
    target_link_libraries(rise-damage-lifecycle-tests PRIVATE Qt6::Core)
    target_include_directories(rise-damage-lifecycle-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(rise-damage-lifecycle-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME rise-damage-lifecycle-tests COMMAND rise-damage-lifecycle-tests)

    add_executable(rise-damage-roster-tests
        tests/rise_damage_roster_tests.cpp
    )
    target_link_libraries(rise-damage-roster-tests PRIVATE
        Qt6::Core monster-core game-rise)
    target_include_directories(rise-damage-roster-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(rise-damage-roster-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME rise-damage-roster-tests COMMAND rise-damage-roster-tests)

    add_executable(damage-party-filter-tests
        tests/damage_party_filter_tests.cpp
    )
    target_link_libraries(damage-party-filter-tests PRIVATE Qt6::Core)
    target_include_directories(damage-party-filter-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(damage-party-filter-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME damage-party-filter-tests COMMAND damage-party-filter-tests)

    add_executable(pet-damage-panel-tests
        tests/pet_damage_panel_tests.cpp
        src/ui/panel_pet_damage.cpp
    )
    # This target exercises the pure row model without constructing a
    # layer-shell window. The implementation keeps that seam explicitly
    # behind MHW_PET_DAMAGE_DATA_TEST.
    set_target_properties(pet-damage-panel-tests PROPERTIES AUTOMOC OFF)
    target_compile_definitions(pet-damage-panel-tests PRIVATE
        MHW_PET_DAMAGE_DATA_TEST
    )
    target_link_libraries(pet-damage-panel-tests PRIVATE Qt6::Core Qt6::Widgets)
    target_include_directories(pet-damage-panel-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(pet-damage-panel-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME pet-damage-panel-tests COMMAND pet-damage-panel-tests)

    add_executable(damage-view-model-tests
        tests/damage_view_model_tests.cpp
    )
    target_link_libraries(damage-view-model-tests PRIVATE Qt6::Core mhw-ui)
    target_include_directories(damage-view-model-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(damage-view-model-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME damage-view-model-tests COMMAND damage-view-model-tests)

    # Pure-logic coverage for the REFramework status-line derivation
    # (mhw::riseReframeworkStatusLines): the state machine that used to be
    # unreachable inside ControlPanel::refreshRiseReframeworkStatus(). Links
    # mhw-ui for the model plus the two data-table translation units that
    # library's other targets need (same pair as damage-view-model-tests);
    # compiles the real qrc so the console.reframework.* strings are
    # exercised as packaged and the assertions see translated text.
    add_executable(rise-reframework-status-tests
        tests/rise_reframework_status_tests.cpp
        src/resources/resources.qrc
    )
    target_link_libraries(rise-reframework-status-tests PRIVATE Qt6::Core mhw-ui)
    target_include_directories(rise-reframework-status-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(rise-reframework-status-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME rise-reframework-status-tests COMMAND rise-reframework-status-tests)

    # Window-layout persistence (mhw::ConsoleLayoutStore): the seven
    # QSettings call sites the console used to open inline. Isolated the
    # same way locale_sync_tests is — XDG_CONFIG_HOME redirected into a
    # QTemporaryDir and a (organization, application) pair that exists
    # nowhere else, so the developer's real
    # ~/.config/monster-overlay/monster-control.conf is never touched.
    # The store is QtCore-only, so this target compiles its two sources
    # directly instead of linking mhw-ui (which would drag in the whole
    # GUI layer, LayerShellQt included, for one settings wrapper).
    add_executable(console-layout-store-tests
        tests/console_layout_store_tests.cpp
        src/ui/viewmodel/console_layout_store.cpp
        src/ui/viewmodel/console_layout_store.h
    )
    target_link_libraries(console-layout-store-tests PRIVATE Qt6::Core)
    target_include_directories(console-layout-store-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(console-layout-store-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME console-layout-store-tests COMMAND console-layout-store-tests)

    # The mask half of the console's `monster-overlay.conf` persistence
    # (mhw::PanelMaskCodec): the parse and serialize logic that used to sit
    # inline in ControlPanel::loadMaskFromDisk / saveMaskToDisk. Those four
    # rows are a user contract — the overlay's locale_sync.h polls the same
    # file, users hand-edit it, an older three-row config must keep loading
    # — so the contract is pinned down here by literal assertions instead of
    # by a QA click on a widget the test cannot reach.
    #
    # Header-only and QtCore-only (QString / QByteArray / QTextStream plus
    # the pure DamageSection table in panel_sections.h), so there is no
    # second source file to compile and no reason to link mhw-ui.
    add_executable(panel-mask-codec-tests
        tests/panel_mask_codec_tests.cpp
        src/ui/viewmodel/panel_mask_codec.h
    )
    target_link_libraries(panel-mask-codec-tests PRIVATE Qt6::Core)
    target_include_directories(panel-mask-codec-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(panel-mask-codec-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME panel-mask-codec-tests COMMAND panel-mask-codec-tests)

    # v0.12 sizing: the shared grid arithmetic every split window uses
    # (src/ui/panel_sizing.h). Header-only and pure C++ — <cmath> is its
    # only include — so like the layout tests this needs no widget library
    # and no second translation unit. What it pins is the invariant that
    # was broken in the first strip: the width a window reserved and the
    # slot width it drew into were computed by two different formulas, so
    # a 91px window ended up giving each of its three pills a 23px slot
    # that could not hold a 9pt timer. gridContentWidth / gridSlotWidth are
    # exact inverses by construction, and the tests sweep the whole input
    # space to keep them that way.
    add_executable(panel-sizing-tests
        tests/panel_sizing_tests.cpp
        src/ui/panel_sizing.h
    )
    target_link_libraries(panel-sizing-tests PRIVATE Qt6::Core)
    target_include_directories(panel-sizing-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(panel-sizing-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME panel-sizing-tests COMMAND panel-sizing-tests)

    # v0.12: the three split windows' content models — buildBarsModel /
    # buildMantleModel / buildWirebugModel, each a pure GameSnapshot ->
    # row-block transformation.
    #
    # This one links mhw-ui rather than compiling the sources itself,
    # because those builders live in .cpp files next to panel classes that
    # carry Q_OBJECT: listing the .cpp here as well would run AUTOMOC over
    # the same headers twice and produce duplicate symbols. Linking keeps
    # exactly one owner per symbol.
    add_executable(player-split-strips-tests
        tests/player_split_strips_tests.cpp
    )
    target_link_libraries(player-split-strips-tests PRIVATE Qt6::Core mhw-ui)
    target_include_directories(player-split-strips-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(player-split-strips-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME player-split-strips-tests COMMAND player-split-strips-tests)

    # v0.12: the weapon identity window's content model — the
    # GameSnapshot -> {wslot, name, master rank} transformation, which is
    # what PlayerPanel used to draw inside a 49px band it could not
    # actually fit a name into.
    add_executable(player-weapon-strip-tests
        tests/player_weapon_strip_tests.cpp
    )
    target_link_libraries(player-weapon-strip-tests PRIVATE Qt6::Core mhw-ui)
    target_include_directories(player-weapon-strip-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(player-weapon-strip-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME player-weapon-strip-tests COMMAND player-weapon-strip-tests)
    # Serialised against the other Widget tests that share the real
    # ~/.config/monster-overlay/panels.ini: every Panel constructor
    # reads and writes it, so running these concurrently aborts them.
    # Offscreen too, so they never grab the user's display.
    set_tests_properties(player-weapon-strip-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        RESOURCE_LOCK "panels-ini")

    # v0.12: the shared rhythm every split window's layout is built from —
    # one margin, two gap grades, one font stack, and the promise that the
    # size a window reserves is the size it draws into. Added because the
    # windows had drifted (the status strip's margin was 6 while every
    # other window's was 11) and nothing was asserting it.
    add_executable(split-layout-consistency-tests
        tests/split_layout_consistency_tests.cpp
    )
    target_link_libraries(split-layout-consistency-tests PRIVATE Qt6::Core mhw-ui)
    target_include_directories(split-layout-consistency-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(split-layout-consistency-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME split-layout-consistency-tests COMMAND split-layout-consistency-tests)
    # Serialised against the other Widget tests that share the real
    # ~/.config/monster-overlay/panels.ini: every Panel constructor
    # reads and writes it, so running these concurrently aborts them.
    # Offscreen too, so they never grab the user's display.
    set_tests_properties(split-layout-consistency-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        RESOURCE_LOCK "panels-ini")

    # v0.13: the per-game layout namespace. What it pins is the migration's
    # four guarantees — only the current game's namespace is written, an
    # already-scoped section is never overwritten, a second run is a no-op,
    # and the handle is passed in so no test opens the developer's real
    # config.
    add_executable(panel-settings-game-scoping-tests
        tests/panel_settings_game_scoping_tests.cpp
    )
    target_link_libraries(panel-settings-game-scoping-tests PRIVATE Qt6::Core mhw-ui)
    target_include_directories(panel-settings-game-scoping-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(panel-settings-game-scoping-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME panel-settings-game-scoping-tests COMMAND panel-settings-game-scoping-tests)

    # v0.14: the layout namespace is per (game, layout mode) — four domains
    # rather than two. What that pins is the property the two-domain version
    # could not express: a user's single-mode layout and their split-mode
    # layout are DIFFERENT layouts describing the SAME window, and writing
    # one while reading the other is exactly the "the console and the
    # overlay disagree" symptom.
    #
    # The migration marker is per game, not per (game, mode) — otherwise a
    # Single-mode run migrates the whole flat file and a later Split-mode
    # run no longer recognises it as already migrated.
    add_executable(panel-settings-scope-tests
        tests/panel_settings_scope_tests.cpp
    )
    target_link_libraries(panel-settings-scope-tests PRIVATE Qt6::Core mhw-ui)
    target_include_directories(panel-settings-scope-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(panel-settings-scope-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME panel-settings-scope-tests COMMAND panel-settings-scope-tests)

    # v0.13: what each window says it can do, derived from the layout hooks
    # it actually answers. A window that claims the columns dial without
    # overriding the vocabulary behind it is a dead key, and this is the
    # test that catches the difference.
    add_executable(panel-capabilities-tests
        tests/panel_capabilities_tests.cpp
    )
    target_link_libraries(panel-capabilities-tests PRIVATE Qt6::Core mhw-ui)
    target_include_directories(panel-capabilities-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(panel-capabilities-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME panel-capabilities-tests COMMAND panel-capabilities-tests)
    # Serialised against the other Widget tests that share the real
    # ~/.config/monster-overlay/panels.ini: every Panel constructor
    # reads and writes it, so running these concurrently aborts them.
    # Offscreen too, so they never grab the user's display.
    set_tests_properties(panel-capabilities-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        RESOURCE_LOCK "panels-ini")

    # v0.12 third wave: the monster panel's blocks as their own windows.
    # What it pins is the migration's two exhaustive sweeps (63 old-schema
    # masks and 64 new-schema ones), the table's disjointness, and the
    # Single arm's byte-identity across all 64 monster masks.
    add_executable(monster-split-models-tests
        tests/monster_split_models_tests.cpp
        src/resources/resources.qrc
    )
    target_link_libraries(monster-split-models-tests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg mhw-ui)
    target_include_directories(monster-split-models-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(monster-split-models-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME monster-split-models-tests
             COMMAND monster-split-models-tests)
    set_tests_properties(monster-split-models-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # v0.12 third wave: the parts window as an OBJECT. The model test covers
    # buildMonsterPartsModel, which is inline in the header and therefore
    # emits no symbol — leaving this .cpp's 21 exported members with nothing
    # asking for them. What only an object test can pin is the display
    # filter it inherits, the columns dial's clamp, and that the height it
    # reports is the height it asks the compositor for.
    add_executable(monster-parts-window-tests
        tests/monster_parts_window_tests.cpp
    )
    target_link_libraries(monster-parts-window-tests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets mhw-ui)
    target_include_directories(monster-parts-window-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(monster-parts-window-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME monster-parts-window-tests COMMAND monster-parts-window-tests)
    set_tests_properties(monster-parts-window-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # v0.12 fourth wave: the console's split preview. What it pins is the
    # guarantee that lets the preview exist at all — the four base slots
    # are untouched in Single mode, so the canvas a user already knows is
    # the canvas they keep. The mutation that matters is dropping the
    # Single-mode clear: sub-windows surviving into Single is exactly the
    # "which mode am I looking at" confusion the split was supposed to end.
    add_executable(hud-canvas-split-tests
        tests/hud_canvas_split_tests.cpp
    )
    target_link_libraries(hud-canvas-split-tests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets mhw-ui)
    target_include_directories(hud-canvas-split-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(hud-canvas-split-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME hud-canvas-split-tests COMMAND hud-canvas-split-tests)
    set_tests_properties(hud-canvas-split-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # v0.12 third wave: the DPS panel's Share / Chart windows. Three
    # targets, because the claim has three parts: what each window draws
    # (model), that the hoisted draw functions and the panel's own inline
    # expressions produce the SAME bytes at four widths (parity), and that
    # a constructed window reserves exactly what it paints (windows).
    add_executable(damage-split-model-tests
        tests/damage_split_model_tests.cpp
    )
    target_link_libraries(damage-split-model-tests PRIVATE Qt6::Core mhw-ui)
    target_include_directories(damage-split-model-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(damage-split-model-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME damage-split-model-tests COMMAND damage-split-model-tests)

    add_executable(damage-split-parity-tests
        tests/damage_split_parity_tests.cpp
    )
    target_link_libraries(damage-split-parity-tests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg mhw-ui)
    target_include_directories(damage-split-parity-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(damage-split-parity-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME damage-split-parity-tests COMMAND damage-split-parity-tests)
    set_tests_properties(damage-split-parity-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(damage-split-windows-tests
        tests/damage_split_windows_tests.cpp
    )
    target_link_libraries(damage-split-windows-tests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg mhw-ui)
    target_include_directories(damage-split-windows-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(damage-split-windows-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME damage-split-windows-tests COMMAND damage-split-windows-tests)
    set_tests_properties(damage-split-windows-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # v0.12 fourth wave: the console's layout mode. What it pins is the
    # priority chain, which must be the SAME one the overlay uses — a
    # second chain here is a disagreement about who wins between the conf
    # file and the user's own --layout=, which is the kind of bug that
    # only shows up as "why did it not do what I typed".
    add_executable(console-layout-mode-tests
        tests/console_layout_mode_tests.cpp
        src/ui/viewmodel/console_layout_mode_codec.h
    )
    target_link_libraries(console-layout-mode-tests PRIVATE Qt6::Core)
    target_include_directories(console-layout-mode-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(console-layout-mode-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME console-layout-mode-tests COMMAND console-layout-mode-tests)

    # v0.13: the first-run cascade. Eleven sub-windows used to hard-code
    # Corner::TopLeft, and a TopLeft default is (20,20) — the same number
    # for every one of them — so a first launch stacked them all on one
    # pixel. The user's panels.ini had four windows with identical
    # ml=20 mt=20, which is the fingerprint of "never dragged".
    #
    # What this pins is that no two sub-windows of one parent share a
    # spot, that a STORED value still wins outright, and that the default
    # needs no screen size (checked on a 640x480 and a 3840x2160 virtual
    # output, since a coordinate-based default is right at one resolution
    # and wrong at every other).
    add_executable(split-default-placement-tests
        tests/split_default_placement_tests.cpp
        src/resources/resources.qrc
    )
    target_link_libraries(split-default-placement-tests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg mhw-ui)
    target_include_directories(split-default-placement-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(split-default-placement-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME split-default-placement-tests COMMAND split-default-placement-tests)
    set_tests_properties(split-default-placement-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # v0.13: no empty window in either game. The reported symptom was "both
    # Rise and World previews have a completely empty floating window", and
    # the mechanism was a short-circuit: hasContent() began `if (demoData_
    # && editMode()) return true;`, which records that the demo seed RAN
    # rather than that it produced anything. A game whose seed deliberately
    # produces nothing — buildMantleModel's Rise arm, buildWirebugModel's
    # World arm, both reading nothing on purpose — therefore mounted an
    # empty rectangle.
    #
    # This asserts the real invariant rather than the two known cases:
    # anywhere hasContent() answers true, the window must have a measurable
    # body. A window claiming content at 2x2 or 0x0 is that same bug wearing
    # a different coat, and this is what catches the next one.
    add_executable(split-no-empty-window-tests
        tests/split_no_empty_window_tests.cpp
        src/resources/resources.qrc
    )
    target_link_libraries(split-no-empty-window-tests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg mhw-ui)
    target_include_directories(split-no-empty-window-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(split-no-empty-window-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME split-no-empty-window-tests COMMAND split-no-empty-window-tests)
    set_tests_properties(split-no-empty-window-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(monster-split-grouping-tests
        tests/monster_split_grouping_tests.cpp
        src/resources/resources.qrc
    )
    target_link_libraries(monster-split-grouping-tests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg mhw-ui)
    target_include_directories(monster-split-grouping-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(monster-split-grouping-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME monster-split-grouping-tests COMMAND monster-split-grouping-tests)
    set_tests_properties(monster-split-grouping-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # v0.13: the console's split preview follows the game and the mode.
    # The root of the reported symptom: the sub-window host was guarded by
    # `if (!splitPreview_)`, true once per process, so a rail switch could
    # re-seed demo data but could never re-run a Panel constructor — and a
    # Panel reads its game-scoped QSettings group exactly once, inside that
    # constructor. Hence 13 sub-window sections under [world] and none
    # under [rise] in the user's panels.ini.
    #
    # The probe compares what the canvas PAINTS per game, so it needs no
    # new public surface on HudCanvas.
    add_executable(console-split-sync-tests
        tests/console_split_sync_tests.cpp
        src/resources/resources.qrc
    )
    target_link_libraries(console-split-sync-tests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg mhw-ui)
    target_include_directories(console-split-sync-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(console-split-sync-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME console-split-sync-tests COMMAND console-split-sync-tests)

    # v0.14: the DPS rows window, and the dispatch that decides which game a
    # sub-window seeds itself for. The second half is the load-bearing one:
    # twelve syncGameForDemo() overloads were dead code, because the host
    # holds a Panel* and an exact Panel* match always beats a Derived*
    # conversion at compile time. Every split window therefore kept its
    # member-initialised game - World for mantles, Rise for wirebugs - which
    # is why the preview showed 衣装 under Rise and 翔虫 under World, and why
    # the same symptom had been "fixed" twice already.
    #
    # §1 of this file is disabled on purpose (see the note there); §1b drives
    # the dispatch through a real console instead.
    add_executable(damage-rows-window-tests
        tests/damage_rows_window_tests.cpp
        src/resources/resources.qrc
    )
    target_link_libraries(damage-rows-window-tests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg mhw-ui)
    target_include_directories(damage-rows-window-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(damage-rows-window-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME damage-rows-window-tests COMMAND damage-rows-window-tests)
    # v0.14.1: the game SEED's own dispatch, observed through content
    # rather than through pixels.
    #
    # The console-side suites compare a Rise canvas with a World canvas,
    # which catches the namespace half of «Rise 显示衣装 / World 显示翔虫»
    # but not the seed half: two canvases differ because every window
    # re-reads [game/mode/key] and lands somewhere new, whether or not any
    # window's game_ moved. Empirical proof — gutting
    # PlayerMantlesStripPanel::setDemoGame() (making it precisely the no-op
    # the twelve dead overloads always were) left both suites green.
    #
    # Mantles are a World system and Rise wears wirebugs, so hasContent() is
    # a direct read of the window's game_, in both directions.
    add_executable(split-game-seed-dispatch-tests
        tests/split_game_seed_dispatch_tests.cpp
        src/resources/resources.qrc
    )
    target_link_libraries(split-game-seed-dispatch-tests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg mhw-ui)
    target_include_directories(split-game-seed-dispatch-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(split-game-seed-dispatch-tests PRIVATE
        -Wall -Wextra -Wpedantic)
    add_test(NAME split-game-seed-dispatch-tests
             COMMAND split-game-seed-dispatch-tests)
    # v0.14: the overlap-free wall, measured against the REAL sizes of the
    # windows it lays out rather than against its own cell constants.
    #
    # The wall's grid is a pure function of (corner, ordinal) so that its
    # answer is resolution-independent and "why is this window here" has an
    # answer. What a pure function of corners and ordinals cannot know is
    # how big the windows are -- and the cells were 260x128 while the real
    # split windows measure up to 560x183, so cells claimed rectangles other
    # windows already occupied. mousePressEvent walks the boxes in reverse,
    # so the window underneath was simply not clickable, which is the whole
    # of "I cannot drag the sub-windows past the midline".
    add_executable(sub-window-wall-tests
        tests/sub_window_wall_tests.cpp
        src/resources/resources.qrc
    )
    target_link_libraries(sub-window-wall-tests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets mhw-ui)
    target_include_directories(sub-window-wall-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(sub-window-wall-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME sub-window-wall-tests COMMAND sub-window-wall-tests)
    # v0.15: the split layout a user builds in the console is saved and then
    # kept. The two halves moved together because neither had a coverage,
    # and each is invisible on its own: the write walks the BASE panels only
    # (a construct predating the split), and the wall lays its cell over every
    # window while a comment on it promises the opposite.
    add_executable(split-console-layout-persistence-tests
        tests/split_console_layout_persistence_tests.cpp
        src/resources/resources.qrc
    )
    target_link_libraries(split-console-layout-persistence-tests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg mhw-ui)
    target_include_directories(split-console-layout-persistence-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(split-console-layout-persistence-tests PRIVATE
        -Wall -Wextra -Wpedantic)
    add_test(NAME split-console-layout-persistence-tests
             COMMAND split-console-layout-persistence-tests)
    set_tests_properties(split-console-layout-persistence-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        RESOURCE_LOCK "panels-ini")

    # v0.15: the console's per-panel screen picker persists. The picker is
    # v0.11, loadConfig()'s read of `output` is v0.8, and the write side was
    # never there — every caller passed persist=false, so the choice died
    # with the console.
    add_executable(console-output-persistence-tests
        tests/console_output_persistence_tests.cpp
        src/resources/resources.qrc
    )
    target_link_libraries(console-output-persistence-tests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg mhw-ui)
    target_include_directories(console-output-persistence-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(console-output-persistence-tests PRIVATE
        -Wall -Wextra -Wpedantic)
    add_test(NAME console-output-persistence-tests
             COMMAND console-output-persistence-tests)
    set_tests_properties(console-output-persistence-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        RESOURCE_LOCK "panels-ini")

    # A DIAGNOSTIC probe, not an assertion: the v0.15 edit-mode focus fix
    # calls requestActivate() before app.exec(), and a review measured that
    # as a no-op on a live Hyprland session while the same calls deferred
    # into the event loop worked. This reproduces panel.cpp's layer-shell
    # assembly verbatim so the question can be settled on the compositor the
    # user is actually on - a plain QMainWindow goes through the WM policy
    # path instead and answers a different question entirely.
    #
    # Excluded from ctest on purpose. It opens two real surfaces on the
    # user's desktop and asserts nothing; it exists to be run by hand and
    # read. It is not a gate.
    add_executable(focus-request-timing-probe
        tests/focus_request_timing_probe.cpp
    )
    target_link_libraries(focus-request-timing-probe PRIVATE mhw-ui)
    target_include_directories(focus-request-timing-probe PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)

    # 「分块的仍然无法越过竖中线」, measured rather than reasoned about.
    #
    # The canvas framed against DP-1 (2560 wide) while every split
    # sub-window clamped against primaryScreen() — HDMI-A-1, 1080 wide,
    # PORTRAIT. asked left=2538 came back as left=1058, which sits left of
    # 1280 and reads as "it stops at the middle". Two independent screen
    # selectors, agreeing only when every window names the same output as
    # the canvas; the eleven sub-windows all carried an EMPTY outputName
    # and empty means primary.
    #
    # The assertions are about AGREEMENT between the two selectors, not
    # about any absolute number — a machine with one screen has nothing to
    # disagree about, and the test skips the drag half there rather than
    # failing on the hardware.
    add_executable(split-output-clamp-tests
        tests/split_output_clamp_tests.cpp
        src/resources/resources.qrc
    )
    target_link_libraries(split-output-clamp-tests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg mhw-ui)
    target_include_directories(split-output-clamp-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    # qpa/qwindowsysteminterface.h — this test now installs the reporter's
    # two fake outputs itself (OUTPUT-OWNERSHIP-DECISION.md: the chain
    # assertion is meaningless on one screen). Same private-include root
    # main-output-wiring-tests takes.
    target_include_directories(split-output-clamp-tests PRIVATE
        ${_QT6_GUI_PRIVATE_INCLUDE_DIR})
    target_compile_options(split-output-clamp-tests PRIVATE
        -Wall -Wextra -Wpedantic)
    add_test(NAME split-output-clamp-tests COMMAND split-output-clamp-tests)
    set_tests_properties(split-output-clamp-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        RESOURCE_LOCK "panels-ini")

    # 「有些窗口显示属于 dp-1,但是实际启动 overlay 结果显示在 hdmi」 —
    # the split sub-window's screen inheritance.
    #
    # src/main.cpp:1051 owns the rule as a lambda inside main():
    # a split sub-window takes its parent panel's outputName when it has
    # none of its own. The eleven call sites at :1074-1084 are eleven
    # chances to forget one window, and the window that is forgotten
    # lands on the OS primary — which on the reporter's machine is a
    # PORTRAIT HDMI-A-1 while the panel was pointed at a landscape DP-1.
    # The user then has to toggle the screen by hand to get it back, and
    # layer-shell has no move gesture, so there is no other way.
    #
    # WHY THE TEST CANNOT CALL main(). The lambda is local to main() and
    # nothing in src/ exposes it; main() also builds its own QApplication
    # and enters an event loop. So this target asserts the rule through
    # the same two calls the lambda makes, against the same Panel
    # classes, in a session with the reporter's two-monitor topology
    # faked through Qt's own QPA seam (QWindowSystemInterface +
    # QPlatformScreen) — offscreen has exactly one screen, named "", and
    # Panel::setOutputName() validates against it, so "DP-1" is always
    # rejected there and every assertion would pass vacuously.
    #
    # MUTATION: delete the body of inheritParentOutput in main.cpp (or
    # comment out the eleven calls, or invert the sub's-own-key guard)
    # and tests/main_output_wiring_tests.cpp turns red — §5 compares a
    # parent's resolved screen width with its sub-window's, and those
    # agree only if both resolved the same QScreen.
    add_executable(main-output-wiring-tests
        tests/main_output_wiring_tests.cpp
        src/resources/resources.qrc
    )
    target_link_libraries(main-output-wiring-tests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg mhw-ui)
    target_include_directories(main-output-wiring-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    # The QPA screen-injection seam. Private headers, but mhw-ui already
    # compiles src/ui/screen_query.cpp against qpa/qplatformscreen.h;
    # this target needs the same path plus qpa/qwindowsysteminterface.h
    # to install the fake outputs, so it gets the same include root.
    target_include_directories(main-output-wiring-tests PRIVATE
        ${_QT6_GUI_PRIVATE_INCLUDE_DIR})
    target_compile_options(main-output-wiring-tests PRIVATE
        -Wall -Wextra -Wpedantic)
    add_test(NAME main-output-wiring-tests COMMAND main-output-wiring-tests)
    set_tests_properties(main-output-wiring-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        RESOURCE_LOCK "panels-ini")

    # 「有些窗口显示属于 dp-1,但是实际启动 overlay 结果显示在 hdmi」 — the
    # CONSOLE's half of the same report, which is a persistence bug rather
    # than an inheritance one.
    #
    # tests/main_output_wiring_tests.cpp above pins what the OVERLAY does
    # with a sub-window that has no output of its own. This target pins
    # what the CONSOLE writes before the overlay ever starts: the user
    # picks DP-1 in the SCREEN dropdown, and the question is which group
    # of panels.ini the pick lands in. The reporter's own file answers it
    # — [world/single/player] held the DP-1 while [world/split/player] was
    # empty, on a machine whose conf said layout=split — and the overlay
    # reads the split group, so the promise the dropdown made never
    # reached the child and it mounted on the primary HDMI.
    #
    # WHY THE ASSERTIONS SPELL THE GROUP OUT. A test that reads whichever
    # (game, mode) scope is CURRENT answers "the value was written" no
    # matter which group it landed in, which is exactly the confusion that
    # produced the bug. Every read here names the full group through
    # mhw::panelSettingsScopeFor(), and the single-mode group is asserted
    # to have NOT gained the pick — so a "fix" that writes to both scopes
    # fails instead of passing.
    #
    # THE SAME QPA SEAM as main-output-wiring-tests, for the same reason:
    # offscreen has exactly one screen, named "", and
    # Panel::setOutputName() validates against QGuiApplication::screens(),
    # so "DP-1" is always rejected there and every assertion would pass
    # vacuously.
    #
    # MUTATION: delete the two mhw::setPanelSettings*() calls at the top of
    # saveBeforeLaunch() (or the same pair in closeEvent(), or the
    # blockSignals guard in syncOutputCombo()) and this target turns red —
    # §4/§5 read [world/split/player]/outputName straight off disk and
    # compare it to the pick that was made.
    add_executable(console-scope-persistence-tests
        tests/console_scope_persistence_tests.cpp
        src/resources/resources.qrc
    )
    target_link_libraries(console-scope-persistence-tests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg mhw-ui)
    target_include_directories(console-scope-persistence-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    # The QPA screen-injection seam, same as main-output-wiring-tests.
    target_include_directories(console-scope-persistence-tests PRIVATE
        ${_QT6_GUI_PRIVATE_INCLUDE_DIR})
    target_compile_options(console-scope-persistence-tests PRIVATE
        -Wall -Wextra -Wpedantic)
    add_test(NAME console-scope-persistence-tests
             COMMAND console-scope-persistence-tests)
    set_tests_properties(console-scope-persistence-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        RESOURCE_LOCK "panels-ini")

    # screen_query's ARITHMETIC, and the two rules the defect violated.
    #
    # A rotated output reports a 1920x1080 panel mode and a 1080x1920
    # geometry; detect() divided one by the other and called the result a
    # device pixel ratio (1.7778). It survived because the check was a
    # RANGE - `dpr > 0.95 && dpr < 3.05` - and 1.7778 is a reasonable-looking
    # number. The rule the code should have used is a MEMBERSHIP test over
    # six discrete values, and the rule for pairing a probe's answer with
    # Qt's is that they must describe the same screen shape.
    #
    # This target exists for the census gate as much as for the assertions:
    # screen_query.cpp had no owner and only survived by being referenced
    # from a TU that was itself unreferenced. Both rules are pure functions,
    # so they are testable without a compositor - the subprocess probes need
    # a live session, and the DEFECT was in what the code did with the
    # number after the probe returned.
    #
    # MUTATION: replace isLegalDpr()'s membership test with the old range
    # test and check 2 turns red.
    add_executable(screen-query-tests
        tests/screen_query_tests.cpp
        src/resources/resources.qrc
    )
    target_link_libraries(screen-query-tests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg mhw-ui)
    target_include_directories(screen-query-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(screen-query-tests PRIVATE
        -Wall -Wextra -Wpedantic)
    add_test(NAME screen-query-tests COMMAND screen-query-tests)
    set_tests_properties(screen-query-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    set_tests_properties(sub-window-wall-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    set_tests_properties(split-game-seed-dispatch-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        RESOURCE_LOCK "panels-ini")

    set_tests_properties(damage-rows-window-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        RESOURCE_LOCK "panels-ini")

    set_tests_properties(console-split-sync-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        RESOURCE_LOCK "panels-ini")

    # v0.12: the 「任务/进程」 window's content model — what the snapshot's
    # quest block and connection block resolve to, and which half of them
    # is allowed to be empty. The window exists to give the diagnostic
    # line its own 11px pitch, so "an attached reader with no quest still
    # counts as content" is the load-bearing claim, not a detail.
    add_executable(player-info-strip-tests
        tests/player_info_strip_tests.cpp
    )
    target_link_libraries(player-info-strip-tests PRIVATE Qt6::Core mhw-ui)
    target_include_directories(player-info-strip-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(player-info-strip-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME player-info-strip-tests COMMAND player-info-strip-tests)
    # Serialised against the other Widget tests that share the real
    # ~/.config/monster-overlay/panels.ini: every Panel constructor
    # reads and writes it, so running these concurrently aborts them.
    # Offscreen too, so they never grab the user's display.
    set_tests_properties(player-info-strip-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        RESOURCE_LOCK "panels-ini")


    # v0.12: the startup mask priority chain (explicit --mask-* CLI value >
    # the console's conf row > everything visible). Both headers are
    # header-only and QtCore-only, so there is still no second source file
    # to compile and no reason to link mhw-ui.
    add_executable(mask-fallback-tests
        tests/mask_fallback_tests.cpp
        src/ui/viewmodel/panel_mask_fallback.h
        src/ui/viewmodel/panel_mask_codec.h
    )
    target_link_libraries(mask-fallback-tests PRIVATE Qt6::Core)
    target_include_directories(mask-fallback-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(mask-fallback-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME mask-fallback-tests COMMAND mask-fallback-tests)

    # v0.12: the dual-mode window topology — mhw::resolveStartupLayoutMode
    # (the cli > compat flag > conf > default chain) and the mask algebra
    # it feeds (playerPanelMaskFor / playerSubWindowMask / the
    # kPlayerSplitTargets table), all in ui/panel_sections.h.
    #
    # Header-only and QtCore-only like panel-mask-codec-tests and
    # mask-fallback-tests: every function under test is inline constexpr
    # over uint32_t and QString, so there is no second source file to
    # compile and no reason to link mhw-ui. That is the point — the
    # pixel-identity promise behind Single mode, and the "no block is
    # drawn by two windows" promise behind Split mode, are both pure
    # assertions about a mask, and should be checkable without a
    # compositor, a QPainter, or a running game.
    #
    # The exhaustive arm walks all 256 masks PlayerSection can express in
    # both modes, which is the whole space of user toggle combinations.
    add_executable(player-layout-tests
        tests/player_layout_tests.cpp
        src/ui/panel_sections.h
    )
    target_link_libraries(player-layout-tests PRIVATE Qt6::Core)
    target_include_directories(player-layout-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(player-layout-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME player-layout-tests COMMAND player-layout-tests)

    # v0.12: the split strip's pure model — mhw::buildStatusStripModel
    # (GameSnapshot -> debuff / buff pill blocks) and mhw::statusStripRows.
    #
    # This target links mhw-ui rather than compiling panel_player_split.cpp
    # itself, for two reasons. Compiling it here would mean a second copy of
    # every symbol it defines (the class has Q_OBJECT, so AUTOMOC would run
    # twice on the same header); and the pet-damage precedent's
    # MHW_*_DATA_TEST guard exists because that file's pure model is tangled
    # with its paint path. buildStatusStripModel is already cleanly
    # separable — the header's inline half plus this target covers the seam,
    # and linking keeps one owner per symbol.
    add_executable(player-status-strip-tests
        tests/player_status_strip_tests.cpp
    )
    target_link_libraries(player-status-strip-tests PRIVATE Qt6::Core mhw-ui)
    target_include_directories(player-status-strip-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(player-status-strip-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME player-status-strip-tests COMMAND player-status-strip-tests)

    # PlayerPanel's identity resolution
    # (mhw::PlayerViewModel::resolveIdentity): the three blocks that used to
    # sit inline in PlayerPanel::update() — the mirroring of (masterRank, name,
    # weaponId, partyCount) from the player struct, the party-override loop
    # that prefers the local member's fields, and the !valid fallback that
    # resets weaponId to -1. A decision this visible at every poll had zero
    # automated coverage because it lived in a 1541-line file that drags in
    # QPainter and QFontMetrics.
    #
    # Like console-layout-store-tests this compiles the ViewModel's own two
    # sources directly instead of linking mhw-ui: the class is QtCore-only
    # (QString) plus the pure data headers that define GameSnapshot, so
    # linking the GUI sink for it would buy nothing but compile time.
    add_executable(player-view-model-tests
        tests/player_view_model_tests.cpp
        src/ui/viewmodel/player_view_model.cpp
        src/ui/viewmodel/player_view_model.h
    )
    target_link_libraries(player-view-model-tests PRIVATE Qt6::Core)
    target_include_directories(player-view-model-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(player-view-model-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME player-view-model-tests COMMAND player-view-model-tests)

    # MonsterPanel's .pc card builder (mhw::MonsterPartListBuilder) — the
    # 337-line ViewModel S3-TEST-COVERAGE.md §4 B1 measured at ZERO ctest
    # coverage: "改了 sanitizePartValue/sanitizeQ10/partDisplayLayers/
    # MonsterPartListBuilder::build 任何规则，42 个 case 全绿". It used to
    # live in MonsterPanel's private section, so its whole contract — the
    # Per-PartType tag/layer dispatch, the HunterPie Row 3 value flip, the
    # v0.10.8 World-multiplayer gauge suppression, the 2026-09-22 NaN crash
    # fix and the 15 s PartAutoHide table with its three reader index scales
    # — was reachable only through a paint.
    #
    # Compiles the ViewModel's own two sources directly instead of linking
    # mhw-ui: the class is QtCore-only (QString / QVector) plus the pure
    # monster data headers and rise/mhr_part_names.h. The qrc is compiled in
    # so the localized card fields (部位 N, 斩, 破) are asserted against the
    # text that actually ships.
    add_executable(monster-view-model-tests
        tests/monster_view_model_tests.cpp
        src/ui/viewmodel/monster_view_model.cpp
        src/ui/viewmodel/monster_view_model.h
        src/resources/resources.qrc
    )
    target_link_libraries(monster-view-model-tests PRIVATE
        Qt6::Core monster-core game-rise game-world)
    target_include_directories(monster-view-model-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(monster-view-model-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME monster-view-model-tests COMMAND monster-view-model-tests)

    # The console's three localized text dispatchers
    # (mhw::consoleText / gameName / sectionLabel) plus the three formatters
    # (mhw::percentage / groupNumber / sizeMultiplier). Both TU sit on the
    # 裸奔 list (S3 §6.3) even though they are the CHEAPEST ones to close:
    # pure functions with no widget and no process.
    #
    # console_text_helpers.cpp carries the whole console's i18n entry point,
    # so the "no fallback in code" rule (docs/I18N.md) is pinned here: a
    # missing key must return the KEY, not a plausible-looking string, or the
    # next typo ships to users instead of being seen. sectionLabel() is the
    # four-way (panel, index) dispatch whose wrong branch would list one
    # panel's section names under another panel's switches — valid strings in
    # the wrong place, which no i18n gate and no pixel baseline can catch.
    # formatters.cpp owns the NaN/Inf fold that keeps a dead pointer from
    # reaching Qt's qRound() and aborting the overlay inside a paint.
    #
    # Both compile their own sources directly: console_text_helpers.cpp
    # needs only QtCore + core/string_table.h + the pure panel_sections.h
    # table, and formatters.cpp needs only QLocale.
    add_executable(console-text-helpers-tests
        tests/console_text_helpers_tests.cpp
        src/ui/viewmodel/console_text_helpers.cpp
        src/ui/viewmodel/console_text_helpers.h
        src/ui/formatters.cpp
        src/ui/formatters.h
        src/resources/resources.qrc
    )
    target_link_libraries(console-text-helpers-tests PRIVATE Qt6::Core monster-core)
    target_include_directories(console-text-helpers-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(console-text-helpers-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME console-text-helpers-tests COMMAND console-text-helpers-tests)

    # v1.0.0 O1: the window model's own TU, and the zero-reference-gate's
    # "write the test" answer rather than an EXEMPT row or a snapshot bump.
    # src/ui/console_fidelity_windows.cpp is the port's new translation unit
    # (the chain resolver, the display label and the winVisible switch), and
    # it was reachable from NO ctest target — the gate's own failure message
    # named it by path. It is compiled DIRECTLY here, not through mhw-ui, so
    # the census records `compiled_by` and not merely `refs`: the difference
    # the gate's doc block calls out between "compiled and called" and
    # "compiled and ignored".
    #
    # Needs only QtCore + QtGui + QtWidgets headers: console_fidelity_windows
    # .cpp includes ui/panel.h (QMainWindow) but calls nothing out of the
    # class, and the whole module's only I/O is the QSettings handle it
    # builds itself. The test redirects that handle with QSettings::setPath
    # into a QTemporaryDir under TMPDIR before anything opens panels.ini, so
    # it cannot read or write the user's real file.
    add_executable(console-fidelity-windows-tests
        tests/console_fidelity_windows_tests.cpp
        src/ui/console_fidelity_windows.cpp
        src/ui/console_fidelity_windows.h
        src/resources/resources.qrc
    )
    target_link_libraries(console-fidelity-windows-tests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets)
    target_include_directories(console-fidelity-windows-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(console-fidelity-windows-tests PRIVATE
        -Wall -Wextra -Wpedantic)
    add_test(NAME console-fidelity-windows-tests
             COMMAND console-fidelity-windows-tests)
    set_tests_properties(console-fidelity-windows-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # v1.0.0 (O1): THE CONSOLE'S MAIN-WINDOW STRUCTURE — the visible
    # inspector (A), the "no selection until the user picks one" rule (B),
    # the real per-section counts and their switches (C) and the stage's
    # shell head (D). It builds the whole GUI shell (mhw-ui + the two game
    # modules, like monster-control-rise-rf-smoke) because every rule is
    # about widgets the user can see, and it redirects XDG_CONFIG_HOME into
    # a QTemporaryDir so panels.ini is never touched.
    add_executable(console-inspector-fidelity-tests
        tests/console_inspector_fidelity_tests.cpp
        src/resources/resources.qrc
        assets/icons.qrc
    )
    target_link_libraries(console-inspector-fidelity-tests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
        LayerShellQt::Interface
        monster-core
        game-rise
        game-world
        mhw-ui
    )
    target_include_directories(console-inspector-fidelity-tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(console-inspector-fidelity-tests PRIVATE
        -Wall -Wextra -Wpedantic)
    add_test(NAME console-inspector-fidelity-tests
             COMMAND console-inspector-fidelity-tests)
    set_tests_properties(console-inspector-fidelity-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # v1.0.0 integration (stages A + the O2 review items): THE WIRING.
    # Console-fidelity mode applied to the shell's own canvas, the
    # `winVisible` row pushed into the canvas AND honoured by the process
    # that mounts the surface (Panel::setVisible) across all four
    # settings domains, the #window-count predicate, and the view/display
    # state contract read AFTER real QMouseEvent deliveries (a board click
    # keeps the overview open and the viewer put; a rail click is the only
    # thing that moves the view). Builds the whole GUI shell like
    # console-inspector-fidelity-tests, and redirects XDG_CONFIG_HOME into
    # a QTemporaryDir BEFORE QApplication so the hide/show round trip it
    # performs on purpose never touches the developer's panels.ini.
    add_executable(console-integration-seams-tests
        tests/console_integration_seams_tests.cpp
        src/resources/resources.qrc
        assets/icons.qrc
    )
    target_link_libraries(console-integration-seams-tests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
        LayerShellQt::Interface
        monster-core
        game-rise
        game-world
        mhw-ui
    )
    target_include_directories(console-integration-seams-tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(console-integration-seams-tests PRIVATE
        -Wall -Wextra -Wpedantic)
    add_test(NAME console-integration-seams-tests
             COMMAND console-integration-seams-tests)
    set_tests_properties(console-integration-seams-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # ── v1.0.0 fidelity (root scroll): THE PAGE SCROLLS, THE BOARD DOES
    # NOT SHRINK — and this is the test of that mechanism itself.
    #
    # The frozen 27-file candidate this round builds on already SHIPS
    # src/ui/console_root_scroll.{h,cpp} (the shell's root QScrollArea,
    # whose minimum-height contract is what lets the page grow past the
    # window), but nothing exercised it: no test target compiled the TU,
    # and the zero-reference census did not list it — `zero-reference-
    # gate` failed on exactly that one entry. So the test is NEW, and it
    # compiles the source directly (NOT through mhw-ui): the four
    # assertions are about a QScrollArea and a layout, so pulling in the
    # whole console would only add ways for the test to fail for reasons
    # that are not the mechanism.
    #
    # What it pins, each a measured behaviour of the class and none of
    # them a number copied out of the prototype: the content's minimum
    # height is its own layout hint (so a taller page scrolls instead of
    # being squeezed); the content width tracks the VIEWPORT, scrollbar
    # included; a page shorter than the viewport is not padded up; and
    # the state recovers in both directions — page shrinks, window grows.
    add_executable(console-root-scroll-tests
        tests/console_root_scroll_tests.cpp
        src/ui/console_root_scroll.cpp
    )
    target_link_libraries(console-root-scroll-tests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets)
    target_include_directories(console-root-scroll-tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(console-root-scroll-tests PRIVATE
        -Wall -Wextra -Wpedantic)
    add_test(NAME console-root-scroll-tests
             COMMAND console-root-scroll-tests)
    set_tests_properties(console-root-scroll-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # ─── OUTPUT-OWNERSHIP-FIX: the viewer is presentation only ───────────
    #
    # reference.html:421 resolves a window's display as
    # `own → panels[win.panel].output → primary`; :513's #display-select
    # only writes `state.display`. The Qt console broke that in four
    # places (see OUTPUT-OWNERSHIP-REVIEW.md): the SCREEN dropdown pushed
    # the VIEWER into every split window's clamp, the stage chips went
    # stale on a switch, the panel view painted every panel's boxes while
    # the chip counted one display's worth, and the overview painted a
    # base panel the window model has no row for.
    #
    # NONE of that is observable on one screen — "the display I am
    # looking at" and "the display this window lives on" are the same
    # string by accident — so this target injects the reporter's own
    # two-monitor topology through Qt's QPA seam (DP-1 2560x1440 plus a
    # PORTRAIT HDMI-A-1 1080x1920 as primary), exactly as
    # main-output-wiring-tests and console-scope-persistence-tests do,
    # and drives the REAL widgets: the screenCombo's currentIndexChanged,
    # the sidebar's nav cards, 400 real QKeyEvents and the console's own
    # saveAllSplitAndBaseWindows().
    #
    # HOME and XDG_CONFIG_HOME point at a QTemporaryDir set before
    # QApplication exists, so the developer's panels.ini is never opened.
    add_executable(console-output-ownership-tests
        tests/console_output_ownership_tests.cpp
        src/resources/resources.qrc
        assets/icons.qrc
    )
    target_link_libraries(console-output-ownership-tests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
        LayerShellQt::Interface
        monster-core
        game-rise
        game-world
        mhw-ui
    )
    target_include_directories(console-output-ownership-tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    # qpa/qwindowsysteminterface.h — the screen-injection seam. Private
    # headers, same include root main-output-wiring-tests takes.
    target_include_directories(console-output-ownership-tests PRIVATE
        ${_QT6_GUI_PRIVATE_INCLUDE_DIR})
    target_compile_options(console-output-ownership-tests PRIVATE
        -Wall -Wextra -Wpedantic)
    add_test(NAME console-output-ownership-tests
             COMMAND console-output-ownership-tests)
    set_tests_properties(console-output-ownership-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        RESOURCE_LOCK "panels-ini")

    # The player panel's sharpness gauge
    # (drawPlayerSharpnessBar / playerSharpnessBarTotalWidth) — the third
    # 裸奔 TU of S3 §6.3. The block moved 1:1 out of panel_player.cpp, so
    # its PIXELS are unchanged; what nothing asserted is the contract the
    # caller drives: the exported total width (the panel trims its MR/name
    # rect by exactly this number) and the "no shrink" return value for
    # every input that must not draw (ranged weapon / broken / Invalid /
    # all-zero / non-increasing thresholds). Each guard is a real game state
    # and each hides or overlaps the row in a different way.
    #
    # Compiles the draw source directly (NOT through mhw-ui): it is a free
    # function over a QPainter, not a QWidget, so it needs only the two Qt
    # modules it names plus the reader header that defines SharpnessSnapshot
    # and riseSharpnessCurrentSegment(). Rendering into an offscreen QImage
    # lets the assertions read real painted pixels (the "never stretch" rule
    # is checked by counting opaque ones), which is why the target also
    # carries QT_QPA_PLATFORM=offscreen.
    add_executable(player-sharpness-tests
        tests/player_sharpness_tests.cpp
        src/ui/panel_player_sharpness.cpp
        src/ui/panel_player_sharpness.h
    )
    target_link_libraries(player-sharpness-tests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets monster-core game-rise game-world)
    target_include_directories(player-sharpness-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(player-sharpness-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME player-sharpness-tests COMMAND player-sharpness-tests)
    set_tests_properties(player-sharpness-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # Source-level CLI/build contract: catches an option that is declared but
    # not registered, or a fourth panel omitted from the product target.
    add_executable(rise-damage-wiring-tests
        tests/rise_damage_wiring_tests.cpp
    )
    target_compile_definitions(rise-damage-wiring-tests PRIVATE
        MONSTER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_compile_options(rise-damage-wiring-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME rise-damage-wiring-tests COMMAND rise-damage-wiring-tests)

    # An unhandled flag used to fall through to the GUI and never exit;
    # --version/--help must return. TIMEOUT turns such a regression into a
    # 10 s failure instead of a hung suite.
    add_test(NAME monster-control-version COMMAND monster-control --version)
    set_tests_properties(monster-control-version PROPERTIES
        TIMEOUT 10
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # monster-doctor must run offline (no game, no display) and exit 0.
    add_test(NAME monster-doctor-smoke
             COMMAND monster-doctor --no-overlay-run --out
                     ${CMAKE_CURRENT_BINARY_DIR}/doctor-smoke.txt)

    # Pure Rise stage-name regression: no game process is required.
    add_executable(rise-zone-mapping-tests
        tests/rise_zone_mapping_tests.cpp
        src/resources/resources.qrc
    )
    # game-rise, not game-world: the vocabulary under test is Rise's
    # (src/rise/rise_zones.h). It was game-world when Rise zones were
    # enumerators in world_types.cpp.
    target_link_libraries(rise-zone-mapping-tests PRIVATE monster-core game-rise)
    target_include_directories(rise-zone-mapping-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(rise-zone-mapping-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME rise-zone-mapping-tests COMMAND rise-zone-mapping-tests)

    # Direct tests for MhrReader's zero-coverage memory readers (readSharpness,
    # readQuest - plus the map contracts they rest on). Until now the ONLY
    # validation tool was rise-probe, which is deliberately EXCLUDE_FROM_ALL
    # and therefore outside ctest, while these six readSharpness / readQuest /
    # readPlayer / readParty / readMonsters / poll bodies were measured at 0%
    # coverage. That is exactly why splitting src/rise/mhr_reader.cpp into a
    # translation unit per function could not be verified by any gate, so this
    # suite is the precondition for that split, not a nice-to-have.
    #
    # No game process is required. The target compiles with -fno-access-control
    # so the TEST translation unit can name MhrReader's private members; the
    # library itself is compiled normally and its ABI is untouched. A 272 MiB
    # PROT_NONE reservation (RW for the image, PROT_NONE guard page after it)
    # plays the part of the game's image, driven by the SHIPPED
    # data/MonsterHunterRise.16.0.2.0.map offsets, so every read goes through
    # the same process_vm_readv path as production.
    add_executable(mhr-reader-direct-tests
        tests/mhr_reader_direct_tests.cpp
        src/resources/resources.qrc
    )
    target_link_libraries(mhr-reader-direct-tests PRIVATE
        Qt6::Core monster-core game-rise game-world)
    target_include_directories(mhr-reader-direct-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(mhr-reader-direct-tests PRIVATE
        MONSTER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_compile_options(mhr-reader-direct-tests PRIVATE
        -Wall -Wextra -Wpedantic -fno-access-control
    )
    add_test(NAME mhr-reader-direct-tests COMMAND mhr-reader-direct-tests)
    # Direct tests for MhwReader's three zero-coverage World monster readers
    # (readMonsterAilments / applyTenderizesToParts / readMonsters). Same
    # mechanism as the Rise suite above — a fake image mapped over the reader's
    # own AddressMap plus -fno-access-control on the TEST TU only — so the
    # World monster path finally has the CI coverage the Rise side already had.
    #
    # AddressMap is implemented in src/core/process_memory.cpp, alongside the
    # header that declares it, so this target links it through monster-core.
    # game-world is still linked for the monster reader bodies below.
    #
    # The 0x10000 scratch stride and the StringTable locale save/restore are
    # World-specific: the Monster struct spans ~0x1C500, and the reader picks
    # its name columns from the active locale, so the test has to put it back.
    #
    # World's readParty is deliberately NOT exercised here: it fills a 4-slot
    # roster by name and never tags a member's PartyMemberKind (v0.10.8 moved
    # isMultiplayer to the session structure), so there is no business rule on
    # the roster to assert.
    #
    # src/world/monster_reader.cpp is listed EXPLICITLY, not pulled in through
    # the game-world library, so `-fno-access-control` also relaxes it — the
    # test calls MhwReader::readMonsters / readMonsterAilments /
    # applyTenderizesToParts, which are private members of that translation
    # unit. That is the same difference between "compiled by a test target" and
    # "only referenced by one" the census records (see monster-locale-tests /
    # rise-monster-metadata-tests above, which list their TUs for the same
    # reason). The library still carries its own normally-compiled copy for the
    # real overlay binary; the object this target builds is the one it links.
    add_executable(game-world-direct-tests
        tests/mhrw_reader_direct_tests.cpp
        src/world/monster_reader.cpp
        src/resources/resources.qrc
    )
    target_link_libraries(game-world-direct-tests PRIVATE
        Qt6::Core monster-core game-world)
    target_include_directories(game-world-direct-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(game-world-direct-tests PRIVATE
        MONSTER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_compile_options(game-world-direct-tests PRIVATE
        -Wall -Wextra -Wpedantic -fno-access-control
    )
    add_test(NAME game-world-direct-tests COMMAND game-world-direct-tests)

    # v0.8.4-r18 player-abnormalities: generated Rise consumable/debuff schema
    # table + the pure evaluation core (flag gates, WithValue sub-ids, the
    # raw/60 conversion, MaxTimer inversion). No game process is required.
    add_executable(rise-abnormality-tests
        tests/rise_abnormality_tests.cpp
        src/resources/resources.qrc
    )
    target_link_libraries(rise-abnormality-tests PRIVATE
        Qt6::Core monster-core game-rise)
    target_include_directories(rise-abnormality-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(rise-abnormality-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME rise-abnormality-tests COMMAND rise-abnormality-tests)
endif()

# ---------------------------------------------------------------------------
# Rise raw-memory probe.
#
# Read-only diagnostic binary that dumps the raw values behind every Rise
# section of the overlay (stage / hud / petalace / player / quest / monsters /
# wirebugs) plus the reader's own interpretation, so a live in-game session can
# be compared against what the process actually contains (zone name mismatch,
# missing hub HP/ST, stamina > 3000, empty monster panel).
#
# Deliberately built EXPLICITLY (EXCLUDE_FROM_ALL) and NOT registered as a
# CTest: it is only meaningful with a running game, so neither the default
# `cmake --build .` nor `ctest` changes behaviour. Build it with:
#
#   cmake --build <builddir> --target rise-probe
#   <builddir>/rise-probe [--map <path>]
# ---------------------------------------------------------------------------
# monster-doctor: one-shot, read-only diagnostic bundle that ships with the
# release so a user can produce a complete report in one command. It links the
# product's own resolver/reader, so its verdict cannot disagree with the
# overlay's. Default target on purpose: the packaging step must find it built.
add_executable(monster-doctor
    tests/monster_doctor.cpp
)
target_link_libraries(monster-doctor PRIVATE
    Qt6::Core monster-core game-rise game-world)
target_include_directories(monster-doctor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(monster-doctor PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(monster-doctor PRIVATE
    MONSTER_VERSION="${PROJECT_VERSION}"
)

add_executable(rise-probe EXCLUDE_FROM_ALL
    tests/rise_probe.cpp
)
target_link_libraries(rise-probe PRIVATE
    Qt6::Core monster-core game-rise game-world)
target_include_directories(rise-probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(rise-probe PRIVATE -Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# Build-everything gate.
#
# The diagnostic targets in this file are EXCLUDE_FROM_ALL because they are
# only MEANINGFUL with a running game — running them in CI would hang or fail.
# That reasoning is sound for RUNNING them and wrong for COMPILING them, and
# the difference cost real time: rise-probe was broken by the ZoneRef change
# (S1) and stayed broken, because nothing ever compiled it. A default `ninja`
# and a green `ctest` both said "fine" while the target could not build.
#
# This test compiles each one and runs none of them, so it catches the class
# of breakage a default build structurally cannot see.
#
# Keep MHW_DIAGNOSTIC_TARGETS in sync with the real EXCLUDE_FROM_ALL list:
# a new one must be added here too.
set(MHW_DIAGNOSTIC_TARGETS
    snap-world-mp
    snap-gauge-fill
    snap-empty-state
    snap-player-offline
    rise-probe
)

# ---------------------------------------------------------------------------
# Locate bash *before* the two gates below that need it. find_program writes
# to the cache, so calling it on first use would evaluate false on a fresh
# configure — the gate would register nothing and the build would carry on
# silently green. Declaring it above the first use keeps the gate present
# from the very first configure of a clean build tree.
# ---------------------------------------------------------------------------
find_program(BASH_EXECUTABLE bash)

if(BASH_EXECUTABLE)
    add_test(NAME diagnostic-targets-build
        COMMAND ${BASH_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/assert_diagnostic_targets.sh
                ${CMAKE_CURRENT_BINARY_DIR}
    )
endif()

# ---------------------------------------------------------------------------
# i18n gates as real tests.
#
# The three scripts used to be a manual step, so a clean ctest run said
# nothing about the translation tables: a zh-only key or a tr() typo both
# sailed through. They now run under ctest, against the source tree (the
# tests are static analysis, no build artefacts needed).
#
# `unused` deliberately stays non-strict by default: the shipped data tables
# keep retired keys on purpose, and --strict has been red for so long that
# nobody would look at it. Adding a key nobody references is a warning.
# ---------------------------------------------------------------------------
find_program(PYTHON3_EXECUTABLE NAMES python3 REQUIRED)

# A cached interpreter path can go stale: a virtualenv recreated, a
# Hermes-managed venv pruned. CMake would keep the cached path, and every
# python-based test would then report "Not Run" — which ctest counts as a
# FAILURE — while the identical command run by hand passes. Re-searching
# here means a broken interpreter surfaces at configure time, where it
# is actionable, instead of silently at test time.
if(NOT EXISTS "${PYTHON3_EXECUTABLE}")
    message(STATUS "cached PYTHON3_EXECUTABLE no longer exists; re-searching")
    unset(PYTHON3_EXECUTABLE CACHE)
    find_program(PYTHON3_EXECUTABLE NAMES python3 REQUIRED)
endif()

set(MHW_I18N_GATES check parity unused)

foreach(gate IN LISTS MHW_I18N_GATES)
    add_test(
        NAME i18n-gate-${gate}
        COMMAND ${PYTHON3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/i18n_gate.py
                ${gate} -q
    )
    # The scripts locate the i18n tree relative to the CURRENT directory,
    # and ctest runs from the build tree by default — where no scripts/ or
    # src/resources/ exists, so every gate reported "Not Run" while the
    # same command run by hand from the repo root passed. Pinning the
    # working directory makes the ctest result mean what it says.
    set_tests_properties(i18n-gate-${gate} PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endforeach()

# ---------------------------------------------------------------------------
# Zero-reference TU gate.
#
# S3 measured, with `nm` symbol reachability out of the built object files,
# how many src translation units no ctest target's code ever touches -- the
# number the CMake text and the binary symbol table both hide. This gate re-runs
# that measurement, so the next TU carved out of a fat file cannot be added
# without anybody deciding (in `owner` or in the exemption table) who tests it.
#
# It is a SNAPSHOT gate: it compares the measured src TU set against the
# recorded one, so it needs no build. What it does *not* do is decide whether a
# ZERO TU is acceptable -- that stays a human call, which is why the exemption
# table lives in tests/ and every entry carries the reason next to it.
# ---------------------------------------------------------------------------
# Tests a committed snapshot of the src translation-unit census. The census is
# refreshed with `python3 scripts/zero_reference_gate.py --update-snapshot`
# (or `--report` to see the live numbers without touching anything).
# Registering only the snapshot comparison, never the census, is deliberate:
# a fresh checkout has no build/ for the census to read object files from.
add_test(NAME zero-reference-gate
    COMMAND ${PYTHON3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/zero_reference_gate.py
            --repo ${CMAKE_CURRENT_SOURCE_DIR}
            --snapshot ${CMAKE_CURRENT_SOURCE_DIR}/tests/tu_census.json
            --quiet
)

# ---------------------------------------------------------------------------
# Library-boundary gate. Reads the built archives with nm and fails if a
# library has picked up another layer's symbols: the kernel carrying a game's
# reader or data table, a game library referencing the other game, or the UI
# referencing a reader.
#
# This is the only check that can catch a boundary violation. The compiler
# cannot: cross-library references are legal, and the old single-library layout
# compiled clean while 60 of monster-core's 140 exported symbols belonged to
# two concrete games. `-Wl,--no-undefined` would only catch a MISSING symbol,
# not a misplaced one, and would also fail the legitimate test targets that
# deliberately link a single .cpp.
#
# Runs against build/, so a fresh checkout without a build reports "skipped"
# rather than failing — the same tradeoff zero-reference-gate makes.
if(BASH_EXECUTABLE)
    add_test(NAME lib-purity-gate
        COMMAND ${BASH_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/assert_lib_purity.sh
                ${CMAKE_CURRENT_BINARY_DIR}
    )
endif()

# ---------------------------------------------------------------------------
# The player-info sub-window's three states, and the enum order that keeps
# them distinct.
#
# The v0.12 split reduced PlayerPanel's conn line to a binary and lost the
# middle state (attached-but-nothing-resolved) — the one the CachyOS
# reports were filed under. It was restored in
# src/ui/panel_player_split_info.cpp, and restoring it added a
# `ReaderStatus` enumerator to `InfoLine` at index 2 with `Meta` still
# last. That ordering is LOAD-BEARING: streamHeight() and paintPanel()
# both walk the enum in numeric order, so a `Meta` that stops being last
# silently reorders the paint and the reservation, compiles clean, and
# passes every other assertion in the suite.
#
# The pitches are NOT constants — every height is
# ceil(QFontMetrics::height()) for the font that line draws, so the
# numbers are host-derived and differ by 1px between the measuring host
# and the shipped build. This target therefore asserts RELATIONSHIPS
# (monotonicity, differences that are exact multiples of the shared gap,
# the reason line collapsing when it has nothing to say) and never a
# pixel height, using only the two exported layout constants
# kSplitMargin and kSplitGapWithin.
#
# The three states are driven through the PUBLIC contract:
# update(snapshot) + contentHeight()/contentWidth()/hasVisibleContent()
# plus an offscreen render. `InfoLine` itself lives in the .cpp's
# anonymous namespace and is deliberately not exported for this test —
# the states must be observable from outside, which is also what makes
# the ordering assertion mean something.
#
# The qrc is compiled in so the labels resolve through the real string
# table rather than the key fallback.
# ---------------------------------------------------------------------------
add_executable(player-info-three-state-tests
    tests/player_info_three_state_tests.cpp
    src/resources/resources.qrc
)
target_link_libraries(player-info-three-state-tests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg mhw-ui)
target_include_directories(player-info-three-state-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(player-info-three-state-tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME player-info-three-state-tests COMMAND player-info-three-state-tests)
# Offscreen is REQUIRED here, not a convention: without it this test passes
# when run directly and fails under ctest, because the platform plugin
# changes what paintPanel() resolves its fonts and metrics to — and this
# file asserts on exactly those. The suite was green on the command line
# and red in the runner for a whole round before the properties below were
# added.
#
# RESOURCE_LOCK for the same reason the other Widget tests carry it: every
# Panel constructor reads and writes ~/.config/monster-overlay/panels.ini,
# so running these concurrently aborts each other. HOME and XDG_CONFIG_HOME
# are redirected inside the test itself; the lock is what keeps two copies
# from racing on it.
set_tests_properties(player-info-three-state-tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    RESOURCE_LOCK "panels-ini")

