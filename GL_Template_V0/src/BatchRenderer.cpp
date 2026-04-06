#define NOMINMAX
#include <Windows.h>

#include "BatchRenderer.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>

namespace
{
std::wstring ToWide(const std::string& text)
{
    return std::wstring(text.begin(), text.end());
}

std::wstring EscapeForCommand(const std::filesystem::path& path)
{
    std::wstring text = path.native();
    std::wstring escaped;
    escaped.reserve(text.size() + 8);
    escaped.push_back(L'"');
    for (const wchar_t value : text)
    {
        if (value == L'"')
        {
            escaped += L"\\\"";
        }
        else
        {
            escaped.push_back(value);
        }
    }
    escaped.push_back(L'"');
    return escaped;
}

std::string BuildFfmpegCommandForLog(const std::filesystem::path& outputPath, const RenderSettings& settings)
{
    std::ostringstream command;
    command << "ffmpeg -y -f rawvideo -pixel_format rgba -video_size "
            << settings.width << "x" << settings.height
            << " -framerate " << settings.fps
            << " -i - -an"
            << " -c:v " << settings.codec
            << " -preset " << settings.preset
            << " -pix_fmt yuv420p "
            << '"' << outputPath.string() << '"';
    return command.str();
}

std::wstring BuildFfmpegCommand(const std::filesystem::path& outputPath, const RenderSettings& settings)
{
    std::wostringstream command;
    command << L"ffmpeg -y -f rawvideo -pixel_format rgba -video_size "
            << settings.width << L"x" << settings.height
            << L" -framerate " << settings.fps
            << L" -i - -an"
            << L" -c:v " << ToWide(settings.codec)
            << L" -preset " << ToWide(settings.preset)
            << L" -pix_fmt yuv420p "
            << EscapeForCommand(outputPath);
    return command.str();
}

std::string ReadCapturedText(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

void CloseHandleIfValid(HANDLE* handle)
{
    if (*handle != nullptr && *handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(*handle);
        *handle = nullptr;
    }
}

struct FfmpegPipe
{
    HANDLE processHandle = nullptr;
    HANDLE threadHandle = nullptr;
    HANDLE stdinWrite = nullptr;
    std::filesystem::path logPath;
};

bool StartFfmpegPipe(const std::filesystem::path& outputPath, const std::filesystem::path& logPath, const RenderSettings& settings,
    FfmpegPipe* pipe, std::string* error)
{
    std::error_code ec;
    std::filesystem::create_directories(outputPath.parent_path(), ec);
    ec.clear();
    std::filesystem::create_directories(logPath.parent_path(), ec);

    SECURITY_ATTRIBUTES securityAttributes{};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE stdinRead = nullptr;
    HANDLE stdinWrite = nullptr;
    if (!CreatePipe(&stdinRead, &stdinWrite, &securityAttributes, 0))
    {
        if (error != nullptr)
        {
            *error = "CreatePipe failed for ffmpeg stdin: " + std::system_category().message(static_cast<int>(GetLastError()));
        }
        return false;
    }

    if (!SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0))
    {
        CloseHandleIfValid(&stdinRead);
        CloseHandleIfValid(&stdinWrite);
        if (error != nullptr)
        {
            *error = "SetHandleInformation failed for ffmpeg stdin: " + std::system_category().message(static_cast<int>(GetLastError()));
        }
        return false;
    }

    HANDLE logFile = CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &securityAttributes,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (logFile == INVALID_HANDLE_VALUE)
    {
        CloseHandleIfValid(&stdinRead);
        CloseHandleIfValid(&stdinWrite);
        if (error != nullptr)
        {
            *error = "CreateFile failed for ffmpeg log capture: " + std::system_category().message(static_cast<int>(GetLastError()));
        }
        return false;
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdInput = stdinRead;
    startupInfo.hStdOutput = logFile;
    startupInfo.hStdError = logFile;

    PROCESS_INFORMATION processInfo{};
    std::wstring command = BuildFfmpegCommand(outputPath, settings);
    std::vector<wchar_t> commandLine(command.begin(), command.end());
    commandLine.push_back(L'\0');

    const BOOL created = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startupInfo, &processInfo);

    CloseHandleIfValid(&stdinRead);
    CloseHandleIfValid(&logFile);

    if (!created)
    {
        CloseHandleIfValid(&stdinWrite);
        if (error != nullptr)
        {
            *error = "failed to launch ffmpeg: " + std::system_category().message(static_cast<int>(GetLastError()));
        }
        return false;
    }

    pipe->processHandle = processInfo.hProcess;
    pipe->threadHandle = processInfo.hThread;
    pipe->stdinWrite = stdinWrite;
    pipe->logPath = logPath;
    return true;
}

bool WriteFrameToPipe(const FfmpegPipe& pipe, const std::vector<unsigned char>& bytes, std::string* error)
{
    std::size_t remaining = bytes.size();
    const unsigned char* data = bytes.data();

    while (remaining > 0)
    {
        const DWORD chunkSize = static_cast<DWORD>(std::min<std::size_t>(remaining, 1u << 30));
        DWORD written = 0;
        if (!WriteFile(pipe.stdinWrite, data, chunkSize, &written, nullptr))
        {
            if (error != nullptr)
            {
                *error = "failed to write raw frame bytes to ffmpeg: " + std::system_category().message(static_cast<int>(GetLastError()));
            }
            return false;
        }

        if (written == 0)
        {
            if (error != nullptr)
            {
                *error = "ffmpeg stdin closed before the frame stream completed";
            }
            return false;
        }

        data += written;
        remaining -= written;
    }

    return true;
}

BatchRenderer::ProcessResult FinishFfmpegPipe(FfmpegPipe* pipe, bool terminateProcess)
{
    BatchRenderer::ProcessResult result;

    CloseHandleIfValid(&pipe->stdinWrite);

    if (terminateProcess && pipe->processHandle != nullptr)
    {
        TerminateProcess(pipe->processHandle, 1);
    }

    if (pipe->processHandle != nullptr)
    {
        WaitForSingleObject(pipe->processHandle, INFINITE);
        DWORD exitCode = 1;
        GetExitCodeProcess(pipe->processHandle, &exitCode);
        result.exitCode = static_cast<int>(exitCode);
        result.success = !terminateProcess && exitCode == 0;
    }

    CloseHandleIfValid(&pipe->threadHandle);
    CloseHandleIfValid(&pipe->processHandle);

    if (!pipe->logPath.empty())
    {
        result.output = ReadCapturedText(pipe->logPath);
    }

    return result;
}
}

