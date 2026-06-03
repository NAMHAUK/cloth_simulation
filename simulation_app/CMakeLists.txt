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
    src/asset/AssetLoader.cpp
    src/asset/AssetLoader.h
    src/asset/MotionConverter.cpp
    src/asset/MotionConverter.h
    src/app/ProjectPaths.h
    src/simulation/SimulationController.cpp
    src/simulation/SimulationController.h
    src/simulation/forces/SimulationForceField.cpp
    src/simulation/forces/SimulationForceField.h
    src/simulation/SimulationSettings.h
    src/simulation/forces/ExternalForceSolver.cpp
    src/simulation/forces/ExternalForceSolver.h
    src/simulation/constraints/StretchConstraintSolver.cpp
    src/simulation/constraints/StretchConstraintSolver.h
    src/simulation/constraints/BendingConstraintSolver.cpp
    src/simulation/constraints/BendingConstraintSolver.h
    src/simulation/collision/GroundCollisionSolver.cpp
    src/simulation/collision/GroundCollisionSolver.h
    src/asset/GarmentAsset.cpp
    src/asset/GarmentAsset.h
    src/asset/MeshGeometryUtils.cpp
    src/asset/MeshGeometryUtils.h
    src/asset/MotionAsset.cpp
    src/asset/MotionAsset.h
    src/gpu/body/CharacterGpuResources.cpp
    src/gpu/body/CharacterGpuResources.h
    src/gpu/cloth/ClothGpuResources.cpp
    src/gpu/cloth/ClothGpuResources.h
    src/rendering/BackgroundGradient.cpp
    src/rendering/BackgroundGradient.h
    src/rendering/GroundGridMesh.cpp
    src/rendering/GroundGridMesh.h
    src/gpu/scene/SceneGpuState.cpp
    src/gpu/scene/SceneGpuState.h
    src/gpu/scene/NormalUpdater.cpp
    src/gpu/scene/NormalUpdater.h
    src/gpu/scene/MeshBufferResources.h
    src/rendering/RenderPipeline.cpp
    src/rendering/RenderPipeline.h
    src/ui/SceneViewport.cpp
    src/ui/SceneViewport.h
    src/rendering/SceneRenderShader.cpp
    src/rendering/SceneRenderShader.h
    src/simulation/SimulationPipeline.cpp
    src/simulation/SimulationPipeline.h
    src/scene/SceneState.cpp
    src/scene/SceneState.h
    src/utils/QtUtils.h
    src/utils/BufferUtils.h
    src/utils/FileUtils.h
    src/utils/ShaderUtils.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/MotionBrowserPanel.cpp
    src/ui/MotionBrowserPanel.h
    shaders/viewer.vert
    shaders/viewer.frag
    shaders/background.vert
    shaders/background.frag
    shaders/triangle_normal.comp
    shaders/vertex_normal.comp
    shaders/cloth_external_force.comp
    shaders/cloth_stretch_constraint.comp
    shaders/cloth_bending_constraint.comp
    shaders/cloth_ground_collision.comp
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
