#include "asset/GarmentConverter.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>

namespace {
void print_usage()
{
    std::cerr << "Usage: garment_converter --input <source.obj> --output <asset.garment>\n";
}

std::optional<std::filesystem::path> find_argument_path(int argc, char** argv, std::string_view name)
{
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::string_view(argv[index]) == name) {
            return std::filesystem::path(argv[index + 1]);
        }
    }
    return std::nullopt;
}

bool has_help_argument(int argc, char** argv)
{
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            return true;
        }
    }
    return false;
}
}

int main(int argc, char** argv)
{
    if (has_help_argument(argc, argv)) {
        print_usage();
        return 0;
    }

    const std::optional<std::filesystem::path> input_path = find_argument_path(argc, argv, "--input");
    const std::optional<std::filesystem::path> output_path = find_argument_path(argc, argv, "--output");
    if (!input_path || !output_path) {
        print_usage();
        return 1;
    }

    GarmentMesh garment_mesh;
    if (!read_garment_obj(*input_path, garment_mesh)) {
        std::cerr << "Garment OBJ conversion failed.\n";
        return 1;
    }

    if (!write_garment_asset(*output_path, garment_mesh)) {
        std::cerr << "Garment asset write failed.\n";
        return 1;
    }

    std::cout << "Garment conversion succeeded.\n";
    return 0;
}
