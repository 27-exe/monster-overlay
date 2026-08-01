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
add_library(mhw-core STATIC
    src/core/string_table.h
    src/core/string_table.cpp
    src/world/world_types.h
    src/world/world_types.cpp
)
target_include_directories(mhw-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(mhw-core PUBLIC Qt6::Core)
target_compile_options(mhw-core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(mhw-overlay
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

target_link_libraries(mhw-overlay PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
    LayerShellQt::Interface
    mhw-core
)

target_compile_definitions(mhw-overlay PRIVATE
    MHW_DEFAULT_MAP="${CMAKE_CURRENT_SOURCE_DIR}/data/MonsterHunterWorld.421810.map"
)

target_compile_options(mhw-overlay PRIVATE -Wall -Wextra -Wpedantic)

# WebEngine spike: prove transparent layer-shell + Chromium rendering.
# Builds a separate binary `mhw-overlay-web` so the main mhw-overlay
# binary stays unchanged. If the spike succeeds, we wire the same
# widget into the main binary as a fallback / low-memory mode.
add_executable(mhw-overlay-web src/spike_web.cpp)
target_compile_definitions(mhw-overlay-web PRIVATE
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
    mhw-core
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
    mhw-core
)
target_include_directories(snap-all-demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(snap-all-demo PRIVATE
    MHW_DEFAULT_MAP="${CMAKE_CURRENT_SOURCE_DIR}/data/MonsterHunterWorld.421810.map"
)
target_compile_options(snap-all-demo PRIVATE -Wall -Wextra -Wpedantic)

# Control console — standalone Qt window, no layer-shell. Reuses the same
# panel paint code as the live overlay to preview section toggles.
add_executable(mhw-control
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
    src/ui/toggle_chip.cpp
    src/ui/toggle_chip.h
    src/ui/section_row.cpp
    src/ui/section_row.h
    src/world/world_types.cpp
    src/resources/resources.qrc
    assets/icons.qrc
)
target_link_libraries(mhw-control PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
    LayerShellQt::Interface
    mhw-core
)
target_include_directories(mhw-control PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(mhw-control PRIVATE
    MHW_DEFAULT_MAP="${CMAKE_CURRENT_SOURCE_DIR}/data/MonsterHunterWorld.421810.map"
)
target_compile_options(mhw-control PRIVATE -Wall -Wextra -Wpedantic)

# L2 smoke test for the control-panel mask persistence round-trip.
# Rebuilds an in-process ControlPanel, flips switches via the public
# SectionRow API (found by findChildren), lets the dtor save to disk,
# then re-opens and verifies the load applied the same state.
add_executable(mhw-control-l2-smoke
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
    src/ui/toggle_chip.cpp
    src/ui/toggle_chip.h
    src/ui/section_row.cpp
    src/ui/section_row.h
    src/world/world_types.cpp
    src/resources/resources.qrc
    assets/icons.qrc
)
target_link_libraries(mhw-control-l2-smoke PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
    LayerShellQt::Interface
    mhw-core
)
target_include_directories(mhw-control-l2-smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(mhw-control-l2-smoke PRIVATE
    MHW_DEFAULT_MAP="${CMAKE_CURRENT_SOURCE_DIR}/data/MonsterHunterWorld.421810.map"
)
target_compile_options(mhw-control-l2-smoke PRIVATE -Wall -Wextra -Wpedantic)

target_link_libraries(mhw-overlay-web PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::WebEngineWidgets
    LayerShellQt::Interface
)
target_compile_definitions(mhw-overlay-web PRIVATE)
target_compile_options(mhw-overlay-web PRIVATE -Wall -Wextra -Wpedantic)

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
    add_executable(mhw-reader-tests
        tests/reader_tests.cpp
        ${MHW_READER_SOURCES}
    )
    target_link_libraries(mhw-reader-tests PRIVATE Qt6::Core mhw-core)
    target_include_directories(mhw-reader-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(mhw-reader-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME mhw-reader-tests COMMAND mhw-reader-tests)

    add_executable(mhw-probe
        tests/mhw_probe.cpp
        ${MHW_READER_SOURCES}
    )
    target_link_libraries(mhw-probe PRIVATE Qt6::Core mhw-core)
    target_include_directories(mhw-probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(mhw-probe PRIVATE -Wall -Wno-unused-variable -Wno-unused-parameter)
    target_compile_definitions(mhw-probe PRIVATE MHW_DEFAULT_MAP="${CMAKE_CURRENT_SOURCE_DIR}/data/MonsterHunterWorld.421810.map")

    add_executable(mhw-scan
        tests/mhw_scan.cpp
        ${MHW_READER_SOURCES}
    )
    target_link_libraries(mhw-scan PRIVATE Qt6::Core mhw-core)
    target_include_directories(mhw-scan PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(mhw-scan PRIVATE -Wall -Wno-unused-variable -Wno-unused-parameter)
    target_compile_definitions(mhw-scan PRIVATE MHW_DEFAULT_MAP="${CMAKE_CURRENT_SOURCE_DIR}/data/MonsterHunterWorld.421810.map")

    add_executable(mhw-probe-parts
        tests/mhw_probe_parts.cpp
        ${MHW_READER_SOURCES}
    )
    target_link_libraries(mhw-probe-parts PRIVATE Qt6::Core mhw-core)
    target_include_directories(mhw-probe-parts PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(mhw-probe-parts PRIVATE -Wall -Wno-unused-variable -Wno-unused-parameter)
    target_compile_definitions(mhw-probe-parts PRIVATE MHW_DEFAULT_MAP="${CMAKE_CURRENT_SOURCE_DIR}/data/MonsterHunterWorld.421810.map")

    add_executable(mhw-probe-ailments
        tests/mhw_probe_ailments.cpp
        ${MHW_READER_SOURCES}
    )
    target_link_libraries(mhw-probe-ailments PRIVATE Qt6::Core mhw-core)
    target_include_directories(mhw-probe-ailments PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(mhw-probe-ailments PRIVATE -Wall -Wno-unused-variable -Wno-unused-parameter)

    add_executable(mhw-probe-ailments-watch
        tests/mhw_probe_ailments_watch.cpp
        ${MHW_READER_SOURCES}
    )
    target_link_libraries(mhw-probe-ailments-watch PRIVATE Qt6::Core mhw-core)
    target_include_directories(mhw-probe-ailments-watch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(mhw-probe-ailments-watch PRIVATE -Wall -Wno-unused-variable -Wno-unused-parameter)

    add_executable(mhw-probe-mantles
        tests/mhw_probe_mantles.cpp
        ${MHW_READER_SOURCES}
    )
    target_link_libraries(mhw-probe-mantles PRIVATE Qt6::Core mhw-core)
    target_include_directories(mhw-probe-mantles PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(mhw-probe-mantles PRIVATE -Wall -Wno-unused-variable -Wno-unused-parameter)

    add_executable(mhw-probe-mantles-wide
        tests/mhw_probe_mantles_wide.cpp
        ${MHW_READER_SOURCES}
    )
    target_link_libraries(mhw-probe-mantles-wide PRIVATE Qt6::Core mhw-core)
    target_include_directories(mhw-probe-mantles-wide PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(mhw-probe-mantles-wide PRIVATE -Wall -Wno-unused-variable -Wno-unused-parameter)

    # Register every probe as a CTest target. The probe binaries read
    # /proc/<mhw pid>/mem at run time; if MHW isn't running they'll
    # print a diagnostic and exit with the documented "MHW not found"
    # code (1 for mhw-probe, 3 for mhw-probe-ailments[-watch]).
    # Mark them as SKIP so ctest still passes on a CI box without MHW.
    add_test(NAME mhw-probe               COMMAND mhw-probe)
    add_test(NAME mhw-scan                COMMAND mhw-scan)
    add_test(NAME mhw-probe-parts         COMMAND mhw-probe-parts --no-watch)
    add_test(NAME mhw-probe-ailments      COMMAND mhw-probe-ailments)
    add_test(NAME mhw-probe-ailments-watch COMMAND mhw-probe-ailments-watch /tmp/mhw-probe-ailments-watch-test.log)
    add_test(NAME mhw-probe-mantles       COMMAND mhw-probe-mantles)
    add_test(NAME mhw-probe-mantles-wide  COMMAND mhw-probe-mantles-wide)
    set_tests_properties(mhw-probe mhw-scan mhw-probe-parts mhw-probe-ailments
        mhw-probe-ailments-watch mhw-probe-mantles mhw-probe-mantles-wide
        PROPERTIES SKIP_RETURN_CODE "1;3")

    # Unit tests for the core string table — independent of MHW.
    add_executable(mhw-core-tests
        tests/string_table_tests.cpp
        src/core/string_table.cpp
        src/core/string_table.h
    )
    target_link_libraries(mhw-core-tests PRIVATE Qt6::Core mhw-core)
    target_include_directories(mhw-core-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(mhw-core-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME mhw-core-tests COMMAND mhw-core-tests)
endif()
