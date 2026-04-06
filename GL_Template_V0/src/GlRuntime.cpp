#include "GlRuntime.h"

#include "../../External_libs/stb/image/stb_image.h"
#include "../../External_libs/stb/image/stb_image_write.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>

namespace
{
constexpr const char* kFullscreenVertexShader = R"(
#version 330 core

void main()
{
    const vec2 positions[3] = vec2[3](
        vec2(-1.0, -1.0),
        vec2(3.0, -1.0),
        vec2(-1.0, 3.0)
    );

    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
)";

GLuint CompileShader(GLenum type, const std::string& source, std::string* log)
{
    GLuint shader = glCreateShader(type);
    const char* sourcePointer = source.c_str();
    glShaderSource(shader, 1, &sourcePointer, nullptr);
    glCompileShader(shader);

    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

    GLint logLength = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
    if (logLength > 1)
    {
        std::string info(static_cast<std::size_t>(logLength), '\0');
        glGetShaderInfoLog(shader, logLength, nullptr, info.data());
        *log += info;
    }

    if (success != GL_TRUE)
    {
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

void FlipVertically(std::vector<unsigned char>* rgba, int width, int height)
{
    if (rgba == nullptr || rgba->empty())
    {
        return;
    }

    const std::size_t rowSize = static_cast<std::size_t>(width) * 4;
    std::vector<unsigned char> row(rowSize);

    for (int y = 0; y < height / 2; ++y)
    {
        unsigned char* top = rgba->data() + static_cast<std::size_t>(y) * rowSize;
        unsigned char* bottom = rgba->data() + static_cast<std::size_t>(height - 1 - y) * rowSize;
        std::copy(top, top + rowSize, row.data());
        std::copy(bottom, bottom + rowSize, top);
        std::copy(row.data(), row.data() + rowSize, bottom);
    }
}
}

GpuShaderProgram::~GpuShaderProgram()
{
    Reset();
}

GpuShaderProgram::GpuShaderProgram(GpuShaderProgram&& other) noexcept
{
    *this = std::move(other);
}

GpuShaderProgram& GpuShaderProgram::operator=(GpuShaderProgram&& other) noexcept
{
    if (this != &other)
    {
        Reset();
        program_ = other.program_;
        resolution_ = other.resolution_;
        time_ = other.time_;
        timeDelta_ = other.timeDelta_;
        frame_ = other.frame_;
        mouse_ = other.mouse_;
        date_ = other.date_;
        channelTime_ = other.channelTime_;
        channelResolution_ = other.channelResolution_;

        other.program_ = 0;
        other.resolution_ = -1;
        other.time_ = -1;
        other.timeDelta_ = -1;
        other.frame_ = -1;
        other.mouse_ = -1;
        other.date_ = -1;
        other.channelTime_ = -1;
        other.channelResolution_ = -1;
    }

    return *this;
}

bool GpuShaderProgram::Build(const std::string& fragmentSource, std::string* buildLog)
{
    Reset();

    if (buildLog != nullptr)
    {
        buildLog->clear();
    }

    std::string log;
    const GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, kFullscreenVertexShader, &log);
    if (vertexShader == 0)
    {
        if (buildLog != nullptr)
        {
            *buildLog = "vertex shader compile failed\n" + log;
        }
        return false;
    }

    const GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragmentSource, &log);
    if (fragmentShader == 0)
    {
        glDeleteShader(vertexShader);
        if (buildLog != nullptr)
        {
            *buildLog = "fragment shader compile failed\n" + log;
        }
        return false;
    }

    program_ = glCreateProgram();
    glAttachShader(program_, vertexShader);
    glAttachShader(program_, fragmentShader);
    glLinkProgram(program_);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint success = GL_FALSE;
    glGetProgramiv(program_, GL_LINK_STATUS, &success);

