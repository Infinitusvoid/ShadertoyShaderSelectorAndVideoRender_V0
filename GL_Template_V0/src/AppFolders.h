#pragma once

#include <filesystem>
#include <optional>

struct AppFolders
{
    std::filesystem::path root;
    std::filesystem::path shadersInput;
    std::filesystem::path shadersValid;
    std::filesystem::path shadersSelected;
    std::filesystem::path shadersRendered;
    std::filesystem::path videosRendered;
    std::filesystem::path logs;
    std::filesystem::path temp;
    std::filesystem::path thumbnails;

    static AppFolders Resolve(const std::optional<std::filesystem::path>& rootOverride);
    bool Ensure() const;
};
