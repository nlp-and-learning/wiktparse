#pragma once
#include <filesystem>
#include <vector>
namespace fs = std::filesystem;

std::vector<std::string> extractTemplatesFromFile(const fs::path& path);