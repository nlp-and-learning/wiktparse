#include "tagTree.h"
#include <format>
#include <stack>
#include <fstream>

#include "tags.h"

// parsing to tree
std::shared_ptr<Node> parseIndentedTree(std::istream& in) {
    auto root = std::make_shared<Node>();
    root->value = "ROOT";

    // pair stack (level indent, pointer to node)
    std::stack<std::pair<int, std::shared_ptr<Node>>> stack;
    stack.emplace(-1, root);

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty())
            continue;

        int indent = count_levelL(line);
        if (indent<2) {
            stack.top().second->lines.push_back(line);
            continue;
        }
        std::string trimmed = trim_tag(line);

        auto node = std::make_shared<Node>();
        node->value = trimmed;

        // znajdź rodzica o mniejszym wcięciu
        while (!stack.empty() && stack.top().first >= indent)
            stack.pop();

        stack.top().second->children.push_back(node);
        stack.emplace(indent, node);
    }

    return root;
}

void printTree(const std::shared_ptr<Node>& node, std::ostream& out, const std::string& prefix, bool isLast) {
    if (node->value != "ROOT") {
        out << prefix;
        out << (isLast ? "└── " : "├── ");
        out << std::format("{}({})\n",node->value, node->lines.size());
    }

    for (size_t i = 0; i < node->children.size(); ++i) {
        bool last = (i == node->children.size() - 1);
        std::string newPrefix = prefix + (node->value != "ROOT" ? (isLast ? "    " : "│   ") : "");
        printTree(node->children[i], out, newPrefix, last);
    }
}
