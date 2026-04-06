#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>

enum class ShaderEntryPoint
{
    MainImage,
    MainFunction
};

struct SanitizedShader
{
    bool supported = false;
    ShaderEntryPoint entryPoint = ShaderEntryPoint::MainFunction;
    std::string source;
    std::vector<std::string> notes;
    std::string rejectionReason;
};

struct ScanIssue
{
    std::filesystem::path path;
    std::string reason;
};

struct ScanSummary
{
    bool inProgress = false;
    std::size_t queued = 0;
    std::size_t processed = 0;
    std::size_t valid = 0;
    std::size_t invalid = 0;
    std::size_t ignored = 0;
};

struct ShaderRecord
{
    std::string stableId;
    std::string displayName;
    std::filesystem::path sourcePath;
    std::filesystem::path validPath;
    std::filesystem::path selectedPath;
    std::filesystem::path renderedShaderPath;
    std::filesystem::path videoPath;
    std::filesystem::path thumbnailPath;
    std::vector<std::string> notes;
    std::string compileLog;
    bool selected = false;
    bool rendered = false;
};

struct RuntimeUniforms
{
    int width = 0;
    int height = 0;
    float time = 0.0f;
    float timeDelta = 0.0f;
    int frame = 0;
    std::array<float, 4> mouse{ 0.0f, 0.0f, 0.0f, 0.0f };
    std::array<float, 4> date{ 0.0f, 0.0f, 0.0f, 0.0f };
};

struct RenderSettings
{
    int width = 3840;
    int height = 2160;
    int fps = 60;
    int durationSeconds = 5;
    std::string codec = "libx264";
    std::string preset = "medium";
    std::string filenamePattern = "{shader}_{width}x{height}_{fps}fps.mp4";
};
