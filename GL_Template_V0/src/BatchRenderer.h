#pragma once

#include "AppFolders.h"
#include "GlRuntime.h"
#include "Logger.h"
#include "ShaderCatalog.h"

#include <GLFW/glfw3.h>

#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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
    ~BatchRenderer();

    void Initialize(const AppFolders& folders, Logger& logger);
    void Shutdown();
    void Start(const RenderSettings& settings, const std::vector<ShaderRecord>& records, Logger& logger);
    void Update(ShaderCatalog& catalog, Logger& logger);

    bool IsBusy() const;
    bool IsFfmpegAvailable() const;
    const std::string& FfmpegStatus() const;
    std::string StatusText() const;
    float Progress() const;
    std::size_t TotalJobCount() const;
    std::size_t CompletedJobCount() const;
    std::size_t FailedJobCount() const;
    std::size_t RemainingJobCount() const;

private:
    struct CompletedEvent
    {
        std::string stableId;
        std::string displayName;
        std::filesystem::path finalVideoPath;
    };

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

    struct JobResult
    {
        enum class Outcome
        {
            Success,
            Failed,
            Stopped
        };

        Outcome outcome = Outcome::Failed;
        std::filesystem::path finalVideoPath;
        std::string message;
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

    void WorkerMain(Logger* logger);
    JobResult RenderJob(OpenGlRenderer& renderer, const Job& job, Logger& logger);
    bool IsStopRequested() const;
    bool JoinWorkerIfFinished();
    std::filesystem::path BuildFinalVideoPath(const Job& job) const;
    static std::string ReadWholeFile(const std::filesystem::path& path);
    static ProcessResult ProbeFfmpeg();
    static std::string ReplaceTokens(std::string pattern, const Job& job, const RenderSettings& settings);

    AppFolders folders_;
    std::filesystem::path manifestPath_;
    std::unordered_map<std::string, ManifestEntry> manifest_;
    mutable std::mutex mutex_;
    std::deque<CompletedEvent> completedEvents_;
    GLFWwindow* workerWindow_ = nullptr;
    std::thread workerThread_;

    bool workerFinished_ = false;
    bool workerContextAvailable_ = false;
    bool queueActive_ = false;
    bool stopRequested_ = false;
    bool ffmpegAvailable_ = false;
    std::string ffmpegStatus_;
    std::string statusText_ = "Idle";
    RenderSettings settings_;
    std::vector<Job> queue_;
    std::size_t totalJobs_ = 0;
    std::size_t completedJobs_ = 0;
    std::size_t failedJobs_ = 0;
    std::string activeJobName_;
    int activeFrame_ = 0;
    int activeTotalFrames_ = 0;

    Stage stage_ = Stage::Idle;
};
