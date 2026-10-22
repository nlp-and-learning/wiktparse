#pragma once
#include <istream>
#include <string>
#include <vector>

// Linking English definitions to translation glosses (from my_c/wikitools/defglos)
namespace defglos {

// A numbered definition ("1.2") or translation gloss ("b") with its text
struct Item {
    std::string id;
    std::string text;
};

// Definitions and translation glosses of one part of speech of an entry
struct Entry {
    std::string title;
    std::string etymology; // "Etym 1"
    std::string pos;
    std::vector<Item> defs;
    std::vector<Item> glosses;
};

// Expected link of a definition to a gloss; hard links need knowledge, not word overlap
struct ExpectedLink {
    size_t def;
    size_t gloss;
    bool hard = false;
};

struct Sample {
    Entry entry;
    std::vector<ExpectedLink> expected;
};

// Reads res/defglos/samples.txt. A sample:
//   = band - Etym 1 - noun
//   1. definition          (one per line, ids like 1, 1.2, 4.6.2)
//   #
//   a. gloss               (one per line)
//   #
//   1.2 - b|c              (expected glosses of a definition, " - hard" at the end
//   3 -                     for hard links; ids that are not in the entry are skipped)
//   <empty line>
std::vector<Sample> readSamples(std::istream &in);
std::vector<Sample> readSamples(const std::string &path);

} // namespace defglos
