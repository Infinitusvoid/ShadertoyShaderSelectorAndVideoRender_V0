#include "BatchRenderer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace
{
std::string EscapeForCommand(const std::filesystem::path& path)
{
    std::string text = path.string();
    std::string escaped;
    escaped.reserve(text.size() + 8);
    escaped.push_back('"');
    for (const char value : text)
    {
        if (value == '"')
        {
            escaped += "\\\"";
        }
        else
        {
            escaped.push_back(value);
        }
    }
    escaped.push_back('"');
    return escaped;
}

std::string BuildFfmpegCommand(const std::filesystem::path& framesDirectory, const std::filesystem::path& outputPath, const RenderSettings& settings)
{
    std::ostringstream command;
    command << "ffmpeg -y -framerate " << settings.fps
            << " -i " << EscapeForCommand(framesDirectory / "frame_%06d.png")
            << " -c:v " << settings.codec
            << " -preset " << settings.preset
            << " -pix_fmt yuv420p "
            << EscapeForCommand(outputPath);
    return command.str();
}
}

void BatchRenderer::Initialize(const AppFolders& folders, Logger& logger)
{
    folders_ = folders;
    manifestPath_ = folders_.logs / "render_manifest.tsv";
    LoadManifest(logger);

    ProcessResult probe = RunFfmpeg(std::filesystem::path{}, std::filesystem::path{}, settings_);
    ffmpegAvailable_ = probe.exitCode == 0;
    ffmpegStatus_ = ffmpegAvailable_ ? "ffmpeg detected on PATH" : "ffmpeg was not detected on PATH";
    logger.Write("ffmpeg", ffmpegStatus_);
}

void BatchRenderer::Start(const RenderSettings& settings, const std::vector<ShaderRecord>& records, Logger& logger)
{
    if (!ffmpegAvailable_)
    {
        logger.Write("render", "batch render could not start because ffmpeg is unavailable");
        return;
    }

    settings_ = settings;
    queue_.clear();
    queueIndex_ = 0;
    queueActive_ = true;
    stage_ = Stage::Idle;
    activeJob_.reset();

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

    SaveManifest();
    logger.Write("render", "queued " + std::to_string(queue_.size()) + " shaders for offline rendering");

    if (queue_.empty())
    {
        queueActive_ = false;
    }
}

