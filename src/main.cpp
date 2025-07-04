#include <algorithm>
#include <iostream>
#include <fstream>
#include <vector>
#include <bzlib.h>
#include <map>
#include <set>
#include <sstream>
#include <ranges>
#include <string_view>
#include <sys/stat.h>

#include "xml/Comments.h"
#include "reader/Index.h"
#include "tag/tags.h"
#include "WikiData.h"
#include "reader/WikiFile.h"
#include "xml/Xml.h"
#include <nlohmann/json.hpp>

#include "tag/tagTree.h"
#include "wikitext/old_Template.h"
#include "wikitext/old_text.h"
#include "wikitext/old_TemplateParser.h"
#include "wikitext/templates.h"
#include "util/Progress.h"
#include "util/structs.h"

using json = nlohmann::json;

using namespace std;


string correct_filename(std::string str) {
    std::replace(str.begin(), str.end(), '/', '_');
    return str;
}

void createPages() {
    fs::create_directory("../pages");
    vector<string> langs = {"pl","en", "fr", "eo"};
    for (const auto& lang: langs) {
        fs::path directory = "../pages/"+lang;
        fs::create_directory(directory);
        WikiName wikiName;
        wikiName.wiktName(lang);
        Index index(wikiName);
        index.readIndex();
        WikiFile wikiFile(index);
        wikiFile.open();
        auto terms_to_extract = readLines("../work/terms_to_extract." + lang + ".txt");
        for (const auto& term : terms_to_extract) {
            cout << term;
            auto termValue = wikiFile.extractTerm(term);
            if (!termValue.empty()) {
                saveToFile(termValue, directory/ (correct_filename(term)+".page"));
                cout << " ok" << endl;
            }
            else
                cout << " failed" << endl;
        }
        wikiFile.close();
    }
}

void processPagetoTree(const fs::path& inputPath, const fs::path& outputPath) {
    std::ifstream in(inputPath);
    if (!in) {
        std::cerr << "can't open " + inputPath.string() + "\n";
        return;
    }
    auto root = parseIndentedTree(in);
    std::ofstream out(outputPath);
    printTree(root, out);
}

void createTreeForPages() {
    fs::path root = "../pages";
    if (!fs::exists(root) || !fs::is_directory(root)) {
        std::cerr << "is not directory: " << root << "\n";
        return;
    }
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().extension() == ".page") {
            fs::path new_path = entry.path();
            new_path.replace_extension(".tree");
            std::cout << new_path << "\n";
            processPagetoTree(entry.path(), new_path);
        }
    }

}

void createPagesWiki() {
    fs::path directory = "../pages/enwiki";
    fs::create_directory(directory);
    WikiName wikiName;
    wikiName.wikiName("en");
    Index index(wikiName);
    index.readIndex();
    WikiFile wikiFile(index);
    wikiFile.open();
    auto terms_to_extract = readLines("../work/terms_to_extract.enwiki.txt");
    for (const auto& term : terms_to_extract) {
        cout << term;
        auto termValue = wikiFile.extractTerm(term);
        if (!termValue.empty()) {
            saveToFile(termValue, directory/ (correct_filename(term)+".page"));
            cout << " ok" << endl;
        } else
            cout << " failed" << endl;
    }
    wikiFile.close();
}


void searchWikiDataWiktNeeded() {
    vector<string> listTosearch;
    listTosearch.emplace_back("\"P220\"");//ISO 639-1 code
    listTosearch.emplace_back("\"Q82042\"");//part of speech
    listTosearch.emplace_back("\"Q8162\"");//linguistics , Etymology
    listTosearch.emplace_back("\"Q34770\"");//language
    vector<string> listNotTosearch;
    listNotTosearch.emplace_back("\"Q5\"");
    WikiData::search(listTosearch,listNotTosearch, "wiktNeeded.jsonl");
}

