#include "Samples.h"
#include <fstream>
#include <stdexcept>
#include "util/textUtils.h"

namespace defglos {

namespace {

// "1.2. text" -> {"1.2", "text"}
Item readItem(const std::string &line) {
    auto text = trim(line);
    size_t space = text.find(' ');
    Item item{text.substr(0, space), space == std::string::npos ? "" : trim(text.substr(space + 1))};
    if (item.id.ends_with('.'))
        item.id.pop_back();
    return item;
}

std::vector<Item> readItems(std::istream &in) {
    std::vector<Item> items;
    std::string line;
    while (std::getline(in, line) && line != "#")
        items.push_back(readItem(line));
    return items;
}

ptrdiff_t indexOf(const std::vector<Item> &items, const std::string &id) {
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].id == id)
            return static_cast<ptrdiff_t>(i);
    return -1;
}

// "1.2 - b|c - hard"
void readExpected(const std::string &line, Sample &sample) {
    size_t dash = line.find(" -");
    if (dash == std::string::npos || dash == 0)
        throw std::runtime_error("defglos sample: no ' - ' in line: " + line);
    auto key = line.substr(0, dash);
    auto rest = trim(line.substr(dash + 2));
    bool hard = rest.ends_with(" - hard");
    if (hard)
        rest = trim(rest.substr(0, rest.size() - 7));
    ptrdiff_t def = indexOf(sample.entry.defs, key);
    if (def < 0)
        return;
    for (const auto &id : split(rest, '|')) {
        ptrdiff_t gloss = indexOf(sample.entry.glosses, trim(id));
        if (gloss >= 0)
            sample.expected.push_back({static_cast<size_t>(def), static_cast<size_t>(gloss), hard});
    }
}

} // namespace

std::vector<Sample> readSamples(std::istream &in) {
    std::vector<Sample> samples;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.starts_with("= "))
            continue;
        // "= band - Etym 1 - noun"; titles may contain '-'
        std::vector<std::string> parts;
        std::string header = line.substr(2);
        for (size_t pos; (pos = header.find(" - ")) != std::string::npos; header = header.substr(pos + 3))
            parts.push_back(trim(header.substr(0, pos)));
        parts.push_back(trim(header));
        Sample sample;
        sample.entry.title = parts[0];
        if (parts.size() >= 3) {
            sample.entry.etymology = parts[1];
            sample.entry.pos = parts[2];
        }
        sample.entry.defs = readItems(in);
        sample.entry.glosses = readItems(in);
        while (std::getline(in, line) && !trim(line).empty())
            readExpected(line, sample);
        samples.push_back(std::move(sample));
    }
    return samples;
}

std::vector<Sample> readSamples(const std::string &path) {
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("Cannot open file: " + path);
    return readSamples(in);
}

} // namespace defglos
