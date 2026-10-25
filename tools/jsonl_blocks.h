#pragma once
// JSONL of wikt2jsonl in compressed blocks: the writer (wikt2jsonl, wiktlink) and a reader
// of the blocks (wiktlink). Format: see JsonlWriter.
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <bzlib.h>
#include <zdict.h>
#include <zstd.h>

namespace jsonlblocks {

namespace fs = std::filesystem;

// Output of one wiki: plain JSONL, or blocks of 100 lines or 512 KB compressed one by one,
// bz2 or zstd (each an independent stream or frame, so bzcat and zstdcat read the file),
// with an index, colon-separated: a line per block "offset:count:uncompressed", then a line
// ":start:title" per page (start in the uncompressed block; a page ends where the next one
// starts, the last at uncompressed), at the end EOF=<size> (a block's compressed length is
// the next offset or EOF minus its offset). Thread-safe: each worker fills a block of its
// own (pages of one part of the dump, not interleaved with other threads' pages), compresses
// it outside the lock and appends it; blocks need no order, the index tells where pages are.
enum class Compression { none, bz2, zstd };

class JsonlWriter {
public:
    // expected: an estimate of the compressed bytes of the whole output (0: not known), for the
    // size of the zstd dictionary
    JsonlWriter(const fs::path &base, Compression compression, int level, size_t workers = 1, uint64_t expected = 0)
        : compression_(compression), level_(level), base_(base), expected_(expected),
          blocks_(std::max<size_t>(workers, 1)) {
        if (compression == Compression::none) {
            data_.open(base.string() + ".jsonl");
            return;
        }
        data_.open(base.string() + (compression == Compression::bz2 ? ".jsonl.bz2" : ".jsonl.zst"), std::ios::binary);
        index_.open(base.string() + ".jsonl.idx");
        index_ << "# wikt2jsonl index (" << (compression == Compression::bz2 ? "bz2" : "zstd, dictionary <wiki>.jsonl.dict")
               << "): offset:count:uncompressed, then :start:title per entry; EOF=<size>\n";
    }
    ~JsonlWriter() {
        close();
        ZSTD_freeCDict(dictionary_);
    }

    // worker: the calling thread's number, below the workers of the constructor
    void add(size_t worker, const std::string &line, const std::string &title) {
        if (compression_ == Compression::none) {
            std::lock_guard lock(mutex_);
            data_ << line << '\n';
            return;
        }
        auto &block = blocks_.at(worker); // only this worker's: no lock
        block.starts.push_back(block.text.size());
        block.text += line;
        block.text += '\n';
        block.titles.push_back(title);
        if (block.titles.size() >= blockLines || block.text.size() >= blockBytes)
            submit(std::exchange(block, {}));
    }

    // After all workers are done
    void close() {
        if (!data_.is_open())
            return;
        if (compression_ != Compression::none) {
            for (auto &block : blocks_)
                if (!block.titles.empty())
                    submit(std::exchange(block, {}));
            if (compression_ == Compression::zstd && !dictionary_) // a small wiki: what there is
                for (auto &block : train())
                    write(std::move(block));
            index_ << "EOF=" << offset_ << '\n';
            index_.close();
        }
        data_.close();
    }

private:
    static constexpr size_t blockLines = 100;
    static constexpr size_t blockBytes = 512 * 1024; // uncompressed: a page read decompresses little
    struct Block {
        std::string text;
        std::vector<std::string> titles;
        std::vector<size_t> starts; // of the lines in text
    };
    Compression compression_;
    int level_;
    std::ofstream data_, index_;
    std::mutex mutex_;          // of the files
    std::vector<Block> blocks_; // the block being filled, per worker
    uint64_t offset_ = 0;
    // zstd: a dictionary of the wiki, trained on its first blocks (kept back until then)
    // (pl: 8 -> 32 MB of training 1% smaller, more no gain; a 1 MB dictionary 7% smaller than 110 KB)
    static constexpr size_t trainBytes = 32 << 20, maxDictionaryBytes = 512 << 10;
    fs::path base_;
    uint64_t expected_;
    std::mutex trainMutex_;
    std::vector<Block> waiting_;
    size_t waitingBytes_ = 0;
    std::atomic<ZSTD_CDict *> dictionary_ = nullptr;