void showTitleTypes() {
    const std::regex pattern(R"((.*)wiktionary.*.txt.bz2)");
    fs::path directory("../dump");
    fs::create_directory("titles");
    for (const auto& entry : fs::recursive_directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        const std::string filename = entry.path().filename().string();
        std::smatch match;
        if (std::regex_match(filename, match, pattern)) {
            if (match[1].str()=="en")continue;
            cout << match[1].str() << endl;
            WikiName wikiName;
            wikiName.wiktName(match[1].str());
            Index index(wikiName);
            index.readIndex();
            map<string, set<string>> setMap;
            set<string> allTitles;
            for (const auto& title : index.objectMap | std::views::keys) {
                if (title.find('/')==string::npos && title.find(':')==string::npos) continue;
                allTitles.insert(title);
                if (title.find('/')!=string::npos) {
                    auto parts = split(title, '/');
                    for (int i=0; i<parts.size(); i++) {
                        string key = format("s{}_{}", i, parts.size());
                        setMap[key].insert(parts[i]);
                    }
                } else if (title.find(':')!=string::npos) {
                    auto parts = split(title, ':');
                    for (int i=0; i<parts.size(); i++) {
                        string key = format("c{}_{}", i, parts.size());
                        setMap[key].insert(parts[i]);
                    }
                }
            }
            auto outname = "titles/" + match[1].str() + ".all.txt";;
            saveToFile(allTitles, outname);
            for (const auto& pair : setMap) {
                const std::string& key = pair.first;
                // if (key!="c0_2" && key!="s1_2") continue;
                outname = "titles/" + match[1].str() + "." + pair.first + ".txt";
                saveToFile(pair.second, outname);
            }
        }
    }
}

enum PropType {Iso, Glottolog, Gost, Lang, Pos, Other,  Pronun, Ling, LingNorm};

std::optional<json> getNested(const json& j, std::vector<std::string> keys) {
    const json* current = &j;
    for (const auto& k : keys) {
        if (!current->contains(k)) return std::nullopt;
        current = &(*current)[k];
    }
    return *current;
}

PropType propType(const json& j) {
    auto resIso = getNested(j, {"claims", "P220"});
    if (resIso)
        return Iso;
    //
    auto resGlottolog = getNested(j, {"claims", "P1394"});
    if (resGlottolog)
        return Glottolog;

    auto resGost = getNested(j, {"claims", "P278"});
    if (resGost)
        return Gost;

    auto res31 = getNested(j, {"claims", "P31"});
    if (res31) {
        if (!res31.value().is_array())
            cout << "not an array" << endl;
        for (const auto & i : res31.value()) {
            auto resCode= getNested(i, {"mainsnak", "datavalue",
            "value", "id"});
            if (to_string(resCode.value())=="\"Q82042\"") {
                return Pos;
            } else if (to_string(resCode.value())=="\"Q34770\"") {
                return Lang;
            }
        }
    }
    auto res361 = getNested(j, {"claims", "P361"});
    if (res361) {
        if (!res361.value().is_array())
            cout << "not an array" << endl;
        for (const auto & i : res361.value()) {
            auto resCode= getNested(i, {"mainsnak", "datavalue",
            "value", "id"});
            if (to_string(resCode.value())=="\"Q34770\"") {
                return Pronun;
            }
        }
    }

    auto res279 = getNested(j, {"claims", "P279"});
    if (res279) {
        if (!res279.value().is_array())
            cout << "not an array" << endl;
        for (const auto & i : res279.value()) {
            auto resCode= getNested(i, {"mainsnak", "datavalue",
            "value", "id"});
            if (to_string(resCode.value())=="\"Q8162\"") {
                return Ling;
            } else if (to_string(resCode.value())=="\"Q1759988\"") {
                return LingNorm;
            }
        }
    }
    return Other;
}

void removeEmptyLinesFromJSonl(const std::string& filename) {
    std::ifstream infile(filename);
    for( std::string line; getline( infile, line ); ) {
        if (line.empty()) continue;
        std::cout << line << std::endl;
    }
}

void splitWikiDataResult() {
    std::vector<std::string> lines;
    std::ifstream infile("../cmake-build-release/wiktNeeded.jsonl");
    int cnt = 0;
    std::ofstream isofile("iso.jsonl");
    std::ofstream otherfile("other.jsonl");
    std::ofstream posfile("pos.jsonl");
    std::ofstream glottologfile("glottolog.jsonl");
    std::ofstream gostfile("gost.jsonl");
    std::ofstream langfile("lang.jsonl");
    std::ofstream pronunfile("pronun.jsonl");
    std::ofstream lingfile("ling.jsonl");
    std::ofstream lingnormfile("lingnorm.jsonl");
    for( std::string line; getline( infile, line ); ) {
        if (line.empty()) continue;
        json j = json::parse(line);
        auto prop = propType(j);
        switch (prop) {
            case Iso: isofile << line << '\n'; break;
            case Glottolog: glottologfile << line << '\n'; break;
            case Gost: gostfile << line << '\n'; break;
            case Lang: langfile << line << '\n'; break;
            case Pos: posfile << line << '\n'; break;
            case Pronun: pronunfile << line << '\n'; break;
            case Ling: lingfile << line << '\n'; break;
            case LingNorm: lingnormfile << line << '\n'; break;
            default: otherfile << line << '\n';
        }
        cnt++;
        if (cnt % 200 ==0) cout << cnt << endl <<flush;
    }
}


