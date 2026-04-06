#include "AppFolders.h"

#include <array>

namespace
{
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
}

AppFolders AppFolders::Resolve(const std::optional<std::filesystem::path>& rootOverride)
{
    const std::filesystem::path root = rootOverride.has_value()
        ? std::filesystem::absolute(*rootOverride)
        : FindWorkspaceRoot(std::filesystem::current_path());

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
