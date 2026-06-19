#pragma once

// Tiny leveled logging interface shared by every REDesk core layer.
//
// ADR-001 doesn't pick a logging library (that's a service/UI concern), so core
// stays dependency-free: it logs through this thin abstraction. The service can
// install a real sink (spdlog/os_log/EventLog) via setLogger(); until then a
// default stderr logger is active so stub builds and tests get output for free.
//
// Header-only + a single .cpp for the default sink; no external deps (std only),
// satisfying the REDESK_USE_REAL_BACKENDS=OFF "must compile anywhere" rule.

#include <memory>
#include <sstream>
#include <string>
#include <string_view>

namespace redesk {

enum class LogLevel {
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Off,
};

const char* toString(LogLevel level) noexcept;

// Sink interface. Implementations must be thread-safe: core layers log from
// capture/encode/transport threads concurrently.
class ILogger {
public:
    virtual ~ILogger() = default;

    // Emit a fully-formatted line at `level` originating from `category`
    // (e.g. "capture", "transport"). `file`/`line` are the call site.
    virtual void log(LogLevel level, std::string_view category,
                     std::string_view message, const char* file, int line) = 0;

    // Cheap level gate so hot-path call sites can skip formatting.
    virtual bool isEnabled(LogLevel level) const = 0;
};

// Process-wide logger. Defaults to a stderr logger at Info. Never null.
ILogger& logger();

// Install a custom sink (e.g. from the service). Passing nullptr restores the
// default stderr logger. Returns the previous logger (ownership not transferred
// for the default; callers replacing a custom logger should manage lifetime).
void setLogger(std::shared_ptr<ILogger> sink);

// Default stderr sink, exposed so tests/services can construct one explicitly.
std::shared_ptr<ILogger> makeStderrLogger(LogLevel min_level = LogLevel::Info);

// ---------------------------------------------------------------------------
// Convenience macros. They gate on the level before building the stream, so a
// disabled level costs one virtual `isEnabled` call. Usage:
//     REDESK_LOG(Info, "capture") << "started display " << id;
// ---------------------------------------------------------------------------
namespace detail {

// RAII line builder: accumulates into a stream and flushes to the logger on
// destruction. Lives only for the duration of the log statement.
class LogLine {
public:
    LogLine(LogLevel level, std::string_view category, const char* file, int line)
        : level_(level), category_(category), file_(file), line_(line) {}

    ~LogLine() {
        logger().log(level_, category_, stream_.str(), file_, line_);
    }

    template <typename T>
    LogLine& operator<<(const T& value) {
        stream_ << value;
        return *this;
    }

    LogLine(const LogLine&) = delete;
    LogLine& operator=(const LogLine&) = delete;

private:
    LogLevel level_;
    std::string_view category_;
    const char* file_;
    int line_;
    std::ostringstream stream_;
};

} // namespace detail
} // namespace redesk

// The `for`-trick keeps the temporary scoped to the statement and short-circuits
// when the level is disabled. `redesk::LogLevel::` qualifies the enum argument.
#define REDESK_LOG(level_name, category)                                        \
    if (!::redesk::logger().isEnabled(::redesk::LogLevel::level_name)) {         \
    } else                                                                      \
        ::redesk::detail::LogLine(::redesk::LogLevel::level_name, (category),    \
                                  __FILE__, __LINE__)
