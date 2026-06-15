#pragma once

#include "gpu/collision/BvhDataTypes.h"
#include "scene/SceneState.h"

#include <cstdint>
#include <vector>

namespace attachment_builder {

std::vector<GarmentAttachmentConstraint> build_garment_attachment_targets(
    const GarmentObject& garment,
    const CharacterMesh& character_mesh,
    std::uint32_t character_frame_index,
    const std::vector<std::uint32_t>& character_triangle_indices,
    const std::vector<MeshBvhNode>& character_bvh_nodes);

}
