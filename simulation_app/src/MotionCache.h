#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct MeshCache {
    float fps = 0.0f;
    std::uint32_t frameCount = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount = 0;
    std::vector<std::uint32_t> indices;
    std::vector<float> vertices;
    Vec3 boundsCenter;
    float boundsRadius = 1.0f;
};

struct MotionEntry {
    std::filesystem::path cachePath;
    std::string displayName;
};

struct ConverterResult {
    bool ok = false;
    int exitCode = -1;
    std::string stdoutText;
    std::string stderrText;
    std::string errorMessage;
};

bool loadCache(const std::filesystem::path& path, MeshCache& cache);
ConverterResult runConverter(
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath);

std::vector<MotionEntry> scanMotionCaches(const std::filesystem::path& projectRoot);
std::filesystem::path makeCachePathForMotion(
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& motionPath);
