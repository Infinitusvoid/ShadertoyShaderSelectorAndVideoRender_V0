#include "Logger.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace
{
std::string TimestampNow()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_s(&localTime, &nowTime);

    std::ostringstream stream;
    stream << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return stream.str();
}
}

bool Logger::Initialize(const std::filesystem::path& logDirectory)
{
    logDirectory_ = logDirectory;
    std::error_code ec;
    std::filesystem::create_directories(logDirectory_, ec);
    return !ec;
}

void Logger::Write(const std::string& category, const std::string& message)
{
    const LogLine line{ TimestampNow(), category, message };

    std::lock_guard<std::mutex> guard(mutex_);

    recentLines_.push_back(line);
    while (recentLines_.size() > 300)
    {
        recentLines_.pop_front();
    }

    const std::string formatted = "[" + line.timestamp + "] [" + line.category + "] " + line.message + "\n";

    if (!logDirectory_.empty())
    {
        {
            std::ofstream stream(logDirectory_ / "app.log", std::ios::app | std::ios::binary);
            stream << formatted;
        }

        std::ofstream categoryStream(logDirectory_ / (category + ".log"), std::ios::app | std::ios::binary);
        categoryStream << formatted;
    }
}

std::vector<LogLine> Logger::RecentLines() const
{
    std::lock_guard<std::mutex> guard(mutex_);
    return std::vector<LogLine>(recentLines_.begin(), recentLines_.end());
}

const std::filesystem::path& Logger::Directory() const
{
    return logDirectory_;
}
