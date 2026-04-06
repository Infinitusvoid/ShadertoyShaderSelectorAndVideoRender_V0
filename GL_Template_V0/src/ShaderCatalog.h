#pragma once

#include "AppFolders.h"
#include "GlRuntime.h"
#include "Logger.h"
#include "ShaderModel.h"

#include <filesystem>
#include <string>
#include <vector>

class ShaderCatalog
{
public:
    void BeginScan(const AppFolders& folders, Logger& logger);
    bool TickScan(OpenGlRenderer& renderer, Logger& logger);

    const std::vector<ShaderRecord>& Records() const;
    const std::vector<ScanIssue>& Issues() const;
    const ScanSummary& Summary() const;
    bool IsScanInProgress() const;

    bool ToggleSelection(std::size_t index, Logger& logger, std::string* error);
    void MarkRendered(const std::string& stableId, const std::filesystem::path& finalVideoPath, Logger& logger);
    int FindIndexById(const std::string& stableId) const;

private:
    struct PendingCandidate
    {
        std::filesystem::path sourcePath;
        std::string stableId;
        std::string outputStem;
    };

    void SortRecords();

    AppFolders folders_;
    std::vector<PendingCandidate> pendingCandidates_;
    std::size_t nextCandidateIndex_ = 0;
    std::vector<ShaderRecord> records_;
    std::vector<ScanIssue> issues_;
    ScanSummary summary_;
};