BatchRenderer::~BatchRenderer()
{
    Shutdown();
}

void BatchRenderer::Initialize(const AppFolders& folders, Logger& logger)
{
    folders_ = folders;
    manifestPath_ = folders_.logs / "render_manifest.tsv";
    LoadManifest(logger);

    ProcessResult probe = ProbeFfmpeg();
    ffmpegAvailable_ = probe.exitCode == 0;
    ffmpegStatus_ = ffmpegAvailable_ ? "ffmpeg detected on PATH" : "ffmpeg was not detected on PATH";

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    workerWindow_ = glfwCreateWindow(16, 16, "Shader Render Worker", nullptr, nullptr);
    workerContextAvailable_ = workerWindow_ != nullptr;
    if (!workerContextAvailable_)
    {
        logger.Write("render", "failed to create hidden OpenGL worker window for background rendering");
    }

    logger.Write("ffmpeg", ffmpegStatus_);
}

void BatchRenderer::Shutdown()
{
    {
        std::lock_guard<std::mutex> guard(mutex_);
        stopRequested_ = true;
    }

    if (workerThread_.joinable())
    {
        workerThread_.join();
    }

    if (workerWindow_ != nullptr)
    {
        glfwDestroyWindow(workerWindow_);
        workerWindow_ = nullptr;
    }

    std::lock_guard<std::mutex> guard(mutex_);
    queueActive_ = false;
    workerFinished_ = false;
    stopRequested_ = false;
    stage_ = Stage::Idle;
    activeJobName_.clear();
    activeFrame_ = 0;
    activeTotalFrames_ = 0;
    statusText_ = "Idle";
}

