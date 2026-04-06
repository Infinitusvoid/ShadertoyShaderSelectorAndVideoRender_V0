#include "ShaderToolApp.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include <GL/glew.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

namespace
{
constexpr double kScanTickIntervalSeconds = 0.03;
constexpr double kThumbnailTickIntervalSeconds = 0.02;
constexpr float kGridCardWidth = 210.0f;
constexpr float kGridThumbnailWidth = 192.0f;
constexpr float kGridThumbnailHeight = 108.0f;

std::string ReadWholeFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

bool Pressed(GLFWwindow* window, int key, bool& latch)
{
    const bool down = glfwGetKey(window, key) == GLFW_PRESS;
    const bool pressed = down && !latch;
    latch = down;
    return pressed;
}
}

bool ShaderToolApp::Initialize(const Options& options)
{
    options_ = options;
    lastError_.clear();
    folders_ = AppFolders::Resolve(options_.rootOverride);
    if (!folders_.Ensure())
    {
        lastError_ = "Failed to create the application folders under:\n" + folders_.root.string();
        return false;
    }

    if (!logger_.Initialize(folders_.logs))
    {
        lastError_ = "Failed to initialize logging under:\n" + folders_.logs.string();
        return false;
    }

    std::string error;
    if (!InitializeWindow(&error))
    {
        lastError_ = error;
        logger_.Write("app", error);
        Shutdown();
        return false;
    }

    if (!renderer_.Initialize(&error))
    {
        lastError_ = error;
        logger_.Write("app", error);
        Shutdown();
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    batchRenderer_.Initialize(folders_, logger_);
    BeginScan();

    statusMessage_ = "Root folder: " + folders_.root.string();
    logger_.Write("app", "application initialized with root " + folders_.root.string());
    return true;
}

const std::string& ShaderToolApp::LastError() const
{
    return lastError_;
}

int ShaderToolApp::Run()
{
    previewStartTime_ = glfwGetTime();
    lastFrameTime_ = previewStartTime_;

    while (window_ != nullptr && !glfwWindowShouldClose(window_))
    {
        glfwPollEvents();
        HandleShortcuts();
        TickBackgroundWork();
        EnsureActiveShaderLoaded();

        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(window_, &framebufferWidth, &framebufferHeight);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (viewMode_ == ViewMode::Preview && activeIndex_ >= 0 && previewProgram_.IsReady())
        {
            renderer_.RenderToScreen(previewProgram_, framebufferWidth, framebufferHeight, BuildPreviewUniforms(framebufferWidth, framebufferHeight));
        }
        else
        {
            glViewport(0, 0, framebufferWidth, framebufferHeight);
            glClearColor(0.06f, 0.07f, 0.09f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        DrawUi();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window_);
    }

    Shutdown();
    return 0;
}

void ShaderToolApp::Shutdown()
{
    batchRenderer_.Update(catalog_, logger_);
    batchRenderer_.Shutdown();
    batchRenderer_.Update(catalog_, logger_);

    previewProgram_.Reset();
    if (window_ != nullptr)
    {
        thumbnails_.Reset(renderer_);
    }

    if (ImGui::GetCurrentContext() != nullptr)
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    renderer_.Shutdown();

    if (window_ != nullptr)
    {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }

    glfwTerminate();
}

bool ShaderToolApp::InitializeWindow(std::string* error)
{
    if (!glfwInit())
    {
        *error = "glfwInit failed";
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window_ = glfwCreateWindow(windowedPlacement_.width, windowedPlacement_.height, "Shader Selector And Offline Renderer", nullptr, nullptr);
    if (window_ == nullptr)
    {
        *error = "failed to create the GLFW window";
        return false;
    }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK)
    {
        *error = "glewInit failed";
        return false;
    }

    return true;
}

void ShaderToolApp::BeginScan()
{
    catalog_.BeginScan(folders_, logger_);
    activeIndex_ = -1;
    loadedPreviewStableId_.clear();
    previewCompileLog_.clear();
    previewProgram_.Reset();
    thumbnails_.Reset(renderer_);
    nextScanTickTime_ = glfwGetTime();
    nextThumbnailTickTime_ = glfwGetTime();
}

void ShaderToolApp::TickBackgroundWork()
{
    const double now = glfwGetTime();

    if (!batchRenderer_.IsBusy() && catalog_.IsScanInProgress() && now >= nextScanTickTime_)
    {
        catalog_.TickScan(renderer_, logger_);
        nextScanTickTime_ = glfwGetTime() + kScanTickIntervalSeconds;
    }

    batchRenderer_.Update(catalog_, logger_);

    if (!batchRenderer_.IsBusy()
        && !catalog_.IsScanInProgress()
        && viewMode_ == ViewMode::Grid
        && now >= nextThumbnailTickTime_)
    {
        thumbnails_.Tick(renderer_, catalog_.Records(), logger_);
        nextThumbnailTickTime_ = glfwGetTime() + kThumbnailTickIntervalSeconds;
    }
}

void ShaderToolApp::EnsureActiveShaderLoaded()
{
    const auto& records = catalog_.Records();
    if (records.empty())
    {
        activeIndex_ = -1;
        loadedPreviewStableId_.clear();
        previewProgram_.Reset();
        return;
    }

    if (activeIndex_ < 0 || activeIndex_ >= static_cast<int>(records.size()))
    {
        activeIndex_ = 0;
    }

    const ShaderRecord& record = records[static_cast<std::size_t>(activeIndex_)];
    if (record.stableId != loadedPreviewStableId_)
    {
        LoadPreviewShader(static_cast<std::size_t>(activeIndex_));
    }
}

void ShaderToolApp::LoadPreviewShader(std::size_t index)
{
    if (index >= catalog_.Records().size())
    {
        return;
    }

    const ShaderRecord& record = catalog_.Records()[index];
    std::string shaderSource = ReadWholeFile(record.validPath);
    std::string buildLog;
    GpuShaderProgram nextProgram;
    if (!nextProgram.Build(shaderSource, &buildLog))
    {
        previewProgram_.Reset();
        loadedPreviewStableId_.clear();
        previewCompileLog_ = buildLog;
        statusMessage_ = "Preview compile failed for " + record.displayName;
        logger_.Write("compile", "preview compile failed for " + record.displayName + ":\n" + buildLog);
        return;
    }

    previewProgram_ = std::move(nextProgram);
    loadedPreviewStableId_ = record.stableId;
    previewCompileLog_ = buildLog;
    previewStartTime_ = glfwGetTime();
    statusMessage_ = "Loaded preview for " + record.displayName;
}

void ShaderToolApp::HandleShortcuts()
{
    if (window_ == nullptr)
    {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput)
    {
        return;
    }

    const auto& records = catalog_.Records();

    if (Pressed(window_, GLFW_KEY_LEFT, leftLatch_) && !records.empty())
    {
        activeIndex_ = (activeIndex_ <= 0) ? static_cast<int>(records.size()) - 1 : activeIndex_ - 1;
        viewMode_ = ViewMode::Preview;
        LoadPreviewShader(static_cast<std::size_t>(activeIndex_));
    }

    if (Pressed(window_, GLFW_KEY_RIGHT, rightLatch_) && !records.empty())
    {
        activeIndex_ = (activeIndex_ + 1) % static_cast<int>(records.size());
        viewMode_ = ViewMode::Preview;
        LoadPreviewShader(static_cast<std::size_t>(activeIndex_));
    }

    if (Pressed(window_, GLFW_KEY_SPACE, spaceLatch_) && activeIndex_ >= 0)
    {
        std::string error;
        if (!catalog_.ToggleSelection(static_cast<std::size_t>(activeIndex_), logger_, &error))
        {
            statusMessage_ = error;
        }
    }

    if (Pressed(window_, GLFW_KEY_F, fLatch_))
    {
        ToggleFullscreen();
    }

    if (Pressed(window_, GLFW_KEY_R, rLatch_))
    {
        batchRenderer_.Start(renderSettings_, catalog_.Records(), logger_);
    }

    if (Pressed(window_, GLFW_KEY_G, gLatch_))
    {
        viewMode_ = (viewMode_ == ViewMode::Preview) ? ViewMode::Grid : ViewMode::Preview;
    }
}

void ShaderToolApp::ToggleFullscreen()
{
    fullscreen_ = !fullscreen_;

    if (fullscreen_)
    {
        glfwGetWindowPos(window_, &windowedPlacement_.x, &windowedPlacement_.y);
        glfwGetWindowSize(window_, &windowedPlacement_.width, &windowedPlacement_.height);

        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        glfwSetWindowMonitor(window_, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    }
    else
    {
        glfwSetWindowMonitor(window_, nullptr, windowedPlacement_.x, windowedPlacement_.y,
            windowedPlacement_.width, windowedPlacement_.height, 0);
    }
}

RuntimeUniforms ShaderToolApp::BuildPreviewUniforms(int framebufferWidth, int framebufferHeight)
{
    RuntimeUniforms uniforms;
    uniforms.width = framebufferWidth;
    uniforms.height = framebufferHeight;
    uniforms.time = static_cast<float>(glfwGetTime() - previewStartTime_);
    uniforms.timeDelta = static_cast<float>(glfwGetTime() - lastFrameTime_);
    uniforms.frame = static_cast<int>((glfwGetTime() - previewStartTime_) * 60.0);

    double mouseX = 0.0;
    double mouseY = 0.0;
    glfwGetCursorPos(window_, &mouseX, &mouseY);

    const bool mousePressed = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    if (mousePressed && !mouseDown_)
    {
        clickOrigin_[0] = static_cast<float>(mouseX);
        clickOrigin_[1] = static_cast<float>(framebufferHeight - mouseY);
    }

    uniforms.mouse = {
        static_cast<float>(mouseX),
        static_cast<float>(framebufferHeight - mouseY),
        clickOrigin_[0],
        clickOrigin_[1]
    };
    uniforms.date = BuildDateUniform(false);

    mouseDown_ = mousePressed;
    lastFrameTime_ = glfwGetTime();
    return uniforms;
}

std::array<float, 4> ShaderToolApp::BuildDateUniform(bool deterministicZero)
{
    if (deterministicZero)
    {
        return { 0.0f, 0.0f, 0.0f, 0.0f };
    }

    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_s(&localTime, &nowTime);

    return {
        static_cast<float>(1900 + localTime.tm_year),
        static_cast<float>(1 + localTime.tm_mon),
        static_cast<float>(localTime.tm_mday),
        static_cast<float>(localTime.tm_hour * 3600 + localTime.tm_min * 60 + localTime.tm_sec)
    };
}

void ShaderToolApp::DrawUi()
{
    DrawTopBar();
    DrawInspector();
    DrawRenderPanel();
    DrawScanPanel();
    DrawLogPanel();

    if (viewMode_ == ViewMode::Grid)
    {
        DrawGrid();
    }
    else
    {
        DrawPreviewOverlay();
    }
}

void ShaderToolApp::DrawTopBar()
{
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Controls"))
    {
        if (ImGui::Button("Rescan Input"))
        {
            BeginScan();
        }
        ImGui::SameLine();
        if (ImGui::Button(viewMode_ == ViewMode::Preview ? "Grid View" : "Preview View"))
        {
            viewMode_ = (viewMode_ == ViewMode::Preview) ? ViewMode::Grid : ViewMode::Preview;
        }
        ImGui::SameLine();
        if (ImGui::Button(fullscreen_ ? "Windowed" : "Fullscreen"))
        {
            ToggleFullscreen();
        }

        ImGui::TextWrapped("%s", statusMessage_.c_str());
        ImGui::TextWrapped("Root: %s", folders_.root.string().c_str());
        ImGui::Text("Shortcuts: Left/Right browse, Space queue, G grid, F fullscreen, R render");
    }
    ImGui::End();
}

void ShaderToolApp::DrawInspector()
{
    ImGui::SetNextWindowPos(ImVec2(10.0f, 150.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420.0f, 320.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Active Shader"))
    {
        const auto& records = catalog_.Records();
        if (activeIndex_ < 0 || activeIndex_ >= static_cast<int>(records.size()))
        {
            ImGui::TextUnformatted("No valid shaders yet.");
        }
        else
        {
            const ShaderRecord& record = records[static_cast<std::size_t>(activeIndex_)];
            ImGui::TextWrapped("%s", record.displayName.c_str());
            ImGui::Separator();
            ImGui::TextWrapped("Source: %s", record.sourcePath.string().c_str());
            ImGui::Text("Selected: %s", record.selected ? "yes" : "no");
            ImGui::Text("Rendered: %s", record.rendered ? "yes" : "no");
            ImGui::Text("Thumbnail: %s", std::filesystem::exists(record.thumbnailPath) ? "cached" : "pending");

            if (ImGui::Button(record.selected ? "Remove From Render Queue" : "Add To Render Queue"))
            {
                std::string error;
                if (!catalog_.ToggleSelection(static_cast<std::size_t>(activeIndex_), logger_, &error))
                {
                    statusMessage_ = error;
                }
            }

            if (viewMode_ == ViewMode::Grid)
            {
                ImGui::SameLine();
                if (ImGui::Button("Open Preview"))
                {
                    viewMode_ = ViewMode::Preview;
                }
            }

            for (const std::string& note : record.notes)
            {
                ImGui::BulletText("%s", note.c_str());
            }

            if (!previewCompileLog_.empty() && loadedPreviewStableId_ != record.stableId)
            {
                ImGui::Separator();
                ImGui::TextWrapped("%s", previewCompileLog_.c_str());
            }
        }
    }
    ImGui::End();
}

void ShaderToolApp::DrawGrid()
{
    ImGui::SetNextWindowPos(ImVec2(440.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(1200.0f, 900.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Shader Grid"))
    {
        char filterBuffer[256] = {};
        std::snprintf(filterBuffer, sizeof(filterBuffer), "%s", filterText_.c_str());
        if (ImGui::InputText("Filter", filterBuffer, sizeof(filterBuffer)))
        {
            filterText_ = filterBuffer;
        }

        const float availableWidth = ImGui::GetContentRegionAvail().x;
        const int columns = std::max(1, static_cast<int>(availableWidth / kGridCardWidth));
        const auto& records = catalog_.Records();

        std::string needle = filterText_;
        std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });

        std::vector<std::size_t> filteredIndices;
        filteredIndices.reserve(records.size());
        for (std::size_t index = 0; index < records.size(); ++index)
        {
            const ShaderRecord& record = records[index];
            if (!needle.empty())
            {
                std::string haystack = record.displayName + " " + record.sourcePath.string();
                std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](unsigned char value) {
                    return static_cast<char>(std::tolower(value));
                });

                if (haystack.find(needle) == std::string::npos)
                {
                    continue;
                }
            }

            filteredIndices.push_back(index);
        }

        if (ImGui::BeginTable("grid_table", columns))
        {
            ImGuiListClipper clipper;
            const int rowCount = static_cast<int>((filteredIndices.size() + static_cast<std::size_t>(columns) - 1) / static_cast<std::size_t>(columns));
            clipper.Begin(rowCount);
            while (clipper.Step())
            {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                {
                    ImGui::TableNextRow();
                    for (int column = 0; column < columns; ++column)
                    {
                        ImGui::TableSetColumnIndex(column);

                        const std::size_t filteredOffset = static_cast<std::size_t>(row) * static_cast<std::size_t>(columns) + static_cast<std::size_t>(column);
                        if (filteredOffset >= filteredIndices.size())
                        {
                            continue;
                        }

                        const std::size_t index = filteredIndices[filteredOffset];
                        const ShaderRecord& record = records[index];

                        thumbnails_.QueueIfNeeded(record);

                        ImGui::BeginGroup();

                        GLuint texture = thumbnails_.GetTextureFor(record, renderer_, logger_);
                        if (texture != 0)
                        {
                            if (ImGui::ImageButton(record.stableId.c_str(), reinterpret_cast<ImTextureID>(static_cast<intptr_t>(texture)),
                                ImVec2(kGridThumbnailWidth, kGridThumbnailHeight)))
                            {
                                activeIndex_ = static_cast<int>(index);
                                LoadPreviewShader(index);
                            }

                            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                            {
                                viewMode_ = ViewMode::Preview;
                            }
                        }
                        else if (ImGui::Button(("Pending##" + record.stableId).c_str(), ImVec2(kGridThumbnailWidth, kGridThumbnailHeight)))
                        {
                            activeIndex_ = static_cast<int>(index);
                            LoadPreviewShader(index);
                        }

                        ImGui::TextWrapped("%s", record.displayName.c_str());
                        ImGui::Text("%s | %s", record.selected ? "Queued" : "Not queued", record.rendered ? "Rendered" : "Not rendered");

                        const std::string queueLabel = std::string(record.selected ? "Unqueue##" : "Queue##") + record.stableId;
                        if (ImGui::Button(queueLabel.c_str()))
                        {
                            activeIndex_ = static_cast<int>(index);
                            std::string error;
                            if (!catalog_.ToggleSelection(index, logger_, &error))
                            {
                                statusMessage_ = error;
                            }
                        }

                        if (ImGui::IsItemHovered())
                        {
                            ImGui::BeginTooltip();
                            ImGui::TextWrapped("%s", record.sourcePath.string().c_str());
                            ImGui::EndTooltip();
                        }

                        ImGui::EndGroup();
                    }
                }
            }

            ImGui::EndTable();
        }
    }
    ImGui::End();
}

void ShaderToolApp::DrawScanPanel()
{
    ImGui::SetNextWindowPos(ImVec2(10.0f, 480.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420.0f, 260.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Scan Status"))
    {
        const ScanSummary& summary = catalog_.Summary();
        ImGui::Text("Queued: %llu", static_cast<unsigned long long>(summary.queued));
        ImGui::Text("Processed: %llu", static_cast<unsigned long long>(summary.processed));
        ImGui::Text("Valid: %llu", static_cast<unsigned long long>(summary.valid));
        ImGui::Text("Rejected: %llu", static_cast<unsigned long long>(summary.invalid));
        ImGui::Text("Ignored: %llu", static_cast<unsigned long long>(summary.ignored));
        ImGui::Text("Scanning: %s", summary.inProgress ? "yes" : "no");
        ImGui::Text("Pending thumbnails: %llu", static_cast<unsigned long long>(thumbnails_.PendingCount()));

        ImGui::Separator();
        ImGui::TextUnformatted("Recent rejects:");
        ImGui::BeginChild("issues", ImVec2(0.0f, 0.0f), true);
        const auto& issues = catalog_.Issues();
        for (auto it = issues.rbegin(); it != issues.rend() && std::distance(issues.rbegin(), it) < 10; ++it)
        {
            ImGui::TextWrapped("%s", it->path.filename().string().c_str());
            ImGui::TextWrapped("%s", it->reason.c_str());
            ImGui::Separator();
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

void ShaderToolApp::DrawRenderPanel()
{
    ImGui::SetNextWindowPos(ImVec2(10.0f, 750.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420.0f, 240.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Offline Render"))
    {
        const auto& records = catalog_.Records();
        const std::size_t queuedSelectionCount = static_cast<std::size_t>(std::count_if(records.begin(), records.end(),
            [](const ShaderRecord& record) {
                return record.selected;
            }));

        ImGui::InputInt("Width", &renderSettings_.width);
        ImGui::InputInt("Height", &renderSettings_.height);
        ImGui::InputInt("FPS", &renderSettings_.fps);
        ImGui::InputInt("Duration Seconds", &renderSettings_.durationSeconds);

        char codec[64] = {};
        std::snprintf(codec, sizeof(codec), "%s", renderSettings_.codec.c_str());
        if (ImGui::InputText("Codec", codec, sizeof(codec)))
        {
            renderSettings_.codec = codec;
        }

        char preset[64] = {};
        std::snprintf(preset, sizeof(preset), "%s", renderSettings_.preset.c_str());
        if (ImGui::InputText("Preset", preset, sizeof(preset)))
        {
            renderSettings_.preset = preset;
        }

        char pattern[256] = {};
        std::snprintf(pattern, sizeof(pattern), "%s", renderSettings_.filenamePattern.c_str());
        if (ImGui::InputText("Filename Pattern", pattern, sizeof(pattern)))
        {
            renderSettings_.filenamePattern = pattern;
        }

        ImGui::Separator();
        ImGui::Text("Queued shaders: %llu", static_cast<unsigned long long>(queuedSelectionCount));
        ImGui::Text("Batch jobs: %llu total | %llu done | %llu failed | %llu waiting",
            static_cast<unsigned long long>(batchRenderer_.TotalJobCount()),
            static_cast<unsigned long long>(batchRenderer_.CompletedJobCount()),
            static_cast<unsigned long long>(batchRenderer_.FailedJobCount()),
            static_cast<unsigned long long>(batchRenderer_.RemainingJobCount()));

        ImGui::TextWrapped("%s", batchRenderer_.FfmpegStatus().c_str());
        ImGui::BeginDisabled(batchRenderer_.IsBusy());
        if (ImGui::Button("Start Batch Render"))
        {
            batchRenderer_.Start(renderSettings_, catalog_.Records(), logger_);
        }
        ImGui::EndDisabled();

        if (batchRenderer_.IsBusy())
        {
            ImGui::ProgressBar(batchRenderer_.Progress(), ImVec2(-1.0f, 0.0f));
        }
        ImGui::TextWrapped("%s", batchRenderer_.StatusText().c_str());
    }
    ImGui::End();
}

void ShaderToolApp::DrawPreviewOverlay()
{
    ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.35f);

    if (ImGui::Begin("Preview Overlay", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        const auto& records = catalog_.Records();
        if (activeIndex_ >= 0 && activeIndex_ < static_cast<int>(records.size()))
        {
            const ShaderRecord& record = records[static_cast<std::size_t>(activeIndex_)];
            ImGui::TextWrapped("%s", record.displayName.c_str());
            ImGui::Text("%s | %s", record.selected ? "Queued" : "Not queued", record.rendered ? "Rendered" : "Not rendered");
        }
        else
        {
            ImGui::TextUnformatted("No active shader");
        }
    }
    ImGui::End();
}

void ShaderToolApp::DrawLogPanel()
{
    ImGui::SetNextWindowPos(ImVec2(1320.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320.0f, 980.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Logs"))
    {
        auto lines = logger_.RecentLines();
        for (auto it = lines.rbegin(); it != lines.rend(); ++it)
        {
            ImGui::TextWrapped("[%s] %s", it->category.c_str(), it->message.c_str());
        }
    }
    ImGui::End();
}
