cmake_minimum_required(VERSION 3.21)

set(CMAKE_CUDA_ARCHITECTURES 86 CACHE STRING "CUDA GPU architectures")

project(simulation_app LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_RUNTIME_LIBRARY Static)
set(CMAKE_AUTOMOC ON)

find_package(CUDAToolkit REQUIRED)
find_package(Qt6 CONFIG REQUIRED COMPONENTS Widgets OpenGLWidgets OpenGL Concurrent Svg)
find_package(glm CONFIG REQUIRED)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

add_executable(simulation_app
    cuda/ClothIntegration.cu
    cuda/ClothIntegration.h
    src/app/main.cpp
    src/asset/AssetConverter.cpp
    src/asset/AssetConverter.h
    src/asset/AssetDataTypes.h
    src/asset/AssetIO.cpp
    src/asset/AssetIO.h
    src/app/ProjectPaths.h
    src/app/SimulationController.cpp
    src/app/SimulationController.h
    src/simulation/SimulationParams.h
    src/simulation/Simulation.h
    src/simulation/pbd/PbdSimulation.cpp
    src/simulation/pbd/PbdSimulation.h
    src/simulation/pd/PdSimulation.cpp
    src/simulation/pd/PdSimulation.h
    src/simulation/pbd/ClothIntegrator.cpp
    src/simulation/pbd/ClothIntegrator.h
    src/simulation/pd/ProjectiveDynamicsSolver.cpp
    src/simulation/pd/ProjectiveDynamicsSolver.h
    src/simulation/pbd/constraints/StretchConstraintSolver.cpp
    src/simulation/pbd/constraints/StretchConstraintSolver.h
    src/simulation/pbd/constraints/BendingConstraintSolver.cpp
    src/simulation/pbd/constraints/BendingConstraintSolver.h
    src/simulation/pbd/constraints/AttachmentConstraintSolver.cpp
    src/simulation/pbd/constraints/AttachmentConstraintSolver.h
    src/simulation/pbd/collision/GroundCollisionSolver.cpp
    src/simulation/pbd/collision/GroundCollisionSolver.h
    src/simulation/bvh/Bvh.h
    src/simulation/bvh/MeshBvhBuilder.cpp
    src/simulation/bvh/MeshBvhBuilder.h
    src/simulation/pbd/collision/CollisionDetector.cpp
    src/simulation/pbd/collision/CollisionDetector.h
    src/simulation/pbd/collision/ClothBodyCollisionSolver.cpp
    src/simulation/pbd/collision/ClothBodyCollisionSolver.h
    src/simulation/pbd/collision/ClothClothCollisionSolver.cpp
    src/simulation/pbd/collision/ClothClothCollisionSolver.h
    src/simulation/pbd/collision/GarmentPrefitSolver.cpp
    src/simulation/pbd/collision/GarmentPrefitSolver.h
    src/utils/MeshGeometryUtils.cpp
    src/utils/MeshGeometryUtils.h
    src/gpu/character/CharacterGpuState.cpp
    src/gpu/character/CharacterGpuDataTypes.h
    src/gpu/character/CharacterGpuState.h
    src/gpu/character/PbdCharacterGpuState.cpp
    src/gpu/character/PbdCharacterGpuState.h
    src/gpu/bvh/BvhBoundsUpdater.cpp
    src/gpu/bvh/BvhBoundsUpdater.h
    src/gpu/cloth/ClothGpuState.cpp
    src/gpu/cloth/ClothGpuDataTypes.h
    src/gpu/cloth/ClothGpuState.h
    src/gpu/cloth/PbdClothGpuState.cpp
    src/gpu/cloth/PbdClothGpuState.h
    src/gpu/cloth/PdClothGpuState.cpp
    src/gpu/cloth/PdClothGpuState.h
    src/gpu/scene/PbdBufferBindings.cpp
    src/gpu/scene/PbdBufferBindings.h
    src/gpu/scene/PbdGpuState.cpp
    src/gpu/scene/PbdGpuState.h
    src/gpu/scene/PdGpuState.cpp
    src/gpu/scene/PdGpuState.h
    src/rendering/BackgroundGradient.cpp
    src/rendering/BackgroundGradient.h
    src/rendering/GroundGridMesh.cpp
    src/rendering/GroundGridMesh.h
    src/gpu/scene/SceneGpuState.cpp
    src/gpu/scene/SceneGpuState.h
    src/rendering/SceneRenderer.cpp
    src/rendering/SceneRenderer.h
    src/ui/Viewport.cpp
    src/ui/Viewport.h
    src/simulation/SceneState.cpp
    src/simulation/SceneState.h
    src/utils/QtUtils.h
    src/utils/BufferUtils.h
    src/utils/CudaUtils.h
    src/utils/FileUtils.h
    src/utils/NumericUtils.h
    src/utils/ShaderUtils.cpp
    src/utils/ShaderUtils.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/AssetBrowserPanel.cpp
    src/ui/AssetBrowserPanel.h
    src/ui/ElidedLabel.h
    src/ui/MotionCard.cpp
    src/ui/MotionCard.h
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
    shaders/simulation_buffer_bindings.glsl
    shaders/rendering/background.vert
    shaders/rendering/background.frag
    shaders/mesh/triangle_normal.comp
    shaders/mesh/vertex_normal.comp
    shaders/character/vertex_position_update.comp
    shaders/character/triangle_geometry_update.comp
    shaders/bvh/body_bounds_update.comp
    shaders/bvh/cloth_bounds_update.comp
    shaders/cloth/integrate_cloth.comp
    shaders/cloth/constraints/stretch.comp
    shaders/cloth/constraints/bending.comp
    shaders/cloth/constraints/attachment.comp
    shaders/collision/ground.comp
    shaders/collision/dispatch_size.comp
    shaders/collision/cloth_cloth/vertex_face_detect.comp
    shaders/collision/cloth_cloth/vertex_face_accumulate.comp
    shaders/collision/cloth_cloth/edge_edge_detect.comp
    shaders/collision/cloth_cloth/edge_edge_accumulate.comp
    shaders/collision/cloth_cloth/contact_correction.glsl
    shaders/collision/cloth_cloth/initial_layer_accumulate.comp
    shaders/collision/cloth_cloth/apply.comp
    shaders/collision/cloth_body/cloth_vertex_body_face_detect.comp
    shaders/collision/cloth_body/cloth_vertex_body_face_accumulate.comp
    shaders/collision/cloth_body/cloth_edge_body_edge_detect.comp
    shaders/collision/cloth_body/cloth_edge_body_edge_accumulate.comp
    shaders/collision/cloth_body/contact_correction.glsl
    shaders/collision/cloth_body/apply.comp
    shaders/bvh/bvh.glsl
    shaders/mesh/primitive_geometry.glsl
    shaders/mesh/position_io.glsl
    shaders/collision/contact_geometry.glsl
    shaders/collision/correction.glsl
    shaders/collision/detection.glsl
    shaders/cloth/setup/garment_prefit.comp
    shaders/cloth/setup/attachment_target_build.comp
    shaders/bvh/body_surface_search.glsl
)

qt_add_resources(simulation_app ui_icons
    PREFIX "/icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/play.svg
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/pause.svg
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/default-pose.svg
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/reset-scene.svg
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/close.svg
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/plus.svg
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/confirm.svg
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/file-import-solid-full.svg
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/motion.png
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/garment.png
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
    src/utils/MeshGeometryUtils.cpp
    src/utils/MeshGeometryUtils.h
)

target_include_directories(simulation_app PRIVATE
    cuda
    src
)

target_include_directories(garment_converter PRIVATE
    src
)

if(MSVC)
    target_compile_options(simulation_app PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/utf-8>
        $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/utf-8>
    )
    target_compile_options(garment_converter PRIVATE /utf-8)
endif()

target_compile_definitions(simulation_app PRIVATE
    PROJECT_ROOT_DIR="${REPO_ROOT}"
)

target_link_libraries(simulation_app PRIVATE
    CUDA::cudart_static
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
