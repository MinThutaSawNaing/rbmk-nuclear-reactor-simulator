cmake_minimum_required(VERSION 3.21)

project(RbmkControlRoom
    VERSION 1.1.0
    DESCRIPTION "RBMK Control Room Simulator"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
qt_standard_project_setup()

# ---------------------------------------------------------------- core engine
add_library(rbmk_core STATIC
    src/core/ReactorCore.cpp
)
target_include_directories(rbmk_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbmk_core PUBLIC Qt6::Core)

# --------------------------------------------------------------------- the game
qt_add_executable(RbmkControlRoom WIN32
    src/main.cpp
    src/ui/AppIcon.cpp
    src/ui/MainWindow.cpp
    src/ui/CoreView.cpp
    src/ui/GaugeCard.cpp
    src/ui/TrendChart.cpp
    src/ui/TurbineView.cpp
    src/ui/Theme.cpp
)
target_link_libraries(RbmkControlRoom PRIVATE rbmk_core Qt6::Widgets Qt6::Gui)

# The research logo is embedded so the window/taskbar icon survives any working
# directory, and so the About dialog and header badge share one source of truth.
# The PNG set is produced by tools/make_icons.py from logos/rbmk_research_logo_500.png.
set(RBMK_ICON_SIZES 16 24 32 48 64 128 256 512)
set(RBMK_ICON_PNGS "")
foreach(icon_size IN LISTS RBMK_ICON_SIZES)
    list(APPEND RBMK_ICON_PNGS "assets/icons/rbmk-logo-${icon_size}.png")
endforeach()
qt_add_resources(RbmkControlRoom "app_icons"
    PREFIX "/icons"
    BASE "assets/icons"
    FILES ${RBMK_ICON_PNGS}
)

# Windows shell integration: executable icon (Explorer, taskbar, shortcuts and
# the installer's uninstall entry) plus the version block shown in Properties.
if(WIN32)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/rbmk.ico")
        message(FATAL_ERROR "Missing assets/icons/rbmk.ico - run: python tools/make_icons.py")
    endif()
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/rbmk.ico" RBMK_ICON_FILE)
    configure_file(src/win/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
    target_sources(RbmkControlRoom PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
    target_link_libraries(RbmkControlRoom PRIVATE shell32)
endif()

set_target_properties(RbmkControlRoom PROPERTIES
    OUTPUT_NAME "RBMK-Control-Room"
    WIN32_EXECUTABLE TRUE
)

# Bundle the Qt runtime next to the executable:  cmake --build build --target deploy
find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT6_INSTALL_PREFIX}/bin")
if(WINDEPLOYQT_EXECUTABLE)
    add_custom_target(deploy
        COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations --no-system-d3d-compiler
                --no-opengl-sw "$<TARGET_FILE:RbmkControlRoom>"
        DEPENDS RbmkControlRoom
        COMMENT "Deploying Qt runtime next to RBMK-Control-Room.exe")
endif()

# -------------------------------------------------------------------- unit tests
find_package(Qt6 6.5 QUIET COMPONENTS Test)
if(TARGET Qt6::Test)
    enable_testing()
    qt_add_executable(rbmk_tests tests/core_tests.cpp)
    target_link_libraries(rbmk_tests PRIVATE rbmk_core Qt6::Test)
    add_test(NAME core_physics COMMAND rbmk_tests)
endif()
