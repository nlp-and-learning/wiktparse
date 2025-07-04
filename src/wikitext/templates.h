#pragma once
#include <filesystem>
#include <vector>
namespace fs = std::filesystem;

std::vector<std::string> extractTemplates(const std::string& text);
std::vector<std::string> extractTemplatesFromFile(const fs::path& path);