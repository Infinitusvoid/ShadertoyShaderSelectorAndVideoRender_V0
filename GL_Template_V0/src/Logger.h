#pragma once

#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

struct LogLine
{
    std::string timestamp;
    std::string category;
    std::string message;
};

class Logger
{
public:
    Logger() = default;

    bool Initialize(const std::filesystem::path& logDirectory);
    void Write(const std::string& category, const std::string& message);
    std::vector<LogLine> RecentLines() const;
    const std::filesystem::path& Directory() const;

private:
    std::filesystem::path logDirectory_;
    mutable std::mutex mutex_;
    std::deque<LogLine> recentLines_;
};
