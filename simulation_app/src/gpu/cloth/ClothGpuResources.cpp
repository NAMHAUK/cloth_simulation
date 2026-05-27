#include "gpu/cloth/ClothGpuResources.h"

#include "gpu/scene/MeshBufferResources.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <utility>

namespace {
constexpr std::uint32_t position_components = 3;
constexpr std::uint32_t normal_components = 4;

struct CompactGarmentData final {
    const GarmentObject* garment = nullptr;
    GarmentGpuData gpu_data;
};

GLsizeiptr byte_size(std::uint32_t count, std::uint32_t component_count, std::size_t component_size)
{
    return static_cast<GLsizeiptr>(
        static_cast<std::size_t>(count) * static_cast<std::size_t>(component_count) * component_size
    );
}

GLsizeiptr scalar_byte_size(std::uint32_t count, std::size_t component_size)
{
    return static_cast<GLsizeiptr>(static_cast<std::size_t>(count) * component_size);
}

std::uint32_t grown_capacity(std::uint32_t current_capacity, std::uint32_t required_capacity)
{
    if (required_capacity <= current_capacity) {
        return current_capacity;
    }
    if (current_capacity == 0) {
        return required_capacity;
    }

    const std::uint32_t doubled_capacity = current_capacity > (std::numeric_limits<std::uint32_t>::max() / 2u)
        ? std::numeric_limits<std::uint32_t>::max()
        : current_capacity * 2u;
    return std::max(required_capacity, doubled_capacity);
}

bool is_uploadable_mesh(const GarmentMesh& garment_mesh)
{
    const std::uint32_t vertex_count = static_cast<std::uint32_t>(garment_mesh.vertices.size() / position_components);
    return !garment_mesh.vertices.empty() &&
           !garment_mesh.indices.empty() &&
           garment_mesh.vertices.size() % position_components  == 0u &&
           garment_mesh.indices.size() % 3u == 0u &&
           garment_mesh.adjacency.is_valid(vertex_count);
}

ClothBufferSet create_buffer_set(const ClothBufferElementCounts& allocated_elements, QOpenGLFunctions_4_5_Core& gl)
{
    ClothBufferSet buffers;
    gl.glCreateVertexArrays(1, &buffers.vao);
    gl.glCreateBuffers(1, &buffers.rest_position);
    gl.glCreateBuffers(1, &buffers.current_position);
    gl.glCreateBuffers(1, &buffers.previous_position);
    gl.glCreateBuffers(1, &buffers.index);
    gl.glCreateBuffers(1, &buffers.adjacency_offset);
    gl.glCreateBuffers(1, &buffers.adjacency_triangle);
    gl.glCreateBuffers(1, &buffers.triangle_normal);
    gl.glCreateBuffers(1, &buffers.vertex_normal);

    gl.glNamedBufferData(buffers.rest_position,
                         byte_size(allocated_elements.vertex, position_components , sizeof(float)),
                         nullptr,
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers.current_position,
                         byte_size(allocated_elements.vertex, position_components , sizeof(float)),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers.previous_position,
                         byte_size(allocated_elements.vertex, position_components , sizeof(float)),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers.index,
                         scalar_byte_size(allocated_elements.index, sizeof(std::uint32_t)),
                         nullptr,
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers.adjacency_offset,
                         scalar_byte_size(allocated_elements.vertex + 1u, sizeof(std::uint32_t)),
                         nullptr,
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers.adjacency_triangle,
                         scalar_byte_size(allocated_elements.adjacency_entry, sizeof(std::uint32_t)),
                         nullptr,
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers.triangle_normal,
                         byte_size(allocated_elements.triangle, normal_components , sizeof(float)),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers.vertex_normal,
                         byte_size(allocated_elements.vertex, normal_components , sizeof(float)),
                         nullptr,
                         GL_DYNAMIC_DRAW);

    return buffers;
}

GarmentGpuData make_garment_gpu_data(const GarmentObject& garment, const ClothBufferElementCounts& offsets)
{
    const VertexTriangleAdjacency& adjacency = garment.mesh.adjacency;

    GarmentGpuData gpu_data;
    gpu_data.id = garment.id;
    gpu_data.vertex_offset = offsets.vertex;
    gpu_data.vertex_count = static_cast<std::uint32_t>(garment.mesh.vertices.size() / position_components);
    gpu_data.index_offset = offsets.index;
    gpu_data.index_count = static_cast<std::uint32_t>(garment.mesh.indices.size());
    gpu_data.triangle_offset = offsets.triangle;
    gpu_data.triangle_count = adjacency.triangle_count;
    gpu_data.adjacency_entry_offset = offsets.adjacency_entry;
    gpu_data.adjacency_entry_count = static_cast<std::uint32_t>(adjacency.triangles.size());
    return gpu_data;
}

bool assign_compact_buffer_ranges(const std::vector<GarmentObject>& garments,
                                  std::vector<CompactGarmentData>& compact_garments,
                                  ClothBufferElementCounts& compact_element_counts)
{
    compact_garments.clear();
    compact_garments.reserve(garments.size());
    compact_element_counts = {};

    for (const GarmentObject& garment : garments) {
        if (!is_uploadable_mesh(garment.mesh)) {
            std::cerr << "Skipping invalid garment mesh for garment id " << garment.id << ".\n";
            continue;
        }

        CompactGarmentData compact_garment;
        compact_garment.garment = &garment;
        compact_garment.gpu_data = make_garment_gpu_data(garment, compact_element_counts);

        compact_element_counts.vertex += compact_garment.gpu_data.vertex_count;
        compact_element_counts.index += compact_garment.gpu_data.index_count;
        compact_element_counts.triangle += compact_garment.gpu_data.triangle_count;
        compact_element_counts.adjacency_entry += compact_garment.gpu_data.adjacency_entry_count;

        compact_garments.push_back(std::move(compact_garment));
    }

    return !compact_garments.empty();
}

void append_global_topology(const GarmentObject& garment,
                            const GarmentGpuData& gpu_data,
                            std::vector<std::uint32_t>& global_indices,
                            std::vector<std::uint32_t>& global_adjacency_offsets,
                            std::vector<std::uint32_t>& global_adjacency_triangles)
{
    const VertexTriangleAdjacency& adjacency = garment.mesh.adjacency;

    for (std::uint32_t local_index : garment.mesh.indices) {
        global_indices.push_back(gpu_data.vertex_offset + local_index);
    }

    for (std::uint32_t local_vertex = 0; local_vertex <= gpu_data.vertex_count; ++local_vertex) {
        const std::uint32_t global_vertex = gpu_data.vertex_offset + local_vertex;
        global_adjacency_offsets[global_vertex] = gpu_data.adjacency_entry_offset + adjacency.offsets[local_vertex];
    }

    for (std::uint32_t local_triangle : adjacency.triangles) {
        global_adjacency_triangles.push_back(gpu_data.triangle_offset + local_triangle);
    }
}

bool copy_position_buffers(const GarmentGpuData& old_data,
                           const GarmentGpuData& next_data,
                           const ClothBufferSet& old_buffers,
                           const ClothBufferSet& next_buffers,
                           QOpenGLFunctions_4_5_Core& gl)
{
    if (old_data.vertex_count != next_data.vertex_count ||
        old_buffers.rest_position == 0 ||
        old_buffers.current_position == 0 ||
        old_buffers.previous_position == 0 ||
        next_buffers.rest_position == 0 ||
        next_buffers.current_position == 0 ||
        next_buffers.previous_position == 0) {
        return false;
    }

    const GLsizeiptr old_offset_bytes = byte_size(old_data.vertex_offset, position_components, sizeof(float));
    const GLsizeiptr next_offset_bytes = byte_size(next_data.vertex_offset, position_components, sizeof(float));
    const GLsizeiptr position_size_bytes = byte_size(next_data.vertex_count, position_components, sizeof(float));

    gl.glCopyNamedBufferSubData(old_buffers.rest_position,
                                next_buffers.rest_position,
                                old_offset_bytes,
                                next_offset_bytes,
                                position_size_bytes);
    gl.glCopyNamedBufferSubData(old_buffers.current_position,
                                next_buffers.current_position,
                                old_offset_bytes,
                                next_offset_bytes,
                                position_size_bytes);
    gl.glCopyNamedBufferSubData(old_buffers.previous_position,
                                next_buffers.previous_position,
                                old_offset_bytes,
                                next_offset_bytes,
                                position_size_bytes);
    return true;
}
}

