cmake_minimum_required(VERSION 3.25)
project(monster_overlay VERSION 0.10.10 LANGUAGES CXX)

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

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Svg)
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
    src/core/steam_game_locator.h
    src/core/steam_game_locator.cpp
    src/core/rise_reframework_manager.h
    src/core/rise_reframework_manager.cpp
    src/core/reframework_fetcher.h
    src/core/reframework_fetcher.cpp
    src/core/map_paths.h
    src/core/map_paths.cpp
    src/rise/mhr_types.h
    src/rise/mhr_reader.h
    src/rise/mhr_reader.cpp
    src/rise/mhr_part_names.h
    src/rise/mhr_part_names.cpp
    src/rise/mhr_monster_names.h
    src/rise/mhr_monster_names.cpp
    src/rise/mhr_abnormalities.h
    src/rise/mhr_abnormalities.cpp
    src/rise/rise_damage_types.h
    src/rise/rise_damage_reader.h
    src/rise/rise_damage_reader.cpp
    src/rise/rise_damage_roster.h
    src/rise/rise_damage_roster.cpp
    src/world/world_types.h
    src/world/world_types.cpp
)
target_include_directories(monster-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(monster-core PUBLIC Qt6::Core Threads::Threads)
target_compile_options(monster-core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(monster-overlay
    src/main.cpp
    src/mhw_reader.cpp
    src/mhw_reader.h
    src/monster/part_schemas.cpp
    src/monster/monster_reader.cpp
    src/monster/target_selector.cpp
    src/player/player_reader.cpp
    src/quest/quest_reader.cpp
    src/world/world_reader.cpp
    src/ui/formatters.cpp
    src/ui/icon.cpp
    src/ui/panel.cpp
    src/ui/panel_monster.cpp
    src/ui/panel_player.cpp
    src/ui/panel_damage.cpp
    src/ui/panel_pet_damage.h
    src/ui/panel_pet_damage.cpp
    src/world/world_types.cpp
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
    src/mhw_reader.cpp
    src/mhw_reader.h
    src/player/player_reader.cpp
    src/quest/quest_reader.cpp
    src/world/world_reader.cpp
    src/monster/monster_reader.cpp
    src/monster/part_schemas.cpp
    src/monster/target_selector.cpp
    src/ui/formatters.cpp
    src/ui/icon.cpp
    src/ui/panel.cpp
    src/ui/panel_player.cpp
    src/ui/panel_damage.cpp
    src/ui/panel_monster.cpp
    src/world/world_types.cpp
    src/resources/resources.qrc
    assets/icons.qrc
)
target_link_libraries(snap-player-demo PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
    LayerShellQt::Interface
    monster-core
)
target_include_directories(snap-player-demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(snap-player-demo PRIVATE -Wall -Wextra -Wpedantic)

add_executable(snap-all-demo
    tests/snap_all_demo.cpp
    src/mhw_reader.cpp
    src/mhw_reader.h
    src/player/player_reader.cpp
    src/quest/quest_reader.cpp
    src/world/world_reader.cpp
    src/monster/monster_reader.cpp
    src/monster/part_schemas.cpp
    src/monster/target_selector.cpp
    src/ui/formatters.cpp
    src/ui/icon.cpp
    src/ui/panel.cpp
    src/ui/panel_player.cpp
    src/ui/panel_damage.cpp
    src/ui/panel_monster.cpp
    src/ui/panel_pet_damage.h
    src/ui/panel_pet_damage.cpp
    src/world/world_types.cpp
    src/resources/resources.qrc
    assets/icons.qrc
)
target_link_libraries(snap-all-demo PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
    LayerShellQt::Interface
    monster-core
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
    src/mhw_reader.cpp
    src/mhw_reader.h
    src/player/player_reader.cpp
    src/quest/quest_reader.cpp
    src/world/world_reader.cpp
    src/monster/monster_reader.cpp
    src/monster/part_schemas.cpp
    src/monster/target_selector.cpp
    src/ui/formatters.cpp
    src/ui/icon.cpp
    src/ui/panel.cpp
    src/ui/panel_player.cpp
    src/ui/panel_damage.cpp
    src/ui/panel_monster.cpp
    src/world/world_types.cpp
    src/resources/resources.qrc
    assets/icons.qrc
)
target_link_libraries(snap-world-mp PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
    LayerShellQt::Interface
    monster-core
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
    src/mhw_reader.cpp
    src/mhw_reader.h
    src/player/player_reader.cpp
    src/quest/quest_reader.cpp
    src/world/world_reader.cpp
    src/monster/monster_reader.cpp
    src/monster/part_schemas.cpp
    src/monster/target_selector.cpp
    src/ui/formatters.cpp
    src/ui/icon.cpp
    src/ui/panel.cpp
    src/ui/panel_player.cpp
    src/ui/panel_damage.cpp
    src/ui/panel_monster.cpp
    src/world/world_types.cpp
    src/resources/resources.qrc
    assets/icons.qrc
)
target_link_libraries(snap-gauge-fill PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
    LayerShellQt::Interface
    monster-core
)
target_include_directories(snap-gauge-fill PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(snap-gauge-fill PRIVATE -Wall -Wextra -Wpedantic)

# Control console — standalone Qt window, no layer-shell. Reuses the same
# panel paint code as the live overlay to preview section toggles.
add_executable(monster-control
    src/main_control.cpp
    src/ui/rise_reframework_bridge.h
    src/ui/rise_reframework_bridge.cpp
    src/mhw_reader.cpp
    src/mhw_reader.h
    src/player/player_reader.cpp
    src/quest/quest_reader.cpp
    src/world/world_reader.cpp
    src/monster/monster_reader.cpp
    src/monster/part_schemas.cpp
    src/monster/target_selector.cpp
    src/core/string_table.cpp
    src/ui/formatters.cpp
    src/ui/icon.cpp
    src/ui/panel.cpp
    src/ui/panel_player.cpp
    src/ui/panel_damage.cpp
    src/ui/panel_monster.cpp
    src/ui/panel_pet_damage.h
    src/ui/panel_pet_damage.cpp
    src/ui/control_panel.cpp
    src/ui/hud_canvas.cpp
    src/ui/hud_canvas.h
    src/ui/panel_source.cpp
    src/ui/panel_source.h
    src/ui/screen_query.cpp
    src/ui/screen_query.h
    src/ui/toggle_chip.cpp
    src/ui/toggle_chip.h
    src/ui/section_row.cpp
    src/ui/section_row.h
    src/ui/section_count_bar.cpp
    src/ui/section_count_bar.h
    src/ui/ui_theme.cpp
    src/ui/ui_theme.h
    src/world/world_types.cpp
    src/resources/resources.qrc
    assets/icons.qrc
)
target_link_libraries(monster-control PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
    LayerShellQt::Interface
    monster-core
)
target_include_directories(monster-control PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# Private QtGui headers (qpa/qplatformscreen.h) live under
# /usr/include/qt6/QtGui/<ver>/QtGui/qpa/. Locate the active
# version's include root and expose it to the monster-control target
# so screen_query.cpp can use QPlatformScreen::geometry() — the
# only public-ish way to get the backend-native screen size in
# physical pixels without guessing from
# QScreen::geometry() × devicePixelRatio().
file(GLOB _qt6_gui_version_dirs
    LIST_DIRECTORIES true
    /usr/include/qt6/QtGui/*
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
target_include_directories(monster-control PRIVATE
    ${_QT6_GUI_PRIVATE_INCLUDE_DIR}
)
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
    src/mhw_reader.cpp
    src/mhw_reader.h
    src/player/player_reader.cpp
    src/quest/quest_reader.cpp
    src/world/world_reader.cpp
    src/monster/monster_reader.cpp
    src/monster/part_schemas.cpp
    src/monster/target_selector.cpp
    src/core/string_table.cpp
    src/ui/formatters.cpp
    src/ui/icon.cpp
    src/ui/panel.cpp
    src/ui/panel_player.cpp
    src/ui/panel_damage.cpp
    src/ui/panel_monster.cpp
    src/ui/panel_pet_damage.h
    src/ui/panel_pet_damage.cpp
    src/ui/control_panel.cpp
    src/ui/hud_canvas.cpp
    src/ui/hud_canvas.h
    src/ui/panel_source.cpp
    src/ui/panel_source.h
    src/ui/screen_query.cpp
    src/ui/screen_query.h
    src/ui/toggle_chip.cpp
    src/ui/toggle_chip.h
    src/ui/section_row.cpp
    src/ui/section_row.h
    src/ui/section_count_bar.cpp
    src/ui/section_count_bar.h
    src/ui/ui_theme.cpp
    src/ui/ui_theme.h
    src/world/world_types.cpp
    src/resources/resources.qrc
    assets/icons.qrc
)
target_link_libraries(monster-control-l2-smoke PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
    LayerShellQt::Interface
    monster-core
)
target_include_directories(monster-control-l2-smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(monster-control-l2-smoke PRIVATE
    ${_QT6_GUI_PRIVATE_INCLUDE_DIR}
)
target_compile_options(monster-control-l2-smoke PRIVATE -Wall -Wextra -Wpedantic)

# Rise-only REFramework console smoke test: builds the full control console
# and verifies the Damage-inspector management card (visible with its three
# action buttons in Rise mode, hidden in World mode). Read-only against the
# Steam tree; config writes go to a redirected XDG_CONFIG_HOME.
add_executable(monster-control-rise-rf-smoke
    tests/control_reframework_smoke.cpp
    src/mhw_reader.cpp
    src/mhw_reader.h
    src/player/player_reader.cpp
    src/quest/quest_reader.cpp
    src/world/world_reader.cpp
    src/monster/monster_reader.cpp
    src/monster/part_schemas.cpp
    src/monster/target_selector.cpp
    src/core/string_table.cpp
    src/ui/formatters.cpp
    src/ui/icon.cpp
    src/ui/panel.cpp
    src/ui/panel_player.cpp
    src/ui/panel_damage.cpp
    src/ui/panel_monster.cpp
    src/ui/panel_pet_damage.h
    src/ui/panel_pet_damage.cpp
    src/ui/control_panel.cpp
    src/ui/hud_canvas.cpp
    src/ui/hud_canvas.h
    src/ui/panel_source.cpp
    src/ui/panel_source.h
    src/ui/screen_query.cpp
    src/ui/screen_query.h
    src/ui/toggle_chip.cpp
    src/ui/toggle_chip.h
    src/ui/section_row.cpp
    src/ui/section_row.h
    src/ui/section_count_bar.cpp
    src/ui/section_count_bar.h
    src/ui/ui_theme.cpp
    src/ui/ui_theme.h
    src/world/world_types.cpp
    src/resources/resources.qrc
    assets/icons.qrc
)
target_link_libraries(monster-control-rise-rf-smoke PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
    LayerShellQt::Interface
    monster-core
)
target_include_directories(monster-control-rise-rf-smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(monster-control-rise-rf-smoke PRIVATE
    ${_QT6_GUI_PRIVATE_INCLUDE_DIR}
)
target_compile_options(monster-control-rise-rf-smoke PRIVATE -Wall -Wextra -Wpedantic)

# v0.5 P0 invariant test: Panel setters + HudCanvas toggle + hit-test.
# Builds without monster-control so it doesn't pull in the full GUI shell.
# Drop the QPA-private include because Panel is purely Qt-public here.
add_executable(monster-control-p0-test
    tests/control_panel_p0_test.cpp
    src/mhw_reader.cpp
    src/mhw_reader.h
    src/player/player_reader.cpp
    src/quest/quest_reader.cpp
    src/world/world_reader.cpp
    src/monster/monster_reader.cpp
    src/monster/part_schemas.cpp
    src/monster/target_selector.cpp
    src/core/string_table.cpp
    src/ui/formatters.cpp
    src/ui/icon.cpp
    src/ui/panel.cpp
    src/ui/panel_player.cpp
    src/ui/panel_damage.cpp
    src/ui/panel_monster.cpp
    src/ui/hud_canvas.cpp
    src/ui/hud_canvas.h
    src/ui/panel_source.cpp
    src/ui/panel_source.h
    src/ui/screen_query.cpp
    src/ui/screen_query.h
    src/ui/toggle_chip.cpp
    src/ui/toggle_chip.h
    src/ui/section_row.cpp
    src/ui/section_row.h
    src/ui/section_count_bar.cpp
    src/ui/section_count_bar.h
    src/ui/ui_theme.cpp
    src/ui/ui_theme.h
    src/world/world_types.cpp
    src/resources/resources.qrc
    assets/icons.qrc
)
target_link_libraries(monster-control-p0-test PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
    LayerShellQt::Interface
    monster-core
)
target_include_directories(monster-control-p0-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(monster-control-p0-test PRIVATE
    ${_QT6_GUI_PRIVATE_INCLUDE_DIR}
)
target_compile_options(monster-control-p0-test PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
if(BUILD_TESTING)
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
        src/mhw_reader.cpp
        src/mhw_reader.h
        src/player/player_reader.cpp
        src/quest/quest_reader.cpp
        src/world/world_reader.cpp
        src/monster/monster_reader.cpp
        src/monster/part_schemas.cpp
        src/monster/target_selector.cpp
        src/ui/formatters.cpp
        src/ui/icon.cpp
        src/ui/panel.cpp
        src/ui/panel_player.cpp
        src/ui/panel_damage.cpp
        src/ui/panel_monster.cpp
        src/world/world_types.cpp
        src/resources/resources.qrc
        assets/icons.qrc
    )
    target_link_libraries(snap-empty-state PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
        LayerShellQt::Interface
        monster-core
    )
    target_include_directories(snap-empty-state PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(snap-empty-state PRIVATE -Wall -Wextra -Wpedantic)

    # Diagnostic snapshot tool for the PlayerPanel no-data states (v0.9.1 P1):
    # waiting-for-exe / read-denied / map-missing / attached-but-empty, all of
    # which used to paint the same unexplained empty panel. Not a CTest — it
    # is a visual aid and needs a QPA platform.
    add_executable(snap-player-offline EXCLUDE_FROM_ALL
        tests/snap_player_offline.cpp
        src/mhw_reader.cpp
        src/mhw_reader.h
        src/player/player_reader.cpp
        src/quest/quest_reader.cpp
        src/world/world_reader.cpp
        src/monster/monster_reader.cpp
        src/monster/part_schemas.cpp
        src/monster/target_selector.cpp
        src/ui/formatters.cpp
        src/ui/icon.cpp
        src/ui/panel.cpp
        src/ui/panel_player.cpp
        src/ui/panel_damage.cpp
        src/ui/panel_monster.cpp
        src/world/world_types.cpp
        src/resources/resources.qrc
        assets/icons.qrc
    )
    target_link_libraries(snap-player-offline PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
        LayerShellQt::Interface
        monster-core
    )
    target_include_directories(snap-player-offline PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(snap-player-offline PRIVATE -Wall -Wextra -Wpedantic)

    # All reader-dependent targets need these sources since MhwReader member
    # functions are defined across multiple .cpp files (one per domain).
    set(MHW_READER_SOURCES
        src/mhw_reader.cpp
        src/mhw_reader.h
        src/monster/part_schemas.cpp
        src/monster/monster_reader.cpp
        src/monster/target_selector.cpp
        src/player/player_reader.cpp
        src/quest/quest_reader.cpp
        src/world/world_reader.cpp
    )
    add_executable(monster-reader-tests
        tests/reader_tests.cpp
        ${MHW_READER_SOURCES}
        # qrc: the tests flip StringTable between zh-CN / en-US to assert
        # both columns of the data-name tables.
        src/resources/resources.qrc
    )
    target_link_libraries(monster-reader-tests PRIVATE Qt6::Core monster-core)
    target_include_directories(monster-reader-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(monster-reader-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME monster-reader-tests COMMAND monster-reader-tests)

    add_executable(monster-probe
        tests/monster_probe.cpp
        ${MHW_READER_SOURCES}
    )
    target_link_libraries(monster-probe PRIVATE Qt6::Core monster-core)
    target_include_directories(monster-probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(monster-probe PRIVATE -Wall -Wno-unused-variable -Wno-unused-parameter)

    add_executable(monster-scan
        tests/monster_scan.cpp
        ${MHW_READER_SOURCES}
    )
    target_link_libraries(monster-scan PRIVATE Qt6::Core monster-core)
    target_include_directories(monster-scan PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(monster-scan PRIVATE -Wall -Wno-unused-variable -Wno-unused-parameter)

    add_executable(monster-probe-parts
        tests/monster_probe_parts.cpp
        ${MHW_READER_SOURCES}
    )
    target_link_libraries(monster-probe-parts PRIVATE Qt6::Core monster-core)
    target_include_directories(monster-probe-parts PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(monster-probe-parts PRIVATE -Wall -Wno-unused-variable -Wno-unused-parameter)

    add_executable(monster-probe-ailments
        tests/monster_probe_ailments.cpp
        ${MHW_READER_SOURCES}
    )
    target_link_libraries(monster-probe-ailments PRIVATE Qt6::Core monster-core)
    target_include_directories(monster-probe-ailments PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(monster-probe-ailments PRIVATE -Wall -Wno-unused-variable -Wno-unused-parameter)

    add_executable(monster-probe-ailments-watch
        tests/monster_probe_ailments_watch.cpp
        ${MHW_READER_SOURCES}
    )
    target_link_libraries(monster-probe-ailments-watch PRIVATE Qt6::Core monster-core)
    target_include_directories(monster-probe-ailments-watch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(monster-probe-ailments-watch PRIVATE -Wall -Wno-unused-variable -Wno-unused-parameter)

    add_executable(monster-probe-mantles
        tests/monster_probe_mantles.cpp
        ${MHW_READER_SOURCES}
    )
    target_link_libraries(monster-probe-mantles PRIVATE Qt6::Core monster-core)
    target_include_directories(monster-probe-mantles PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(monster-probe-mantles PRIVATE -Wall -Wno-unused-variable -Wno-unused-parameter)

    add_executable(monster-probe-mantles-wide
        tests/monster_probe_mantles_wide.cpp
        ${MHW_READER_SOURCES}
    )
    target_link_libraries(monster-probe-mantles-wide PRIVATE Qt6::Core monster-core)
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
    add_executable(rise-monster-metadata-tests
        tests/rise_monster_metadata_tests.cpp
        src/monster/part_schemas.cpp
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
        src/monster/part_schemas.cpp
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
        src/core/string_table.cpp
        src/core/string_table.h
    )
    target_link_libraries(monster-core-tests PRIVATE Qt6::Core monster-core)
    target_include_directories(monster-core-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(monster-core-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME monster-core-tests COMMAND monster-core-tests)

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
    )
    target_link_libraries(rise-reframework-manager-tests PRIVATE monster-core)
    target_include_directories(rise-reframework-manager-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(rise-reframework-manager-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME rise-reframework-manager-tests COMMAND rise-reframework-manager-tests)

    add_executable(reframework-fetcher-tests
        tests/reframework_fetcher_tests.cpp
    )
    target_link_libraries(reframework-fetcher-tests PRIVATE monster-core)
    target_include_directories(reframework-fetcher-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(reframework-fetcher-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME reframework-fetcher-tests COMMAND reframework-fetcher-tests)

    # i18n locale plumbing: conf read/write (locale_conf.h), the overlay's
    # change-detection state machine (locale_sync.h) and the per-locale
    # multi-file StringTable loader. Compiles the real qrc so the shipped
    # locale files (zh-CN/ en-US/ directories) are exercised as packaged.
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
    # Same reader sources as rise-probe: MhwReader spans per-domain .cpp files
    # and monster-core only ships the Rise reader.
    src/mhw_reader.cpp
    src/mhw_reader.h
    src/monster/part_schemas.cpp
    src/monster/monster_reader.cpp
    src/monster/target_selector.cpp
    src/player/player_reader.cpp
    src/quest/quest_reader.cpp
    src/world/world_reader.cpp
)
target_link_libraries(monster-doctor PRIVATE Qt6::Core monster-core)
target_include_directories(monster-doctor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(monster-doctor PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(monster-doctor PRIVATE
    MONSTER_VERSION="${PROJECT_VERSION}"
)

add_executable(rise-probe EXCLUDE_FROM_ALL
    tests/rise_probe.cpp
    # MhwReader is defined across per-domain .cpp files (see
    # MHW_READER_SOURCES); the probe needs them because monster-core only
    # ships the Rise reader. Keeping the list here keeps this target
    # independent of BUILD_TESTING.
    src/mhw_reader.cpp
    src/mhw_reader.h
    src/monster/part_schemas.cpp
    src/monster/monster_reader.cpp
    src/monster/target_selector.cpp
    src/player/player_reader.cpp
    src/quest/quest_reader.cpp
    src/world/world_reader.cpp
)
target_link_libraries(rise-probe PRIVATE Qt6::Core monster-core)
target_include_directories(rise-probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(rise-probe PRIVATE -Wall -Wextra -Wpedantic)
