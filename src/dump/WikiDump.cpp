#include "WikiDump.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>

#include <wikilib/dump/index_chunker.h>
#include <wikilib/markup/tokenizer.h>

#include "../titles.h"
#include "../util/Progress.h"

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
    std::optional<Progress> progress;
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
    return std::max<size_t>(1, std::min(threads, path().dump_parts().size()));
}

void WikiDump::forEachPageParallel(const ParallelPageVisitor &visitor, size_t threads, double progressStep) {
    const auto parts = path().dump_parts();
    if (parts.empty())
        throw std::runtime_error("Dump file not found: " + path().dump_path().string());
    const size_t workers = workerCount(threads);

    uint64_t totalBytes = 0;
    for (const auto &part : parts)
        totalBytes += std::filesystem::file_size(part);
    std::vector<std::atomic<uint64_t>> partBytes(parts.size());
    Progress progress(totalBytes, progressStep);
    std::mutex progressMutex;
    auto report = [&](size_t part, uint64_t bytes) {
        partBytes[part] = bytes;
        std::lock_guard lock(progressMutex);
        uint64_t sum = 0;
        for (const auto &b : partBytes)
            sum += b;
        progress.update(sum);
    };

    std::atomic<size_t> nextPart{0};
    std::atomic<bool> failed{false};
    std::string error;
    auto work = [&](size_t worker) {
        for (size_t part; !failed && (part = nextPart++) < parts.size();) {
            auto partError = process_dump_file(
                parts[part],
                [&](const std::string &title, const std::string &content) {
                    if (getTitleType(title).first != TitleType::Other)
                        visitor(worker, title, pageText(content));
                    return !failed.load();
                },
                [&](const DumpReader::ProcessProgress &p) { report(part, p.bytes_compressed); });
            if (!partError.empty() && !failed.exchange(true)) {
                std::lock_guard lock(progressMutex);
                error = parts[part].filename().string() + ": " + partError;
            }
        }
    };
    std::vector<std::thread> pool;
    for (size_t worker = 0; worker < workers; worker++)
        pool.emplace_back(work, worker);
    for (auto &thread : pool)
        thread.join();
    if (failed)
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

void WikiDump::forEachTitle(const std::function<void(const std::string &title)> &visitor) const {
    auto chunker = IndexChunker::from_file(path().index_path().string(), 0);
    for (IndexChunk chunk; chunker.next_chunk(chunk);) {
        for (const auto &entry : chunk.entries)
            visitor(entry.title);
    }
    if (!chunker.error().empty())
        throw std::runtime_error(std::string(chunker.error()));
}
