cmake_minimum_required(VERSION 3.20)
project(raven_ide VERSION 0.1.0 LANGUAGES C CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(RAVEN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(RAVEN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(RAVEN_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(RAVEN_BUILD_WEB OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/Raven EXCLUDE_FROM_ALL)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/yaml-cpp EXCLUDE_FROM_ALL)

find_program(GLSLANG_VALIDATOR glslangValidator REQUIRED)
find_package(SDL2 REQUIRED CONFIG)
set(RAVEN_IDE_PLANET_SHADER "${CMAKE_CURRENT_BINARY_DIR}/shaders/planet.spv")
add_custom_command(
    OUTPUT "${RAVEN_IDE_PLANET_SHADER}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    COMMAND "${GLSLANG_VALIDATOR}" -V -I"${CMAKE_CURRENT_SOURCE_DIR}/third_party/Raven/include/raven/shaders"
            "${CMAKE_CURRENT_SOURCE_DIR}/ui/shaders/planet.frag" -o "${RAVEN_IDE_PLANET_SHADER}"
    DEPENDS ui/shaders/planet.frag third_party/Raven/include/raven/shaders/raven_effect.glsl
)
add_custom_target(raven_ide_planet_shader DEPENDS "${RAVEN_IDE_PLANET_SHADER}")

add_library(raven_ide_core
    src/checkpoints.cpp
    src/claude.cpp
    src/composer.cpp
    src/diff.cpp
    src/endurance.cpp
    src/endurance_controller.cpp
    src/knowledge.cpp
    src/memory.cpp
    src/missions.cpp
    src/notifications.cpp
    src/opening_audio.cpp
    src/planet_art.cpp
    src/profile.cpp
    src/recall.cpp
    src/recovery.cpp
    src/splash.cpp
    src/theme_gallery.cpp
    src/tesseract.cpp
    src/threads.cpp
    src/verification.cpp
    src/workspace.cpp
)
add_dependencies(raven_ide_core raven_ide_planet_shader)
target_compile_definitions(raven_ide_core PRIVATE RAVEN_IDE_BUILD_PLANET_SHADER="${RAVEN_IDE_PLANET_SHADER}")
target_include_directories(raven_ide_core PUBLIC src third_party)
target_link_libraries(raven_ide_core PUBLIC raven::raven SDL2::SDL2 yaml-cpp)
if(UNIX AND NOT APPLE)
    target_link_libraries(raven_ide_core PUBLIC pthread)
endif()

add_executable(raven-ide src/main.cpp src/app.cpp src/app_endurance.cpp src/platform.cpp)
target_link_libraries(raven-ide PRIVATE raven_ide_core)
if(APPLE)
    target_link_libraries(raven-ide PRIVATE "-framework AppKit" "-framework Carbon" "-framework CoreGraphics")
endif()
target_compile_definitions(raven-ide PRIVATE
    RAVEN_IDE_UI_DIR="${CMAKE_CURRENT_SOURCE_DIR}/ui"
    RAVEN_IDE_KNOWLEDGE_BIN="${CMAKE_BINARY_DIR}/raven-ide-knowledge"
    RAVEN_IDE_INSTALLED_UI_DIR="${CMAKE_INSTALL_FULL_DATADIR}/raven-ide/ui"
    RAVEN_IDE_INSTALLED_KNOWLEDGE_BIN="${CMAKE_INSTALL_FULL_BINDIR}/raven-ide-knowledge"
)

install(TARGETS raven-ide RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY ui/ DESTINATION ${CMAKE_INSTALL_DATADIR}/raven-ide/ui)
install(FILES "${RAVEN_IDE_PLANET_SHADER}" DESTINATION ${CMAKE_INSTALL_DATADIR}/raven-ide/ui/shaders)
install(FILES assets/raven-ide.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/128x128/apps)
install(DIRECTORY assets/icons/ DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor)
install(FILES packaging/raven-ide.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

add_executable(raven-ide-knowledge src/knowledge_main.cpp)
target_link_libraries(raven-ide-knowledge PRIVATE raven_ide_core)
install(TARGETS raven-ide-knowledge RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

include(CTest)
if(BUILD_TESTING)
    add_executable(raven-ide-core-tests tests/core_tests.cpp)
    target_link_libraries(raven-ide-core-tests PRIVATE raven_ide_core)
    add_test(NAME raven-ide-core-tests COMMAND raven-ide-core-tests)
    add_executable(raven-ide-endurance-tests tests/endurance_tests.cpp)
    target_link_libraries(raven-ide-endurance-tests PRIVATE raven_ide_core)
    add_test(NAME raven-ide-endurance-tests COMMAND raven-ide-endurance-tests)
    add_executable(raven-ide-endurance-runtime-tests tests/endurance_runtime_tests.cpp)
    target_link_libraries(raven-ide-endurance-runtime-tests PRIVATE raven_ide_core)
    add_test(NAME raven-ide-endurance-runtime-tests COMMAND raven-ide-endurance-runtime-tests)
endif()
