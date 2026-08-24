cmake_minimum_required(VERSION 3.25)
project(mhw_linux_overlay VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Svg)
find_package(Qt6 6.8 REQUIRED COMPONENTS WebEngineWidgets)
find_package(LayerShellQt REQUIRED)

# Core library: foundational types, memory reader, string table.
# Other modules (address_map, monster, player, quest, world, ui)
# will be added here incrementally.
add_library(monster-core STATIC
    src/core/string_table.h
    src/core/string_table.cpp
    src/core/game_detector.h
    src/core/game_detector.cpp
    src/rise/mhr_types.h
    src/rise/mhr_reader.h
    src/rise/mhr_reader.cpp
    src/rise/rise_damage_reader.cpp
    src/world/world_types.h
    src/world/world_types.cpp
)
target_include_directories(monster-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(monster-core PUBLIC Qt6::Core)
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
    MHW_DEFAULT_MAP="${CMAKE_CURRENT_SOURCE_DIR}/data/MonsterHunterWorld.421810.map"
    MHR_DEFAULT_MAP="${CMAKE_CURRENT_SOURCE_DIR}/data/MonsterHunterRise.16.0.2.0.map"
)

target_compile_options(monster-overlay PRIVATE -Wall -Wextra -Wpedantic)

# WebEngine spike: prove transparent layer-shell + Chromium rendering.
# Builds a separate binary `monster-overlay-web` so the main monster-overlay
# binary stays unchanged. If the spike succeeds, we wire the same
# widget into the main binary as a fallback / low-memory mode.
add_executable(monster-overlay-web src/spike_web.cpp)
target_compile_definitions(monster-overlay-web PRIVATE
    SPIKE_HTML_PATH="${CMAKE_CURRENT_SOURCE_DIR}/web/spike.html")

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
target_compile_definitions(snap-player-demo PRIVATE
    MHW_DEFAULT_MAP="${CMAKE_CURRENT_SOURCE_DIR}/data/MonsterHunterWorld.421810.map"
)
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
target_compile_definitions(snap-all-demo PRIVATE
    MHW_DEFAULT_MAP="${CMAKE_CURRENT_SOURCE_DIR}/data/MonsterHunterWorld.421810.map"
)
target_compile_options(snap-all-demo PRIVATE -Wall -Wextra -Wpedantic)

# Control console — standalone Qt window, no layer-shell. Reuses the same
# panel paint code as the live overlay to preview section toggles.
add_executable(monster-control
    src/main_control.cpp
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
    MHW_DEFAULT_MAP="${CMAKE_CURRENT_SOURCE_DIR}/data/MonsterHunterWorld.421810.map"
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
target_compile_definitions(monster-control-l2-smoke PRIVATE
    MHW_DEFAULT_MAP="${CMAKE_CURRENT_SOURCE_DIR}/data/MonsterHunterWorld.421810.map"
)
target_compile_options(monster-control-l2-smoke PRIVATE -Wall -Wextra -Wpedantic)

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
target_compile_definitions(monster-control-p0-test PRIVATE
    MHW_DEFAULT_MAP="${CMAKE_CURRENT_SOURCE_DIR}/data/MonsterHunterWorld.421810.map"
)
target_compile_options(monster-control-p0-test PRIVATE -Wall -Wextra -Wpedantic)

target_link_libraries(monster-overlay-web PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::WebEngineWidgets
    LayerShellQt::Interface
)
target_compile_definitions(monster-overlay-web PRIVATE)
target_compile_options(monster-overlay-web PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
if(BUILD_TESTING)
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
    target_compile_definitions(monster-probe PRIVATE MHW_DEFAULT_MAP="${CMAKE_CURRENT_SOURCE_DIR}/data/MonsterHunterWorld.421810.map")

    add_executable(monster-scan
        tests/monster_scan.cpp
        ${MHW_READER_SOURCES}
    )
    target_link_libraries(monster-scan PRIVATE Qt6::Core monster-core)
    target_include_directories(monster-scan PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(monster-scan PRIVATE -Wall -Wno-unused-variable -Wno-unused-parameter)
    target_compile_definitions(monster-scan PRIVATE MHW_DEFAULT_MAP="${CMAKE_CURRENT_SOURCE_DIR}/data/MonsterHunterWorld.421810.map")

    add_executable(monster-probe-parts
        tests/monster_probe_parts.cpp
        ${MHW_READER_SOURCES}
    )
    target_link_libraries(monster-probe-parts PRIVATE Qt6::Core monster-core)
    target_include_directories(monster-probe-parts PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(monster-probe-parts PRIVATE -Wall -Wno-unused-variable -Wno-unused-parameter)
    target_compile_definitions(monster-probe-parts PRIVATE MHW_DEFAULT_MAP="${CMAKE_CURRENT_SOURCE_DIR}/data/MonsterHunterWorld.421810.map")

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
endif()