ClothGpuResources::ClothGpuResources(ClothGpuResources&& other) noexcept
{
    take_gpu_resources_from(other);
}

void ClothGpuResources::take_gpu_resources_from(ClothGpuResources& other) noexcept
{
    buffers_ = other.buffers_;
    garments_ = std::move(other.garments_);
    used_elements_ = other.used_elements_;
    allocated_elements_ = other.allocated_elements_;

    other.reset_resources();
}

void ClothGpuResources::reset_resources() noexcept
{
    buffers_ = {};
    garments_.clear();
    used_elements_ = {};
    allocated_elements_ = {};
}

void ClothGpuResources::release(QOpenGLFunctions_4_5_Core& gl)
{
    delete_gpu_objects(gl);
    reset_resources();
}

MeshTopologyResources ClothGpuResources::mesh_topology_resources() const
{
    MeshTopologyResources topology;
    topology.position_buffer = buffers_.current_position;
    topology.index_buffer = buffers_.index;
    topology.adjacency_offset_buffer = buffers_.adjacency_offset;
    topology.adjacency_triangle_buffer = buffers_.adjacency_triangle;
    topology.position_component_offset = 0;
    topology.vertex_count = used_elements_.vertex;
    topology.triangle_count = used_elements_.triangle;
    return topology;
}

