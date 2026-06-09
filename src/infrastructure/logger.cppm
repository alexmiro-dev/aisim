// infrastructure:: concrete logger — an async, file-backed kernel::ILogger.
// Internal partition of aisim.infrastructure; the composition root installs it.

module;

#include <filesystem>
#include <memory>
#include <source_location>
#include <string_view>

export module aisim.infrastructure:logger;

import aisim.kernel;

namespace aisim::infrastructure {

class Logger : public aisim::kernel::ILogger {
public:
    explicit Logger(const std::filesystem::path& path);
    ~Logger() override;

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void log(aisim::kernel::LogLevel level, std::string_view message,
             std::source_location where) override;

private:
    // Pimpl: the file stream, mutex, and sequence counter live in the
    // implementation unit so <fstream>/<mutex> stay out of this module's BMI.
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace aisim::infrastructure
