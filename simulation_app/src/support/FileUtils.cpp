#include "support/FileUtils.h"

#include <fstream>
#include <iostream>
#include <sstream>

std::optional<std::string> read_text_file(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open text file: " << path << '\n';
        return std::nullopt;
    }

    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}