MeshNormalResources ClothGpuResources::mesh_normal_resources() const
{
    MeshNormalResources normals;
    normals.triangle_normal_buffer = buffers_.triangle_normal;
    normals.vertex_normal_buffer = buffers_.vertex_normal;
    return normals;
}

void ClothGpuResources::sync_garments(const std::vector<GarmentObject>& garments, QOpenGLFunctions_4_5_Core& gl)
{
    if (garments.empty()) {
        release(gl);
        return;
    }

    bool needs_compaction = false;
    for (const GarmentGpuData& gpu_data : garments_) {
        const auto iter = std::find_if(garments.begin(), garments.end(),
            [&gpu_data](const GarmentObject& garment) {
                return garment.id == gpu_data.id;
            }
        );

        if (iter == garments.end()) {
            needs_compaction = true;
            break;
        }
    }

    if (needs_compaction) {
        rebuild_compact_buffers(garments, gl);
        return;
    }

    for (const GarmentObject& garment : garments) {
        if (find_garment_data(garment.id) == nullptr) {
            append_garment(garment, gl);
        }
    }
}

// GPU buffer 관리 //

// garment이 삭제되어 빈 공간이 생길 경우 -> 남은 garment들을 빈틈 없이 연속적으로 새 buffer에 재배치 (compaction)
void ClothGpuResources::rebuild_compact_buffers(const std::vector<GarmentObject>& garments, QOpenGLFunctions_4_5_Core& gl)
{
    if (garments.empty()) {
        release(gl);
        return;
    }

    std::vector<CompactGarmentData> compact_garments;
    ClothBufferElementCounts compact_element_counts;

    // 새로운 GPU buffer에서 garment들이 사용할 구간 배정 (빈틈 없이 연속적으로)
    if (!assign_compact_buffer_ranges(garments, compact_garments, compact_element_counts)) {
        release(gl);
        return;
    }

    const ClothBufferSet old_buffer_set = buffers_;
    const std::vector<GarmentGpuData>& old_garments = garments_;

    // 새롭게 할당할 GPU buffer 생성
    ClothBufferSet new_buffer_set = create_buffer_set(compact_element_counts, gl);

    // 새 buffer에 upload할 topology 데이터를 담을 임시 배열 준비
    std::vector<std::uint32_t> global_indices;
    std::vector<std::uint32_t> global_adjacency_offsets(static_cast<std::size_t>(compact_element_counts.vertex) + 1u, 0);
    std::vector<std::uint32_t> global_adjacency_triangles;

    global_indices.reserve(compact_element_counts.index);
    global_adjacency_triangles.reserve(compact_element_counts.adjacency_entry);

    // 각 garment의 position buffer를 새 offset으로 복사 & topology를 global 값으로 변환
    for (const CompactGarmentData& compact_garment : compact_garments) {
        const GarmentObject& garment = *compact_garment.garment;
        const GarmentGpuData& gpu_data = compact_garment.gpu_data;

        // 기존 buffer에서 각 garment의 위치를 찾음 -> 새 buffer에 (rest/current/previous_position)값 복사
        const auto old_iter = std::find_if(old_garments.begin(), old_garments.end(),
            [&gpu_data](const GarmentGpuData& old_data) {
                return old_data.id == gpu_data.id;
            }
        );

        if (old_iter == old_garments.end() || !copy_position_buffers(*old_iter, gpu_data, old_buffer_set, new_buffer_set, gl)) {
            std::cerr << "Cannot compact garment position buffers for garment id " << gpu_data.id << ".\n";
            delete_buffer_set(new_buffer_set, gl);
            return;
        }

        // 새 buffer에서 사용할 값 (global index/adjacency offset/adjacency triangle) 계산 -> 임시 배열에 저장
        append_global_topology(garment, gpu_data, global_indices, global_adjacency_offsets, global_adjacency_triangles);
    }

    // global index/adjacency offset/adjacency triangle 값을 새 buffer에 upload
    gl.glNamedBufferSubData(new_buffer_set.index,
                            0,
                            scalar_byte_size(static_cast<std::uint32_t>(global_indices.size()), sizeof(std::uint32_t)),
                            global_indices.data());
    gl.glNamedBufferSubData(new_buffer_set.adjacency_offset,
                            0,
                            scalar_byte_size(static_cast<std::uint32_t>(global_adjacency_offsets.size()), sizeof(std::uint32_t)),
                            global_adjacency_offsets.data());
    gl.glNamedBufferSubData(new_buffer_set.adjacency_triangle,
                            0,
                            scalar_byte_size(static_cast<std::uint32_t>(global_adjacency_triangles.size()), sizeof(std::uint32_t)),
                            global_adjacency_triangles.data());

    // 기존 buffer 삭제 & 새 buffer로 교체
    delete_gpu_objects(gl);
    buffers_ = new_buffer_set;

    garments_.clear();
    garments_.reserve(compact_garments.size());
    for (const CompactGarmentData& compact_garment : compact_garments) {
        garments_.push_back(compact_garment.gpu_data);
    }

    used_elements_ = compact_element_counts;
    allocated_elements_ = compact_element_counts;

    configure_vao(gl);
}

