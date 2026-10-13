#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include "util/JsonValue.hpp"

namespace taskforge {
namespace pi {

struct PiSkillInfo {
    std::string id;
    std::string name;
    std::string path;
    std::string description;
};

struct PiStageInfo {
    std::string id;
    std::string name;
    std::string model;
    std::string skillPath;
    std::string prompt;
    std::string validationTarget;
    int minSizeBytes = 0;
};

struct PiProjectScanResult {
    bool isValid = false;
    std::string rootPath;
    std::string projectName;
    std::string defaultProvider = "deepseek";
    std::string defaultModel = "deepseek-v4-pro";
    std::string destinationPostsDir;
    std::string destinationDiagramsDir;
    std::vector<PiSkillInfo> skills;
    std::vector<PiStageInfo> stages;
    std::vector<std::string> articles;
    int completedArticlesCount = 0;
    std::string stateFilePath;
};

class PiProjectScanner {
public:
    static PiProjectScanResult scan(const std::string& directoryPath);
    static std::vector<std::string> getKnownPiProjects();
    static taskforge::Json toJson(const PiProjectScanResult& result);
};

} // namespace pi
} // namespace taskforge
