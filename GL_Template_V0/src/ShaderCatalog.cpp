#include "ShaderCatalog.h"

#include "ShaderSanitizer.h"

#include <algorithm>
#include <fstream>

void ShaderCatalog::BeginScan(const AppFolders& folders, Logger& logger)
{
    folders_ = folders;
    pendingCandidates_.clear();
    nextCandidateIndex_ = 0;
    records_.clear();
    issues_.clear();
    summary_ = {};
    summary_.inProgress = true;

    std::error_code ec;
    if (!std::filesystem::exists(folders_.shadersInput, ec))
    {
        logger.Write("scan", "shader input folder does not exist yet: " + folders_.shadersInput.string());
        summary_.inProgress = false;
        return;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(folders_.shadersInput, ec))
    {
        if (ec)
        {
            logger.Write("scan", "failed while enumerating input folder: " + ec.message());
            break;
        }

        if (!entry.is_regular_file())
        {
            continue;
        }

        const std::filesystem::path path = entry.path();
        if (ShaderSanitizer::IsClearlyUnsupportedExtension(path))
        {
            ++summary_.ignored;
            logger.Write("ignored", "ignored unsupported file type: " + path.string());
            continue;
        }

        PendingCandidate candidate;
        candidate.sourcePath = path;
        candidate.stableId = ShaderSanitizer::MakeStableId(path.lexically_relative(folders_.root));
        candidate.outputStem = ShaderSanitizer::MakeOutputStem(path);
        pendingCandidates_.push_back(std::move(candidate));
    }

    summary_.queued = pendingCandidates_.size();
    logger.Write("scan", "queued " + std::to_string(summary_.queued) + " candidate files for validation");
}

bool ShaderCatalog::TickScan(OpenGlRenderer& renderer, Logger& logger)
{
    if (!summary_.inProgress)
    {
        return false;
    }

    if (nextCandidateIndex_ >= pendingCandidates_.size())
    {
        summary_.inProgress = false;
        SortRecords();
        logger.Write("scan", "scan finished with " + std::to_string(summary_.valid) + " valid shaders and "
            + std::to_string(summary_.invalid) + " rejected candidates");
        return false;
    }

    const PendingCandidate& candidate = pendingCandidates_[nextCandidateIndex_++];

    std::string rawText;
    std::string loadError;
    if (!ShaderSanitizer::ReadTextFileLossy(candidate.sourcePath, &rawText, &loadError))
    {
        ++summary_.invalid;
        ++summary_.processed;
        issues_.push_back({ candidate.sourcePath, loadError });
        logger.Write("scan", "rejected " + candidate.sourcePath.string() + ": " + loadError);
        return true;
    }

    if (!ShaderSanitizer::LooksShaderLike(rawText))
    {
        ++summary_.invalid;
        ++summary_.processed;
        issues_.push_back({ candidate.sourcePath, "text file does not look like a supported single-pass shader" });
        logger.Write("scan", "rejected " + candidate.sourcePath.string() + ": not shader-like");
        return true;
    }

    SanitizedShader sanitized = ShaderSanitizer::Sanitize(rawText);
    if (!sanitized.supported)
    {
        ++summary_.invalid;
        ++summary_.processed;
        issues_.push_back({ candidate.sourcePath, sanitized.rejectionReason });
        logger.Write("scan", "rejected " + candidate.sourcePath.string() + ": " + sanitized.rejectionReason);
        return true;
    }

    RuntimeUniforms uniforms;
    uniforms.width = 16;
    uniforms.height = 16;
    uniforms.time = 1.0f;
    uniforms.timeDelta = 1.0f / 60.0f;
    uniforms.frame = 60;

    std::vector<unsigned char> pixels;
    std::string compileError;
    if (!renderer.RenderToImage(sanitized.source, 16, 16, uniforms, &pixels, &compileError))
    {
        ++summary_.invalid;
        ++summary_.processed;
        issues_.push_back({ candidate.sourcePath, compileError });
        logger.Write("compile", "rejected " + candidate.sourcePath.string() + " due to compile/link/render failure:\n" + compileError);
        return true;
    }

    const std::filesystem::path validPath = folders_.shadersValid / (candidate.outputStem + ".glsl");
    {
        std::ofstream stream(validPath, std::ios::binary);
        stream << sanitized.source;
    }

    ShaderRecord record;
    record.stableId = candidate.stableId;
    record.displayName = candidate.sourcePath.filename().string();
    record.sourcePath = candidate.sourcePath;
    record.validPath = validPath;
    record.selectedPath = folders_.shadersSelected / validPath.filename();
    record.renderedShaderPath = folders_.shadersRendered / validPath.filename();
    record.videoPath = folders_.videosRendered / (candidate.outputStem + ".mp4");
    record.thumbnailPath = folders_.thumbnails / (candidate.outputStem + ".png");
    record.notes = std::move(sanitized.notes);
    record.selected = std::filesystem::exists(record.selectedPath);
    record.rendered = std::filesystem::exists(record.renderedShaderPath) || std::filesystem::exists(record.videoPath);

    for (const std::string& note : record.notes)
    {
        logger.Write("sanitize", candidate.sourcePath.string() + ": " + note);
    }

    records_.push_back(std::move(record));
    ++summary_.valid;
    ++summary_.processed;
    logger.Write("scan", "validated shader: " + candidate.sourcePath.string());
    return true;
}