void BatchRenderer::Start(const RenderSettings& settings, const std::vector<ShaderRecord>& records, Logger& logger)
{
    if (!ffmpegAvailable_)
    {
        logger.Write("render", "batch render could not start because ffmpeg is unavailable");
        return;
    }

    if (!workerContextAvailable_ || workerWindow_ == nullptr)
    {
        logger.Write("render", "batch render could not start because the hidden worker OpenGL context is unavailable");
        return;
    }

    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (queueActive_)
        {
            logger.Write("render", "batch render is already in progress");
            return;
        }
    }

    if (workerThread_.joinable())
    {
        workerThread_.join();
    }

    settings_ = settings;
    queue_.clear();

    for (const ShaderRecord& record : records)
    {
        if (record.selected && !record.rendered)
        {
            queue_.push_back(Job{
                record.stableId,
                record.displayName,
                record.validPath,
                record.selectedPath,
                record.renderedShaderPath,
                record.videoPath
            });
            SetManifest(record.stableId, record.displayName, "queued", record.videoPath.string(), "queued for rendering");
        }
    }

    {
        std::lock_guard<std::mutex> guard(mutex_);
        totalJobs_ = queue_.size();
        completedJobs_ = 0;
        failedJobs_ = 0;
        activeJobName_.clear();
        activeFrame_ = 0;
        activeTotalFrames_ = 0;
        stage_ = Stage::Idle;
        statusText_ = queue_.empty() ? "No queued shaders selected" : "Starting background render worker";
        completedEvents_.clear();
        stopRequested_ = false;
        workerFinished_ = false;
        queueActive_ = !queue_.empty();
    }

    SaveManifest();
    logger.Write("render", "queued " + std::to_string(queue_.size()) + " shaders for offline rendering");

    if (!queue_.empty())
    {
        workerThread_ = std::thread(&BatchRenderer::WorkerMain, this, &logger);
    }
}

void BatchRenderer::Update(ShaderCatalog& catalog, Logger& logger)
{
    std::vector<CompletedEvent> completed;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        while (!completedEvents_.empty())
        {
            completed.push_back(std::move(completedEvents_.front()));
            completedEvents_.pop_front();
        }
    }

    for (const CompletedEvent& event : completed)
    {
        catalog.MarkRendered(event.stableId, event.finalVideoPath, logger);
    }

    if (JoinWorkerIfFinished())
    {
        logger.Write("render", "background render worker finished");
    }
}

bool BatchRenderer::IsBusy() const
{
    std::lock_guard<std::mutex> guard(mutex_);
    return queueActive_;
}

bool BatchRenderer::IsFfmpegAvailable() const
{
    return ffmpegAvailable_;
}

const std::string& BatchRenderer::FfmpegStatus() const
{
    return ffmpegStatus_;
}

std::string BatchRenderer::StatusText() const
{
    std::lock_guard<std::mutex> guard(mutex_);
    return statusText_;
}

float BatchRenderer::Progress() const
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (totalJobs_ == 0)
    {
        return 0.0f;
    }

    float processedJobs = static_cast<float>(completedJobs_ + failedJobs_);
    if (stage_ == Stage::Rendering && activeTotalFrames_ > 0)
    {
        processedJobs += static_cast<float>(activeFrame_) / static_cast<float>(activeTotalFrames_);
    }
    else if (stage_ == Stage::Encoding)
    {
        processedJobs += 0.98f;
    }

    return std::clamp(processedJobs / static_cast<float>(totalJobs_), 0.0f, 1.0f);
}

std::size_t BatchRenderer::TotalJobCount() const
{
    std::lock_guard<std::mutex> guard(mutex_);
    return totalJobs_;
}

std::size_t BatchRenderer::CompletedJobCount() const
{
    std::lock_guard<std::mutex> guard(mutex_);
    return completedJobs_;
}

std::size_t BatchRenderer::FailedJobCount() const
{
    std::lock_guard<std::mutex> guard(mutex_);
    return failedJobs_;
}

std::size_t BatchRenderer::RemainingJobCount() const
{
    std::lock_guard<std::mutex> guard(mutex_);
    const std::size_t processed = completedJobs_ + failedJobs_;
    const std::size_t active = (!activeJobName_.empty() && stage_ != Stage::Idle) ? 1u : 0u;
    if (totalJobs_ <= processed + active)
    {
        return 0;
    }
    return totalJobs_ - processed - active;
}

void BatchRenderer::LoadManifest(Logger& logger)
{
    manifest_.clear();

    std::ifstream stream(manifestPath_, std::ios::binary);
    if (!stream)
    {
        return;
    }

    std::string line;
    std::getline(stream, line);
    while (std::getline(stream, line))
    {
        std::istringstream parser(line);
        std::string stableId;
        ManifestEntry entry;

        std::getline(parser, stableId, '\t');
        std::getline(parser, entry.shaderName, '\t');
        std::getline(parser, entry.state, '\t');
        std::getline(parser, entry.videoPath, '\t');
        std::getline(parser, entry.message, '\t');

        if (!stableId.empty())
        {
            if (entry.state == "rendering" || entry.state == "encoding")
            {
                entry.state = "queued";
                entry.message = "recovered after restart";
                logger.Write("render", "re-queued interrupted job " + entry.shaderName + " after restart");
            }

            manifest_[stableId] = entry;
        }
    }

    SaveManifest();
}

