#pragma once
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <wikilib/dump/dump_reader.h>

// Wikimedia dump read through wikilib. Dump location comes from the wikilib
// config (WIKILIB_DUMP_DIR or ~/.config/wikilib/config).
// Visited page texts have comments removed by default; pages with titles of
// TitleType::Other are skipped.
class WikiDump {
    wikilib::dump::DumpReader reader;
    bool cleanComments;
    std::string pageText(std::string_view text) const;
public:
    // Return false to stop visiting
    using PageVisitor = std::function<bool(const std::string &title, const std::string &text)>;

    explicit WikiDump(const wikilib::dump::DumpPath &path, bool cleanComments = true);
    static wikilib::dump::DumpPath dumpPath(wikilib::dump::WikiProject project, const std::string &lang = "");
    static WikiDump wiktionary(const std::string &lang);
    static WikiDump wikipedia(const std::string &lang);
    static WikiDump wikidata();

    [[nodiscard]] const wikilib::dump::DumpPath &path() const { return reader.path(); }
    // Uses the index when present, otherwise reads dump (or its parts) sequentially.
    // Returns false when stopped by the visitor.
    bool forEachPage(const PageVisitor &visitor, double progressStep = 0.01);

    // Called concurrently; worker is 0..workerCount()-1, one worker calls it sequentially
    using ParallelPageVisitor = std::function<void(size_t worker, const std::string &title, const std::string &text)>;
    // Number of workers forEachPageParallel uses: threads; without index at most one per dump part
    [[nodiscard]] size_t workerCount(size_t threads) const;
    // Reads the dump in parallel (wikilib::dump::process_parallel): with index, each
    // worker takes the next indexed range (multistream); otherwise the next dump part
    void forEachPageParallel(const ParallelPageVisitor &visitor, size_t threads, double progressStep = 0.01);
    // Texts of terms in the same order; empty string for missing term
    std::vector<std::string> extractTerms(const std::vector<std::string> &terms);
    // Titles from the index, without reading the dump
    void forEachTitle(const std::function<void(const std::string &title)> &visitor) const;
    // Raw texts (comments kept) of pages whose title passes the filter; reads
    // only the indexed ranges containing them
    std::map<std::string, std::string> extractMatching(const std::function<bool(const std::string &title)> &filter);
};
