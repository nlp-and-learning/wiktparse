#include "Matcher.h"
#include <algorithm>

namespace defglos {

std::string_view methodName(Method method) {
    switch (method) {
        case Method::Positional: return "positional";
        case Method::Best: return "best";
        case Method::Hierarchy: return "hierarchy";
    }
    return "";
}

bool isSubdefinition(const std::string &child, const std::string &parent) {
    return child.size() > parent.size() + 1 && child.starts_with(parent) && child[parent.size()] == '.';
}

namespace {

using Matrix = std::vector<std::vector<double>>;

// Order-preserving alignment maximizing the sum of similarities >= threshold
// (weighted longest common subsequence; lists are short, so plain DP)
std::vector<Link> align(const Matrix &sim, double threshold) {
    const size_t n = sim.size();
    const size_t m = n ? sim[0].size() : 0;
    Matrix best(n + 1, std::vector<double>(m + 1, 0.0));
    for (size_t i = 1; i <= n; ++i)
        for (size_t j = 1; j <= m; ++j) {
            best[i][j] = std::max(best[i - 1][j], best[i][j - 1]);
            if (sim[i - 1][j - 1] >= threshold)
                best[i][j] = std::max(best[i][j], best[i - 1][j - 1] + sim[i - 1][j - 1]);
        }
    std::vector<Link> links;
    for (size_t i = n, j = m; i > 0 && j > 0;) {
        if (best[i][j] == best[i - 1][j])
            --i;
        else if (best[i][j] == best[i][j - 1])
            --j;
        else {
            links.push_back({i - 1, j - 1, sim[i - 1][j - 1], Method::Positional});
            --i;
            --j;
        }
    }
    std::ranges::reverse(links);
    return links;
}

} // namespace

std::vector<Link> match(const Entry &entry, const Similarity &similarity, const MatchOptions &options) {
    const auto &defs = entry.defs;
    const auto &glosses = entry.glosses;
    Matrix sim(defs.size(), std::vector<double>(glosses.size()));
    for (size_t i = 0; i < defs.size(); ++i)
        for (size_t j = 0; j < glosses.size(); ++j)
            sim[i][j] = similarity(defs[i].text, glosses[j].text);

    // 1. Positional
    auto links = align(sim, options.positional);
    std::vector<bool> defLinked(defs.size()), glossLinked(glosses.size());
    for (const auto &link : links)
        defLinked[link.def] = glossLinked[link.gloss] = true;

    // 2. Best definition of each gloss left
    for (size_t j = 0; j < glosses.size(); ++j) {
        if (glossLinked[j])
            continue;
        size_t bestDef = defs.size();
        for (size_t i = 0; i < defs.size(); ++i)
            if (sim[i][j] >= options.best && (bestDef == defs.size() || sim[i][j] > sim[bestDef][j]))
                bestDef = i;
        if (bestDef < defs.size()) {
            links.push_back({bestDef, j, sim[bestDef][j], Method::Best});
            defLinked[bestDef] = glossLinked[j] = true;
        }
    }

    // 3. Subdefinitions without a gloss share the gloss of their parent
    if (options.hierarchy) {
        const size_t direct = links.size();
        for (size_t l = 0; l < direct; ++l) {
            const Link parent = links[l];
            for (size_t i = 0; i < defs.size(); ++i)
                if (!defLinked[i] && isSubdefinition(defs[i].id, defs[parent.def].id)) {
                    links.push_back({i, parent.gloss, parent.score, Method::Hierarchy});
                    defLinked[i] = true;
                }
        }
    }

    std::ranges::sort(links, {}, [](const Link &link) { return std::pair(link.def, link.gloss); });
    return links;
}

} // namespace defglos
