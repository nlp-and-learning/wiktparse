#include <algorithm>
#include <atomic>
#include <iostream>
#include <mutex>
#include <filesystem>
#include <fstream>
#include <vector>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string_view>

#include "headers/headers.h"
#include "WikiData.h"
#include "dump/WikiDump.h"
#include <nlohmann/json.hpp>
#include <wikilib/templates/template_parser.h>

#include "headers/headerTree.h"
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
    IndonesiaEth, Lang, Pos, Other,  Pronun, Ling, LingNorm, Grammar, Script};

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
    if (WikiData::isWritingSystem(j))
        return Script;
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

// wiktneeded output: wiktNeeded_<n>.jsonl of each thread, or older single wiktNeeded.jsonl
std::vector<fs::path> wiktNeededFiles(const fs::path& dir) {
    const std::regex pattern(R"(wiktNeeded_\d+\.jsonl)");
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file() && std::regex_match(entry.path().filename().string(), pattern))
            files.push_back(entry.path());
    }
    if (files.empty())
        files.push_back(dir / "wiktNeeded.jsonl");
    std::ranges::sort(files);
    return files;
}

void splitWikiDataResult() {
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
    std::ofstream scriptfile("script.jsonl");
    for (const auto& file : wiktNeededFiles("../cmake-build-release")) {
    std::ifstream infile(file);
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
            case Script: scriptfile << line << '\n'; break;
            default: otherfile << line << '\n';
        }
        cnt++;
        if (cnt % 200 ==0) cout << cnt << endl <<flush;
    }
    }
}


// Template name as MediaWiki compares it: first letter case-insensitive, '_' = ' '
std::string normalizedTemplateName(std::string name) {
    name = trim(name);
    std::replace(name.begin(), name.end(), '_', ' ');
    if (!name.empty())
        name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    return name;
}

// Infobox language (and language family) templates of English Wikipedia,
// read in parallel threads (indexed multistream ranges):
// infoboxes.txt - raw templates; infoboxes.jsonl - one infobox per line:
// {"title":..,"template":..,"params":{"name":"raw value",..}}; positional
// parameters are named "1", "2"...
void wikipediaInfoboxes(size_t threads) {
    auto wikiDump = WikiDump::wikipedia("en");
    std::cout << wikiDump.path().dump_path().string() << std::endl;
    std::ofstream out("infoboxes.txt");
    std::ofstream jsonl("infoboxes.jsonl");
    std::mutex outMutex;
    std::atomic<size_t> count{0};
    wikiDump.forEachPageParallel([&](size_t, const std::string &title, const std::string &text) {
        // Cheap check before parsing templates of every article
        if (text.find("nfobox language") == std::string::npos && text.find("nfobox_language") == std::string::npos)
            return;
        for (const auto &invocation : wikilib::templates::find_invocations(text)) {
            auto name = normalizedTemplateName(invocation.name);
            if (name != "Infobox language" && name != "Infobox language family")
                continue;
            json params = json::object();
            size_t positional = 0;
            for (const auto &[key, value] : invocation.parameters)
                params[key.empty() ? std::to_string(++positional) : key] = value;
            auto raw = text.substr(invocation.location.begin.offset, invocation.location.length());
            auto line = json{{"title", title}, {"template", name}, {"params", params}}.dump(
                    -1, ' ', false, json::error_handler_t::replace);
            std::lock_guard lock(outMutex);
            out << raw << "\n\n";
            jsonl << line << '\n';
            count++;
        }
    }, threads, 0.001);
    std::cout << count << " infoboxes written to infoboxes.txt and infoboxes.jsonl" << std::endl;
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

// wiktparse [threads]   (default 16)
int main(int argc, char *argv[]) {
    size_t threads = argc > 1 ? std::max<size_t>(1, std::stoul(argv[1])) : 16;
    wikipediaInfoboxes(threads);
    //splitWikiDataResult();
}
