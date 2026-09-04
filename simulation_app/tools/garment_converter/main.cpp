#include "GarmentConverter.h"

#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>

namespace {
std::optional<std::string_view> find_argument_value(int argc, char** argv, std::string_view name)
{
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::string_view(argv[index]) == name) {
            return std::string_view(argv[index + 1]);
        }
    }
    return std::nullopt;
}

std::optional<GarmentCategory> parse_garment_category(int argc, char** argv)
{
    const auto garment_category = find_argument_value(argc, argv, "--garment-category");
    if (!garment_category) {
        return std::nullopt;
    }
    if (*garment_category == "top") {
        return GarmentCategory::Top;
    }
    if (*garment_category == "bottom") {
        return GarmentCategory::Bottom;
    }
    if (*garment_category == "full-body") {
        return GarmentCategory::FullBody;
    }

    return std::nullopt;
}
}

int main(int argc, char** argv)
{
    try {
        const std::filesystem::path input_path(find_argument_value(argc, argv, "--input").value_or(""));
        const std::filesystem::path output_path(find_argument_value(argc, argv, "--output").value_or(""));
        const auto garment_category = parse_garment_category(argc, argv);

        if (input_path.empty() || output_path.empty() || !garment_category) {
            std::cerr << "Garment conversion failed: missing or invalid arguments.\n";
            return 1;
        }

        const GarmentMesh garment_mesh = read_garment_obj(input_path, *garment_category);
        write_garment_asset(output_path, garment_mesh);

        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Garment conversion failed: " << error.what() << '\n';
        return 1;
    }
}
