// SPDX-License-Identifier: GPL-3.0-only
// The operator log (server/log.h): levels, the stderr sink that adds a UTC time, and Format.
// Plain printf underneath, because sco-core's rules keep iostreams out of this code.
#include "server/log.h"
#include <cstdarg>
#include <cstdio>
#include <ctime>

namespace sco::relay {

namespace {

std::string VFormat(const char* format, va_list args) {
    va_list again;
    va_copy(again, args);
    const int size = std::vsnprintf(nullptr, 0, format, again);
    va_end(again);
    std::string text;
    if (size > 0) {
        text.resize(static_cast<size_t>(size));
        std::vsnprintf(text.data(), text.size() + 1, format, args);
    }
    return text;
}

}  // namespace

const char* LogLevelName(LogLevel level) {
    switch (level) {
    case LogLevel::Error: return "error";
    case LogLevel::Warn:  return "warn";
    case LogLevel::Info:  return "info";
    case LogLevel::Debug: return "debug";
    }
    return "info";
}

bool ParseLogLevel(std::string_view text, LogLevel* out) {
    for (LogLevel level : { LogLevel::Error, LogLevel::Warn, LogLevel::Info, LogLevel::Debug }) {
        if (text == LogLevelName(level)) {
            *out = level;
            return true;
        }
    }
    return false;
}

void Logger::Log(LogLevel level, const char* format, ...) {
    if (!Enabled(level) || !sink_) return;
    va_list args;
    va_start(args, format);
    const std::string line = VFormat(format, args);
    va_end(args);
    sink_(level, line);
}

std::string Format(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::string text = VFormat(format, args);
    va_end(args);
    return text;
}

Logger::Sink StderrSink() {
    return [](LogLevel level, const std::string& line) {
        const std::time_t now = std::time(nullptr);
        std::tm utc{};
        char stamp[32] = "";
        if (gmtime_r(&now, &utc)) std::strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%SZ", &utc);
        std::fprintf(stderr, "%s %-5s %s\n", stamp, LogLevelName(level), line.c_str());
        std::fflush(stderr);
    };
}

}  // namespace sco::relay
