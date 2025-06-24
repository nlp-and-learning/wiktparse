#pragma once
#include <string>
#include <utility>

enum class TitleType {Main, Translations, Thesaurus, Other};

void collectAllTags();
std::pair<TitleType,std::string> getTitleType(const std::string &title);