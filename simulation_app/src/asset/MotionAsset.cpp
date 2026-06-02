#include "asset/MotionAsset.h"

#include "simulation/SimulationSettings.h"
#include "utils/QtUtils.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>

#include <QStringList>

namespace {
template <typename T>
bool read_value(std::ifstream& input, T& value)
{
    input.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(input);
}

bool validate_motion_asset(
    std::ifstream& input,
    const std::filesystem::path& motion_asset_path)
{
    if (!input) {
        std::cerr << "Failed to open motion asset: " << motion_asset_path << '\n';
        return false;
    }

    std::array<char, 8> magic = {};
    input.read(magic.data(), magic.size());
    if (!input || std::string(magic.data(), magic.size()) != "SMPLCACH") {
        std::cerr << "Invalid motion asset magic: " << motion_asset_path << '\n';
        return false;
    }

    return true;
}

bool prepare_converter_paths(
    const ProjectPaths& project_paths,
    const std::filesystem::path& motion_asset_path,
    ConverterCommand& command)
{
    if (!std::filesystem::exists(project_paths.python)) {
        command.error_message = "Missing project Python: " + project_paths.python.string();
        std::cerr << command.error_message << '\n';
        return false;
    }
    if (!std::filesystem::exists(project_paths.converter_script)) {
        command.error_message = "Missing converter script: " + project_paths.converter_script.string();
        std::cerr << command.error_message << '\n';
        return false;
    }

    std::filesystem::create_directories(motion_asset_path.parent_path());
    return true;
}

bool is_path_inside(const std::filesystem::path& path, const std::filesystem::path& root)
{
    std::error_code error;
    const std::filesystem::path relative = std::filesystem::relative(path, root, error);
    if (error || relative.empty()) {
        return false;
    }

    for (const auto& part : relative) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}
}

// asset에서 motion mesh 값을 읽고 저장
bool read_character_mesh_asset(const std::filesystem::path& motion_asset_path, CharacterMesh& character_mesh)
{
    std::ifstream input(motion_asset_path, std::ios::binary);
    if (!validate_motion_asset(input, motion_asset_path)) {
        return false;
    }

    // header를 읽고 올바른 형식인지 확인
    std::uint32_t version = 0;
    if (!read_value(input, version) ||
        !read_value(input, character_mesh.fps) ||
        !read_value(input, character_mesh.frame_count) ||
        !read_value(input, character_mesh.vertex_count) ||
        !read_value(input, character_mesh.index_count) ||
        !read_value(input, character_mesh.bounds_center.x) ||
        !read_value(input, character_mesh.bounds_center.y) ||
        !read_value(input, character_mesh.bounds_center.z) ||
        !read_value(input, character_mesh.bounds_radius)) {
        std::cerr << "Invalid motion asset header: " << motion_asset_path << '\n';
        return false;
    }

    if (version != 1 || character_mesh.fps <= 0.0f ||
        character_mesh.frame_count == 0 || character_mesh.vertex_count == 0 ||
        character_mesh.index_count == 0 || character_mesh.bounds_radius <= 0.0f) {
        std::cerr << "Unsupported motion asset header values: " << motion_asset_path << '\n';
        return false;
    }

    // asset에서 payload 읽기
    character_mesh.indices.resize(character_mesh.index_count);
    character_mesh.vertices.resize(
        static_cast<std::size_t>(character_mesh.frame_count) *
        static_cast<std::size_t>(character_mesh.vertex_count) * 3
    );

    input.read(
        reinterpret_cast<char*>(character_mesh.indices.data()),
        static_cast<std::streamsize>(character_mesh.indices.size() * sizeof(std::uint32_t))
    );
    input.read(
        reinterpret_cast<char*>(character_mesh.vertices.data()),
        static_cast<std::streamsize>(character_mesh.vertices.size() * sizeof(float))
    );

    if (!input) {
        std::cerr << "Failed to read full motion asset payload: " << motion_asset_path << '\n';
        return false;
    }

    std::cout << "Loaded motion asset: " << motion_asset_path << '\n';
    std::cout << "  fps=" << character_mesh.fps
              << " frames=" << character_mesh.frame_count
              << " vertices=" << character_mesh.vertex_count
              << " indices=" << character_mesh.index_count << '\n';
    return true;
}

// amass motion -> motion asset 변환을 위한 python 명령 생성
ConverterCommand make_converter_command(
    const ProjectPaths& project_paths,
    const std::filesystem::path& amass_motion_path,
    const std::filesystem::path& motion_asset_path)
{
    ConverterCommand command;

    if (!prepare_converter_paths(project_paths, motion_asset_path, command)) {
        return command;
    }

    // 명령 생성
    command.program = to_q_string(project_paths.python);
    command.arguments = {
        to_q_string(project_paths.converter_script),
        "--input", to_q_string(amass_motion_path),
        "--model-dir", to_q_string(project_paths.smpl_model_dir),
        "--output", to_q_string(motion_asset_path),
        "--target-fps", QString::number(simulation_settings::character_motion_fps),
        "--batch-size", "64",
    };
    command.working_directory = to_q_string(project_paths.root);

    std::cout << "Prepared converter command...\n"
              << "  program=" << project_paths.python << '\n'
              << "  amass_motion=" << amass_motion_path << '\n'
              << "  motion_asset=" << motion_asset_path << '\n';

    command.is_valid = true;
    return command;
}
 
// motion asset scan -> list 반환
std::vector<MotionAsset> scan_motion_assets(const ProjectPaths& project_paths)
{
    std::vector<MotionAsset> assets;
    if (!std::filesystem::exists(project_paths.motion_asset_dir)) {
        return assets;
    }

    for (const auto& file : std::filesystem::recursive_directory_iterator(project_paths.motion_asset_dir)) {
        if (!file.is_regular_file() || file.path().extension() != ".cache") {
            continue;
        }

        assets.push_back({
            file.path(),
            file.path().stem().string(),
        });
    }

    std::sort(assets.begin(), assets.end(), [](const MotionAsset& lhs, const MotionAsset& rhs) {
        return lhs.display_name < rhs.display_name;
    });
    return assets;
}

std::filesystem::path make_motion_asset_path(const ProjectPaths& project_paths, const std::filesystem::path& amass_motion_path)
{
    const std::string motion_asset_file_name = amass_motion_path.stem().string() + ".cache";

    if (is_path_inside(amass_motion_path, project_paths.amass_dir)) {
        std::error_code error;
        std::filesystem::path relative_path = std::filesystem::relative(amass_motion_path, project_paths.amass_dir, error);
        if (!error) {
            relative_path.replace_filename(motion_asset_file_name);
            return project_paths.motion_asset_dir / relative_path;
        }
    }

    return project_paths.motion_asset_dir / "imported" / motion_asset_file_name;
}
