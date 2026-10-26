// Section names of Wiktionaries (synonyms, references, noun...) in many
// languages, into wiktsections.sqlite (next to wiktlangs.sqlite).
//
// English Wiktionary section headings (wiktheaders: headers/en.tsv, main
// namespace, level 2 and below) without languages (wiktlangs.sqlite) are
// the sections; each is looked up in wiktNeeded by its English label or alias,
// and the labels and aliases of that entity give its names in other
// languages. Headings of other Wiktionaries named by hand come from
// res/wiktionary_section_names.tsv. Parts of speech and lexical relations are
// the Wikidata items of --pos (instances of Q82042, part of speech) and
// --relations (Q2594143, lexical relation: holonyms, meronyms…): sections of
// kind pos and relation, named in all languages of their labels; English names
// the items lack, by hand in res/wiktionary_section_english.tsv.
//
// wiktsections [-o wiktsections.sqlite] [--headers headers] [--db wiktlangs.sqlite]
//              [--min-pages 3] [--min-labels 10] [--pos Q82042-P31.jsonl]
//              [--relations Q2594143-P1269.jsonl] [wiktNeeded.jsonl...]
//
// Tables:
//   section(section, pages, qid, kind): English headings, with the Wikidata item; kind pos or
//     relation of the Wikidata items, otherwise empty (or of res/wiktionary_section_names.tsv)
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
#include <tuple>
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
        // Items of parts of speech (instances of Q82042) and of lexical relations (Q2594143)
        fs::path posFile = "Q82042-P31.jsonl", relationsFile = "Q2594143-P1269.jsonl";
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
            else if (arg == "--pos" && i + 1 < argc)
                posFile = argv[++i];
            else if (arg == "--relations" && i + 1 < argc)
                relationsFile = argv[++i];
            else if (arg == "-h" || arg == "--help") {
                std::cout << "usage: wiktsections [-o wiktsections.sqlite] [--headers headers] [--db wiktlangs.sqlite] "
                             "[--min-pages 3] [--min-labels 10] [--pos Q82042-P31.jsonl] "
                             "[--relations Q2594143-P1269.jsonl] [wiktNeeded.jsonl...]\n";
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
            // "=Derived terms", "Derived terms=": stray '=' of a heading written unevenly
            while (!heading.empty() && (heading.front() == '=' || heading.back() == '=')) {
                if (heading.front() == '=')
                    heading.erase(0, 1);
                if (!heading.empty() && heading.back() == '=')
                    heading.pop_back();
                heading = trim(heading);
            }
            if (heading.empty())
                continue;
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

        // Parts of speech and lexical relations of Wikidata: their kind (the only parts of speech
        // of the table), and their labels and aliases in all languages as names; the English name
        // is the English label (a heading of that name when there is one: "verb" -> "Verb"), or
        // by hand (res/wiktionary_section_english.tsv: pl "Przeczenie" -> "Negation")
        std::map<std::string, std::string> englishOf;
        for (const auto &line : readLines(WIKTPARSE_RES_DIR "/wiktionary_section_english.tsv")) {
            auto f = split(line, '\t');
            if (f.size() >= 2 && f[0].starts_with("Q"))
                englishOf[f[0]] = trim(f[1]);
        }
        std::set<std::tuple<std::string, std::string, std::string>> named; // section, lang, name
        for (const auto &row : nameRows)
            named.insert({row[0], row[1], row[2]});
        std::vector<std::vector<std::string>> kindRows, kindNames;
        std::set<std::string> posSections;
        for (const auto &[file, kind] : {std::pair{posFile, "pos"}, std::pair{relationsFile, "relation"}}) {
            std::ifstream in(file);
            if (!in) {
                std::cout << "no " << file.string() << ": no " << kind << " items" << std::endl;
                continue;
            }
            size_t items = 0;
            for (std::string line; std::getline(in, line);) {
                auto e = json::parse(line, nullptr, false);
                if (e.is_discarded() || !e.contains("id"))
                    continue;
                const std::string qid = e["id"];
                std::string english;
                if (auto it = englishOf.find(qid); it != englishOf.end())
                    english = it->second;
                else if (e.contains("labels") && e["labels"].contains("en"))
                    english = e["labels"]["en"]["value"];
                if (english.empty()) {
                    std::cout << kind << " " << qid << ": no English name (res/wiktionary_section_english.tsv)" << std::endl;
                    continue;
                }
                std::string section = english;
                if (auto it = byLower.find(lower(english)); it != byLower.end())
                    section = it->second;
                else if (size_t pos = 0; wikilib::unicode::decode_utf8(section, pos))
                    section = wikilib::unicode::to_upper(section.substr(0, pos)) + section.substr(pos);
                kindRows.push_back({section, "0", qid, kind});
                if (std::string(kind) == "pos")
                    posSections.insert(section);
                auto addName = [&](const std::string &lang, const std::string &name, const char *source) {
                    if (named.insert({section, lang, name}).second)
                        kindNames.push_back({section, lang, name, source});
                };
                for (const auto &[lang, label] : e.value("labels", json::object()).items())
                    addName(lang, label["value"], "wikidata-label");
                for (const auto &[lang, list] : e.value("aliases", json::object()).items())
                    for (const auto &a : list)
                        addName(lang, a["value"], "wikidata-alias");
                ++items;
            }
            std::cout << items << " " << kind << " items of " << file.string() << std::endl;
        }
        // (a heading already in the table keeps its pages and gets the kind; its qid when it had none)
        db.insert("INSERT OR IGNORE INTO section VALUES(?,?,?,?)", kindRows);
        for (const auto &row : kindRows)
            db.insert("UPDATE section SET kind=?2, qid=COALESCE(NULLIF(qid,''),?3) WHERE section=?1",
                      {{row[0], row[3], row[2]}});
        nameRows.insert(nameRows.end(), kindNames.begin(), kindNames.end());
        // Other parts of speech (by hand, "Symbol") are not Wikidata's: no kind
        if (!posSections.empty())
            for (const auto &section : db.column("SELECT section FROM section WHERE kind='pos'"))
                if (!posSections.contains(section))
                    db.insert("UPDATE section SET kind='' WHERE section=?1", {{section}});

        db.insert("INSERT INTO section_name VALUES(?,?,?,?)", nameRows);
        db.exec("CREATE INDEX section_name_lang ON section_name(lang, name); COMMIT;");
        std::cout << nameRows.size() << " names (" << manual << " by hand) written to " << output.string() << std::endl;
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
