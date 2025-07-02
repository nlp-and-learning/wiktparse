#include "titles.h"

bool isClear(const std::string &str) {
    return str.find('/') == std::string::npos && str.find(':') == std::string::npos;
}

std::pair<TitleType,std::string> getTitleType(const std::string &title) {
    if (isClear(title))
        return make_pair(TitleType::Main, title);
    auto pos = title.find("/translations");
    if (pos != std::string::npos) {
        auto partA = title.substr(0, pos);
        auto partB = title.substr(pos + 1);
        if (isClear(partA) && isClear(partB))
            return make_pair(TitleType::Translations, partA);
        else
            return make_pair(TitleType::Other, title);
    }
    pos = title.find(':');
    if (pos != std::string::npos) {
        auto partA = title.substr(0, pos);
        auto partB = title.substr(pos + 1);
        if (partA == "Thesaurus" && isClear(partB))
            return make_pair(TitleType::Thesaurus, partB);
        else
            return make_pair(TitleType::Other, title);
    }
    return make_pair(TitleType::Other, title);
}

