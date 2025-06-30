#include "Comments.h"

#include <algorithm>
#include <iostream>

#include "Index.h"
#include "tags.h"
#include "WikiFile.h"
#include "xml.h"
#include <sstream>


std::vector<std::pair<size_t, Comments::Marker>> Comments::classify_line(const std::string &line) {
    std::vector<std::pair<size_t, Marker>> events;
    size_t pos = 0;
    while ((pos = line.find("<nowiki>", pos)) != std::string::npos) {
        events.emplace_back(pos, Marker::StartNowiki);
        pos += 8;
    }
    pos = 0;
    while ((pos = line.find("</nowiki>", pos)) != std::string::npos) {
        events.emplace_back(pos, Marker::EndNowiki);
        pos += 9;
    }
    pos = 0;
    while ((pos = line.find("<!--", pos)) != std::string::npos) {
        events.emplace_back(pos, Marker::StartComment);
        pos += 4;
    }
    pos = 0;
    while ((pos = line.find("-->", pos)) != std::string::npos) {
        events.emplace_back(pos, Marker::EndComment);
        pos += 3;
    }

    std::sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    return events;
}

std::string Comments::clean(const std::string &input) {
    std::istringstream in(input);
        std::ostringstream out;
        std::string line;

        bool in_nowiki = false;
        bool in_comment = false;

        while (std::getline(in, line)) {
            auto events = classify_line(line);
            if (events.empty()) {
                if (!in_comment)
                    out << line << '\n';
                continue;
            }

            std::string result;
            size_t pos = 0;

            for (const auto& [index, marker] : events) {
                if (in_comment) {
                    if (marker == Marker::EndComment) {
                        in_comment = false;
                        pos = index + 3;
                    }
                    // in comment: we ignore other markups
                    continue;
                }

                if (in_nowiki) {
                    if (marker == Marker::EndNowiki) {
                        result += line.substr(pos, index + 9 - pos);
                        in_nowiki = false;
                        pos = index + 9;
                    }
                    continue;
                }

                // outside nowiki and outside comment
                switch (marker) {
                    case Marker::StartNowiki:
                        result += line.substr(pos, index + 8 - pos);
                        in_nowiki = true;
                        pos = index + 8;
                        break;
                    case Marker::StartComment:
                        result += line.substr(pos, index - pos);
                        in_comment = true;
                        pos = index + 4;
                        break;
                    default:
                        break; // end_... we ignore because only start set mode
                }
            }

            // end lines
            if (!in_comment && pos < line.size()) {
                result += line.substr(pos);
            }

            if (!in_comment) {
                if (!result.empty() || line.empty()) {
                    out << result << '\n';
                }
            }
        }
        return out.str();
}

void Comments::searchForComments(const std::string &lang) {
    WikiName wikiName;
    wikiName.wiktName(lang);
    Index index(wikiName);
    index.readIndex();
    WikiFile wikiFile(index);
    wikiFile.open();
    int onePercent = index.size() / 100;
    for (int i=0; i<index.size(); i++) {
        if (i % onePercent  == 0)
            std::cout << i/onePercent << "%" << std::endl;
        auto chunkStr = wikiFile.decompressChunkByIndex(i);
        auto objects =  allFromXML(chunkStr);
        for (auto &p : objects) {
            auto lines = splitLines(p.second);
            for (const auto& line : lines) {
                auto trimmed = trim(line);
                if (trimmed.find("<!--")!=std::string::npos) {
                    std::cout << p.first << " : " << trimmed << std::endl;
                }
            }
        }
    }
    wikiFile.close();
}
