// Languages of English Wiktionary (Module:languages/data..., etymology
// languages, families, scripts) into SQLite, with names of the languages in
// many languages from Wikidata (labels and aliases of their Q items).
//
// usage: wiktlangs [output.sqlite] [wikidata.jsonl...]
//   default output wiktlangs.sqlite; Wikidata entities from wiktNeeded_*.jsonl
//   (or wiktNeeded.jsonl) in the current directory
//
// Tables:
//   lang(code, kind, name, qid, family, parent, ancestors, scripts, type, module)
//       kind: language | etymology (variety, historical stage) | family | script
//   lang_alias(code, alias, type)      type: alias | variety | other (en.wikt)
//   lang_name(code, qid, lang, name, alias)  Wikidata label (alias=0) or alias (1)
// Also writes <output>_qid.txt (all Q ids, for wiktneeded --items) and
// <output>_missing_qid.txt (Q ids not found in the Wikidata files).
#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "dump/WikiDump.h"
#include "wikitext/LuaData.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

class Db {
    sqlite3 *db = nullptr;
public:
    explicit Db(const fs::path &file) {
        fs::remove(file);
        if (sqlite3_open(file.string().c_str(), &db) != SQLITE_OK)
            throw std::runtime_error("Cannot open " + file.string());
    }
    ~Db() { sqlite3_close(db); }
    void exec(const std::string &sql) {
        char *err = nullptr;
        if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
            std::string message = err ? err : "sqlite error";
            sqlite3_free(err);
            throw std::runtime_error(message + " in: " + sql);
        }
    }
    // Inserts rows; empty strings are stored as NULL
    void insert(const std::string &sql, const std::vector<std::vector<std::string>> &rows) {
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
            throw std::runtime_error(std::string(sqlite3_errmsg(db)) + " in: " + sql);
        for (const auto &row : rows) {
            for (size_t i = 0; i < row.size(); i++) {
                if (row[i].empty())
                    sqlite3_bind_null(stmt, int(i + 1));
                else
                    sqlite3_bind_text(stmt, int(i + 1), row[i].c_str(), -1, SQLITE_TRANSIENT);
            }
            if (sqlite3_step(stmt) != SQLITE_DONE)
                throw std::runtime_error(sqlite3_errmsg(db));
            sqlite3_reset(stmt);
        }
        sqlite3_finalize(stmt);
    }
};

std::string str(const json &v) {
    if (v.is_string())
        return v.get<std::string>();
    if (v.is_number_integer())
        return std::to_string(v.get<long long>());
    return "";
}

// Q id from a Wikidata item number (809 -> Q809)
std::string qid(const json &v) {
    if (v.is_number_integer() && v.get<long long>() > 0)
        return "Q" + std::to_string(v.get<long long>());
    if (v.is_string() && v.get<std::string>().starts_with("Q"))
        return v.get<std::string>();
    return "";
}

// Strings of a value: "a", {"a", "b"}, {"a", {"b", "c"}} -> a, b, c
void collectStrings(const json &v, std::vector<std::string> &out) {
    if (v.is_string())
        out.push_back(v.get<std::string>());
    else if (v.is_object() && v.contains("#"))
        for (const auto &item : v["#"])
            collectStrings(item, out);
    else if (v.is_array())
        for (const auto &item : v)
            collectStrings(item, out);
}

std::string joined(const json &v) {
    std::vector<std::string> items;
    collectStrings(v, items);
    std::string out;
    for (const auto &item : items)
        out += (out.empty() ? "" : ",") + item;
    return out;
}

const std::regex languageModule(R"(Module:languages/data(/exceptional|/2|/3/[a-z])?)");
const std::regex languageExtraModule(R"(Module:languages/data(/exceptional|/2|/3/[a-z])/extra)");

bool wantedModule(const std::string &title) {
    return std::regex_match(title, languageModule) || std::regex_match(title, languageExtraModule) ||
           title == "Module:etymology languages/data" || title == "Module:families/data" ||
           title == "Module:scripts/data";
}

// Wikidata labels and aliases by Q id
std::map<std::string, json> loadWikidata(const std::vector<fs::path> &files) {
    std::map<std::string, json> entities;
    for (const auto &file : files) {
        std::ifstream in(file);
        for (std::string line; std::getline(in, line);) {
            auto entity = json::parse(line, nullptr, false);
            if (entity.is_discarded() || !entity.contains("id"))
                continue;
            entities[entity["id"].get<std::string>()] = {{"labels", entity.value("labels", json::object())},
                                                        {"aliases", entity.value("aliases", json::object())}};
        }
    }
    return entities;
}

std::vector<fs::path> defaultWikidataFiles() {
    std::vector<fs::path> files;
    const std::regex pattern(R"(wiktNeeded_\d+\.jsonl)");
    for (const auto &entry : fs::directory_iterator("."))
        if (std::regex_match(entry.path().filename().string(), pattern))
            files.push_back(entry.path());
    if (files.empty() && fs::exists("wiktNeeded.jsonl"))
        files.emplace_back("wiktNeeded.jsonl");
    std::ranges::sort(files);
    return files;
}

} // namespace

