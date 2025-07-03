#pragma once
#include <cstddef>
#include <deque>
#include <fstream>
#include <map>
constexpr std::size_t MAX_LIST_SIZE = 10;

template<typename K, typename V>
class LimitedListMap {
    std::map<K, std::deque<V>> map;
public:
    void add(const K& key, const V& value) {
        auto& list = map[key];
        if (list.size() >= MAX_LIST_SIZE) {
            return;
        }
        list.push_back(value);
    }
    void print(std::ofstream& out) const {
        for (const auto& [k, v] : map) {
            out << k << "||";
            for (const auto& item : v) {
                out << item << ',';
            }
            out << '\n';
        }
    }
};

