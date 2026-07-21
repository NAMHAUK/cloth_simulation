cmake_minimum_required(VERSION 3.21)

project(simulation_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 CONFIG REQUIRED COMPONENTS Widgets OpenGLWidgets OpenGL Concurrent)
find_package(glm CONFIG REQUIRED)

include(FetchContent)
FetchContent_Declare(
    QtColorWidgets
    GIT_REPOSITORY https://gitlab.com/mattbas/Qt-Color-Widgets.git
    GIT_TAG 5d52e907e50dc88cf969b41cea44665ff6c475b1
    # Only Color2DSlider is needed, so skip the upstream full-library target.
    SOURCE_SUBDIR unused
)
FetchContent_MakeAvailable(QtColorWidgets)

add_library(QtColorWidgets STATIC
    ${qtcolorwidgets_SOURCE_DIR}/include/QtColorWidgets/color_2d_slider.hpp
    ${qtcolorwidgets_SOURCE_DIR}/src/QtColorWidgets/color_2d_slider.cpp
)
target_compile_definitions(QtColorWidgets PUBLIC QTCOLORWIDGETS_STATICALLY_LINKED)
target_include_directories(QtColorWidgets PUBLIC ${qtcolorwidgets_SOURCE_DIR}/include)
target_link_libraries(QtColorWidgets PRIVATE Qt6::Widgets)

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
    src/simulation/SimulationGpuViews.h
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
    src/gpu/character/CharacterGpuResources.cpp
    src/gpu/character/CharacterGpuDataTypes.h
    src/gpu/character/CharacterGpuResources.h
    src/gpu/bvh/BvhDataTypes.h
    src/gpu/bvh/BvhDataTypes.cpp
    src/gpu/bvh/BvhBuildUtils.cpp
    src/gpu/bvh/BvhBuildUtils.h
    src/gpu/bvh/MeshBvhBuilder.cpp
    src/gpu/bvh/MeshBvhBuilder.h
    src/gpu/bvh/BodyBvhBoundsUpdater.cpp
    src/gpu/bvh/BodyBvhBoundsUpdater.h
    src/gpu/bvh/ClothBvhBoundsUpdater.cpp
    src/gpu/bvh/ClothBvhBoundsUpdater.h
    src/gpu/character/CharacterGpuStateUpdater.cpp
    src/gpu/character/CharacterGpuStateUpdater.h
    src/gpu/cloth/ClothGpuResources.cpp
    src/gpu/cloth/ClothGpuDataTypes.h
    src/gpu/cloth/ClothGpuResources.h
    src/gpu/cloth/ClothBvhResources.cpp
    src/gpu/cloth/ClothBvhResources.h
    src/rendering/BackgroundGradient.cpp
    src/rendering/BackgroundGradient.h
    src/rendering/GroundGridMesh.cpp
    src/rendering/GroundGridMesh.h
    src/gpu/scene/SceneGpuState.cpp
    src/gpu/scene/SceneGpuState.h
    src/gpu/scene/CollisionCandidateBuffers.cpp
    src/gpu/scene/CollisionCandidateBuffers.h
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
    shaders/cloth/setup/cloth_body_triangle_id_build.comp
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
    shaders/collision/common/cloth_cloth_candidate_output.glsl
    shaders/cloth/setup/garment_prefit.comp
    shaders/cloth/setup/garment_attachment_target_build.comp
    shaders/body/body_mesh_search.glsl
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
    target_compile_options(QtColorWidgets PRIVATE /Zc:lambda)
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
    QtColorWidgets
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
