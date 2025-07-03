#pragma once
#include <chrono>
#include <string>

class Progress {
    size_t filesize;
    double stepLen;
    int stepLenDigits = 0;
    size_t lastStep = 0;
    std::string lastNumStr = "0";
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point lastTimerShown;
    static std::string format_time(double seconds);
    static std::string trim_after_difference(const std::string &a_raw, const std::string &b_raw);
    double timerIntereval = 20;
public:
    explicit Progress(size_t filesize, double stepLen = 0.01);
    int update(size_t currentPos);
};
