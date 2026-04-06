#pragma once

#include "ShaderModel.h"

#include <GL/glew.h>

#include <filesystem>
#include <string>
#include <vector>

class GpuShaderProgram
{
public:
    GpuShaderProgram() = default;
    ~GpuShaderProgram();

    GpuShaderProgram(const GpuShaderProgram&) = delete;
    GpuShaderProgram& operator=(const GpuShaderProgram&) = delete;

    GpuShaderProgram(GpuShaderProgram&& other) noexcept;
    GpuShaderProgram& operator=(GpuShaderProgram&& other) noexcept;

    bool Build(const std::string& fragmentSource, std::string* buildLog);
    void Reset();
    bool IsReady() const;
    GLuint Id() const;
    void ApplyUniforms(const RuntimeUniforms& uniforms) const;

private:
    GLuint program_ = 0;
    GLint resolution_ = -1;
    GLint time_ = -1;
    GLint timeDelta_ = -1;
    GLint frame_ = -1;
    GLint mouse_ = -1;
    GLint date_ = -1;
    GLint channelTime_ = -1;
    GLint channelResolution_ = -1;
};

class OpenGlRenderer
{
public:
    OpenGlRenderer() = default;
    ~OpenGlRenderer();

    bool Initialize(std::string* error);
    void Shutdown();

    bool RenderToScreen(const GpuShaderProgram& program, int width, int height, const RuntimeUniforms& uniforms);
    bool RenderToImage(const std::string& fragmentSource, int width, int height, const RuntimeUniforms& uniforms, std::vector<unsigned char>* pixels, std::string* error);
    bool RenderWithProgram(const GpuShaderProgram& program, int width, int height, const RuntimeUniforms& uniforms, std::vector<unsigned char>* pixels, std::string* error);

    bool SavePng(const std::filesystem::path& path, int width, int height, const std::vector<unsigned char>& rgba, std::string* error) const;
    bool LoadTextureFromFile(const std::filesystem::path& path, GLuint* texture, int* width, int* height, std::string* error) const;
    void DeleteTexture(GLuint texture) const;

private:
    struct RenderTarget
    {
        GLuint framebuffer = 0;
        GLuint colorTexture = 0;
        int width = 0;
        int height = 0;

        void Destroy();
    };

    bool EnsureDrawResources(std::string* error);
    bool EnsureRenderTarget(int width, int height, std::string* error);
    void DrawInternal(const GpuShaderProgram& program, int width, int height, const RuntimeUniforms& uniforms) const;
    void BindDefaultTextures() const;

    GLuint dummyVao_ = 0;
    GLuint blackTexture_ = 0;
    RenderTarget renderTarget_;
};