    void submit(Block block) {
        if (compression_ != Compression::zstd || dictionary_) {
            write(std::move(block));
            return;
        }
        std::vector<Block> ready;
        {
            std::lock_guard lock(trainMutex_);
            if (!dictionary_) {
                waitingBytes_ += block.text.size();
                waiting_.push_back(std::move(block));
                if (waitingBytes_ < trainBytes)
                    return;
                ready = trainLocked();
            } else
                ready.push_back(std::move(block));
        }
        for (auto &b : ready)
            write(std::move(b));
    }

    std::vector<Block> train() {
        std::lock_guard lock(trainMutex_);
        return trainLocked();
    }

    // The dictionary from the pages of the waiting blocks, written to <wiki>.jsonl.dict
    std::vector<Block> trainLocked() {
        std::string samples;
        std::vector<size_t> sizes;
        for (const auto &b : waiting_)
            for (size_t i = 0; i < b.starts.size(); ++i) {
                const size_t end = i + 1 < b.starts.size() ? b.starts[i + 1] : b.text.size();
                samples.append(b.text, b.starts[i], end - b.starts[i]);
                sizes.push_back(end - b.starts[i]);
            }
        // Its size from the expected compressed output: 0.5 MB from 14 MB on (a dictionary saves
        // some 7%), below half the saving, none under 16 KB; each block is compressed once
        // (a wiki whose output all waited: of that, some 0.12 of it; a larger one: of its dump)
        double compressed = 0.12 * static_cast<double>(waitingBytes_);
        if (waitingBytes_ >= trainBytes)
            compressed = std::max(compressed, static_cast<double>(expected_));
        size_t size = std::min<size_t>(maxDictionaryBytes, static_cast<size_t>(0.035 * compressed));
        if (size * 8 > samples.size())
            size = samples.size() / 8; // not most of the samples
        std::string dict;
        if (size >= (16 << 10) && sizes.size() >= 10) {
            dict.resize(size);
            const size_t trained = ZDICT_trainFromBuffer(dict.data(), dict.size(), samples.data(), sizes.data(),
                                                         static_cast<unsigned>(sizes.size()));
            dict.resize(ZDICT_isError(trained) ? 0 : trained);
        }
        const size_t made = dict.size();
        std::ofstream(base_.string() + ".jsonl.dict", std::ios::binary).write(dict.data(), static_cast<std::streamsize>(made));
        dictionary_ = ZSTD_createCDict(dict.data(), dict.size(), level_);
        waitingBytes_ = 0;
        return std::exchange(waiting_, {});
    }

    std::string compress(const std::string &text) const {
        if (compression_ == Compression::zstd) {
            thread_local std::unique_ptr<ZSTD_CCtx, decltype(&ZSTD_freeCCtx)> context(ZSTD_createCCtx(), ZSTD_freeCCtx);
            std::string out(ZSTD_compressBound(text.size()), '\0');
            const size_t length = ZSTD_compress_usingCDict(context.get(), out.data(), out.size(), text.data(), text.size(),
                                                           dictionary_.load());
            if (ZSTD_isError(length))
                throw std::runtime_error(std::string("zstd compression failed: ") + ZSTD_getErrorName(length));
            out.resize(length);
            return out;
        }
        std::string out(text.size() + text.size() / 100 + 600, '\0');
        auto length = static_cast<unsigned>(out.size());
        if (BZ2_bzBuffToBuffCompress(out.data(), &length, const_cast<char *>(text.data()),
                                     static_cast<unsigned>(text.size()), level_, 0, 0) != BZ_OK)
            throw std::runtime_error("bz2 compression failed");
        out.resize(length);
        return out;
    }

