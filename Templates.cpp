#include "Templates.h"
#include <iostream>
#include "textUtils.h"
#include "Comments.h"

std::vector<std::string> extractTemplates(const std::string& text) {
    std::vector<std::string> templates;
    size_t pos = 0;

    while ((pos = text.find("{{", pos)) != std::string::npos) {
        size_t start = pos;
        size_t braceCount = 2; // mamy już {{
        pos += 2;

        while (pos < text.size()) {
            if (text.compare(pos, 2, "{{") == 0) {
                braceCount += 2;
                pos += 2;
            } else if (text.compare(pos, 2, "}}") == 0) {
                braceCount -= 2;
                pos += 2;
                if (braceCount == 0) {
                    templates.emplace_back(text.substr(start, pos - start));
                    break;
                }
            } else {
                ++pos;
            }
        }
    }
    return templates;
}

void findTemplatesInFile(const fs::path &path) {
    auto content = read_file_to_string(path);
    auto templates = extractTemplates(Comments::clean(content));
    std::cout << templates.size() << "\n";
}
