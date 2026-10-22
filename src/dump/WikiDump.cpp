#include "WikiDump.h"

#include <optional>
#include <stdexcept>

#include <wikilib/core/progress.h>
#include <wikilib/dump/index_chunker.h>
#include <wikilib/dump/parallel_reader.h>
#include <wikilib/markup/tokenizer.h>

#include "../titles.h"

using namespace wikilib::dump;

WikiDump::WikiDump(const DumpPath &path, bool cleanComments) : reader(path), cleanComments(cleanComments) {
}

DumpPath WikiDump::dumpPath(WikiProject project, const std::string &lang) {
    DumpPath path = DumpPath::from_config();
    path.set_project(project).set_language(lang);
    return path;
}

WikiDump WikiDump::wiktionary(const std::string &lang) {
    return WikiDump(dumpPath(WikiProject::Wiktionary, lang));
}

WikiDump WikiDump::wikipedia(const std::string &lang) {
    return WikiDump(dumpPath(WikiProject::Wikipedia, lang));
}

WikiDump WikiDump::wikidata() {
    // Wikidata pages are JSON, not wikitext: no comment stripping
    return WikiDump(dumpPath(WikiProject::Wikidata), false);
}

std::string WikiDump::pageText(std::string_view text) const {
    if (cleanComments)
        return wikilib::markup::strip_comments(text);
    return std::string(text);
}

bool WikiDump::forEachPage(const PageVisitor &visitor, double progressStep) {
    std::optional<wikilib::ProgressReporter> progress;
    auto report = [&](const DumpReader::ProcessProgress &p) {
        if (!progress)
            progress.emplace(p.bytes_total, progressStep);
        progress->update(p.bytes_compressed);
    };
    bool stopped = false;
    auto visit = [&](const std::string &title, std::string_view content) {
        if (getTitleType(title).first == TitleType::Other)
            return true;
        if (!visitor(title, pageText(content))) {
            stopped = true;
            return false;
        }
        return true;
    };
    if (path().index_exists())
        reader.process_indexed([&](const Page &page) { return visit(page.info.title, page.content()); }, report);
    else
        reader.process_all([&](const std::string &title, const std::string &content) { return visit(title, content); },
                           report);
    if (!reader.error().empty())
        throw std::runtime_error(reader.error());
    return !stopped;
}

size_t WikiDump::workerCount(size_t threads) const {
    return parallel_worker_count(path(), threads);
}

void WikiDump::forEachPageParallel(const ParallelPageVisitor &visitor, size_t threads, double progressStep) {
    std::optional<wikilib::ProgressReporter> progress;
    auto error = process_parallel(
            path(), threads,
            [&](size_t worker, const std::string &title, std::string_view content) {
                if (getTitleType(title).first != TitleType::Other)
                    visitor(worker, title, pageText(content));
                return true;
            },
            [&](const DumpReader::ProcessProgress &p) {
                if (!progress)
                    progress.emplace(p.bytes_total, progressStep);
                progress->update(p.bytes_compressed);
            });
    if (!error.empty())
        throw std::runtime_error(error);
}

std::vector<std::string> WikiDump::extractTerms(const std::vector<std::string> &terms) {
    if (!reader.index_loaded()) {
        reader.load_index();
        if (!reader.error().empty())
            throw std::runtime_error(reader.error());
    }
    auto pages = reader.extract_pages(terms);
    if (!reader.error().empty())
        throw std::runtime_error(reader.error());
    std::vector<std::string> result;
    result.reserve(pages.size());
    for (const auto &page : pages)
        result.push_back(page.found ? pageText(page.content) : "");
    return result;
}

std::map<std::string, std::string> WikiDump::extractMatching(const std::function<bool(const std::string &)> &filter) {
    std::map<std::string, std::string> pages;
    bool ok = reader.process_matching(filter, [&](const Page &page) {
        pages[page.info.title] = std::string(page.content());
        return true;
    });
    if (!ok)
        throw std::runtime_error(reader.error());
    return pages;
}

void WikiDump::forEachTitle(const std::function<void(const std::string &title)> &visitor) const {
    auto chunker = IndexChunker::from_file(path().index_path().string(), 0);
    for (IndexChunk chunk; chunker.next_chunk(chunk);) {
        for (const auto &entry : chunk.entries)
            visitor(entry.title);
    }
    if (!chunker.error().empty())
        throw std::runtime_error(std::string(chunker.error()));
}