void BatchRenderer::SaveManifest() const
{
    std::ofstream stream(manifestPath_, std::ios::binary | std::ios::trunc);
    stream << "stable_id\tshader_name\tstate\tvideo_path\tmessage\n";
    for (const auto& [stableId, entry] : manifest_)
    {
        stream << stableId << '\t'
               << entry.shaderName << '\t'
               << entry.state << '\t'
               << entry.videoPath << '\t'
               << entry.message << '\n';
    }
}

void BatchRenderer::SetManifest(const std::string& stableId, const std::string& shaderName, const std::string& state,
    const std::string& videoPath, const std::string& message)
{
    manifest_[stableId] = ManifestEntry{ shaderName, state, videoPath, message };
}

void BatchRenderer::WorkerMain(Logger* logger)
{
    if (workerWindow_ == nullptr)
    {
        std::lock_guard<std::mutex> guard(mutex_);
        queueActive_ = false;
        workerFinished_ = true;
        failedJobs_ = totalJobs_;
        statusText_ = "Background render worker is unavailable";
        return;
    }

    glfwMakeContextCurrent(workerWindow_);

    OpenGlRenderer renderer;
    std::string error;
    if (!renderer.Initialize(&error))
    {
        logger->Write("render", "background render worker failed to initialize OpenGL: " + error);
        for (const Job& job : queue_)
        {
            SetManifest(job.stableId, job.displayName, "failed", job.suggestedVideoPath.string(), error);
        }
        SaveManifest();

        std::lock_guard<std::mutex> guard(mutex_);
        failedJobs_ = queue_.size();
        queueActive_ = false;
        workerFinished_ = true;
        statusText_ = "Background render worker failed to initialize";
        return;
    }

    for (const Job& job : queue_)
    {
        if (IsStopRequested())
        {
            break;
        }

        JobResult result = RenderJob(renderer, job, *logger);

        std::lock_guard<std::mutex> guard(mutex_);
        activeJobName_.clear();
        activeFrame_ = 0;
        activeTotalFrames_ = 0;

        if (result.outcome == JobResult::Outcome::Success)
        {
            ++completedJobs_;
            completedEvents_.push_back(CompletedEvent{ job.stableId, job.displayName, result.finalVideoPath });
            statusText_ = "Completed " + job.displayName;
        }
        else if (result.outcome == JobResult::Outcome::Failed)
        {
            ++failedJobs_;
            statusText_ = "Failed " + job.displayName;
        }
        else
        {
            statusText_ = "Stopped background render worker";
            break;
        }
    }

    renderer.Shutdown();
    glfwMakeContextCurrent(nullptr);

    std::lock_guard<std::mutex> guard(mutex_);
    queueActive_ = false;
    workerFinished_ = true;
    stage_ = Stage::Idle;
    activeJobName_.clear();
    activeFrame_ = 0;
    activeTotalFrames_ = 0;
    stopRequested_ = false;
    if (statusText_.empty() || statusText_ == "Starting background render worker")
    {
        statusText_ = "Batch render complete";
    }
}

