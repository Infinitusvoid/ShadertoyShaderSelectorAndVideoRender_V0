#pragma once

#include "GlRuntime.h"
#include "Logger.h"
#include "ShaderModel.h"

#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>

class ThumbnailCache
{
public:
    ~ThumbnailCache();

    void Reset(OpenGlRenderer& renderer);
    void QueueMissing(const std::vector<ShaderRecord>& records);
    bool Tick(OpenGlRenderer& renderer, const std::vector<ShaderRecord>& records, Logger& logger);
    GLuint GetTextureFor(const ShaderRecord& record, OpenGlRenderer& renderer, Logger& logger);

private:
    struct LoadedTexture
    {
        GLuint texture = 0;
        std::filesystem::file_time_type fileTime{};
    };

    static const ShaderRecord* FindById(const std::vector<ShaderRecord>& records, const std::string& stableId);

    std::deque<std::string> generationQueue_;
    std::unordered_set<std::string> queuedIds_;
    std::unordered_map<std::string, LoadedTexture> textures_;
};
