#include "MotionCache.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <limits>

#include <QProcess>
#include <QString>
#include <QStringList>

namespace {
template <typename T>
bool readValue(std::ifstream& input, T& value)
{
    input.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(input);
}

void computeViewFit(MeshCache& cache)
{
    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float minZ = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    float maxZ = std::numeric_limits<float>::lowest();

    for (std::size_t i = 0; i + 2 < cache.vertices.size(); i += 3) {
        const float x = cache.vertices[i + 0];
        const float y = cache.vertices[i + 1];
        const float z = cache.vertices[i + 2];
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        minZ = std::min(minZ, z);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
        maxZ = std::max(maxZ, z);
    }

    cache.boundsCenter = {
        (minX + maxX) * 0.5f,
        (minY + maxY) * 0.5f,
        (minZ + maxZ) * 0.5f,
    };

    const Vec3 extents = {
        std::max(0.001f, maxX - minX),
        std::max(0.001f, maxY - minY),
        std::max(0.001f, maxZ - minZ),
    };
    cache.boundsRadius = std::max({extents.x, extents.y, extents.z}) * 0.5f;
}

QString toQString(const std::filesystem::path& path)
{
    return QString::fromStdWString(path.wstring());
}

bool isPathInside(const std::filesystem::path& path, const std::filesystem::path& root)
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

std::string makeCacheFileName(const std::filesystem::path& motionPath)
{
    std::string baseName = motionPath.stem().string();
    constexpr const char* suffix = "_poses";
    constexpr std::size_t suffixLength = 6;
    if (baseName.size() > suffixLength &&
        baseName.compare(baseName.size() - suffixLength, suffixLength, suffix) == 0) {
        baseName.erase(baseName.size() - suffixLength);
    }
    return baseName + "_body.cache";
}
}

bool loadCache(const std::filesystem::path& path, MeshCache& cache)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::cerr << "Failed to open cache: " << path << '\n';
        return false;
    }

    std::array<char, 8> magic = {};
    input.read(magic.data(), magic.size());
    if (!input || std::string(magic.data(), magic.size()) != "SMPLCACH") {
        std::cerr << "Invalid cache magic: " << path << '\n';
        return false;
    }

    std::uint32_t version = 0;
    if (!readValue(input, version) ||
        !readValue(input, cache.fps) ||
        !readValue(input, cache.frameCount) ||
        !readValue(input, cache.vertexCount) ||
        !readValue(input, cache.indexCount)) {
        std::cerr << "Invalid cache header: " << path << '\n';
        return false;
    }

    if (version != 1 || cache.fps <= 0.0f ||
        cache.frameCount == 0 || cache.vertexCount == 0 || cache.indexCount == 0) {
        std::cerr << "Unsupported cache header values: " << path << '\n';
        return false;
    }

    cache.indices.resize(cache.indexCount);
    cache.vertices.resize(
        static_cast<std::size_t>(cache.frameCount) *
        static_cast<std::size_t>(cache.vertexCount) * 3
    );

    input.read(
        reinterpret_cast<char*>(cache.indices.data()),
        static_cast<std::streamsize>(cache.indices.size() * sizeof(std::uint32_t))
    );
    input.read(
        reinterpret_cast<char*>(cache.vertices.data()),
        static_cast<std::streamsize>(cache.vertices.size() * sizeof(float))
    );

    if (!input) {
        std::cerr << "Failed to read full cache payload: " << path << '\n';
        return false;
    }

    computeViewFit(cache);
    std::cout << "Loaded cache: " << path << '\n';
    std::cout << "  fps=" << cache.fps
              << " frames=" << cache.frameCount
              << " vertices=" << cache.vertexCount
              << " indices=" << cache.indexCount << '\n';
    return true;
}

