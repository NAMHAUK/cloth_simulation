#pragma once

#include <cstdint>
#include <vector>

#include <vector_types.h>

struct AttachmentConstraint;
struct GarmentObject;
struct MappedMeshBuffers;
struct TriangleVertexIndices;
class ClothGpuState;
template <typename Constraint>
struct ConstraintSet;

void initialize_constraints(AttachmentConstraint* constraints,
                            const std::vector<GarmentObject>& garments,
                            const ClothGpuState& cloth_state);

void find_attachment_targets(const ConstraintSet<AttachmentConstraint>& constraints,
                             const float4* cloth_positions,
                             const MappedMeshBuffers& character_buffers,
                             std::uint32_t body_triangle_count);
void project_constraints(const ConstraintSet<AttachmentConstraint>& constraints,
                         const float4* body_positions,
                         const TriangleVertexIndices* body_triangles);
