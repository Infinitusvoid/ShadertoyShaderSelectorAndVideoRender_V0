#pragma once

#include "ShaderModel.h"

#include <filesystem>
#include <string>

class ShaderSanitizer
{
public:
    static bool IsClearlyUnsupportedExtension(const std::filesystem::path& path);
    static std::string MakeStableId(const std::filesystem::path& path);
    static std::string MakeOutputStem(const std::filesystem::path& path);
    static bool ReadTextFileLossy(const std::filesystem::path& path, std::string* text, std::string* error);
    static bool LooksShaderLike(const std::string& text);
    static SanitizedShader Sanitize(const std::string& rawText);
};
