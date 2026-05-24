#pragma once

#include <filesystem>

#include <QString>

inline QString to_q_string(const std::filesystem::path& path)
{
    return QString::fromStdWString(path.wstring());
}