// 새 garment 추가
bool ClothGpuResources::append_garment(const GarmentObject& garment, QOpenGLFunctions_4_5_Core& gl)
{
    if (!is_uploadable_mesh(garment.mesh)) {
        std::cerr << "Cannot upload invalid garment mesh for garment id " << garment.id << ".\n";
        return false;
    }

    // 이 upload된 garment면 기존 GPU buffer 사용
    if (find_garment_data(garment.id) != nullptr) {
        return true;
    }

    const VertexTriangleAdjacency& adjacency = garment.mesh.adjacency;

    // 현재 GPU buffer에 새 garment가 사용할 구간이 남아있는지 확인 -> 부족하면 새 buffer 할당
    GarmentGpuData gpu_data = make_garment_gpu_data(garment, used_elements_);

    ClothBufferElementCounts required_elements = used_elements_;
    required_elements.vertex += gpu_data.vertex_count;
    required_elements.index += gpu_data.index_count;
    required_elements.triangle += gpu_data.triangle_count;
    required_elements.adjacency_entry += gpu_data.adjacency_entry_count;

    ensure_capacity(required_elements, gl);

    // 새 garment의 topology 데이터를 새 buffer의 할당된 구간에 맞게 변환 -> 새 buffer에 upload
    const GLsizeiptr position_offset_bytes = byte_size(gpu_data.vertex_offset, position_components , sizeof(float));
    const GLsizeiptr position_size_bytes = byte_size(gpu_data.vertex_count, position_components , sizeof(float));
    gl.glNamedBufferSubData(buffers_.rest_position, position_offset_bytes, position_size_bytes, garment.mesh.vertices.data());
    gl.glNamedBufferSubData(buffers_.current_position, position_offset_bytes, position_size_bytes, garment.mesh.vertices.data());
    gl.glNamedBufferSubData(buffers_.previous_position, position_offset_bytes, position_size_bytes, garment.mesh.vertices.data());

    std::vector<std::uint32_t> global_indices;
    global_indices.reserve(garment.mesh.indices.size());
    for (std::uint32_t local_index : garment.mesh.indices) {
        global_indices.push_back(gpu_data.vertex_offset + local_index);
    }

    gl.glNamedBufferSubData(buffers_.index,
                            scalar_byte_size(gpu_data.index_offset, sizeof(std::uint32_t)),
                            scalar_byte_size(static_cast<std::uint32_t>(global_indices.size()), sizeof(std::uint32_t)),
                            global_indices.data());

    std::vector<std::uint32_t> global_adjacency_offsets;
    global_adjacency_offsets.reserve(adjacency.offsets.size());
    for (std::uint32_t local_offset : adjacency.offsets) {
        global_adjacency_offsets.push_back(gpu_data.adjacency_entry_offset + local_offset);
    }

    gl.glNamedBufferSubData(buffers_.adjacency_offset,
                            scalar_byte_size(gpu_data.vertex_offset, sizeof(std::uint32_t)),
                            scalar_byte_size(static_cast<std::uint32_t>(global_adjacency_offsets.size()), sizeof(std::uint32_t)),
                            global_adjacency_offsets.data());

    std::vector<std::uint32_t> global_adjacency_triangles;
    global_adjacency_triangles.reserve(adjacency.triangles.size());
    for (std::uint32_t local_triangle : adjacency.triangles) {
        global_adjacency_triangles.push_back(gpu_data.triangle_offset + local_triangle);
    }

    gl.glNamedBufferSubData(buffers_.adjacency_triangle,
                            scalar_byte_size(gpu_data.adjacency_entry_offset, sizeof(std::uint32_t)),
                            scalar_byte_size(static_cast<std::uint32_t>(global_adjacency_triangles.size()), sizeof(std::uint32_t)),
                            global_adjacency_triangles.data());

    // garment GPU data 등록, buffer 사용량 update
    garments_.push_back(gpu_data);
    used_elements_ = required_elements;
    return true;
}