    void write(Block block) {
        const std::string out = compress(block.text);
        std::lock_guard lock(mutex_);
        data_.write(out.data(), static_cast<std::streamsize>(out.size()));
        index_ << offset_ << ':' << block.titles.size() << ':' << block.text.size() << '\n';
        for (size_t i = 0; i < block.titles.size(); ++i)
            index_ << ':' << block.starts[i] << ':' << block.titles[i] << '\n';
        offset_ += out.size();
    }
};

// The blocks of a <base>.jsonl.zst or .jsonl.bz2 with its .jsonl.idx (and the zstd
// .jsonl.dict), read block by block as lines; thread-safe (a block per call)
class BlockReader {
public:
    explicit BlockReader(const fs::path &data) : path_(data) {
        const std::string name = data.string();
        zstd_ = name.ends_with(".zst");
        const std::string base = name.substr(0, name.size() - 4); // <base>.jsonl
        std::ifstream index(base + ".idx");
        uint64_t end = 0;
        for (std::string l; std::getline(index, l);) {
            if (l.empty() || l[0] == '#' || l[0] == ':')
                continue;
            if (l.starts_with("EOF=")) {
                end = std::stoull(l.substr(4));
                continue;
            }
            const auto a = l.find(':'), b = l.find(':', a + 1);
            blocks_.push_back({std::stoull(l.substr(0, a)), 0, std::stoull(l.substr(b + 1))});
        }
        if (!end)
            end = fs::file_size(data);
        for (size_t i = 0; i < blocks_.size(); ++i)
            blocks_[i].compressed = (i + 1 < blocks_.size() ? blocks_[i + 1].offset : end) - blocks_[i].offset;
        if (zstd_) {
            std::ifstream in(base + ".dict", std::ios::binary);
            const std::string dict((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (!dict.empty())
                dictionary_ = ZSTD_createDDict(dict.data(), dict.size());
        }
    }
    ~BlockReader() { ZSTD_freeDDict(dictionary_); }
    BlockReader(const BlockReader &) = delete;
    BlockReader &operator=(const BlockReader &) = delete;

    [[nodiscard]] size_t size() const { return blocks_.size(); }

    // The lines of block i, without their '\n'
    [[nodiscard]] std::vector<std::string> lines(size_t i) const {
        const Block &b = blocks_.at(i);
        std::string compressed(b.compressed, '\0');
        {
            std::ifstream in(path_, std::ios::binary);
            in.seekg(static_cast<std::streamoff>(b.offset));
            in.read(compressed.data(), static_cast<std::streamsize>(compressed.size()));
        }
        std::string text(b.uncompressed, '\0');
        if (zstd_) {
            thread_local std::unique_ptr<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)> context(ZSTD_createDCtx(), ZSTD_freeDCtx);
            const size_t n = dictionary_ ? ZSTD_decompress_usingDDict(context.get(), text.data(), text.size(),
                                                                      compressed.data(), compressed.size(), dictionary_)
                                         : ZSTD_decompressDCtx(context.get(), text.data(), text.size(), compressed.data(),
                                                               compressed.size());
            if (ZSTD_isError(n))
                throw std::runtime_error(path_.string() + ": " + ZSTD_getErrorName(n));
            text.resize(n);
        } else {
            auto n = static_cast<unsigned>(text.size());
            if (BZ2_bzBuffToBuffDecompress(text.data(), &n, compressed.data(), static_cast<unsigned>(compressed.size()), 0,
                                           0) != BZ_OK)
                throw std::runtime_error(path_.string() + ": bz2 block at " + std::to_string(b.offset));
            text.resize(n);
        }
        std::vector<std::string> result;
        for (size_t from = 0; from < text.size();) {
            size_t to = text.find('\n', from);
            if (to == std::string::npos)
                to = text.size();
            result.emplace_back(text, from, to - from);
            from = to + 1;
        }
        return result;
    }

    // The titles of the pages of every block, from the index (the lines of block i are of titles[i])
    [[nodiscard]] std::vector<std::vector<std::string>> titles() const {
        const std::string name = path_.string();
        std::ifstream index(name.substr(0, name.size() - 4) + ".idx");
        std::vector<std::vector<std::string>> result;
        for (std::string l; std::getline(index, l);) {
            if (l.empty() || l[0] == '#' || l.starts_with("EOF="))
                continue;
            if (l[0] != ':')
                result.emplace_back();
            else if (const auto colon = l.find(':', 1); colon != std::string::npos && !result.empty())
                result.back().push_back(l.substr(colon + 1));
        }
        return result;
    }

private:
    struct Block {
        uint64_t offset = 0, compressed = 0, uncompressed = 0;
    };
    fs::path path_;
    bool zstd_ = false;
    ZSTD_DDict *dictionary_ = nullptr;
    std::vector<Block> blocks_;
};

} // namespace jsonlblocks
