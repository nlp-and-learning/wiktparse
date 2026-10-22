// Section names of Wiktionaries (synonyms, references, noun...) in many
// languages, into wiktsections.sqlite (next to wiktlangs.sqlite).
//
// English Wiktionary section headings (wiktheaders: headers/en.tsv, main
// namespace, level 2 and below) without languages (wiktlangs.sqlite) are
// the sections; each is looked up in wiktNeeded by its English label or alias,
// and the labels and aliases of that entity give its names in other
// languages. Headings of other Wiktionaries named by hand come from
// res/wiktionary_section_names.tsv.
//
// wiktsections [-o wiktsections.sqlite] [--headers headers] [--db wiktlangs.sqlite]
//              [--min-pages 3] [--min-labels 10] [wiktNeeded.jsonl...]
//
// Tables:
//   section(section, pages, qid, kind): English headings, with the Wikidata item
//   section_name(section, lang, name, source): names; source wikidata-label,
//     wikidata-alias or manual (lang is then the wiki code)
#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <wikilib/core/unicode_utils.h>

#include "util/textUtils.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::string lower(const std::string &s) { return wikilib::unicode::to_lower(s); }

class Db {
public:
    explicit Db(const fs::path &file, bool readOnly = false) {
        int flags = readOnly ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
        if (sqlite3_open_v2(file.string().c_str(), &db_, flags, nullptr) != SQLITE_OK)
            throw std::runtime_error("Cannot open " + file.string());
    }
    ~Db() { sqlite3_close(db_); }
    void exec(const std::string &sql) {
        char *err = nullptr;
        if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
            std::string message = err ? err : "?";
            sqlite3_free(err);
            throw std::runtime_error(message + " in " + sql);
        }
    }
    void insert(const std::string &sql, const std::vector<std::vector<std::string>> &rows) {
        sqlite3_stmt *stmt = nullptr;
        sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        for (const auto &row : rows) {
            for (size_t i = 0; i < row.size(); ++i)
                sqlite3_bind_text(stmt, static_cast<int>(i + 1), row[i].c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_reset(stmt);
        }
        sqlite3_finalize(stmt);
    }
    std::vector<std::string> column(const std::string &sql) {
        std::vector<std::string> result;
        sqlite3_stmt *stmt = nullptr;
        sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        while (sqlite3_step(stmt) == SQLITE_ROW)
            if (auto text = sqlite3_column_text(stmt, 0))
                result.emplace_back(reinterpret_cast<const char *>(text));
        sqlite3_finalize(stmt);
        return result;
    }

private:
    sqlite3 *db_ = nullptr;
};

} // namespace

