#include "utils.h"
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#pragma comment(lib, "shell32.lib")
#endif

std::string getDownloadsPath() {
#ifdef _WIN32
    PWSTR pathTmp;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, NULL, &pathTmp))) {
        char path[MAX_PATH];
        wcstombs(path, pathTmp, MAX_PATH);
        CoTaskMemFree(pathTmp);
        return std::string(path);
    }
    return ".\\";
#else
    const char* home = getenv("HOME");
    if (!home) return "./";
    return std::string(home) + "/Downloads/";
#endif
}

std::string generateUniqueFilename(const std::string& basePath, const std::string& baseName, const std::string& ext) {
    namespace fs = std::filesystem;

    std::ostringstream nameWithTimestamp;
    auto now = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(now);
    nameWithTimestamp << baseName << "_" << std::put_time(std::localtime(&timeT), "%Y-%m-%d_%H-%M-%S");

    std::string fullPath = basePath + "/" + nameWithTimestamp.str() + ext;

    int counter = 1;
    while (fs::exists(fullPath)) {
        fullPath = basePath + "/" + nameWithTimestamp.str() + "_" + std::to_string(counter++) + ext;
    }

    return fullPath;
}

