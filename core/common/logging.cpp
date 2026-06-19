// Default stderr logger implementation (ADR-001 §1: core is dependency-free; a
// real sink is injected by the service). Thread-safe via a single mutex around
// the write — adequate for a default sink; the production sink will batch.

#include "core/common/logging.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <mutex>

namespace redesk {

const char* toString(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Off:   return "OFF";
    }
    return "?";
}

namespace {

// Strip the directory portion of __FILE__ so lines stay short and don't leak
// the build machine's absolute paths.
std::string_view basename(const char* path) {
    std::string_view p{path ? path : ""};
    const auto slash = p.find_last_of("/\\");
    return slash == std::string_view::npos ? p : p.substr(slash + 1);
}

class StderrLogger final : public ILogger {
public:
    explicit StderrLogger(LogLevel min_level) : min_level_(min_level) {}

    void log(LogLevel level, std::string_view category, std::string_view message,
             const char* file, int line) override {
        if (!isEnabled(level)) {
            return;
        }
        // Wall-clock timestamp (ms precision) — coarse but enough for a default.
        using namespace std::chrono;
        const auto now = system_clock::now();
        const auto t = system_clock::to_time_t(now);
        const auto ms =
            duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
        std::tm tm_buf{};
#if defined(_WIN32)
        localtime_s(&tm_buf, &t);
#else
        localtime_r(&t, &tm_buf);
#endif
        char ts[32];
        std::snprintf(ts, sizeof(ts), "%02d:%02d:%02d.%03d", tm_buf.tm_hour,
                      tm_buf.tm_min, tm_buf.tm_sec, static_cast<int>(ms));

        std::lock_guard<std::mutex> lock(mutex_);
        std::fprintf(stderr, "[%s %-5s %.*s] %.*s (%.*s:%d)\n", ts,
                     toString(level), static_cast<int>(category.size()),
                     category.data(), static_cast<int>(message.size()),
                     message.data(),
                     static_cast<int>(basename(file).size()),
                     basename(file).data(), line);
    }

    bool isEnabled(LogLevel level) const override {
        return static_cast<int>(level) >=
               static_cast<int>(min_level_.load(std::memory_order_relaxed));
    }

private:
    std::atomic<LogLevel> min_level_;
    std::mutex mutex_;
};

// Holder so the active logger is replaceable atomically. The default sink is a
// function-local static (constructed on first use, never destroyed early).
std::shared_ptr<ILogger>& activeSlot() {
    static std::shared_ptr<ILogger> slot = makeStderrLogger();
    return slot;
}

std::mutex& slotMutex() {
    static std::mutex m;
    return m;
}

} // namespace

std::shared_ptr<ILogger> makeStderrLogger(LogLevel min_level) {
    return std::make_shared<StderrLogger>(min_level);
}

ILogger& logger() {
    std::lock_guard<std::mutex> lock(slotMutex());
    return *activeSlot();
}

void setLogger(std::shared_ptr<ILogger> sink) {
    std::lock_guard<std::mutex> lock(slotMutex());
    activeSlot() = sink ? std::move(sink) : makeStderrLogger();
}

} // namespace redesk
