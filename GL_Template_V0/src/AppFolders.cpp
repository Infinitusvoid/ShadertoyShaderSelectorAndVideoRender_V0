#define NOMINMAX
#include <Windows.h>

#include "AppFolders.h"

#include <array>

namespace
{
std::filesystem::path ExecutableDirectory()
{
    std::array<wchar_t, 32768> buffer{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size())
    {
        return std::filesystem::current_path();
    }

    return std::filesystem::path(buffer.data(), buffer.data() + length).parent_path();
}

std::filesystem::path FindWorkspaceRoot(const std::filesystem::path& start)
{
    std::filesystem::path current = std::filesystem::absolute(start);

    for (int depth = 0; depth < 8; ++depth)
    {
        if (std::filesystem::exists(current / "ShadertoyShaderSelectorAndVideoRender_V0.sln"))
        {
            return current;
        }

        if (!current.has_parent_path())
        {
            break;
        }

        const std::filesystem::path parent = current.parent_path();
        if (parent == current)
        {
            break;
        }

        current = parent;
    }

    return std::filesystem::absolute(start);
}

bool IsBuildOutputDirectory(const std::filesystem::path& executableDirectory, const std::filesystem::path& workspaceRoot)
{
    std::error_code ec;
    const std::filesystem::path relative = std::filesystem::absolute(executableDirectory).lexically_relative(std::filesystem::absolute(workspaceRoot));
    if (ec || relative.empty())
    {
        return false;
    }

    auto part = relative.begin();
    if (part == relative.end())
    {
        return false;
    }

    return *part == "build";
}
}

AppFolders AppFolders::Resolve(const std::optional<std::filesystem::path>& rootOverride)
{
    const std::filesystem::path executableDirectory = ExecutableDirectory();
    std::filesystem::path root;
    if (rootOverride.has_value())
    {
        root = std::filesystem::absolute(*rootOverride);
    }
    else
    {
        const std::filesystem::path workspaceRoot = FindWorkspaceRoot(executableDirectory);
        root = IsBuildOutputDirectory(executableDirectory, workspaceRoot)
            ? workspaceRoot
            : std::filesystem::absolute(executableDirectory);
    }

    return AppFolders{
        root,
        root / "shaders_input",
        root / "shaders_valid",
        root / "shaders_selected_for_rendering",
        root / "shaders_rendered",
        root / "video_rendered",
        root / "logs",
        root / "temp",
        root / "thumbnails"
    };
}

bool AppFolders::Ensure() const
{
    const std::array<std::filesystem::path, 8> directories = {
        shadersInput,
        shadersValid,
        shadersSelected,
        shadersRendered,
        videosRendered,
        logs,
        temp,
        thumbnails
    };

    for (const auto& directory : directories)
    {
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        if (ec)
        {
            return false;
        }
    }

    return true;
}
