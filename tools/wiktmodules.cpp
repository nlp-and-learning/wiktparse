// Finds Lua modules of Wiktionaries that list languages: tables of names to
// codes (zh Module:Languages/canonical names: ["阿歐語"] = "aou") or of codes
// to data with a name (en Module:languages/data/2: m["pl"] = { "Polish", ...}).
// Such modules give the language names a wiki uses in its headings.
//
// wiktmodules [-j N] [-o modules.tsv] [--min 50] [wiki...]
//
// Reads only the indexed ranges holding pages of the module namespace (828,
// named from the dump's <siteinfo>). Output: wiki, module, kind (name-code or
// code-data), entries, local (names that are no English en.wikt names), examples.
#include <algorithm>
#include <atomic>
#include <exception>
#include <fstream>
#include <iostream>
#include <mutex>
#include <regex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <sqlite3.h>
#include <wikilib/dump/dump_reader.h>

#include "dump/WikiDump.h"

using namespace wikilib::dump;

namespace {

struct Found {
    std::string module, kind, examples;
    size_t entries = 0;
    size_t local = 0; // names that are no English names of en.wikt: the wiki's own
};

// Codes of en.wikt (languages, families, etymology languages) from wiktlangs.sqlite: entries with
// other "codes" are transliterations or inflection tables (五笔, hak-pron, odmiana)
std::set<std::string> knownCodes;
std::set<std::string> englishNames;

std::vector<Found> scan(const std::string &title, const std::string &text, size_t minEntries) {
    // ["name"] = "code"  and  m["code"] = { "Name"
    static const std::regex nameCode(R"re(\[\s*["']([^"'\]]{1,80})["']\s*\]\s*=\s*["']([a-z]{2,3}(?:-[a-zA-Z]{2,8})*)["'])re");
    static const std::regex codeData(R"re(\w+\s*\[\s*["']([a-z]{2,3}(?:-[a-zA-Z]{2,8})*)["']\s*\]\s*=\s*(?:\w+\s*)?\{\s*["']([^"']{1,80})["'])re");
    std::vector<Found> result;
    for (auto [re, kind] : {std::pair{&nameCode, "name-code"}, std::pair{&codeData, "code-data"}}) {
        Found f{title, kind, "", 0};
        size_t all = 0;
        const int codeGroup = std::string_view(kind) == "name-code" ? 2 : 1;
        for (std::sregex_iterator it(text.begin(), text.end(), *re), end; it != end; ++it) {
            ++all;
            if (!knownCodes.empty() && !knownCodes.contains((*it)[codeGroup].str()))
                continue;
            const auto name = (*it)[codeGroup == 2 ? 1 : 2].str();
            if (!englishNames.contains(name))
                ++f.local;
            if (f.entries < 3)
                f.examples += (f.examples.empty() ? "" : "; ") + (*it)[1].str() + " = " + (*it)[2].str();
            ++f.entries;
        }
        if (f.entries >= minEntries && f.entries * 2 >= all)
            result.push_back(std::move(f));
    }
    return result;
}

} // namespace

int main(int argc, char *argv[]) {
    try {
        size_t threads = 8, minEntries = 50;
        std::string output = "modules.tsv";
        std::vector<std::string> wikis;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-j" && i + 1 < argc)
                threads = std::max(1, std::stoi(argv[++i]));
            else if (arg == "-o" && i + 1 < argc)
                output = argv[++i];
            else if (arg == "--min" && i + 1 < argc)
                minEntries = std::stoul(argv[++i]);
            else if (arg == "-h" || arg == "--help") {
                std::cout << "usage: wiktmodules [-j N] [-o modules.tsv] [--min 50] [wiki...]\n";
                return 0;
            } else
                wikis.push_back(arg);
        }
        if (sqlite3 *db = nullptr; sqlite3_open_v2("wiktlangs.sqlite", &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK) {
            sqlite3_stmt *stmt = nullptr;
            sqlite3_prepare_v2(db, "SELECT code FROM lang", -1, &stmt, nullptr);
            while (sqlite3_step(stmt) == SQLITE_ROW)
                if (auto code = sqlite3_column_text(stmt, 0))
                    knownCodes.emplace(reinterpret_cast<const char *>(code));
            sqlite3_finalize(stmt);
            sqlite3_prepare_v2(db, "SELECT name FROM lang", -1, &stmt, nullptr);
            while (sqlite3_step(stmt) == SQLITE_ROW)
                if (auto name = sqlite3_column_text(stmt, 0))
                    englishNames.emplace(reinterpret_cast<const char *>(name));
            sqlite3_finalize(stmt);
            sqlite3_close(db);
        }
        if (knownCodes.empty())
            std::cerr << "warning: no ./wiktlangs.sqlite, entries are not checked against language codes" << std::endl;
        if (wikis.empty())
            wikis = WikiDump::dumpPath(WikiProject::Wiktionary, "en").available_languages();

        std::ofstream out(output);
        out << "wiki\tmodule\tkind\tentries\tlocal\texamples\n";
        std::mutex mutex;
        std::atomic<size_t> next{0};
        auto work = [&] {
            for (size_t i; (i = next++) < wikis.size();) {
                const auto &wiki = wikis[i];
                try {
                    DumpReader reader(WikiDump::dumpPath(WikiProject::Wiktionary, wiki));
                    auto site = reader.read_site_info();
                    if (!site)
                        throw std::runtime_error(reader.error());
                    std::vector<Found> found;
                    size_t modules = 0;
                    reader.process_matching([&](const std::string &t) { return site->namespace_of(t) == 828; },
                                            [&](const Page &page) {
                                                ++modules;
                                                for (auto &f : scan(page.info.title, std::string(page.content()), minEntries))
                                                    found.push_back(std::move(f));
                                                return true;
                                            });
                    std::ranges::sort(found, std::greater{}, &Found::entries);
                    std::lock_guard lock(mutex);
                    for (const auto &f : found)
                        out << wiki << '\t' << f.module << '\t' << f.kind << '\t' << f.entries << '\t' << f.local << '\t' << f.examples << '\n';
                    std::cout << wiki << ": " << modules << " modules, " << found.size() << " with language tables" << std::endl;
                } catch (const std::exception &e) {
                    std::lock_guard lock(mutex);
                    std::cerr << wiki << ": " << e.what() << std::endl;
                }
            }
        };
        std::vector<std::thread> pool;
        for (size_t t = 0; t < std::min(threads, wikis.size()); ++t)
            pool.emplace_back(work);
        for (auto &thread : pool)
            thread.join();
        std::cout << "written to " << output << std::endl;
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
