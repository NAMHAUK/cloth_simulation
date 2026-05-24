#include "io/GarmentAsset.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace {
constexpr float obj_to_world_scale = 0.01f;

// TODO: token 정보 모두 받기
// OBJ 파일의 token 하나에서 vertex index만 읽는 함수
// Ex) "1/2/3" -> 0
bool parse_face_token(const std::string& token, std::uint32_t vertex_count, std::uint32_t& index)
{   
    // vertex index 부분만 잘라내기
    const std::string_view token_view{token};
    const std::string_view vertex_text = token_view.substr(0, token_view.find('/'));
    if (vertex_text.empty()) {
        return false;
    }

    // 숫자로 변환하고 유효성 검사
    std::uint32_t parsed_index = 0;
    const char* text_begin = vertex_text.data();
    const char* text_end = text_begin + vertex_text.size();
    const auto [parsed_end, error] = std::from_chars(text_begin, text_end, parsed_index);

    if (error != std::errc{} || parsed_end != text_end || parsed_index == 0 || parsed_index > vertex_count) {
        return false;
    }

    // index 값 조정
    index = parsed_index - 1;
    return true;
}

void assign_bounds(GarmentMesh& garment_mesh, const glm::vec3& min_bounds, const glm::vec3& max_bounds)
{
    garment_mesh.bounds_center = (min_bounds + max_bounds) * 0.5f;
    garment_mesh.bounds_radius = glm::length(max_bounds - min_bounds) * 0.5f;
}

// vertex 좌표 parsing
bool parse_vertex_line(std::istringstream& line_stream,
                       GarmentMesh& garment_mesh,
                       glm::vec3& min_bounds,
                       glm::vec3& max_bounds,
                       std::uint32_t& vertex_count)
{
    glm::vec3 vertex{};
    if (!(line_stream >> vertex.x >> vertex.y >> vertex.z)) {
        return false;
    }

    vertex *= obj_to_world_scale;

    garment_mesh.vertices.push_back(vertex.x);
    garment_mesh.vertices.push_back(vertex.y);
    garment_mesh.vertices.push_back(vertex.z);

    min_bounds = glm::min(min_bounds, vertex);
    max_bounds = glm::max(max_bounds, vertex);

    ++vertex_count;
    return true;
}

// 삼각형 face index parsing
// 현재는 삼각형만 지원
bool parse_face_line(std::istringstream& line_stream,
                     std::uint32_t vertex_count,
                     GarmentMesh& garment_mesh)
{
    std::array<std::uint32_t, 3> face_indices{};
    std::string face_token;
    std::size_t face_vertex_count = 0;

    while (line_stream >> face_token) {
        if (face_vertex_count >= face_indices.size()) {
            return false;
        }

        std::uint32_t index = 0;
        if (!parse_face_token(face_token, vertex_count, index)) {
            return false;
        }
        face_indices[face_vertex_count] = index;
        ++face_vertex_count;
    }

    if (face_vertex_count != face_indices.size()) {
        return false;
    }

    garment_mesh.indices.push_back(face_indices[0]);
    garment_mesh.indices.push_back(face_indices[1]);
    garment_mesh.indices.push_back(face_indices[2]);
    return true;
}
}

bool load_garment_mesh(const std::filesystem::path& obj_path, GarmentMesh& garment_mesh)
{
    std::ifstream input(obj_path);
    if (!input) {
        std::cerr << "Failed to open garment OBJ: " << obj_path << '\n';
        return false;
    }

    GarmentMesh next_mesh;
    glm::vec3 min_bounds{
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()
    };
    glm::vec3 max_bounds{
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest()
    };
    std::string line;
    std::uint32_t vertex_count = 0;
    std::uint32_t line_number = 0;

    // OBJ 파일을 한 줄씩 읽으며 parsing
    while (std::getline(input, line)) {
        ++line_number;

        std::istringstream line_stream(line);
        std::string tag;
        line_stream >> tag;

        if (tag.empty() || tag[0] == '#') {
            continue;
        }

        if (tag == "v") {
            if (!parse_vertex_line(line_stream, next_mesh, min_bounds, max_bounds, vertex_count)) {
                std::cerr << "Invalid garment vertex at line " << line_number << ": " << obj_path << '\n';
                return false;
            }
        } else if (tag == "f") {
            if (!parse_face_line(line_stream, vertex_count, next_mesh)) {
                std::cerr << "Invalid garment face at line " << line_number << ": " << obj_path << '\n';
                return false;
            }
        }
    }

    if (next_mesh.vertices.empty() || next_mesh.indices.empty()) {
        std::cerr << "Empty garment mesh: " << obj_path << '\n';
        return false;
    }

    // garment 경계값 계산
    assign_bounds(next_mesh, min_bounds, max_bounds);
    if (next_mesh.bounds_radius <= 0.0f) {
        std::cerr << "Invalid garment bounds: " << obj_path << '\n';
        return false;
    }

    std::cout << "Loaded garment OBJ: " << obj_path << '\n';
    std::cout << "  vertices=" << next_mesh.vertices.size() / 3
              << " indices=" << next_mesh.indices.size()
              << " bounds_radius=" << next_mesh.bounds_radius << '\n';

    garment_mesh = std::move(next_mesh);
    return true;
}