int main(int argc, char *argv[]) {
    try {
        fs::path output = "wiktsections.sqlite", headersDir = "headers", langsFile = "wiktlangs.sqlite";
        size_t minPages = 3, minLabels = 10;
        std::vector<fs::path> wikidataFiles;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-o" && i + 1 < argc)
                output = argv[++i];
            else if (arg == "--headers" && i + 1 < argc)
                headersDir = argv[++i];
            else if (arg == "--db" && i + 1 < argc)
                langsFile = argv[++i];
            else if (arg == "--min-pages" && i + 1 < argc)
                minPages = std::stoul(argv[++i]);
            else if (arg == "--min-labels" && i + 1 < argc)
                minLabels = std::stoul(argv[++i]);
            else if (arg == "-h" || arg == "--help") {
                std::cout << "usage: wiktsections [-o wiktsections.sqlite] [--headers headers] [--db wiktlangs.sqlite] "
                             "[--min-pages 3] [--min-labels 10] [wiktNeeded.jsonl...]\n";
                return 0;
            } else
                wikidataFiles.emplace_back(arg);
        }
        if (wikidataFiles.empty())
            for (const auto &entry : fs::directory_iterator("."))
                if (entry.path().filename().string().starts_with("wiktNeeded_") && entry.path().extension() == ".jsonl")
                    wikidataFiles.push_back(entry.path());
        if (wikidataFiles.empty() && fs::exists("wiktNeeded.jsonl"))
            wikidataFiles.emplace_back("wiktNeeded.jsonl");

        // English names of languages (and families, scripts) are no sections
        std::set<std::string> languages;
        {
            Db langs(langsFile, true);
            for (const auto &n : langs.column("SELECT name FROM language"))
                languages.insert(lower(n));
            for (const auto &n : langs.column("SELECT name FROM language_name WHERE in_code='en'"))
                languages.insert(lower(n));
        }

        // English section headings below language level; "Etymology 2" counts as "Etymology"
        static const std::regex numbered(R"(^(.*\S)\s+\d+(?:\.\d+)*$)");
        std::map<std::string, size_t> pages; // heading as written -> pages
        for (const auto &line : readLines((headersDir / "en.tsv").string())) {
            auto f = split(line, '\t');
            if (f.size() < 4 || f[0] != "(main)" || f[1] == "level" || std::stoi(f[1]) < 2)
                continue;
            std::string heading = trim(f[3]);
            std::smatch m;
            if (std::regex_match(heading, m, numbered))
                heading = m[1].str();
            if (languages.contains(lower(heading)))
                continue;
            pages[heading] += std::stoul(f[2]);
        }
        // Spellings of one heading ("Derived terms", "Derived Terms") count as the most frequent one
        {
            std::map<std::string, std::string> best;
            for (const auto &[heading, count] : pages)
                if (auto &b = best[lower(heading)]; b.empty() || count > pages[b])
                    b = heading;
            std::map<std::string, size_t> merged;
            for (const auto &[heading, count] : pages)
                merged[best[lower(heading)]] += count;
            pages = std::move(merged);
        }
        std::map<std::string, std::string> byLower; // lowercase -> heading
        for (auto it = pages.begin(); it != pages.end();) {
            if (it->second < minPages) {
                it = pages.erase(it);
                continue;
            }
            byLower[lower(it->first)] = it->first;
            // "Hypernyms": the label is singular ("hypernym")
            if (auto l = lower(it->first); l.size() > 3 && l.ends_with('s') && !l.ends_with("ss"))
                byLower.emplace(l.substr(0, l.size() - 1), it->first);
            ++it;
        }
        std::cout << pages.size() << " English section headings (>= " << minPages << " pages)" << std::endl;

        // Entities of wiktNeeded whose English label or alias is a heading; the
        // one with labels in most languages wins
        struct Match {
            std::string qid;
            size_t languages = 0;
            json labels, aliases;
        };
        std::map<std::string, Match> matches;
        for (const auto &file : wikidataFiles) {
            std::ifstream in(file);
            for (std::string line; std::getline(in, line);) {
                auto e = json::parse(line, nullptr, false);
                // Few labels: a chance namesake ("References", an academic chapter)
                if (e.is_discarded() || !e.contains("labels") || e["labels"].size() < minLabels)
                    continue;
                std::vector<std::string> english;
                if (auto it = e["labels"].find("en"); it != e["labels"].end())
                    english.push_back((*it)["value"]);
                if (e.contains("aliases"))
                    if (auto it = e["aliases"].find("en"); it != e["aliases"].end())
                        for (const auto &a : *it)
                            english.push_back(a["value"]);
                for (const auto &name : english) {
                    auto heading = byLower.find(lower(name));
                    if (heading == byLower.end())
                        continue;
                    auto &match = matches[heading->second];
                    if (e["labels"].size() > match.languages)
                        match = {e["id"], e["labels"].size(), e["labels"], e.value("aliases", json::object())};
                }
            }
        }
        // Items of sections missing in wiktNeeded (res/wiktionary_section_qids.tsv): Q without names until
        // wiktneeded --items selects them
        std::map<std::string, std::string> extraQids;
        for (const auto &line : readLines(WIKTPARSE_RES_DIR "/wiktionary_section_qids.tsv")) {
            auto f = split(line, '\t');
            if (f.size() >= 2 && f[1].starts_with("Q") && !matches.contains(f[0]))
                extraQids[f[0]] = f[1];
        }
        std::cout << matches.size() << " of them found in " << wikidataFiles.size() << " wiktNeeded files" << std::endl;

        fs::remove(output);
        Db db(output);
        db.exec("BEGIN; CREATE TABLE section(section TEXT PRIMARY KEY, pages INTEGER, qid TEXT, kind TEXT);"
                "CREATE TABLE section_name(section TEXT, lang TEXT, name TEXT, source TEXT);");
        std::vector<std::vector<std::string>> sectionRows, nameRows;
        for (const auto &[heading, count] : pages) {
            auto it = matches.find(heading);
            std::string qid = it != matches.end() ? it->second.qid : extraQids.contains(heading) ? extraQids[heading] : "";
            sectionRows.push_back({heading, std::to_string(count), qid, ""});
            if (it == matches.end())
                continue;
            for (const auto &[lang, label] : it->second.labels.items())
                nameRows.push_back({heading, lang, label["value"], "wikidata-label"});
            for (const auto &[lang, list] : it->second.aliases.items())
                for (const auto &a : list)
                    nameRows.push_back({heading, lang, a["value"], "wikidata-alias"});
        }
        // Headings of other Wiktionaries named by hand: lang is the wiki
        size_t manual = 0;
        for (const auto &line : readLines(WIKTPARSE_RES_DIR "/wiktionary_section_names.tsv")) {
            auto f = split(line, '\t');
            if (f.size() < 4 || f[0] == "wiki" || f[0].starts_with("#"))
                continue;
            std::string section = f[2];
            if (auto it = byLower.find(lower(section)); it != byLower.end())
                section = it->second; // "references" -> "References"
            else
                sectionRows.push_back({section, "0", "", f[3]});
            nameRows.push_back({section, f[0], f[1], "manual"});
            ++manual;
        }
        db.insert("INSERT OR IGNORE INTO section VALUES(?,?,?,?)", sectionRows);
        db.insert("INSERT INTO section_name VALUES(?,?,?,?)", nameRows);
        db.exec("CREATE INDEX section_name_lang ON section_name(lang, name); COMMIT;");
        std::cout << nameRows.size() << " names (" << manual << " by hand) written to " << output.string() << std::endl;
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
