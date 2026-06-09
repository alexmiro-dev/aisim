// infrastructure:: Logger definitions — the implementation unit of
// aisim.infrastructure. Names the primary module (no partition, no `export`),
// so it sees every partition's declarations and contributes nothing to the BMI.
// Heavy includes live in the global module fragment here and never leak to
// consumers. See docs/architecture-v2.md §5.4, §7.

module;

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <source_location>
#include <string_view>
#include <format>

module aisim.infrastructure;

import :logger;        // the interface partition this unit defines
import aisim.kernel;

namespace aisim::infrastructure {

namespace {

constexpr std::string_view level_name(aisim::kernel::LogLevel level) noexcept {
    using aisim::kernel::LogLevel;
    switch (level) {
        case LogLevel::Debug:    return "DEBUG";
        case LogLevel::Info:     return "INFO";
        case LogLevel::Warning:  return "WARN";
        case LogLevel::Error:    return "ERROR";
        case LogLevel::Critical: return "CRIT";
    }
    return "?";
}

}  // namespace

// The state the interface partition forward-declares as Logger::Impl. Defined
// here so <fstream>/<mutex>/<atomic> stay confined to this translation unit.
struct Logger::Impl {
    std::ofstream out;
    std::mutex mutex;
    std::atomic<std::uint64_t> seq{0};
};

Logger::Logger(const std::filesystem::path& path)
    : impl_{std::make_unique<Impl>()} {
    // Best-effort: ensure the parent directory exists before opening. The
    // composition root chooses the path (XDG state dir); creating it here keeps
    // the call site simple.
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    impl_->out.open(path, std::ios::out | std::ios::app);
}

Logger::~Logger() = default;

void Logger::log(aisim::kernel::LogLevel level, std::string_view message,
                 std::source_location where) {
    const std::uint64_t seq =
        impl_->seq.fetch_add(1, std::memory_order_relaxed);
    const auto now = std::chrono::system_clock::now();

    // Format outside the lock; hold it only for the write so contention is
    // bounded by I/O, not formatting.
    const std::string line = std::format(
        "{} [{:>5}] #{} {}:{} {}\n",
        now, level_name(level), seq,
        std::filesystem::path{where.file_name()}.filename().string(),
        where.line(), message);

    std::lock_guard<std::mutex> guard(impl_->mutex);
    if (impl_->out.is_open()) {
        impl_->out << line;
        impl_->out.flush();
    }
}

}  // namespace aisim::infrastructure
