#pragma once
#include <string>
#include <vector>
#include "Samples.h"
#include "Similarity.h"

namespace defglos {

// How a link was found
enum class Method {
    Positional, // order-preserving alignment of definitions and glosses
    Best,       // best definition of a gloss left after the alignment
    Hierarchy,  // subdefinition without its own gloss gets the gloss of its parent
};

std::string_view methodName(Method method);

struct Link {
    size_t def;
    size_t gloss;
    double score;
    Method method;
};

struct MatchOptions {
    // Defaults are the best on res/defglos/samples.txt (defglos --sweep)
    double positional = 0.05; // minimal similarity of an aligned pair
    double best = 0.1;        // minimal similarity of a non-positional pair
    bool hierarchy = false;  // lowers precision on the samples: subdefinitions often have no gloss
};

// Deterministic stages of linking (doc/defglos/wstepny_pseudokod.txt); pairs
// left without a link are for embeddings or an LLM
std::vector<Link> match(const Entry &entry, const Similarity &similarity, const MatchOptions &options = {});

// Whether definition id `child` ("3.1", "3.1.2") is below `parent` ("3")
bool isSubdefinition(const std::string &child, const std::string &parent);

} // namespace defglos
