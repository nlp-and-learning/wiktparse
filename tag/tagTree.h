#pragma once
#include <memory>
#include <vector>

struct Node {
    std::string value;
    std::vector<std::string> lines;
    std::vector<std::shared_ptr<Node>> children;
};

std::shared_ptr<Node> parseIndentedTree(std::istream& in);
void printTree(const std::shared_ptr<Node>& node, std::ostream& out, const std::string& prefix = "", bool isLast = true);