void BatchRenderer::Update(OpenGlRenderer& renderer, ShaderCatalog& catalog, Logger& logger)
{
    if (!queueActive_)
    {
        return;
    }

    if (stage_ == Stage::Idle)
    {
        if (!StartNextJob(logger))
        {
            queueActive_ = false;
        }
        return;
    }

    if (stage_ == Stage::Rendering)
    {
        RuntimeUniforms uniforms;
        uniforms.width = settings_.width;
        uniforms.height = settings_.height;
        uniforms.time = static_cast<float>(activeJob_->renderedFrames) / static_cast<float>(std::max(1, settings_.fps));
        uniforms.timeDelta = 1.0f / static_cast<float>(std::max(1, settings_.fps));
        uniforms.frame = activeJob_->renderedFrames;

        std::vector<unsigned char> pixels;
        std::string error;
        if (!renderer.RenderWithProgram(activeJob_->program, settings_.width, settings_.height, uniforms, &pixels, &error))
        {
            FailActiveJob(logger, "frame render failed: " + error);
            return;
        }

        char filename[64] = {};
        std::snprintf(filename, sizeof(filename), "frame_%06d.png", activeJob_->renderedFrames);
        const std::filesystem::path framePath = activeJob_->tempFrameDirectory / filename;
        if (!renderer.SavePng(framePath, settings_.width, settings_.height, pixels, &error))
        {
            FailActiveJob(logger, "frame save failed: " + error);
            return;
        }

        ++activeJob_->renderedFrames;

        if (activeJob_->renderedFrames >= activeJob_->totalFrames)
        {
            SetManifest(activeJob_->job.stableId, activeJob_->job.displayName, "encoding", activeJob_->finalVideoPath.string(), "starting ffmpeg encoding");
            SaveManifest();
            logger.Write("ffmpeg", BuildFfmpegCommand(activeJob_->tempFrameDirectory, activeJob_->tempVideoPath, settings_));
            ffmpegFuture_ = std::async(std::launch::async, &BatchRenderer::RunFfmpeg, activeJob_->tempFrameDirectory, activeJob_->tempVideoPath, settings_);
            stage_ = Stage::Encoding;
        }
        return;
    }

    if (stage_ == Stage::Encoding)
    {
        if (ffmpegFuture_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        {
            return;
        }

        ProcessResult result = ffmpegFuture_.get();
        if (!result.success)
        {
            FailActiveJob(logger, "ffmpeg encode failed: " + result.output);
            return;
        }

        std::error_code ec;
        std::filesystem::create_directories(activeJob_->finalVideoPath.parent_path(), ec);
        std::filesystem::rename(activeJob_->tempVideoPath, activeJob_->finalVideoPath, ec);
        if (ec)
        {
            ec.clear();
            std::filesystem::copy_file(activeJob_->tempVideoPath, activeJob_->finalVideoPath, std::filesystem::copy_options::overwrite_existing, ec);
            if (!ec)
            {
                std::filesystem::remove(activeJob_->tempVideoPath, ec);
            }
        }

        if (ec)
        {
            FailActiveJob(logger, "failed to move encoded video into final folder: " + ec.message());
            return;
        }

        CompleteActiveJob(catalog, logger);
    }
}

bool BatchRenderer::IsBusy() const
{
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
    if (!queueActive_)
    {
        return "Idle";
    }

    if (!activeJob_)
    {
        return "Waiting to start render jobs";
    }

    if (stage_ == Stage::Rendering)
    {
        return "Rendering " + activeJob_->job.displayName + " frame "
            + std::to_string(activeJob_->renderedFrames) + " / " + std::to_string(activeJob_->totalFrames);
    }

    if (stage_ == Stage::Encoding)
    {
        return "Encoding video for " + activeJob_->job.displayName;
    }

    return "Idle";
}

float BatchRenderer::Progress() const
{
    if (!queueActive_ || queue_.empty())
    {
        return 0.0f;
    }

    float completedJobs = static_cast<float>(queueIndex_);
    if (activeJob_ && stage_ == Stage::Rendering && activeJob_->totalFrames > 0)
    {
        completedJobs += static_cast<float>(activeJob_->renderedFrames) / static_cast<float>(activeJob_->totalFrames);
    }
    else if (activeJob_ && stage_ == Stage::Encoding)
    {
        completedJobs += 1.0f;
    }

    return std::clamp(completedJobs / static_cast<float>(queue_.size()), 0.0f, 1.0f);
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

bool BatchRenderer::StartNextJob(Logger& logger)
{
    while (queueIndex_ < queue_.size())
    {
        const Job& job = queue_[queueIndex_];

        activeJob_ = std::make_unique<ActiveJob>();
        activeJob_->job = job;
        activeJob_->totalFrames = std::max(1, settings_.fps * settings_.durationSeconds);
        activeJob_->tempFrameDirectory = folders_.temp / ("frames_" + job.stableId);
        activeJob_->tempVideoPath = activeJob_->tempFrameDirectory / "encoded.mp4";
        activeJob_->finalVideoPath = BuildFinalVideoPath(job);

        std::error_code ec;
        std::filesystem::remove_all(activeJob_->tempFrameDirectory, ec);
        ec.clear();
        std::filesystem::create_directories(activeJob_->tempFrameDirectory, ec);
        if (ec)
        {
            FailActiveJob(logger, "failed to prepare temp frame directory: " + ec.message());
            return true;
        }

        std::string shaderSource = ReadWholeFile(job.validPath);
        std::string buildLog;
        if (!activeJob_->program.Build(shaderSource, &buildLog))
        {
            FailActiveJob(logger, "shader failed to compile for offline rendering:\n" + buildLog);
            return true;
        }

        SetManifest(job.stableId, job.displayName, "rendering", activeJob_->finalVideoPath.string(), "writing frame sequence");
        SaveManifest();
        logger.Write("render", "started rendering " + job.displayName + " to " + activeJob_->finalVideoPath.string());
        stage_ = Stage::Rendering;
        return true;
    }

    stage_ = Stage::Idle;
    activeJob_.reset();
    return false;
}

void BatchRenderer::FailActiveJob(Logger& logger, const std::string& message)
{
    if (activeJob_)
    {
        SetManifest(activeJob_->job.stableId, activeJob_->job.displayName, "failed", activeJob_->finalVideoPath.string(), message);
        SaveManifest();
        logger.Write("render", "job failed for " + activeJob_->job.displayName + ": " + message);
    }

    ++queueIndex_;
    activeJob_.reset();
    stage_ = Stage::Idle;
}

void BatchRenderer::CompleteActiveJob(ShaderCatalog& catalog, Logger& logger)
{
    std::error_code ec;
    std::filesystem::remove_all(activeJob_->tempFrameDirectory, ec);

    SetManifest(activeJob_->job.stableId, activeJob_->job.displayName, "done", activeJob_->finalVideoPath.string(), "render complete");
    SaveManifest();

    catalog.MarkRendered(activeJob_->job.stableId, activeJob_->finalVideoPath, logger);
    logger.Write("render", "completed render for " + activeJob_->job.displayName + " -> " + activeJob_->finalVideoPath.string());

    ++queueIndex_;
    activeJob_.reset();
    stage_ = Stage::Idle;
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

BatchRenderer::ProcessResult BatchRenderer::RunFfmpeg(const std::filesystem::path& framesDirectory, const std::filesystem::path& outputPath, const RenderSettings& settings)
{
    if (framesDirectory.empty() && outputPath.empty())
    {
        ProcessResult result;
        FILE* pipe = _popen("ffmpeg -version 2>&1", "r");
        if (pipe == nullptr)
        {
            result.exitCode = -1;
            result.output = "failed to launch ffmpeg";
            return result;
        }

        char buffer[512];
        while (fgets(buffer, static_cast<int>(sizeof(buffer)), pipe) != nullptr)
        {
            result.output += buffer;
        }

        result.exitCode = _pclose(pipe);
        result.success = result.exitCode == 0;
        return result;
    }

    std::ostringstream command;
    command << BuildFfmpegCommand(framesDirectory, outputPath, settings) << " 2>&1";

    ProcessResult result;
    FILE* pipe = _popen(command.str().c_str(), "r");
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
