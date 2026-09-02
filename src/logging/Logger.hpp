#pragma once
#include "app/Config.hpp"
#include <memory>
#include <spdlog/async.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <string>


namespace svs {

/// Initializes all named async loggers with rotating file + console sinks.
/// Usage: Logger::init(cfg); then spdlog::get("HID_EVENTS")->info("...");
class Logger {
public:
  static void init(const Config &cfg) {
    const std::string log_dir = cfg.get("LOG_DIR", "./logs");
    const std::string log_level = cfg.get("LOG_LEVEL", "info");
    const int max_mb = cfg.getInt("LOG_MAX_SIZE_MB", 10);
    const int max_files = cfg.getInt("LOG_MAX_FILES", 5);

    // Create output directory
    std::filesystem::create_directories(log_dir);

    // Thread pool for async logging (8192 queue slots, 2 threads)
    spdlog::init_thread_pool(8192, 2);

    auto level = spdlog::level::from_str(log_level);

    // Shared sinks
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(level);

    // Create a named logger for each category
    for (auto &name : categories()) {
      auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
          log_dir + "/" + name + ".log",
          static_cast<size_t>(max_mb) * 1024 * 1024, max_files);
      file_sink->set_level(level);

      auto logger = std::make_shared<spdlog::async_logger>(
          name, spdlog::sinks_init_list{file_sink, console_sink},
          spdlog::thread_pool(), spdlog::async_overflow_policy::block);

      logger->set_level(level);
      logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
      spdlog::register_logger(logger);
    }

    // Extremely important: Force async logger to flush to disk every second
    // so we can read logs in real-time without needing to shutdown the app.
    spdlog::flush_every(std::chrono::seconds(1));
  }

  static void shutdown() { spdlog::shutdown(); }

  // -- Convenience accessors --
  static std::shared_ptr<spdlog::logger> hid() {
    return spdlog::get("HID_EVENTS");
  }
  static std::shared_ptr<spdlog::logger> session() {
    return spdlog::get("SESSION");
  }
  static std::shared_ptr<spdlog::logger> api() {
    return spdlog::get("API_CALLS");
  }
  static std::shared_ptr<spdlog::logger> security() {
    return spdlog::get("SECURITY");
  }
  static std::shared_ptr<spdlog::logger> perf() {
    return spdlog::get("PERFORMANCE");
  }
  static std::shared_ptr<spdlog::logger> errors() {
    return spdlog::get("ERRORS");
  }

private:
  static const std::vector<std::string> &categories() {
    static const std::vector<std::string> cats = {"HID_EVENTS",  "SESSION",
                                                  "API_CALLS",   "SECURITY",
                                                  "PERFORMANCE", "ERRORS"};
    return cats;
  }
};

} // namespace svs
