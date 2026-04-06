#include "ShaderSanitizer.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace
{
constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

void AppendUtf8(char32_t codePoint, std::string* output)
{
    if (codePoint <= 0x7F)
    {
        output->push_back(static_cast<char>(codePoint));
    }
    else if (codePoint <= 0x7FF)
    {
        output->push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
        output->push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
    else if (codePoint <= 0xFFFF)
    {
        output->push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
        output->push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        output->push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
    else
    {
        output->push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
        output->push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
        output->push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        output->push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
}

std::string DecodeUtf16(const std::vector<unsigned char>& bytes, bool littleEndian)
{
    std::string output;
    for (std::size_t i = 2; i + 1 < bytes.size(); i += 2)
    {
        std::uint16_t value = littleEndian
            ? static_cast<std::uint16_t>(bytes[i] | (bytes[i + 1] << 8))
            : static_cast<std::uint16_t>((bytes[i] << 8) | bytes[i + 1]);

        if (value == 0)
        {
            continue;
        }

        AppendUtf8(value, &output);
    }

    return output;
}

std::string NormalizeNewlines(std::string text)
{
    text.erase(std::remove(text.begin(), text.end(), '\0'), text.end());

    std::string normalized;
    normalized.reserve(text.size());

    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '\r')
        {
            if (i + 1 < text.size() && text[i + 1] == '\n')
            {
                ++i;
            }

            normalized.push_back('\n');
            continue;
        }

        normalized.push_back(text[i]);
    }

    return normalized;
}

std::string StripCommentsForDetection(const std::string& text)
{
    std::string stripped;
    stripped.reserve(text.size());

    bool inBlockComment = false;
    bool inLineComment = false;

    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const char current = text[i];
        const char next = (i + 1 < text.size()) ? text[i + 1] : '\0';

        if (inBlockComment)
        {
            if (current == '*' && next == '/')
            {
                inBlockComment = false;
                ++i;
            }
            continue;
        }

        if (inLineComment)
        {
            if (current == '\n')
            {
                inLineComment = false;
                stripped.push_back('\n');
            }
            continue;
        }

        if (current == '/' && next == '*')
        {
            inBlockComment = true;
            ++i;
            continue;
        }

        if (current == '/' && next == '/')
        {
            inLineComment = true;
            ++i;
            continue;
        }

        stripped.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(current))));
    }

    return stripped;
}

std::string Trim(const std::string& text)
{
    const auto first = std::find_if_not(text.begin(), text.end(), [](unsigned char value) { return std::isspace(value) != 0; });
    const auto last = std::find_if_not(text.rbegin(), text.rend(), [](unsigned char value) { return std::isspace(value) != 0; }).base();
    return (first < last) ? std::string(first, last) : std::string();
}

std::string ReplaceAll(std::string text, const std::string& from, const std::string& to)
{
    std::size_t position = 0;
    while ((position = text.find(from, position)) != std::string::npos)
    {
        text.replace(position, from.size(), to);
        position += to.size();
    }
    return text;
}

std::string BuildPrelude(bool needsFragColor, bool needsWrapper)
{
    std::ostringstream stream;
    stream << "#version 330 core\n"
           << "uniform vec3 iResolution;\n"
           << "uniform float iTime;\n"
           << "uniform float iTimeDelta;\n"
           << "uniform int iFrame;\n"
           << "uniform vec4 iMouse;\n"
           << "uniform vec4 iDate;\n"
           << "uniform sampler2D iChannel0;\n"
           << "uniform sampler2D iChannel1;\n"
           << "uniform sampler2D iChannel2;\n"
           << "uniform sampler2D iChannel3;\n"
           << "uniform float iChannelTime[4];\n"
           << "uniform vec3 iChannelResolution[4];\n";

    if (needsFragColor || needsWrapper)
    {
        stream << "out vec4 FragColor;\n";
    }

    stream << "\n";
    return stream.str();
}

bool ContainsWord(const std::string& haystack, const std::string& word)
{
    const std::regex expression("\\b" + word + "\\b");
    return std::regex_search(haystack, expression);
}
}

bool ShaderSanitizer::IsClearlyUnsupportedExtension(const std::filesystem::path& path)
{
    static const std::unordered_set<std::string> ignored = {
        ".png", ".jpg", ".jpeg", ".bmp", ".gif", ".webp", ".avi", ".mp4", ".mpeg", ".mpg",
        ".mov", ".mkv", ".wav", ".mp3", ".ogg", ".flac", ".zip", ".7z", ".rar", ".dll",
        ".lib", ".exe", ".obj", ".pdb", ".ttf", ".otf"
    };

    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return ignored.contains(extension);
}

std::string ShaderSanitizer::MakeStableId(const std::filesystem::path& path)
{
    std::uint64_t hash = kFnvOffset;
    const std::string text = path.generic_string();

    for (const unsigned char value : text)
    {
        hash ^= value;
        hash *= kFnvPrime;
    }

    std::ostringstream stream;
    stream << std::hex << std::setw(16) << std::setfill('0') << hash;
    return stream.str();
}

std::string ShaderSanitizer::MakeOutputStem(const std::filesystem::path& path)
{
    std::string stem = path.stem().string();
    if (stem.empty())
    {
        stem = "shader";
    }

    for (char& value : stem)
    {
        if (!std::isalnum(static_cast<unsigned char>(value)) && value != '_' && value != '-')
        {
            value = '_';
        }
    }

    return stem + "__" + MakeStableId(path).substr(0, 8);
}

