#include "Xml.h"

#include <iostream>
#include <pugixml.hpp>
#include <sstream>
#include <vector>
#include "tags.h"

std::string Xml::termFromChunk(const std::string &term, const std::string &chunk) {
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
                    std::string text = text_node.text().as_string();
                    if (cleanComments)
                        text = Comments::clean(text);
                    return text;
                }
            }
            break;  // found and display, end loop
        }
    }
    return "";
}

std::vector<std::pair<std::string,std::string>> Xml::allFromChunk(const std::string &chunk) {
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
                std::string text = text_node.text().as_string();
                if (cleanComments)
                    text = Comments::clean(text);
                v.emplace_back(title, text);
            }
        }
    }
    return v;
}
