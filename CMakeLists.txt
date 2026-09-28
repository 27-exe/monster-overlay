cmake_minimum_required(VERSION 3.25)
project(monster_overlay VERSION 0.11.2 LANGUAGES CXX)

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

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Widgets Svg)
find_package(LayerShellQt REQUIRED)
find_package(Threads REQUIRED)

# Core library: foundational types, memory reader, string table.
# Other modules (address_map, monster, player, quest, world, ui)
# will be added here incrementally.
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
    src/rise/mhr_types.h
    src/rise/mhr_reader.h
    src/rise/mhr_reader.cpp
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
    src/rise/reframework/reframework_fetcher.h
    src/rise/reframework/reframework_fetcher.cpp
    src/rise/part_schemas.cpp
    src/world/world_types.h
    src/world/world_types.cpp
    # World monster DATA tables (kPartSchemas / kAilmentNames / kAilmentNamesEn /
    # kCrownThresholds / kMonsterCaptureThresholds) — their own translation
    # unit; the lookup code that reads them lives elsewhere. scripts/gen_schema.py
    # rewrites THIS file in place.
    src/monster/data/part_schemas_data.cpp
)
target_include_directories(monster-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(monster-core PUBLIC Qt6::Core Threads::Threads)
target_compile_options(monster-core PRIVATE -Wall -Wextra -Wpedantic)

# Reader library: the process-memory readers for the World build (MhwReader
# itself plus the five per-domain readers that define its member functions).
# These files used to be listed directly by 20 targets, which meant the whole
# reader layer was compiled 20 times over per build and every new target had
# a silent way to drift from the rest by picking the wrong subset. The five
# domain readers are internally decoupled (each includes only mhw_reader.h
# and its own types header), so they share one library safely; this is the
# World counterpart of monster-core's Rise reader.
add_library(mhw-reader STATIC
    src/world/world_reader.h
    src/world/world_reader.cpp
    src/world/monster_reader.cpp
    src/world/quest_reader.cpp
    src/world/player_reader.cpp
    src/world/world_severable_scan.h
    src/monster/target_selector.cpp
)
target_include_directories(mhw-reader PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(mhw-reader PUBLIC Qt6::Core monster-core)
target_compile_options(mhw-reader PRIVATE -Wall -Wextra -Wpedantic)

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
# monster-core / mhw-reader headers (core/…, player/…, quest/…, world/…,
# rise/…), never the reverse. That is what makes it safe to compile once
# and link 12 ways.
# ---------------------------------------------------------------------------
add_library(mhw-ui STATIC
    src/ui/control_panel.cpp
    src/ui/control_panel.h
    src/ui/control_panel_styles.cpp
    src/ui/control_panel_styles.h
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
    src/ui/panel_damage.cpp
    src/ui/panel_damage.h
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
    src/ui/panel_pet_damage.cpp
    src/ui/panel_pet_damage.h
    src/ui/panel_player.cpp
    src/ui/panel_player.h
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
    LayerShellQt::Interface
    monster-core
    mhw-reader
)
target_compile_options(mhw-ui PRIVATE -Wall -Wextra -Wpedantic)

# Private QtGui headers (qpa/qplatformscreen.h) live under
# /usr/include/qt6/QtGui/<ver>/QtGui/qpa/. Locate the active
# version's include root and expose it to mhw-ui so screen_query.cpp
# can use QPlatformScreen::geometry() — the only public-ish way to get
# the backend-native screen size in physical pixels without guessing from
# QScreen::geometry() × devicePixelRatio(). It stays PRIVATE to mhw-ui:
# a consumer only calls screen_query::detect()/listOutputs(), it does not
# include qpa headers itself, so no consumer needs this path.
file(GLOB _qt6_gui_version_dirs
    LIST_DIRECTORIES true
    "/usr/include/qt6/QtGui/*"
)
set(_QT6_GUI_PRIVATE_INCLUDE_DIR "")
foreach(_d IN LISTS _qt6_gui_version_dirs)
    if(IS_DIRECTORY ${_d}/QtGui/qpa)
        set(_QT6_GUI_PRIVATE_INCLUDE_DIR ${_d}/QtGui)
        break()
    endif()
endforeach()
if(NOT _QT6_GUI_PRIVATE_INCLUDE_DIR)
    message(FATAL_ERROR "Could not locate Qt6 qpa headers under /usr/include/qt6")
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
    mhw-reader
    mhw-ui
)

target_compile_definitions(monster-overlay PRIVATE
    MONSTER_VERSION="${PROJECT_VERSION}"
)

target_compile_options(monster-overlay PRIVATE -Wall -Wextra -Wpedantic)

# Offscreen snapshot of the player panel in edit/demo mode. Used for
# visual regression testing without launching the layer-shell overlay
# (which would steal keyboard focus and lock the user out of the
# desktop — see ~/.hermes/memory KDE/overlay testing note).
add_executable(snap-player-demo
    tests/snap_player_demo.cpp
    src/resources/resources.qrc
    assets/icons.qrc
)
target_link_libraries(snap-player-demo PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
    LayerShellQt::Interface
    monster-core
    mhw-reader
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
    mhw-reader
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
    mhw-reader
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
    mhw-reader
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
    mhw-reader
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
    mhw-reader
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
    mhw-reader
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
    mhw-reader
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
    mhw-reader
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
        mhw-reader
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
        mhw-reader
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
    target_link_libraries(monster-reader-tests PRIVATE Qt6::Core monster-core mhw-reader)
    target_include_directories(monster-reader-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(monster-reader-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME monster-reader-tests COMMAND monster-reader-tests)

    add_executable(monster-probe
        tests/monster_probe.cpp
    )
    target_link_libraries(monster-probe PRIVATE Qt6::Core monster-core mhw-reader)
    target_include_directories(monster-probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(monster-probe PRIVATE -Wall -Wno-unused-variable -Wno-unused-parameter)

    add_executable(monster-scan
        tests/monster_scan.cpp
    )
    target_link_libraries(monster-scan PRIVATE Qt6::Core monster-core mhw-reader)
    target_include_directories(monster-scan PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(monster-scan PRIVATE -Wall -Wno-unused-variable -Wno-unused-parameter)

    add_executable(monster-probe-parts
        tests/monster_probe_parts.cpp
    )
    target_link_libraries(monster-probe-parts PRIVATE Qt6::Core monster-core mhw-reader)
    target_include_directories(monster-probe-parts PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(monster-probe-parts PRIVATE -Wall -Wno-unused-variable -Wno-unused-parameter)

    add_executable(monster-probe-ailments
        tests/monster_probe_ailments.cpp
    )
    target_link_libraries(monster-probe-ailments PRIVATE Qt6::Core monster-core mhw-reader)
    target_include_directories(monster-probe-ailments PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(monster-probe-ailments PRIVATE -Wall -Wno-unused-variable -Wno-unused-parameter)

    add_executable(monster-probe-ailments-watch
        tests/monster_probe_ailments_watch.cpp
    )
    target_link_libraries(monster-probe-ailments-watch PRIVATE Qt6::Core monster-core mhw-reader)
    target_include_directories(monster-probe-ailments-watch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(monster-probe-ailments-watch PRIVATE -Wall -Wno-unused-variable -Wno-unused-parameter)

    add_executable(monster-probe-mantles
        tests/monster_probe_mantles.cpp
    )
    target_link_libraries(monster-probe-mantles PRIVATE Qt6::Core monster-core mhw-reader)
    target_include_directories(monster-probe-mantles PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(monster-probe-mantles PRIVATE -Wall -Wno-unused-variable -Wno-unused-parameter)

    add_executable(monster-probe-mantles-wide
        tests/monster_probe_mantles_wide.cpp
    )
    target_link_libraries(monster-probe-mantles-wide PRIVATE Qt6::Core monster-core mhw-reader)
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
    add_executable(rise-monster-metadata-tests
        tests/rise_monster_metadata_tests.cpp
        src/monster/data/part_schemas_data.cpp
        src/rise/part_schemas.cpp
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
    target_link_libraries(rise-monster-name-tests PRIVATE Qt6::Core monster-core)
    target_include_directories(rise-monster-name-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(rise-monster-name-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME rise-monster-name-tests COMMAND rise-monster-name-tests)

    add_executable(rise-part-names-hp-tests
        tests/rise_part_names_hp_tests.cpp
        src/resources/resources.qrc
    )
    target_link_libraries(rise-part-names-hp-tests PRIVATE Qt6::Core monster-core)
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

    # Steam/Rise installation discovery and REFramework lifecycle tests are
    # QtCore-only and link the same production core library used by the
    # console. They never touch the real Steam or game directory.
    add_executable(steam-game-locator-tests
        tests/steam_game_locator_tests.cpp
    )
    target_link_libraries(steam-game-locator-tests PRIVATE monster-core)
    target_include_directories(steam-game-locator-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(steam-game-locator-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME steam-game-locator-tests COMMAND steam-game-locator-tests)

    add_executable(rise-reframework-manager-tests
        tests/rise_reframework_manager_tests.cpp
        src/resources/resources.qrc
    )
    target_link_libraries(rise-reframework-manager-tests PRIVATE monster-core)
    target_include_directories(rise-reframework-manager-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(rise-reframework-manager-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME rise-reframework-manager-tests COMMAND rise-reframework-manager-tests)

    add_executable(reframework-fetcher-tests
        tests/reframework_fetcher_tests.cpp
        src/resources/resources.qrc
    )
    target_link_libraries(reframework-fetcher-tests PRIVATE monster-core)
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

    # Rise damage ingestion and pet-panel behavior are independent targets so
    # failures identify the feed or rendering contract directly.
    add_executable(rise-damage-reader-tests
        tests/rise_damage_reader_tests.cpp
    )
    target_link_libraries(rise-damage-reader-tests PRIVATE Qt6::Core monster-core)
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
    target_link_libraries(rise-damage-roster-tests PRIVATE Qt6::Core monster-core)
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
    target_link_libraries(rise-zone-mapping-tests PRIVATE monster-core)
    target_include_directories(rise-zone-mapping-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(rise-zone-mapping-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME rise-zone-mapping-tests COMMAND rise-zone-mapping-tests)

    # v0.8.4-r18 player-abnormalities: generated Rise consumable/debuff schema
    # table + the pure evaluation core (flag gates, WithValue sub-ids, the
    # raw/60 conversion, MaxTimer inversion). No game process is required.
    add_executable(rise-abnormality-tests
        tests/rise_abnormality_tests.cpp
        src/resources/resources.qrc
    )
    target_link_libraries(rise-abnormality-tests PRIVATE Qt6::Core monster-core)
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
target_link_libraries(monster-doctor PRIVATE Qt6::Core monster-core mhw-reader)
target_include_directories(monster-doctor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(monster-doctor PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(monster-doctor PRIVATE
    MONSTER_VERSION="${PROJECT_VERSION}"
)

add_executable(rise-probe EXCLUDE_FROM_ALL
    tests/rise_probe.cpp
)
target_link_libraries(rise-probe PRIVATE Qt6::Core monster-core mhw-reader)
target_include_directories(rise-probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(rise-probe PRIVATE -Wall -Wextra -Wpedantic)

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

set(MHW_I18N_GATES check parity unused)

foreach(gate IN LISTS MHW_I18N_GATES)
    add_test(
        NAME i18n-gate-${gate}
        COMMAND ${PYTHON3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/i18n_gate.py
                ${gate} -q
    )
endforeach()