    GLint logLength = 0;
    glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &logLength);
    if (logLength > 1)
    {
        std::string info(static_cast<std::size_t>(logLength), '\0');
        glGetProgramInfoLog(program_, logLength, nullptr, info.data());
        log += info;
    }

    if (success != GL_TRUE)
    {
        Reset();
        if (buildLog != nullptr)
        {
            *buildLog = "program link failed\n" + log;
        }
        return false;
    }

    resolution_ = glGetUniformLocation(program_, "iResolution");
    time_ = glGetUniformLocation(program_, "iTime");
    timeDelta_ = glGetUniformLocation(program_, "iTimeDelta");
    frame_ = glGetUniformLocation(program_, "iFrame");
    mouse_ = glGetUniformLocation(program_, "iMouse");
    date_ = glGetUniformLocation(program_, "iDate");
    channelTime_ = glGetUniformLocation(program_, "iChannelTime");
    channelResolution_ = glGetUniformLocation(program_, "iChannelResolution");

    glUseProgram(program_);
    glUniform1i(glGetUniformLocation(program_, "iChannel0"), 0);
    glUniform1i(glGetUniformLocation(program_, "iChannel1"), 1);
    glUniform1i(glGetUniformLocation(program_, "iChannel2"), 2);
    glUniform1i(glGetUniformLocation(program_, "iChannel3"), 3);
    glUseProgram(0);

    if (buildLog != nullptr)
    {
        *buildLog = log;
    }

    return true;
}

void GpuShaderProgram::Reset()
{
    if (program_ != 0)
    {
        glDeleteProgram(program_);
        program_ = 0;
    }
}

bool GpuShaderProgram::IsReady() const
{
    return program_ != 0;
}

GLuint GpuShaderProgram::Id() const
{
    return program_;
}

void GpuShaderProgram::ApplyUniforms(const RuntimeUniforms& uniforms) const
{
    if (program_ == 0)
    {
        return;
    }

    if (resolution_ >= 0)
    {
        glUniform3f(resolution_, static_cast<float>(uniforms.width), static_cast<float>(uniforms.height), 1.0f);
    }

    if (time_ >= 0)
    {
        glUniform1f(time_, uniforms.time);
    }

    if (timeDelta_ >= 0)
    {
        glUniform1f(timeDelta_, uniforms.timeDelta);
    }

    if (frame_ >= 0)
    {
        glUniform1i(frame_, uniforms.frame);
    }

    if (mouse_ >= 0)
    {
        glUniform4f(mouse_, uniforms.mouse[0], uniforms.mouse[1], uniforms.mouse[2], uniforms.mouse[3]);
    }

    if (date_ >= 0)
    {
        glUniform4f(date_, uniforms.date[0], uniforms.date[1], uniforms.date[2], uniforms.date[3]);
    }

    if (channelTime_ >= 0)
    {
        const float values[4] = { uniforms.time, uniforms.time, uniforms.time, uniforms.time };
        glUniform1fv(channelTime_, 4, values);
    }

    if (channelResolution_ >= 0)
    {
        const GLfloat values[12] = {
            1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f
        };
        glUniform3fv(channelResolution_, 4, values);
    }
}

OpenGlRenderer::~OpenGlRenderer()
{
    Shutdown();
}

bool OpenGlRenderer::Initialize(std::string* error)
{
    return EnsureDrawResources(error);
}

void OpenGlRenderer::Shutdown()
{
    renderTarget_.Destroy();

    if (blackTexture_ != 0)
    {
        glDeleteTextures(1, &blackTexture_);
        blackTexture_ = 0;
    }

    if (dummyVao_ != 0)
    {
        glDeleteVertexArrays(1, &dummyVao_);
        dummyVao_ = 0;
    }
}

