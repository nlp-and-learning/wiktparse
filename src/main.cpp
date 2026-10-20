#include <algorithm>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <vector>
#include <map>
#include <set>
#include <sstream>
#include <string_view>

#include "headers/headers.h"
#include "WikiData.h"
#include "dump/WikiDump.h"
#include <nlohmann/json.hpp>

#include "headers/headerTree.h"
#include "markup/wikinodes/TemplateParser.h"
#include "wikitext/templates.h"
#include "util/structs.h"
#include "util/textUtils.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

using namespace std;


string correct_filename(std::string str) {
    std::replace(str.begin(), str.end(), '/', '_');
    return str;
}

void extractTermsToPages(WikiDump &wikiDump, const std::string &termsFile, const fs::path &directory) {
    auto terms_to_extract = readLines(termsFile);
    auto termValues = wikiDump.extractTerms(terms_to_extract);
    for (size_t i = 0; i < terms_to_extract.size(); i++) {
        const auto& term = terms_to_extract[i];
        cout << term;
        if (!termValues[i].empty()) {
            saveToFile(termValues[i], directory/ (correct_filename(term)+".page"));
            cout << " ok" << endl;
        }
        else
            cout << " failed" << endl;
    }
}

void createPages() {
    fs::create_directory("../pages");
    vector<string> langs = {"pl","en", "fr", "eo"};
    for (const auto& lang: langs) {
        fs::path directory = "../pages/"+lang;
        fs::create_directory(directory);
        auto wikiDump = WikiDump::wiktionary(lang);
        extractTermsToPages(wikiDump, "../work/terms_to_extract." + lang + ".txt", directory);
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
    auto wikiDump = WikiDump::wikipedia("en");
    extractTermsToPages(wikiDump, "../work/terms_to_extract.enwiki.txt", directory);
}


void showTitleTypes() {
    fs::create_directory("titles");
    auto langs = WikiDump::dumpPath(wikilib::dump::WikiProject::Wiktionary).available_languages();
    for (const auto& lang : langs) {
        if (lang=="en")continue;
        cout << lang << endl;
        auto wikiDump = WikiDump::wiktionary(lang);
        if (!wikiDump.path().index_exists()) continue;
        map<string, set<string>> setMap;
        set<string> allTitles;
        wikiDump.forEachTitle([&](const string& title) {
            if (title.find('/')==string::npos && title.find(':')==string::npos) return;
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
        });
        auto outname = "titles/" + lang + ".all.txt";;
        saveToFile(allTitles, outname);
        for (const auto& pair : setMap) {
            const std::string& key = pair.first;
            // if (key!="c0_2" && key!="s1_2") continue;
            outname = "titles/" + lang + "." + pair.first + ".txt";
            saveToFile(pair.second, outname);
        }
    }
}

enum PropType {Iso, Glottolog, Gost, Unesco, EndAngered,LinguistList, NativeLabel, IndonesiaCode,
    IndonesiaEth, Lang, Pos, Other,  Pronun, Ling, LingNorm, Grammar};

std::optional<json> getNested(const json& j, std::vector<std::string> keys) {
    const json* current = &j;
    for (const auto& k : keys) {
        if (!current->contains(k)) return std::nullopt;
        current = &(*current)[k];
    }
    return std::optional<json>{*current};
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

    auto resUnesco = getNested(j, {"claims", "P2355"});
    if (resUnesco)
        return Unesco;

    auto resEndAngered = getNested(j, {"claims", "P2192"});
    if (resEndAngered)
        return EndAngered;

    auto resLinguistList = getNested(j, {"claims", "P1232"});
    if (resLinguistList)
        return LinguistList;

    auto resIndonesiaCode = getNested(j, {"claims", "P2590"});
    if (resIndonesiaCode)
        return IndonesiaCode;

    auto res31 = getNested(j, {"claims", "P31"});
    if (res31) {
        if (!res31.value().is_array())
            cout << "not an array" << endl;
        for (const auto & i : res31.value()) {
            auto resCode= getNested(i, {"mainsnak", "datavalue",
            "value", "id"});
            if (!resCode) continue;
            if (to_string(resCode.value())=="\"Q82042\"") {
                return Pos;
            } else if (to_string(resCode.value())=="\"Q34770\"") {
                auto resIndonesiaEth = getNested(j, {"claims", "P2589"});
                if (resIndonesiaEth)
                    return IndonesiaEth;
                auto resNativeLabel = getNested(j, {"claims", "P1705"});
                if (resNativeLabel)
                    return NativeLabel;
                return Lang;
            }
        }
    }
    if (WikiData::isLexical(j))
        return Pos;
    if (WikiData::isGrammatical(j))
        return Grammar;
    if (WikiData::isLanguage(j))
        return Lang;
    auto res361 = getNested(j, {"claims", "P361"});
    if (res361) {
        if (!res361.value().is_array())
            cout << "not an array" << endl;
        for (const auto & i : res361.value()) {
            auto resCode= getNested(i, {"mainsnak", "datavalue",
            "value", "id"});
            if (!resCode) continue;
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
            if (!resCode) continue;
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
    std::ofstream unescofile("unesco.jsonl");
    std::ofstream endAngeredfile("endAngered.jsonl");
    std::ofstream linguistListfile("linguistList.jsonl");
    std::ofstream nativeLabelfile("nativeLabel.jsonl");
    std::ofstream IndonesiaCodefile("IndonesiaCode.jsonl");
    std::ofstream IndonesiaEthfile("IndonesiaEth.jsonl");
    std::ofstream langfile("lang.jsonl");
    std::ofstream pronunfile("pronun.jsonl");
    std::ofstream lingfile("ling.jsonl");
    std::ofstream lingnormfile("lingnorm.jsonl");
    std::ofstream grammarfile("grammar.jsonl");
    for( std::string line; getline( infile, line ); ) {
        if (line.empty()) continue;
        json j = json::parse(line);
        auto prop = propType(j);
        switch (prop) {
            case Iso: isofile << line << '\n'; break;
            case Glottolog: glottologfile << line << '\n'; break;
            case Gost: gostfile << line << '\n'; break;
            case Unesco: unescofile << line << '\n'; break;
            case EndAngered: endAngeredfile << line << '\n'; break;
            case LinguistList: linguistListfile << line << '\n'; break;
            case NativeLabel: nativeLabelfile << line << '\n'; break;
            case IndonesiaCode: IndonesiaCodefile << line << '\n'; break;
            case IndonesiaEth: IndonesiaEthfile << line << '\n'; break;
            case Lang: langfile << line << '\n'; break;
            case Pos: posfile << line << '\n'; break;
            case Pronun: pronunfile << line << '\n'; break;
            case Ling: lingfile << line << '\n'; break;
            case LingNorm: lingnormfile << line << '\n'; break;
            case Grammar: grammarfile << line << '\n'; break;
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
}

void wikipediaInfoboxes() {
    auto wikiDump = WikiDump::wikipedia("en");
    std::ofstream out("infoboxes.txt");
    wikiDump.forEachPage([&](const std::string &title, const std::string &text) {
        auto templates = extractTemplates(text);
        for (auto &tstr: templates) {
            size_t pos = 0;
            TemplateParser parser(tstr, 0);
            auto t = parser.parse();
            auto name = trim(t->name);
            if (name == "Infobox language")
                out << tstr << endl << endl;
        }
        return true;
    }, 0.001);
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
            TemplateParser parser(tstr, 0);
            auto t = parser.parse();
            cout << t->dump() << std::endl;
        }
    }

}

void searchForComments(const std::string &lang) {
    auto wikiDump = WikiDump::wiktionary(lang);
    wikiDump.forEachPage([&](const std::string &title, const std::string &text) {
        auto lines = splitLines(text);
        for (const auto& line : lines) {
            auto trimmed = trim(line);
            if (trimmed.find("<!--")!=std::string::npos) {
                std::cout << title << " : " << trimmed << std::endl;
            }
        }
        return true;
    });
}

void collectAllHeaders() {
    auto wikiDump = WikiDump::wiktionary("en");
    LimitedListMap<string,string> allHeaders;
    wikiDump.forEachPage([&](const std::string &title, const std::string &text) {
        auto lines = splitLines(text);
        for (const auto& line : lines) {
            if (count_levelL(line)>1) {
                auto tag = trim_header(trim(line));
                allHeaders.add(tag,title);
            }
        }
        return true;
    });
    std::ofstream out("allHeadersWhere.txt");
    if (!out) throw std::runtime_error("Cannot open file");
    allHeaders.print(out);
}

int main() {
    wikipediaInfoboxes();
    //splitWikiDataResult();
}
