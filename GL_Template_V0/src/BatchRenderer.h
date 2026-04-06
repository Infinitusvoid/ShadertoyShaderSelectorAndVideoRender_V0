#pragma once

#include "AppFolders.h"
#include "GlRuntime.h"
#include "Logger.h"
#include "ShaderCatalog.h"

#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class BatchRenderer
{
public:
    struct ProcessResult
    {
        bool success = false;
        int exitCode = -1;
        std::string output;
    };

    BatchRenderer() = default;

    void Initialize(const AppFolders& folders, Logger& logger);
    void Start(const RenderSettings& settings, const std::vector<ShaderRecord>& records, Logger& logger);
    void Update(OpenGlRenderer& renderer, ShaderCatalog& catalog, Logger& logger);

    bool IsBusy() const;
    bool IsFfmpegAvailable() const;
    const std::string& FfmpegStatus() const;
    std::string StatusText() const;
    float Progress() const;

private:
    struct ManifestEntry
    {
        std::string shaderName;
        std::string state;
        std::string videoPath;
        std::string message;
    };

    struct Job
    {
        std::string stableId;
        std::string displayName;
        std::filesystem::path validPath;
        std::filesystem::path selectedPath;
        std::filesystem::path renderedShaderPath;
        std::filesystem::path suggestedVideoPath;
    };

    struct ActiveJob
    {
        Job job;
        GpuShaderProgram program;
        int totalFrames = 0;
        int renderedFrames = 0;
        std::filesystem::path tempFrameDirectory;
        std::filesystem::path tempVideoPath;
        std::filesystem::path finalVideoPath;
    };

    enum class Stage
    {
        Idle,
        Rendering,
        Encoding
    };

    void LoadManifest(Logger& logger);
    void SaveManifest() const;
    void SetManifest(const std::string& stableId, const std::string& shaderName, const std::string& state,
        const std::string& videoPath, const std::string& message);

    bool StartNextJob(Logger& logger);
    void FailActiveJob(Logger& logger, const std::string& message);
    void CompleteActiveJob(ShaderCatalog& catalog, Logger& logger);
    std::filesystem::path BuildFinalVideoPath(const Job& job) const;
    static std::string ReadWholeFile(const std::filesystem::path& path);
    static ProcessResult RunFfmpeg(const std::filesystem::path& framesDirectory, const std::filesystem::path& outputPath, const RenderSettings& settings);
    static std::string ReplaceTokens(std::string pattern, const Job& job, const RenderSettings& settings);

    AppFolders folders_;
    std::filesystem::path manifestPath_;
    std::unordered_map<std::string, ManifestEntry> manifest_;

    bool queueActive_ = false;
    bool ffmpegAvailable_ = false;
    std::string ffmpegStatus_;
    RenderSettings settings_;
    std::vector<Job> queue_;
    std::size_t queueIndex_ = 0;

    Stage stage_ = Stage::Idle;
    std::unique_ptr<ActiveJob> activeJob_;
    std::future<ProcessResult> ffmpegFuture_;
};
