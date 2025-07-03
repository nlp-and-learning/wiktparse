#include "Progress.h"
#include <format>
#include <iostream>
#include <cmath>
#include <ctime>

Progress::Progress(size_t filesize, double stepLen)
    : filesize(filesize),stepLen(stepLen) {
    if (stepLen<=0 || stepLen>1) throw std::invalid_argument("Invalid step lengths");
    while (stepLen<1) {
        stepLen *= 10;
        stepLenDigits++;
    }
    startTime = std::chrono::steady_clock::now();
    lastTimerShown = startTime;
}

std::string Progress::format_time(double seconds) {
    if (seconds < 60) {
        return std::format("{:.0f} s", seconds);
    } else if (seconds < 600) {
        return std::format("{:.1f} m", seconds / 60.0);
    } else if (seconds < 3600) {
        return std::format("{:.0f} m", seconds / 60.0);
    } else {
        return std::format("{:.1f} h", seconds / 3600.0);
    }
}

size_t afterDot(const std::string& a) {
    auto pos = a.find('.');
    if (pos == std::string::npos) return 0;
    return a.size() - pos - 1;
}

std::string Progress::trim_after_difference(const std::string& a_raw, const std::string& b_raw) {
    std::string a = a_raw;
    std::string b = b_raw;
    if (a==b) return a;

    if (a.find('.') == std::string::npos) a += ".";
    if (b.find('.') == std::string::npos) b += ".";
    auto a_afterDort = afterDot(a);
    auto b_afterDort = afterDot(b);
    auto max_afterDort = std::max(a_afterDort, b_afterDort);
    auto min_afterDort = std::min(a_afterDort, b_afterDort);
    std::string s(max_afterDort - min_afterDort, '0');
    if (a_afterDort < max_afterDort)
        a += s;
    else
        b += s;

    size_t len = std::min(a.size(), b.size());
    size_t i = 0;
    for (; i < len; ++i) {
        if (a[i] != b[i]) {
            ++i; // take into account this one differing digit
            break;
        }
    }
    std::string result = b.substr(0, i);
    // Remove the trailing ‘.’ only if it is the end
    if (!result.empty() && result.back() == '.') {
        result += "0";
    }

    return result;
}

int Progress::update(size_t currentPos) {
    if (filesize == 0) return 0;
    double progress = static_cast<double>(currentPos) / filesize;
    auto step = size_t(progress / stepLen);
    auto now = std::chrono::steady_clock::now();
    double elapsedSeconds = std::chrono::duration<double>(now - startTime).count();
    double sinceLastShown = std::chrono::duration<double>(now - lastTimerShown).count();
    bool forceByTimer = sinceLastShown >= timerIntereval;
    bool percentChanged = step > lastStep;
    double stepDouble = (double)step*stepLen*100;
    if (percentChanged || forceByTimer) {
        lastStep = step;
        lastTimerShown = now;
        std::ostringstream oss;

        std::string numStr;
        if (percentChanged) {
            oss << std::fixed << std::setprecision(std::max(0, stepLenDigits - 2)) << stepDouble;
            lastNumStr = oss.str();
        } else {
            numStr = std::format("{}",progress*100);
            lastNumStr = trim_after_difference(lastNumStr, numStr);
        }
        std::string line = std::format("{} %", lastNumStr);
        double totalSeconds = progress>0?elapsedSeconds / progress:0;
        double remaining = totalSeconds - elapsedSeconds;
        if (totalSeconds >= 60) {
            line += std::format("  ({} elapsed, {} left", format_time(elapsedSeconds), format_time(remaining));
            if (totalSeconds >= 300) {
                std::time_t now_time = std::time(nullptr);
                std::time_t end_time = now_time + static_cast<std::time_t>(remaining);
                
                std::tm now_local{};
                localtime_r(&now_time, &now_local);

                std::tm end_local{};
                localtime_r(&end_time, &end_local);

                if (end_local.tm_mday != now_local.tm_mday) {
                    line += std::format(", ends at {:04}-{:02}-{:02} {:02}:{:02}",
                        1900 + end_local.tm_year, end_local.tm_mon + 1, end_local.tm_mday,
                        end_local.tm_hour, end_local.tm_min);
                } else {
                    line += std::format(", ends at {:02}:{:02}", end_local.tm_hour, end_local.tm_min);
                }
            }
            line += ")";
        }
        std::cout << line << "\n";
        return percentChanged?1:2;
    }
    return 0;
}
