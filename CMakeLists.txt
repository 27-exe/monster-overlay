cmake_minimum_required(VERSION 3.25)
project(mhw_linux_overlay VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)
find_package(LayerShellQt REQUIRED)

add_executable(mhw-overlay
    src/main.cpp
    src/mhw_reader.cpp
    src/mhw_reader.h
    src/overlay_window.cpp
    src/overlay_window.h
)

target_link_libraries(mhw-overlay PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    LayerShellQt::Interface
)

target_compile_definitions(mhw-overlay PRIVATE
    MHW_DEFAULT_MAP="${CMAKE_CURRENT_SOURCE_DIR}/data/MonsterHunterWorld.421810.map"
)

target_compile_options(mhw-overlay PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
if(BUILD_TESTING)
    add_executable(mhw-reader-tests
        tests/reader_tests.cpp
        src/mhw_reader.cpp
        src/mhw_reader.h
    )
    target_link_libraries(mhw-reader-tests PRIVATE Qt6::Core)
    target_include_directories(mhw-reader-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(mhw-reader-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME mhw-reader-tests COMMAND mhw-reader-tests)

    add_executable(mhw-probe
        tests/mhw_probe.cpp
        src/mhw_reader.cpp
        src/mhw_reader.h
    )
    target_link_libraries(mhw-probe PRIVATE Qt6::Core)
    target_include_directories(mhw-probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(mhw-probe PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_definitions(mhw-probe PRIVATE MHW_DEFAULT_MAP="${CMAKE_CURRENT_SOURCE_DIR}/data/MonsterHunterWorld.421810.map")
endif()