bool ShaderSanitizer::ReadTextFileLossy(const std::filesystem::path& path, std::string* text, std::string* error)
{
    constexpr std::size_t kMaxFileSize = 4 * 1024 * 1024;

    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        *error = "unable to open file";
        return false;
    }

    stream.seekg(0, std::ios::end);
    const std::streamoff size = stream.tellg();
    stream.seekg(0, std::ios::beg);

    if (size < 0)
    {
        *error = "failed to query file size";
        return false;
    }

    if (static_cast<std::size_t>(size) > kMaxFileSize)
    {
        *error = "file is too large for shader triage";
        return false;
    }

    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty())
    {
        stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    if (!stream.good() && !stream.eof())
    {
        *error = "failed to read file contents";
        return false;
    }

    if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE)
    {
        *text = NormalizeNewlines(DecodeUtf16(bytes, true));
        return true;
    }

    if (bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF)
    {
        *text = NormalizeNewlines(DecodeUtf16(bytes, false));
        return true;
    }

    std::size_t nullCount = 0;
    std::size_t controlCount = 0;
    for (const unsigned char value : bytes)
    {
        if (value == 0)
        {
            ++nullCount;
        }
        else if (value < 32 && value != '\n' && value != '\r' && value != '\t' && value != '\f')
        {
            ++controlCount;
        }
    }

    if (!bytes.empty() && (nullCount > bytes.size() / 32 || controlCount > bytes.size() / 16))
    {
        *error = "file looks binary rather than text";
        return false;
    }

    text->assign(bytes.begin(), bytes.end());
    *text = NormalizeNewlines(*text);
    return true;
}

bool ShaderSanitizer::LooksShaderLike(const std::string& text)
{
    const std::string stripped = StripCommentsForDetection(text);

    const bool hasEntryPoint = stripped.find("void mainimage") != std::string::npos
        || stripped.find("void main") != std::string::npos;
    const bool hasShaderSignals = stripped.find("gl_fragcoord") != std::string::npos
        || stripped.find("gl_fragcolor") != std::string::npos
        || stripped.find("iresolution") != std::string::npos
        || stripped.find("itime") != std::string::npos
        || stripped.find("fragcoord") != std::string::npos;

    return hasEntryPoint || hasShaderSignals;
}

SanitizedShader ShaderSanitizer::Sanitize(const std::string& rawText)
{
    SanitizedShader result;

    std::string text = rawText;
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF
        && static_cast<unsigned char>(text[1]) == 0xBB
        && static_cast<unsigned char>(text[2]) == 0xBF)
    {
        text.erase(0, 3);
        result.notes.push_back("removed UTF-8 BOM");
    }

    text = NormalizeNewlines(text);

    const std::string strippedForDetection = StripCommentsForDetection(text);
    const bool hasMainImage = strippedForDetection.find("void mainimage") != std::string::npos;
    const bool hasMain = strippedForDetection.find("void main(") != std::string::npos;

    if (!hasMainImage && !hasMain)
    {
        result.rejectionReason = "no supported fragment shader entry point was found";
        return result;
    }

    const std::regex versionLine(R"(^\s*#\s*version\b)", std::regex_constants::icase);
    const std::regex precisionLine(R"(^\s*precision\s+\w+\s+\w+\s*;)", std::regex_constants::icase);
    const std::regex builtinUniformLine(
        R"(^\s*uniform\b.*\b(iResolution|iTime|iTimeDelta|iFrame|iMouse|iDate|iChannel0|iChannel1|iChannel2|iChannel3|iChannelTime|iChannelResolution)\b.*;)",
        std::regex_constants::icase);
    const std::regex unsupportedExtensionLine(
        R"(^\s*#\s*extension\s+GL_(OES_standard_derivatives|EXT_shader_texture_lod|EXT_frag_depth)\b.*)",
        std::regex_constants::icase);

    std::istringstream input(text);
    std::ostringstream body;
    std::string line;

    while (std::getline(input, line))
    {
        const std::string trimmed = Trim(line);
        if (trimmed.empty())
        {
            body << '\n';
            continue;
        }

        if (std::regex_search(line, versionLine))
        {
            result.notes.push_back("removed source #version directive");
            continue;
        }

        if (std::regex_search(line, precisionLine))
        {
            result.notes.push_back("removed GLES precision qualifier");
            continue;
        }

        if (std::regex_search(line, builtinUniformLine))
        {
            result.notes.push_back("removed conflicting built-in uniform declaration: " + trimmed);
            continue;
        }

        if (std::regex_search(line, unsupportedExtensionLine))
        {
            result.notes.push_back("removed unsupported GLES extension directive");
            continue;
        }

        body << line << '\n';
    }

    std::string bodyText = body.str();
    const std::string originalBody = bodyText;

    bodyText = ReplaceAll(bodyText, "texture2D(", "texture(");
    bodyText = ReplaceAll(bodyText, "textureCube(", "texture(");
    bodyText = ReplaceAll(bodyText, "gl_FragColor", "FragColor");

    if (bodyText != originalBody)
    {
        result.notes.push_back("applied legacy GLSL token replacements");
    }

    const bool needsWrapper = hasMainImage && !hasMain;
    const bool needsFragColor = ContainsWord(bodyText, "FragColor") && !ContainsWord(StripCommentsForDetection(bodyText), "out vec4 fragcolor");

    std::ostringstream finalSource;
    finalSource << BuildPrelude(needsFragColor, needsWrapper) << bodyText;

    if (needsWrapper)
    {
        finalSource << "\nvoid main()\n"
                    << "{\n"
                    << "    vec4 __codexColor = vec4(0.0);\n"
                    << "    mainImage(__codexColor, gl_FragCoord.xy);\n"
                    << "    FragColor = __codexColor;\n"
                    << "}\n";
        result.entryPoint = ShaderEntryPoint::MainImage;
    }
    else
    {
        result.entryPoint = ShaderEntryPoint::MainFunction;
    }

    result.supported = true;
    result.source = finalSource.str();
    return result;
}
