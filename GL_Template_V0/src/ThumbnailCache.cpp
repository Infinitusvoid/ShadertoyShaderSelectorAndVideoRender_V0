#include "ThumbnailCache.h"

#include <fstream>
#include <sstream>

namespace
{
std::string ReadWholeFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}
}

ThumbnailCache::~ThumbnailCache() = default;

void ThumbnailCache::Reset(OpenGlRenderer& renderer)
{
    for (const auto& [id, texture] : textures_)
    {
        renderer.DeleteTexture(texture.texture);
    }

    textures_.clear();
    generationQueue_.clear();
    queuedIds_.clear();
}

void ThumbnailCache::QueueMissing(const std::vector<ShaderRecord>& records)
{
    for (const ShaderRecord& record : records)
    {
        std::error_code ec;
        const bool missing = !std::filesystem::exists(record.thumbnailPath, ec);
        const bool stale = !missing
            && std::filesystem::last_write_time(record.thumbnailPath, ec) < std::filesystem::last_write_time(record.validPath, ec);

        if ((missing || stale) && !queuedIds_.contains(record.stableId))
        {
            generationQueue_.push_back(record.stableId);
            queuedIds_.insert(record.stableId);
        }
    }
}

bool ThumbnailCache::Tick(OpenGlRenderer& renderer, const std::vector<ShaderRecord>& records, Logger& logger)
{
    if (generationQueue_.empty())
    {
        return false;
    }

    const std::string stableId = generationQueue_.front();
    generationQueue_.pop_front();
    queuedIds_.erase(stableId);

    const ShaderRecord* record = FindById(records, stableId);
    if (record == nullptr)
    {
        return true;
    }

    const std::string shaderSource = ReadWholeFile(record->validPath);

    RuntimeUniforms uniforms;
    uniforms.width = 320;
    uniforms.height = 180;
    uniforms.time = 1.5f;
    uniforms.timeDelta = 1.0f / 60.0f;
    uniforms.frame = 90;

    std::vector<unsigned char> pixels;
    std::string error;
    if (!renderer.RenderToImage(shaderSource, 320, 180, uniforms, &pixels, &error))
    {
        logger.Write("thumbnail", "failed to render thumbnail for " + record->displayName + ": " + error);
        return true;
    }

    if (!renderer.SavePng(record->thumbnailPath, 320, 180, pixels, &error))
    {
        logger.Write("thumbnail", "failed to save thumbnail for " + record->displayName + ": " + error);
        return true;
    }

    auto existing = textures_.find(stableId);
    if (existing != textures_.end())
    {
        renderer.DeleteTexture(existing->second.texture);
        textures_.erase(existing);
    }

    logger.Write("thumbnail", "generated thumbnail for " + record->displayName);
    return true;
}

GLuint ThumbnailCache::GetTextureFor(const ShaderRecord& record, OpenGlRenderer& renderer, Logger& logger)
{
    std::error_code ec;
    if (!std::filesystem::exists(record.thumbnailPath, ec))
    {
        return 0;
    }

    const std::filesystem::file_time_type currentFileTime = std::filesystem::last_write_time(record.thumbnailPath, ec);

    auto existing = textures_.find(record.stableId);
    if (existing != textures_.end() && existing->second.fileTime == currentFileTime)
    {
        return existing->second.texture;
    }

    if (existing != textures_.end())
    {
        renderer.DeleteTexture(existing->second.texture);
        textures_.erase(existing);
    }

    GLuint texture = 0;
    int width = 0;
    int height = 0;
    std::string error;
    if (!renderer.LoadTextureFromFile(record.thumbnailPath, &texture, &width, &height, &error))
    {
        logger.Write("thumbnail", "failed to load thumbnail texture for " + record.displayName + ": " + error);
        return 0;
    }

    textures_[record.stableId] = LoadedTexture{ texture, currentFileTime };
    return texture;
}

const ShaderRecord* ThumbnailCache::FindById(const std::vector<ShaderRecord>& records, const std::string& stableId)
{
    for (const ShaderRecord& record : records)
    {
        if (record.stableId == stableId)
        {
            return &record;
        }
    }

    return nullptr;
}
