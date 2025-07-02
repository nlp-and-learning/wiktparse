#pragma once
#include <string>
#include <vector>

class Comments {
    enum class Marker {
        StartNowiki,
        EndNowiki,
        StartComment,
        EndComment
    };
    static std::vector<std::pair<size_t, Marker>> classify_line(const std::string& line);
public:
    static std::string clean(const std::string& input);
};