bool OpenGlRenderer::RenderToScreen(const GpuShaderProgram& program, int width, int height, const RuntimeUniforms& uniforms)
{
    if (!program.IsReady())
    {
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    DrawInternal(program, width, height, uniforms);
    return glGetError() == GL_NO_ERROR;
}

bool OpenGlRenderer::RenderToImage(const std::string& fragmentSource, int width, int height, const RuntimeUniforms& uniforms, std::vector<unsigned char>* pixels, std::string* error)
{
    GpuShaderProgram program;
    std::string buildLog;
    if (!program.Build(fragmentSource, &buildLog))
    {
        if (error != nullptr)
        {
            *error = buildLog;
        }
        return false;
    }

    return RenderWithProgram(program, width, height, uniforms, pixels, error);
}

bool OpenGlRenderer::RenderWithProgram(const GpuShaderProgram& program, int width, int height, const RuntimeUniforms& uniforms, std::vector<unsigned char>* pixels, std::string* error)
{
    if (!EnsureRenderTarget(width, height, error))
    {
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, renderTarget_.framebuffer);
    DrawInternal(program, width, height, uniforms);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        if (error != nullptr)
        {
            *error = "framebuffer became incomplete during render";
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    pixels->assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels->data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (glGetError() != GL_NO_ERROR)
    {
        if (error != nullptr)
        {
            *error = "OpenGL readback failed";
        }
        return false;
    }

    FlipVertically(pixels, width, height);
    return true;
}

bool OpenGlRenderer::SavePng(const std::filesystem::path& path, int width, int height, const std::vector<unsigned char>& rgba, std::string* error) const
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    const int stride = width * 4;
    const int written = stbi_write_png(path.string().c_str(), width, height, 4, rgba.data(), stride);
    if (written == 0)
    {
        if (error != nullptr)
        {
            *error = "stbi_write_png failed";
        }
        return false;
    }

    return true;
}

bool OpenGlRenderer::LoadTextureFromFile(const std::filesystem::path& path, GLuint* texture, int* width, int* height, std::string* error) const
{
    int imageWidth = 0;
    int imageHeight = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(path.string().c_str(), &imageWidth, &imageHeight, &channels, 4);
    if (pixels == nullptr)
    {
        if (error != nullptr)
        {
            *error = "failed to load thumbnail image";
        }
        return false;
    }

    GLuint loadedTexture = 0;
    glGenTextures(1, &loadedTexture);
    glBindTexture(GL_TEXTURE_2D, loadedTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, imageWidth, imageHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(pixels);

    *texture = loadedTexture;
    *width = imageWidth;
    *height = imageHeight;
    return true;
}

void OpenGlRenderer::DeleteTexture(GLuint texture) const
{
    if (texture != 0)
    {
        glDeleteTextures(1, &texture);
    }
}

void OpenGlRenderer::RenderTarget::Destroy()
{
    if (colorTexture != 0)
    {
        glDeleteTextures(1, &colorTexture);
        colorTexture = 0;
    }

    if (framebuffer != 0)
    {
        glDeleteFramebuffers(1, &framebuffer);
        framebuffer = 0;
    }

    width = 0;
    height = 0;
}

bool OpenGlRenderer::EnsureDrawResources(std::string* error)
{
    if (dummyVao_ == 0)
    {
        glGenVertexArrays(1, &dummyVao_);
    }

    if (blackTexture_ == 0)
    {
        const std::array<unsigned char, 4> black = { 0, 0, 0, 255 };
        glGenTextures(1, &blackTexture_);
        glBindTexture(GL_TEXTURE_2D, blackTexture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    if (glGetError() != GL_NO_ERROR)
    {
        if (error != nullptr)
        {
            *error = "failed to create OpenGL draw resources";
        }
        return false;
    }

    return true;
}

bool OpenGlRenderer::EnsureRenderTarget(int width, int height, std::string* error)
{
    if (!EnsureDrawResources(error))
    {
        return false;
    }

    if (renderTarget_.framebuffer != 0 && renderTarget_.width == width && renderTarget_.height == height)
    {
        return true;
    }

    renderTarget_.Destroy();

    glGenFramebuffers(1, &renderTarget_.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, renderTarget_.framebuffer);

    glGenTextures(1, &renderTarget_.colorTexture);
    glBindTexture(GL_TEXTURE_2D, renderTarget_.colorTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderTarget_.colorTexture, 0);

    renderTarget_.width = width;
    renderTarget_.height = height;

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        if (error != nullptr)
        {
            *error = "failed to create a complete framebuffer";
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        renderTarget_.Destroy();
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void OpenGlRenderer::DrawInternal(const GpuShaderProgram& program, int width, int height, const RuntimeUniforms& uniforms) const
{
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(program.Id());
    BindDefaultTextures();
    program.ApplyUniforms(uniforms);

    glBindVertexArray(dummyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
}

void OpenGlRenderer::BindDefaultTextures() const
{
    for (GLuint unit = 0; unit < 4; ++unit)
    {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, blackTexture_);
    }

    glActiveTexture(GL_TEXTURE0);
}
