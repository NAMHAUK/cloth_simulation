#pragma once

#include <filesystem>
#include <optional>
#include <string>

std::optional<std::string> read_text_file(const std::filesystem::path& path);
