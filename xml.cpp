#include "xml.h"

#include <iostream>
#include <pugixml.hpp>
#include <sstream>
#include <vector>

#include "Comments.h"
#include "tags.h"

std::string extractFromXML(const std::string &term, const std::string &chunk) {
    std::string xmlStr = "<mediawiki>\n"+ chunk +"</mediawiki>\n";
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_string(xmlStr.c_str());

    if (!result) {
        std::cerr << "Error XML parsing: " << result.description() << "\n";
        return "";
    }

    for (pugi::xml_node page : doc.child("mediawiki").children("page")) {
        std::string title = page.child_value("title");
        if (title == term) {
            pugi::xml_node revision = page.child("revision");
            if (revision) {
                pugi::xml_node text_node = revision.child("text");
                if (text_node) {
                    return text_node.text().as_string();
                }
            }
            break;  // found and display, end loop
        }
    }
    return "";
}

std::vector<std::pair<std::string,std::string>> allFromXML(const std::string &chunk) {
    Comments comments;
    std::vector<std::pair<std::string,std::string>> v;
    std::string xmlStr = "<mediawiki>\n"+ chunk +"</mediawiki>\n";
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_string(xmlStr.c_str());

    if (!result) {
        std::cerr << "Error XML parsing: " << result.description() << "\n";
        return {};
    }

    for (pugi::xml_node page : doc.child("mediawiki").children("page")) {
        std::string title = page.child_value("title");
        auto titleType = getTitleType(title);
        if (titleType.first == TitleType::Other)
            continue;
        pugi::xml_node revision = page.child("revision");
        if (revision) {
            pugi::xml_node text_node = revision.child("text");
            if (text_node) {
                v.emplace_back(title, comments.clean(text_node.text().as_string()));
            }
        }
    }
    return v;
}
