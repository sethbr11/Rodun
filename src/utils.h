#pragma once
#include <string>

std::string getDownloadsPath();

std::string generateUniqueFilename(const std::string& basePath, const std::string& baseName, const std::string& ext);