void ClothGpuResources::ensure_capacity(const ClothBufferElementCounts& required_elements, QOpenGLFunctions_4_5_Core& gl)
{
    if (required_elements.vertex <= allocated_elements_.vertex &&
        required_elements.index <= allocated_elements_.index &&
        required_elements.triangle <= allocated_elements_.triangle &&
        required_elements.adjacency_entry <= allocated_elements_.adjacency_entry) {
        return;
    }

    ClothBufferElementCounts next_allocated_elements;
    next_allocated_elements.vertex = grown_capacity(allocated_elements_.vertex, required_elements.vertex);
    next_allocated_elements.index = grown_capacity(allocated_elements_.index, required_elements.index);
    next_allocated_elements.triangle = grown_capacity(allocated_elements_.triangle, required_elements.triangle);
    next_allocated_elements.adjacency_entry = grown_capacity(allocated_elements_.adjacency_entry, required_elements.adjacency_entry);

    ClothBufferSet old_buffers = buffers_;

    create_buffers(next_allocated_elements, gl);

    if (old_buffers.rest_position != 0) {
        const GLsizeiptr position_bytes = byte_size(used_elements_.vertex, position_components , sizeof(float));
        const GLsizeiptr index_bytes = scalar_byte_size(used_elements_.index, sizeof(std::uint32_t));
        const GLsizeiptr adjacency_offset_bytes = scalar_byte_size(used_elements_.vertex + 1u, sizeof(std::uint32_t));
        const GLsizeiptr adjacency_triangle_bytes = scalar_byte_size(used_elements_.adjacency_entry, sizeof(std::uint32_t));

        if (position_bytes > 0) {
            gl.glCopyNamedBufferSubData(old_buffers.rest_position, buffers_.rest_position, 0, 0, position_bytes);
            gl.glCopyNamedBufferSubData(old_buffers.current_position, buffers_.current_position, 0, 0, position_bytes);
            gl.glCopyNamedBufferSubData(old_buffers.previous_position, buffers_.previous_position, 0, 0, position_bytes);
        }
        if (index_bytes > 0) {
            gl.glCopyNamedBufferSubData(old_buffers.index, buffers_.index, 0, 0, index_bytes);
        }
        if (adjacency_offset_bytes > 0) {
            gl.glCopyNamedBufferSubData(old_buffers.adjacency_offset, buffers_.adjacency_offset, 0, 0, adjacency_offset_bytes);
        }
        if (adjacency_triangle_bytes > 0) {
            gl.glCopyNamedBufferSubData(old_buffers.adjacency_triangle, buffers_.adjacency_triangle, 0, 0, adjacency_triangle_bytes);
        }
    }

    delete_buffer_set(old_buffers, gl);
}

bool ClothGpuResources::has_gpu_objects() const
{
    return buffers_.vao != 0 &&
           buffers_.rest_position != 0 &&
           buffers_.current_position != 0 &&
           buffers_.previous_position != 0 &&
           buffers_.index != 0 &&
           buffers_.adjacency_offset != 0 &&
           buffers_.adjacency_triangle != 0 &&
           buffers_.triangle_normal != 0 &&
           buffers_.vertex_normal != 0;
}

