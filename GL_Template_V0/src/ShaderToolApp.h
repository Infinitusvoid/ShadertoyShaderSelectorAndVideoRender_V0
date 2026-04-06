#pragma once

#include "AppFolders.h"
#include "BatchRenderer.h"
#include "GlRuntime.h"
#include "Logger.h"
#include "ShaderCatalog.h"
#include "ThumbnailCache.h"

#include <GLFW/glfw3.h>

#include <array>
#include <filesystem>
#include <optional>
#include <string>

class ShaderToolApp
{
public:
    struct Options
    {
        std::optional<std::filesystem::path> rootOverride;
    };

    bool Initialize(const Options& options);
    int Run();

private:
    enum class ViewMode
    {
        Preview,
        Grid
    };

    struct WindowedPlacement
    {
        int x = 60;
        int y = 60;
        int width = 1600;
        int height = 1000;
    };

    void Shutdown();
    bool InitializeWindow(std::string* error);
    void BeginScan();
    void TickBackgroundWork();
    void EnsureActiveShaderLoaded();
    void LoadPreviewShader(std::size_t index);
    void HandleShortcuts();
    void ToggleFullscreen();

    RuntimeUniforms BuildPreviewUniforms(int framebufferWidth, int framebufferHeight);
    static std::array<float, 4> BuildDateUniform(bool deterministicZero);

    void DrawUi();
    void DrawTopBar();
    void DrawInspector();
    void DrawGrid();
    void DrawScanPanel();
    void DrawRenderPanel();
    void DrawPreviewOverlay();
    void DrawLogPanel();

    Options options_;
    AppFolders folders_;
    Logger logger_;
    GLFWwindow* window_ = nullptr;
    OpenGlRenderer renderer_;
    ShaderCatalog catalog_;
    ThumbnailCache thumbnails_;
    BatchRenderer batchRenderer_;
    GpuShaderProgram previewProgram_;

    std::string loadedPreviewStableId_;
    std::string previewCompileLog_;
    std::string statusMessage_;
    std::string filterText_;

    RenderSettings renderSettings_;
    ViewMode viewMode_ = ViewMode::Preview;
    int activeIndex_ = -1;
    bool fullscreen_ = false;
    WindowedPlacement windowedPlacement_;
    double previewStartTime_ = 0.0;
    double lastFrameTime_ = 0.0;
    bool mouseDown_ = false;
    std::array<float, 2> clickOrigin_{ 0.0f, 0.0f };

    bool leftLatch_ = false;
    bool rightLatch_ = false;
    bool spaceLatch_ = false;
    bool fLatch_ = false;
    bool rLatch_ = false;
    bool gLatch_ = false;
};
