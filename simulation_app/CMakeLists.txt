cmake_minimum_required(VERSION 3.21)

project(simulation_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 CONFIG REQUIRED COMPONENTS Widgets OpenGLWidgets OpenGL)
find_package(glm CONFIG REQUIRED)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

add_executable(simulation_app
    src/app/main.cpp
    src/app/ProjectPaths.h
    src/assets/GarmentAsset.cpp
    src/assets/GarmentAsset.h
    src/assets/MotionAsset.cpp
    src/assets/MotionAsset.h
    src/gpu/CharacterGpuState.cpp
    src/gpu/CharacterGpuState.h
    src/gpu/ClothGpuState.cpp
    src/gpu/ClothGpuState.h
    src/gpu/GridGpuState.cpp
    src/gpu/GridGpuState.h
    src/gpu/SimulationGpuState.cpp
    src/gpu/SimulationGpuState.h
    src/rendering/SimulationViewport.cpp
    src/rendering/SimulationViewport.h
    src/rendering/ViewerShaderProgram.cpp
    src/rendering/ViewerShaderProgram.h
    src/simulation/SimulationScene.cpp
    src/simulation/SimulationScene.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/MotionBrowserPanel.cpp
    src/ui/MotionBrowserPanel.h
    shaders/viewer.vert
    shaders/viewer.frag
)

target_include_directories(simulation_app PRIVATE
    src
)

target_compile_definitions(simulation_app PRIVATE
    PROJECT_ROOT_DIR="${REPO_ROOT}"
)

target_link_libraries(simulation_app PRIVATE
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::OpenGL
    glm::glm
    opengl32
)

if(WIN32)
    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_BIN_DIR}" REQUIRED)

    add_custom_command(TARGET simulation_app POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            "$<IF:$<CONFIG:Debug>,--debug,--release>"
            "$<TARGET_FILE:simulation_app>"
        COMMENT "Deploying Qt runtime dependencies"
    )
endif()
