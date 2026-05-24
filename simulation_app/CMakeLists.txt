cmake_minimum_required(VERSION 3.21)

project(simulation_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 CONFIG REQUIRED COMPONENTS Widgets OpenGLWidgets OpenGL Concurrent)
find_package(glm CONFIG REQUIRED)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

add_executable(simulation_app
    src/app/main.cpp
    src/app/AssetLoader.cpp
    src/app/AssetLoader.h
    src/app/MotionConverter.cpp
    src/app/MotionConverter.h
    src/app/ProjectPaths.h
    src/app/AppController.cpp
    src/app/AppController.h
    src/io/GarmentAsset.cpp
    src/io/GarmentAsset.h
    src/io/MotionAsset.cpp
    src/io/MotionAsset.h
    src/gpu/body/CharacterGpuResources.cpp
    src/gpu/body/CharacterGpuResources.h
    src/gpu/cloth/ClothGpuResources.cpp
    src/gpu/cloth/ClothGpuResources.h
    src/rendering/GroundMesh.cpp
    src/rendering/GroundMesh.h
    src/gpu/scene/SceneGpuResources.cpp
    src/gpu/scene/SceneGpuResources.h
    src/rendering/SceneRenderer.cpp
    src/rendering/SceneRenderer.h
    src/ui/SceneViewport.cpp
    src/ui/SceneViewport.h
    src/rendering/SceneRenderShader.cpp
    src/rendering/SceneRenderShader.h
    src/simulation/SimulationEngine.cpp
    src/simulation/SimulationEngine.h
    src/scene/SceneState.cpp
    src/scene/SceneState.h
    src/support/QtHelpers.h
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
    Qt6::Concurrent
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
