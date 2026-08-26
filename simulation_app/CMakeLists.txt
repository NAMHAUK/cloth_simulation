cmake_minimum_required(VERSION 3.21)

project(simulation_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 CONFIG REQUIRED COMPONENTS Widgets OpenGLWidgets OpenGL Concurrent Svg)
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
    src/app/SimulationController.cpp
    src/app/SimulationController.h
    src/simulation/Kinematics.cpp
    src/simulation/Kinematics.h
    src/gpu/scene/SimulationGpuView.h
    src/simulation/SimulationParams.h
    src/simulation/ExternalForceSolver.cpp
    src/simulation/ExternalForceSolver.h
    src/simulation/constraints/StretchConstraintSolver.cpp
    src/simulation/constraints/StretchConstraintSolver.h
    src/simulation/constraints/BendingConstraintSolver.cpp
    src/simulation/constraints/BendingConstraintSolver.h
    src/simulation/constraints/AttachmentConstraintSolver.cpp
    src/simulation/constraints/AttachmentConstraintSolver.h
    src/simulation/collision/GroundCollisionSolver.cpp
    src/simulation/collision/GroundCollisionSolver.h
    src/simulation/collision/Bvh.h
    src/simulation/collision/MeshBvhBuilder.cpp
    src/simulation/collision/MeshBvhBuilder.h
    src/simulation/collision/ClothBodyCollisionDetector.cpp
    src/simulation/collision/ClothBodyCollisionDetector.h
    src/simulation/collision/ClothBodyCollisionSolver.cpp
    src/simulation/collision/ClothBodyCollisionSolver.h
    src/simulation/collision/ClothClothCollisionDetector.cpp
    src/simulation/collision/ClothClothCollisionDetector.h
    src/simulation/collision/ClothClothCollisionSolver.cpp
    src/simulation/collision/ClothClothCollisionSolver.h
    src/simulation/collision/GarmentPrefitSolver.cpp
    src/simulation/collision/GarmentPrefitSolver.h
    src/asset/MeshGeometryUtils.cpp
    src/asset/MeshGeometryUtils.h
    src/gpu/character/CharacterGpuState.cpp
    src/gpu/character/CharacterGpuDataTypes.h
    src/gpu/character/CharacterGpuState.h
    src/gpu/bvh/BvhBufferView.h
    src/gpu/bvh/BodyBvhBoundsUpdater.cpp
    src/gpu/bvh/BodyBvhBoundsUpdater.h
    src/gpu/bvh/ClothBvhBoundsUpdater.cpp
    src/gpu/bvh/ClothBvhBoundsUpdater.h
    src/gpu/cloth/ClothGpuState.cpp
    src/gpu/cloth/ClothGpuDataTypes.h
    src/gpu/cloth/ClothGpuState.h
    src/rendering/BackgroundGradient.cpp
    src/rendering/BackgroundGradient.h
    src/rendering/GroundGridMesh.cpp
    src/rendering/GroundGridMesh.h
    src/gpu/scene/SceneGpuState.cpp
    src/gpu/scene/SceneGpuState.h
    src/rendering/RenderPipeline.cpp
    src/rendering/RenderPipeline.h
    src/ui/Viewport.cpp
    src/ui/Viewport.h
    src/rendering/SceneRenderShader.cpp
    src/rendering/SceneRenderShader.h
    src/simulation/SimulationPipeline.cpp
    src/simulation/SimulationPipeline.h
    src/simulation/SceneState.cpp
    src/simulation/SceneState.h
    src/utils/QtUtils.h
    src/utils/BufferUtils.h
    src/utils/FileUtils.h
    src/utils/NumericUtils.h
    src/utils/ShaderUtils.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/AssetBrowserPanel.cpp
    src/ui/AssetBrowserPanel.h
    src/ui/GarmentColorPanel.cpp
    src/ui/GarmentColorPanel.h
    src/ui/GarmentCardsPanel.cpp
    src/ui/GarmentCardsPanel.h
    src/ui/PlacementPanel.cpp
    src/ui/PlacementPanel.h
    src/ui/PlacementController.cpp
    src/ui/PlacementController.h
    shaders/rendering/viewer.vert
    shaders/rendering/viewer.frag
    shaders/rendering/background.vert
    shaders/rendering/background.frag
    shaders/mesh/triangle_normal.comp
    shaders/mesh/vertex_normal.comp
    shaders/character/character_vertex_position_update.comp
    shaders/character/character_triangle_geometry_update.comp
    shaders/body/body_bvh_bounds_update.comp
    shaders/cloth/cloth_external_force.comp
    shaders/cloth/cloth_bvh_bounds_update.comp
    shaders/cloth/constraints/cloth_stretch_constraint.comp
    shaders/cloth/constraints/cloth_bending_constraint.comp
    shaders/cloth/constraints/cloth_attachment_constraint.comp
    shaders/collision/cloth_ground_collision.comp
    shaders/collision/collision_dispatch_size.comp
    shaders/collision/cloth_cloth_vertex_face_detect.comp
    shaders/collision/cloth_cloth_vertex_face_accumulate.comp
    shaders/collision/cloth_cloth_initial_layer_accumulate.comp
    shaders/cloth/setup/cloth_body_triangle_index_build.comp
    shaders/collision/cloth_cloth_collision_apply.comp
    shaders/collision/cloth_vertex_body_face_detect.comp
    shaders/collision/cloth_vertex_body_face_accumulate.comp
    shaders/collision/body_vertex_cloth_face_detect.comp
    shaders/collision/body_vertex_cloth_face_accumulate.comp
    shaders/collision/cloth_edge_body_edge_detect.comp
    shaders/collision/cloth_edge_body_edge_accumulate.comp
    shaders/collision/cloth_body_collision_apply.comp
    shaders/mesh/bvh_common.glsl
    shaders/mesh/primitive_geometry.glsl
    shaders/mesh/position_io.glsl
    shaders/collision/common/collision_geometry_common.glsl
    shaders/collision/common/collision_correction_common.glsl
    shaders/collision/common/collision_candidate_output.glsl
    shaders/cloth/setup/garment_prefit.comp
    shaders/cloth/setup/garment_attachment_target_build.comp
    shaders/body/body_mesh_search.glsl
)

qt_add_resources(simulation_app ui_icons
    PREFIX "/icons"
    BASE "${REPO_ROOT}/data/sources/icon"
    FILES
        ${REPO_ROOT}/data/sources/icon/play.svg
        ${REPO_ROOT}/data/sources/icon/pause.svg
        ${REPO_ROOT}/data/sources/icon/default-pose.svg
        ${REPO_ROOT}/data/sources/icon/reset-scene.svg
        ${REPO_ROOT}/data/sources/icon/close.svg
        ${REPO_ROOT}/data/sources/icon/plus.svg
        ${REPO_ROOT}/data/sources/icon/confirm.svg
        ${REPO_ROOT}/data/sources/icon/file-import-solid-full.svg
)

qt_add_resources(simulation_app ui_styles
    PREFIX "/styles"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/styles"
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/styles/app.qss
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

if(MSVC)
    target_compile_options(simulation_app PRIVATE /utf-8)
    target_compile_options(garment_converter PRIVATE /utf-8)
endif()

target_compile_definitions(simulation_app PRIVATE
    PROJECT_ROOT_DIR="${REPO_ROOT}"
)

target_link_libraries(simulation_app PRIVATE
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::OpenGL
    Qt6::Concurrent
    Qt6::Svg
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