BatchRenderer::JobResult BatchRenderer::RenderJob(OpenGlRenderer& renderer, const Job& job, Logger& logger)
{
    JobResult result;
    result.finalVideoPath = BuildFinalVideoPath(job);

    const std::filesystem::path tempVideoPath = folders_.temp / (job.stableId + "_encoding.mp4");
    const std::filesystem::path ffmpegLogPath = folders_.temp / (job.stableId + "_ffmpeg.log");

    std::error_code ec;
    std::filesystem::remove(tempVideoPath, ec);
    ec.clear();
    std::filesystem::remove(ffmpegLogPath, ec);

    const int totalFrames = std::max(1, settings_.fps * settings_.durationSeconds);

    {
        std::lock_guard<std::mutex> guard(mutex_);
        stage_ = Stage::Rendering;
        activeJobName_ = job.displayName;
        activeFrame_ = 0;
        activeTotalFrames_ = totalFrames;
        statusText_ = "Rendering " + job.displayName + " in the background";
    }

    SetManifest(job.stableId, job.displayName, "rendering", result.finalVideoPath.string(), "streaming raw frames into ffmpeg");
    SaveManifest();

    const std::string shaderSource = ReadWholeFile(job.validPath);
    std::string buildLog;
    GpuShaderProgram program;
    if (!program.Build(shaderSource, &buildLog))
    {
        result.outcome = JobResult::Outcome::Failed;
        result.message = "shader failed to compile for offline rendering:\n" + buildLog;
        SetManifest(job.stableId, job.displayName, "failed", result.finalVideoPath.string(), result.message);
        SaveManifest();
        logger.Write("render", "job failed for " + job.displayName + ": " + result.message);
        return result;
    }

    logger.Write("ffmpeg", BuildFfmpegCommandForLog(tempVideoPath, settings_));

    FfmpegPipe pipe;
    std::string pipeError;
    if (!StartFfmpegPipe(tempVideoPath, ffmpegLogPath, settings_, &pipe, &pipeError))
    {
        result.outcome = JobResult::Outcome::Failed;
        result.message = pipeError;
        SetManifest(job.stableId, job.displayName, "failed", result.finalVideoPath.string(), result.message);
        SaveManifest();
        logger.Write("render", "job failed for " + job.displayName + ": " + result.message);
        return result;
    }

    std::vector<unsigned char> pixels;
    for (int frame = 0; frame < totalFrames; ++frame)
    {
        if (IsStopRequested())
        {
            FinishFfmpegPipe(&pipe, true);
            result.outcome = JobResult::Outcome::Stopped;
            result.message = "render stopped before completion";
            logger.Write("render", "stopped background render for " + job.displayName);
            return result;
        }

        RuntimeUniforms uniforms;
        uniforms.width = settings_.width;
        uniforms.height = settings_.height;
        uniforms.time = static_cast<float>(frame) / static_cast<float>(std::max(1, settings_.fps));
        uniforms.timeDelta = 1.0f / static_cast<float>(std::max(1, settings_.fps));
        uniforms.frame = frame;

        std::string renderError;
        if (!renderer.RenderWithProgram(program, settings_.width, settings_.height, uniforms, &pixels, &renderError))
        {
            ProcessResult ffmpegResult = FinishFfmpegPipe(&pipe, true);
            result.outcome = JobResult::Outcome::Failed;
            result.message = "frame render failed: " + renderError;
            if (!ffmpegResult.output.empty())
            {
                result.message += "\nffmpeg output:\n" + ffmpegResult.output;
            }
            SetManifest(job.stableId, job.displayName, "failed", result.finalVideoPath.string(), result.message);
            SaveManifest();
            logger.Write("render", "job failed for " + job.displayName + ": " + result.message);
            return result;
        }

        if (!WriteFrameToPipe(pipe, pixels, &renderError))
        {
            ProcessResult ffmpegResult = FinishFfmpegPipe(&pipe, true);
            result.outcome = JobResult::Outcome::Failed;
            result.message = renderError;
            if (!ffmpegResult.output.empty())
            {
                result.message += "\nffmpeg output:\n" + ffmpegResult.output;
            }
            SetManifest(job.stableId, job.displayName, "failed", result.finalVideoPath.string(), result.message);
            SaveManifest();
            logger.Write("render", "job failed for " + job.displayName + ": " + result.message);
            return result;
        }

        if ((frame & 3) == 0 || frame + 1 == totalFrames)
        {
            std::lock_guard<std::mutex> guard(mutex_);
            stage_ = Stage::Rendering;
            activeJobName_ = job.displayName;
            activeFrame_ = frame + 1;
            activeTotalFrames_ = totalFrames;
            statusText_ = "Rendering " + job.displayName + " frame "
                + std::to_string(frame + 1) + " / " + std::to_string(totalFrames);
        }
    }

    {
        std::lock_guard<std::mutex> guard(mutex_);
        stage_ = Stage::Encoding;
        activeJobName_ = job.displayName;
        activeFrame_ = totalFrames;
        activeTotalFrames_ = totalFrames;
        statusText_ = "Finalizing " + job.displayName;
    }

    SetManifest(job.stableId, job.displayName, "encoding", result.finalVideoPath.string(), "finalizing ffmpeg output");
    SaveManifest();

    ProcessResult ffmpegResult = FinishFfmpegPipe(&pipe, false);
    if (!ffmpegResult.success)
    {
        result.outcome = JobResult::Outcome::Failed;
        result.message = "ffmpeg encode failed";
        if (!ffmpegResult.output.empty())
        {
            result.message += ":\n" + ffmpegResult.output;
        }
        SetManifest(job.stableId, job.displayName, "failed", result.finalVideoPath.string(), result.message);
        SaveManifest();
        logger.Write("render", "job failed for " + job.displayName + ": " + result.message);
        return result;
    }

    std::filesystem::create_directories(result.finalVideoPath.parent_path(), ec);
    ec.clear();
    std::filesystem::rename(tempVideoPath, result.finalVideoPath, ec);
    if (ec)
    {
        ec.clear();
        std::filesystem::copy_file(tempVideoPath, result.finalVideoPath, std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec)
        {
            std::filesystem::remove(tempVideoPath, ec);
        }
    }

    if (ec)
    {
        result.outcome = JobResult::Outcome::Failed;
        result.message = "failed to move encoded video into final folder: " + ec.message();
        SetManifest(job.stableId, job.displayName, "failed", result.finalVideoPath.string(), result.message);
        SaveManifest();
        logger.Write("render", "job failed for " + job.displayName + ": " + result.message);
        return result;
    }

    std::filesystem::remove(ffmpegLogPath, ec);

    result.outcome = JobResult::Outcome::Success;
    result.message = "render complete";
    SetManifest(job.stableId, job.displayName, "done", result.finalVideoPath.string(), result.message);
    SaveManifest();
    logger.Write("render", "completed render for " + job.displayName + " -> " + result.finalVideoPath.string());
    return result;
}

