// kernel:: — the shared kernel. The INNERMOST ring: cross-cutting seams every
// layer may use but that name no technology. Depends on NOTHING.
//
// Holds the logging seam the core calls through; the concrete logger lives in
// infrastructure and implements this. See docs/architecture-v2.md §5.4, §7.

module;

#include <chrono>
#include <cstdint>
#include <source_location>
#include <string>
#include <thread>
#include <string_view>
#include <source_location>

export module aisim.kernel;

export namespace aisim::kernel {

enum class LogLevel : uint8_t {
    Debug = 0,
    Info = 1,
    Warning = 2,
    Error = 3,
    Critical = 4,
};

struct LogRecord {
    // Monotonic counter: incremented with std::atomic::fetch_add in the log() call. Breaks
    // timestamp ties when two threads log in the same nanosec, giving a strict toal order
    // in the output file.
    std::uint64_t seq_number;

    std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds> timestamp;

    std::thread::id thread_id;

    LogLevel level;

    std::string file_name;    // source_location::file_name()
    std::string function_name; // source_location::function_name()
    std::uint32_t line;        // source_location::line()
                               //
    std::string message;

    template <typename... Args>
    [[nodiscard]]
    static LogRecord make(
            std::uint64_t seq,
            LogLevel level,
            std::format_string<Args...> fmt,
            Args&&... args,
            std::source_location loc = std::source_location::current()) {

        LogRecord record;
        record.seq_number = seq;
        record.timestamp = std::chrono::time_point_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now());
        record.thread_id = std::this_thread::get_id();
        record.file_name = loc.file_name();
        record.function_name = loc.function_name();
        record.line = loc.line();
        record.level = level;
        record.message = std::format(fmt, std::format<Args>(args)...);

        return record;
    }
}; 

// The logging seam. Implemented by infrastructure::Logger; injected at startup.
class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void log(LogLevel level, std::string_view message,
                     std::source_location where =
                         std::source_location::current()) = 0;
};

// Ambient accessor: the composition root installs the instance once at startup.
ILogger& logger();
void install_logger(ILogger& instance);

}  // namespace aisim::kernel