void testTemplates() {
    const fs::path& dir = "../pages";
    std::vector<fs::path> pagesFiles;

    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".page") {
            pagesFiles.push_back(entry.path());
        }
    }
    for (const auto& path : pagesFiles) {
        std::cout << path << "\n";
        extractTemplatesFromFile(path);
    }

    std::string input = "{{Infobox person|name=John|birth={{birth date|1990|1|1}}|known={{#if:1|Author|Unknown}}}}";
    size_t pos = 0;
    auto t = old_TemplateParser::parseTemplate(input, pos);
    std::cout << t->toWikitext(FormatStyle::Compact) << std::endl;
    std::cout << t->toWikitext(FormatStyle::Multiline) << std::endl;
}

void wikipediaInfoboxes() {
    WikiName wikiName;
    wikiName.wikiName("en");
    Index index(wikiName);
    WikiFile wikiFile(index);
    wikiFile.open();
    index.open();
    Xml xml;
    Progress progress(wikiFile.size(), 0.001);
    for( WikiIndexChunk indexChunk; index.getChunk(indexChunk);) {
        auto chunkStr = wikiFile.decompressChunk(indexChunk);
        progress.update(wikiFile.filePos());
        auto objects =  xml.allFromChunk(chunkStr);
        for (auto &p : objects) {
            auto templates = extractTemplates(Comments::clean(p.second));
            for (auto &tstr: templates) {
                size_t pos = 0;
                cout << format("parseTemplate {} {}\n",p.first, tstr);
                auto t = old_TemplateParser::parseTemplate(tstr, pos);
                auto name = trim(t->name);
                if (name == "Infobox language")
                    cout << format("{}\n",p.first);
            }
        }
    }
    index.close();
    wikiFile.close();
}

void pagesInfoboxes() {
    const fs::path& dir = "../pages";
    std::vector<fs::path> pagesFiles;

    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".page1") {
            pagesFiles.push_back(entry.path());
        }
    }
    for (const auto& path : pagesFiles) {
        auto templates = extractTemplatesFromFile(path);
        for (auto &tstr: templates) {
            size_t pos = 0;
            cout << format("parse: {}\n",tstr) << flush;
            auto t = old_TemplateParser::parseTemplate(tstr, pos);
            cout << t->toWikitext(FormatStyle::Compact) << std::endl;
        }
    }

}

void searchForComments(const std::string &lang) {
    WikiName wikiName;
    wikiName.wiktName(lang);
    Index index(wikiName);
    WikiFile wikiFile(index);
    wikiFile.open();
    index.open();
    Xml xml;
    Progress progress(wikiFile.size());
    for( WikiIndexChunk indexChunk; index.getChunk(indexChunk);) {
        auto chunkStr = wikiFile.decompressChunk(indexChunk);
        progress.update(wikiFile.filePos());
        auto objects =  xml.allFromChunk(chunkStr);
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
    index.close();
    wikiFile.close();
}

void collectAllTags() {
    WikiName wikiName;
    wikiName.wiktName("en");
    Index index(wikiName);
    WikiFile wikiFile(index);
    wikiFile.open();
    index.open();
    LimitedListMap<string,string> allTags;
    Xml xml;
    Progress progress(wikiFile.size());
    for( WikiIndexChunk indexChunk; index.getChunk(indexChunk);) {
        auto chunkStr = wikiFile.decompressChunk(indexChunk);
        progress.update(wikiFile.filePos());
        auto objects =  xml.allFromChunk(chunkStr);
        for (auto &p : objects) {
            auto lines = splitLines(p.second);
            for (const auto& line : lines) {
                if (count_levelL(line)>1) {
                    auto tag = trim_tag(trim(line));
                    allTags.add(tag,p.first);
                }
            }
        }
    }
    index.close();
    wikiFile.close();
    std::ofstream out("allTagsWhere.txt");
    if (!out) throw std::runtime_error("Cannot open file");
    allTags.print(out);
}

int main() {
    wikipediaInfoboxes();
}