int main(int argc, char *argv[]) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (!args.empty() && (args[0] == "-h" || args[0] == "--help")) {
        std::cout << "usage: wiktlangs [output.sqlite] [wikidata.jsonl...]\n";
        return 0;
    }
    try {
        fs::path output = args.empty() ? "wiktlangs.sqlite" : args[0];
        std::vector<fs::path> wikidataFiles(args.size() > 1 ? args.begin() + 1 : args.end(), args.end());
        if (wikidataFiles.empty())
            wikidataFiles = defaultWikidataFiles();

        auto wikiDump = WikiDump::wiktionary("en");
        std::cout << wikiDump.path().dump_path().string() << std::endl;
        auto modules = wikiDump.extractMatching(wantedModule);
        std::cout << modules.size() << " modules read" << std::endl;

        std::vector<std::vector<std::string>> langRows, aliasRows;
        std::map<std::string, std::string> codeQid;
        auto addAliases = [&](const std::string &code, const json &entry) {
            for (const auto &[key, type] : {std::pair{"aliases", "alias"}, {"varieties", "variety"}, {"otherNames", "other"}}) {
                if (!entry.contains(key))
                    continue;
                std::vector<std::string> names;
                collectStrings(entry[key], names);
                for (const auto &name : names)
                    aliasRows.push_back({code, name, type});
            }
        };
        for (const auto &[title, text] : modules) {
            auto entries = LuaData::parseModule(text);
            bool extra = std::regex_match(title, languageExtraModule);
            std::string kind = title.starts_with("Module:languages") ? "language"
                             : title.starts_with("Module:etymology") ? "etymology"
                             : title.starts_with("Module:families") ? "family" : "script";
            std::cout << "  " << title << ": " << entries.size() << std::endl;
            for (const auto &[code, entry] : entries) {
                if (!entry.is_object())
                    continue;
                addAliases(code, entry);
                if (extra)
                    continue;
                auto q = qid(LuaData::positional(entry, 1));
                std::string third = str(LuaData::positional(entry, 2));
                // languages: family; etymology languages: parent language; families: parent
                // family; scripts: script type (alphabet, abugida...)
                std::string family = kind == "language" ? third : str(entry.value("family", json()));
                std::string parent = kind == "etymology" || kind == "family" ? third : "";
                std::string type = kind == "script" ? third : str(entry.value("type", json()));
                std::string scripts = kind == "language" ? joined(LuaData::positional(entry, 3))
                                                         : joined(entry.value("scripts", json()));
                langRows.push_back({code, kind, str(LuaData::positional(entry, 0)), q, family, parent,
                                    joined(entry.value("ancestors", json())), scripts, type, title});
                if (!q.empty())
                    codeQid[code] = q;
            }
        }

        auto wikidata = loadWikidata(wikidataFiles);
        std::cout << wikidata.size() << " Wikidata entities from " << wikidataFiles.size() << " files" << std::endl;
        std::vector<std::vector<std::string>> nameRows;
        std::set<std::string> missing;
        for (const auto &[code, q] : codeQid) {
            auto it = wikidata.find(q);
            if (it == wikidata.end()) {
                missing.insert(q);
                continue;
            }
            for (const auto &[lang, label] : it->second["labels"].items())
                if (label.is_object())
                    nameRows.push_back({code, q, lang, label.value("value", ""), "0"});
            for (const auto &[lang, list] : it->second["aliases"].items())
                if (list.is_array())
                    for (const auto &alias : list)
                        nameRows.push_back({code, q, lang, alias.value("value", ""), "1"});
        }

        Db db(output);
        db.exec("PRAGMA journal_mode=OFF; PRAGMA synchronous=OFF; BEGIN;");
        db.exec("CREATE TABLE lang(code TEXT, kind TEXT, name TEXT, qid TEXT, family TEXT, parent TEXT, "
                "ancestors TEXT, scripts TEXT, type TEXT, module TEXT, PRIMARY KEY(code, kind))");
        db.exec("CREATE TABLE lang_alias(code TEXT, alias TEXT, type TEXT)");
        db.exec("CREATE TABLE lang_name(code TEXT, qid TEXT, lang TEXT, name TEXT, alias INTEGER)");
        db.insert("INSERT OR REPLACE INTO lang VALUES(?,?,?,?,?,?,?,?,?,?)", langRows);
        db.insert("INSERT INTO lang_alias VALUES(?,?,?)", aliasRows);
        db.insert("INSERT INTO lang_name VALUES(?,?,?,?,?)", nameRows);
        db.exec("CREATE INDEX lang_qid ON lang(qid); CREATE INDEX lang_alias_code ON lang_alias(code); "
                "CREATE INDEX lang_name_code ON lang_name(code); CREATE INDEX lang_name_lang ON lang_name(lang, name); "
                "COMMIT;");

        std::ofstream missingOut(output.parent_path() / (output.stem().string() + "_missing_qid.txt"));
        for (const auto &q : missing)
            missingOut << q << '\n';
        // All Q ids, for wiktneeded --items (select them from the Wikidata dump)
        std::set<std::string> allQids;
        for (const auto &[code, q] : codeQid)
            allQids.insert(q);
        std::ofstream allOut(output.parent_path() / (output.stem().string() + "_qid.txt"));
        for (const auto &q : allQids)
            allOut << q << '\n';
        std::cout << langRows.size() << " codes (" << codeQid.size() << " with Q), " << aliasRows.size()
                  << " aliases, " << nameRows.size() << " Wikidata names; " << missing.size()
                  << " Q ids not in Wikidata files (listed in " << output.stem().string() << "_missing_qid.txt)"
                  << std::endl;
        std::cout << "written " << output.string() << std::endl;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