bool BatchRenderer::IsStopRequested() const
{
    std::lock_guard<std::mutex> guard(mutex_);
    return stopRequested_;
}

bool BatchRenderer::JoinWorkerIfFinished()
{
    bool shouldJoin = false;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        shouldJoin = workerFinished_ && workerThread_.joinable();
    }

    if (!shouldJoin)
    {
        return false;
    }

    workerThread_.join();

    std::lock_guard<std::mutex> guard(mutex_);
    workerFinished_ = false;
    return true;
}

std::filesystem::path BatchRenderer::BuildFinalVideoPath(const Job& job) const
{
    std::filesystem::path basePath = folders_.videosRendered / ReplaceTokens(settings_.filenamePattern, job, settings_);
    if (!basePath.has_extension())
    {
        basePath += ".mp4";
    }

    if (!std::filesystem::exists(basePath))
    {
        return basePath;
    }

    for (int suffix = 1; suffix < 1000; ++suffix)
    {
        std::filesystem::path candidate = basePath.parent_path() /
            (basePath.stem().string() + "_" + std::to_string(suffix) + basePath.extension().string());
        if (!std::filesystem::exists(candidate))
        {
            return candidate;
        }
    }

    return basePath;
}

std::string BatchRenderer::ReadWholeFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

BatchRenderer::ProcessResult BatchRenderer::ProbeFfmpeg()
{
    ProcessResult result;
    FILE* pipe = _popen("ffmpeg -version 2>&1", "r");
    if (pipe == nullptr)
    {
        result.exitCode = -1;
        result.output = "failed to launch ffmpeg";
        return result;
    }

    char buffer[1024];
    while (fgets(buffer, static_cast<int>(sizeof(buffer)), pipe) != nullptr)
    {
        result.output += buffer;
    }

    result.exitCode = _pclose(pipe);
    result.success = result.exitCode == 0;
    return result;
}

std::string BatchRenderer::ReplaceTokens(std::string pattern, const Job& job, const RenderSettings& settings)
{
    const std::array<std::pair<std::string, std::string>, 4> replacements = {
        std::pair<std::string, std::string>{ "{shader}", std::filesystem::path(job.validPath).stem().string() },
        std::pair<std::string, std::string>{ "{width}", std::to_string(settings.width) },
        std::pair<std::string, std::string>{ "{height}", std::to_string(settings.height) },
        std::pair<std::string, std::string>{ "{fps}", std::to_string(settings.fps) }
    };

    for (const auto& [token, replacement] : replacements)
    {
        std::size_t position = 0;
        while ((position = pattern.find(token, position)) != std::string::npos)
        {
            pattern.replace(position, token.size(), replacement);
            position += replacement.size();
        }
    }

    return pattern;
}
