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
    src/asset/AssetConverter.cpp
    src/asset/AssetConverter.h
    src/asset/AssetConverterCommands.cpp
    src/asset/AssetConverterCommands.h
    src/asset/AssetDataTypes.h
    src/asset/AssetIO.cpp
    src/asset/AssetIO.h
    src/asset/AssetLoader.cpp
    src/asset/AssetLoader.h
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
    src/simulation/constraints/AttachmentConstraintSolver.cpp
    src/simulation/constraints/AttachmentConstraintSolver.h
    src/simulation/collision/GroundCollisionSolver.cpp
    src/simulation/collision/GroundCollisionSolver.h
    src/simulation/collision/BodyVertexClothFaceCollisionSolver.cpp
    src/simulation/collision/BodyVertexClothFaceCollisionSolver.h
    src/simulation/collision/ClothVertexBodyFaceCollisionSolver.cpp
    src/simulation/collision/ClothVertexBodyFaceCollisionSolver.h
    src/simulation/collision/GarmentPrefitSolver.cpp
    src/simulation/collision/GarmentPrefitSolver.h
    src/asset/MeshGeometryUtils.cpp
    src/asset/MeshGeometryUtils.h
    src/gpu/body/CharacterGpuResources.cpp
    src/gpu/body/CharacterGpuDataTypes.h
    src/gpu/body/CharacterGpuResources.h
    src/gpu/body/bvh/MeshBvhBuilder.cpp
    src/gpu/body/bvh/BvhDataTypes.h
    src/gpu/body/bvh/MeshBvhBuilder.h
    src/gpu/body/bvh/VertexBvhBuilder.cpp
    src/gpu/body/bvh/VertexBvhBuilder.h
    src/gpu/body/bvh/MeshBvhBoundsUpdater.cpp
    src/gpu/body/bvh/MeshBvhBoundsUpdater.h
    src/gpu/body/bvh/VertexBvhBoundsUpdater.cpp
    src/gpu/body/bvh/VertexBvhBoundsUpdater.h
    src/gpu/body/CharacterGpuStateUpdater.cpp
    src/gpu/body/CharacterGpuStateUpdater.h
    src/gpu/cloth/ClothGpuResources.cpp
    src/gpu/cloth/ClothGpuDataTypes.h
    src/gpu/cloth/ClothGpuResources.h
    src/rendering/BackgroundGradient.cpp
    src/rendering/BackgroundGradient.h
    src/rendering/GroundGridMesh.cpp
    src/rendering/GroundGridMesh.h
    src/gpu/scene/SceneGpuState.cpp
    src/gpu/scene/SceneGpuState.h
    src/gpu/scene/AttachmentTargetBuilder.cpp
    src/gpu/scene/AttachmentTargetBuilder.h
    src/gpu/scene/NormalUpdater.cpp
    src/gpu/scene/NormalUpdater.h
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
    src/utils/GpuElapsedTimer.cpp
    src/utils/GpuElapsedTimer.h
    src/utils/NumericUtils.h
    src/utils/ShaderUtils.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/AssetBrowserPanel.cpp
    src/ui/AssetBrowserPanel.h
    src/ui/GarmentPlacementPanel.cpp
    src/ui/GarmentPlacementPanel.h
    shaders/rendering/viewer.vert
    shaders/rendering/viewer.frag
    shaders/rendering/background.vert
    shaders/rendering/background.frag
    shaders/gpu/triangle_normal.comp
    shaders/gpu/vertex_normal.comp
    shaders/gpu/character_vertex_position_update.comp
    shaders/gpu/character_triangle_geometry_update.comp
    shaders/gpu/character_bvh_bounds_update.comp
    shaders/gpu/character_vertex_bvh_bounds_update.comp
    shaders/simulation/cloth_external_force.comp
    shaders/simulation/cloth_stretch_constraint.comp
    shaders/simulation/cloth_bending_constraint.comp
    shaders/simulation/cloth_attachment_constraint.comp
    shaders/simulation/collision/cloth_ground_collision.comp
    shaders/simulation/collision/body_vertex_cloth_face_pair_generate.comp
    shaders/simulation/collision/body_vertex_cloth_face_pair_accumulate.comp
    shaders/simulation/collision/body_vertex_cloth_face_pair_apply.comp
    shaders/simulation/collision/cloth_vertex_body_face_collision.comp
    shaders/simulation/garment_prefit.comp
    shaders/simulation/collision/cloth_collision_common.glsl
    shaders/simulation/garment_attachment_target_build.comp
)

add_executable(garment_converter
    tools/garment_converter/main.cpp
    tools/garment_converter/GarmentConverter.cpp
    tools/garment_converter/GarmentConverter.h
    src/asset/AssetDataTypes.h
    src/asset/AssetIO.cpp
    src/asset/AssetIO.h
    src/asset/MeshGeometryUtils.cpp
    src/asset/MeshGeometryUtils.h
)

target_include_directories(simulation_app PRIVATE
    src
)

target_include_directories(garment_converter PRIVATE
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

target_link_libraries(garment_converter PRIVATE
    glm::glm
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