ConverterResult runConverter(
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath)
{
    ConverterResult result;
    const std::filesystem::path pythonPath = projectRoot / "envs" / "cloth-sim" / "python.exe";
    const std::filesystem::path scriptPath = projectRoot / "tools" / "convert_amass_to_cache.py";
    const std::filesystem::path modelDir = projectRoot / "data" / "smpl" / "models";

    if (!std::filesystem::exists(pythonPath)) {
        result.errorMessage = "Missing project Python: " + pythonPath.string();
        std::cerr << result.errorMessage << '\n';
        return result;
    }
    if (!std::filesystem::exists(scriptPath)) {
        result.errorMessage = "Missing converter script: " + scriptPath.string();
        std::cerr << result.errorMessage << '\n';
        return result;
    }

    std::filesystem::create_directories(outputPath.parent_path());

    QProcess process;
    process.setProgram(toQString(pythonPath));
    process.setArguments({
        toQString(scriptPath),
        "--input", toQString(inputPath),
        "--model-dir", toQString(modelDir),
        "--output", toQString(outputPath),
        "--target-fps", "30",
        "--batch-size", "64",
    });
    process.setWorkingDirectory(toQString(projectRoot));

    std::cout << "Running converter with QProcess...\n"
              << "  program=" << pythonPath << '\n'
              << "  input=" << inputPath << '\n'
              << "  output=" << outputPath << '\n';

    process.start();
    if (!process.waitForStarted()) {
        result.errorMessage = "Failed to start converter process: " +
            process.errorString().toStdString();
        result.stderrText = process.readAllStandardError().toStdString();
        result.stdoutText = process.readAllStandardOutput().toStdString();
        std::cerr << result.errorMessage << '\n';
        return result;
    }

    process.waitForFinished(-1);
    result.exitCode = process.exitCode();
    result.stderrText = process.readAllStandardError().toStdString();
    result.stdoutText = process.readAllStandardOutput().toStdString();

    if (!result.stdoutText.empty()) {
        std::cout << result.stdoutText;
    }
    if (!result.stderrText.empty()) {
        std::cerr << result.stderrText;
    }

    if (process.exitStatus() != QProcess::NormalExit) {
        result.errorMessage = "Converter process crashed: " +
            process.errorString().toStdString();
        std::cerr << result.errorMessage << '\n';
        return result;
    }

    if (result.exitCode != 0) {
        result.errorMessage =
            "Converter failed with exit code: " + std::to_string(result.exitCode);
        std::cerr << result.errorMessage << '\n';
        return result;
    }

    result.ok = true;
    return result;
}

std::vector<MotionEntry> scanMotionCaches(const std::filesystem::path& projectRoot)
{
    std::vector<MotionEntry> entries;
    const std::filesystem::path cacheRoot = projectRoot / "data" / "cache";
    if (!std::filesystem::exists(cacheRoot)) {
        return entries;
    }

    for (const auto& file : std::filesystem::recursive_directory_iterator(cacheRoot)) {
        if (!file.is_regular_file() || file.path().extension() != ".cache") {
            continue;
        }

        std::error_code error;
        const std::filesystem::path relativePath =
            std::filesystem::relative(file.path(), cacheRoot, error);
        entries.push_back({
            file.path(),
            error ? file.path().filename().string() : relativePath.generic_string(),
        });
    }

    std::sort(entries.begin(), entries.end(), [](const MotionEntry& lhs, const MotionEntry& rhs) {
        return lhs.displayName < rhs.displayName;
    });
    return entries;
}

std::filesystem::path makeCachePathForMotion(
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& motionPath)
{
    const std::filesystem::path cacheRoot = projectRoot / "data" / "cache";
    const std::filesystem::path amassRoot = projectRoot / "data" / "amass";
    const std::string cacheFileName = makeCacheFileName(motionPath);

    if (isPathInside(motionPath, amassRoot)) {
        std::error_code error;
        std::filesystem::path relativePath = std::filesystem::relative(motionPath, amassRoot, error);
        if (!error) {
            relativePath.replace_filename(cacheFileName);
            return cacheRoot / relativePath;
        }
    }

    return cacheRoot / "imported" / cacheFileName;
}