const std::vector<ShaderRecord>& ShaderCatalog::Records() const
{
    return records_;
}

const std::vector<ScanIssue>& ShaderCatalog::Issues() const
{
    return issues_;
}

const ScanSummary& ShaderCatalog::Summary() const
{
    return summary_;
}

bool ShaderCatalog::IsScanInProgress() const
{
    return summary_.inProgress;
}

bool ShaderCatalog::ToggleSelection(std::size_t index, Logger& logger, std::string* error)
{
    if (index >= records_.size())
    {
        if (error != nullptr)
        {
            *error = "invalid shader index";
        }
        return false;
    }

    ShaderRecord& record = records_[index];
    std::error_code ec;

    if (!record.selected)
    {
        std::filesystem::copy_file(record.validPath, record.selectedPath, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
        {
            if (error != nullptr)
            {
                *error = "failed to copy selected shader to queue folder: " + ec.message();
            }
            return false;
        }

        record.selected = true;
        logger.Write("selection", "selected shader " + record.displayName);
    }
    else
    {
        std::filesystem::remove(record.selectedPath, ec);
        if (ec)
        {
            if (error != nullptr)
            {
                *error = "failed to remove selected shader from queue folder: " + ec.message();
            }
            return false;
        }

        record.selected = false;
        logger.Write("selection", "deselected shader " + record.displayName);
    }

    return true;
}

void ShaderCatalog::MarkRendered(const std::string& stableId, const std::filesystem::path& finalVideoPath, Logger& logger)
{
    const int index = FindIndexById(stableId);
    if (index < 0)
    {
        return;
    }

    ShaderRecord& record = records_[static_cast<std::size_t>(index)];
    std::error_code ec;
    std::filesystem::copy_file(record.validPath, record.renderedShaderPath, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
    {
        logger.Write("render", "failed to archive rendered shader copy for " + record.displayName + ": " + ec.message());
    }

    std::filesystem::remove(record.selectedPath, ec);
    if (ec)
    {
        logger.Write("render", "failed to remove queued shader after render for " + record.displayName + ": " + ec.message());
    }

    record.selected = false;
    record.rendered = true;
    record.videoPath = finalVideoPath;
}

int ShaderCatalog::FindIndexById(const std::string& stableId) const
{
    for (std::size_t index = 0; index < records_.size(); ++index)
    {
        if (records_[index].stableId == stableId)
        {
            return static_cast<int>(index);
        }
    }

    return -1;
}

void ShaderCatalog::SortRecords()
{
    std::sort(records_.begin(), records_.end(), [](const ShaderRecord& left, const ShaderRecord& right) {
        if (left.displayName == right.displayName)
        {
            return left.sourcePath.string() < right.sourcePath.string();
        }
        return left.displayName < right.displayName;
    });
}