void ClothGpuResources::create_buffers(const ClothBufferElementCounts& allocated_elements,
                                       QOpenGLFunctions_4_5_Core& gl)
{
    buffers_ = create_buffer_set(allocated_elements, gl);
    allocated_elements_ = allocated_elements;

    configure_vao(gl);
}

void ClothGpuResources::delete_buffer_set(ClothBufferSet& buffers, QOpenGLFunctions_4_5_Core& gl)
{
    if (buffers.vertex_normal != 0) {
        gl.glDeleteBuffers(1, &buffers.vertex_normal);
    }
    if (buffers.triangle_normal != 0) {
        gl.glDeleteBuffers(1, &buffers.triangle_normal);
    }
    if (buffers.adjacency_triangle != 0) {
        gl.glDeleteBuffers(1, &buffers.adjacency_triangle);
    }
    if (buffers.adjacency_offset != 0) {
        gl.glDeleteBuffers(1, &buffers.adjacency_offset);
    }
    if (buffers.index != 0) {
        gl.glDeleteBuffers(1, &buffers.index);
    }
    if (buffers.previous_position != 0) {
        gl.glDeleteBuffers(1, &buffers.previous_position);
    }
    if (buffers.current_position != 0) {
        gl.glDeleteBuffers(1, &buffers.current_position);
    }
    if (buffers.rest_position != 0) {
        gl.glDeleteBuffers(1, &buffers.rest_position);
    }
    if (buffers.vao != 0) {
        gl.glDeleteVertexArrays(1, &buffers.vao);
    }

    buffers = {};
}

void ClothGpuResources::delete_gpu_objects(QOpenGLFunctions_4_5_Core& gl)
{
    delete_buffer_set(buffers_, gl);
}

// rendering //
void ClothGpuResources::bind_vertex_normals(GLuint binding_index, QOpenGLFunctions_4_5_Core& gl) const
{
    if (buffers_.vertex_normal == 0) {
        return;
    }

    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding_index, buffers_.vertex_normal);
}

void ClothGpuResources::draw_garment(GarmentId garment_id, QOpenGLFunctions_4_5_Core& gl) const
{
    const GarmentGpuData* gpu_data = find_garment_data(garment_id);
    if (!is_initialized() || gpu_data == nullptr || gpu_data->index_count == 0) {
        return;
    }

    const auto index_offset_bytes = static_cast<std::uintptr_t>(gpu_data->index_offset) * sizeof(std::uint32_t);
    gl.glBindVertexArray(buffers_.vao);
    gl.glDrawElements(GL_TRIANGLES,
                      static_cast<GLsizei>(gpu_data->index_count),
                      GL_UNSIGNED_INT,
                      reinterpret_cast<const void*>(index_offset_bytes));
}

void ClothGpuResources::configure_vao(QOpenGLFunctions_4_5_Core& gl)
{
    constexpr GLuint position_attribute_location = 0;
    constexpr GLuint position_binding_index = 0;
    constexpr GLuint position_relative_offset = 0;

    gl.glVertexArrayVertexBuffer(buffers_.vao,
                                 position_binding_index,
                                 buffers_.current_position,
                                 0,
                                 position_components  * static_cast<GLsizei>(sizeof(float)));
    gl.glEnableVertexArrayAttrib(buffers_.vao, position_attribute_location);
    gl.glVertexArrayAttribFormat(buffers_.vao, position_attribute_location, 3, GL_FLOAT, GL_FALSE, position_relative_offset);
    gl.glVertexArrayAttribBinding(buffers_.vao, position_attribute_location, position_binding_index);
    gl.glVertexArrayElementBuffer(buffers_.vao, buffers_.index);
}

// until //
const GarmentGpuData* ClothGpuResources::find_garment_data(GarmentId garment_id) const
{
    const auto iter = std::find_if(garments_.begin(), garments_.end(),
        [garment_id](const GarmentGpuData& gpu_data) {
            return gpu_data.id == garment_id;
        }
    );
    if (iter == garments_.end()) {
        return nullptr;
    }

    return &(*iter);
}

bool ClothGpuResources::is_initialized() const
{
    return has_gpu_objects() &&
           !garments_.empty() &&
           used_elements_.vertex > 0 &&
           used_elements_.index > 0 &&
           used_elements_.triangle > 0;
}
