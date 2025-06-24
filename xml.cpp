#include "xml.h"

#include <iostream>
#include <pugixml.hpp>
#include <sstream>

std::string extractFromXML(const std::string &term, const std::string &chunkStr) {
    std::string xmlStr = "<mediawiki>\n"+ chunkStr +"</mediawiki>\n";
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_string(xmlStr.c_str());

    if (!result) {
        std::cerr << "Error XML parsing: " << result.description() << "\n";
        return "";
    }

    for (pugi::xml_node page : doc.child("mediawiki").children("page")) {
        std::string title = page.child_value("title");
        if (title == term) {
            std::ostringstream oss;
            page.print(oss);
            std::string page_xml = oss.str();
            pugi::xml_node revision = page.child("revision");
            if (revision) {
                pugi::xml_node text_node = revision.child("text");
                if (text_node) {
                    //std::cout << text_node.child_value() << std::endl;
                    return text_node.text().as_string();
                }
            }
            break;  // found and display, end loop
        }
    }
}
