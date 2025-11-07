// logger.h
#pragma once
#define FMT_HEADER_ONLY
#include <fmt/core.h>
#include <atomic>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

enum class LogLevel : int { TRACE = 0, DEBUG = 1, INFO = 2, ERROR = 3 };

inline std::atomic<LogLevel> g_log_level{LogLevel::INFO};

inline void set_log_level(LogLevel lvl) {
  g_log_level.store(lvl, std::memory_order_relaxed);
}

inline LogLevel get_log_level() {
  return g_log_level.load(std::memory_order_relaxed);
}

inline void set_log_level(const std::string& s) {
  std::string lower;
  lower.reserve(s.size());
  for (char c : s) {
      lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  if (lower == "trace") { set_log_level(LogLevel::TRACE);}
  if (lower == "debug") { set_log_level(LogLevel::DEBUG);}
  if (lower == "info")  { set_log_level(LogLevel::INFO);}
}

inline void log_line(LogLevel lvl, const std::string& msg){
  using namespace std::chrono;
  const auto now = system_clock::to_time_t(system_clock::now());
  std::tm tm{};
  localtime_r(&now, &tm);
  std::ostream& out = std::cout;
  out << std::put_time(&tm, "%F %T") << ' ';
  switch (lvl) {
    case LogLevel::TRACE: out << "[TRACE] "; break;
    case LogLevel::DEBUG: out << "[DEBUG] "; break;
    case LogLevel::INFO:  out << "[INFO ] "; break;
    case LogLevel::ERROR: out << "[ERROR] "; break;
    default: break;
  }
  out << msg << '\n';
  out.flush();
}

template <typename... Args>
inline void log_fmt(LogLevel lvl, fmt::format_string<Args...> fmt_str, Args&&... args) {
  const LogLevel cur = get_log_level();
  if (lvl < cur) return;
  log_line(lvl, fmt::format(fmt_str, std::forward<Args>(args)...));
}

#define LOG_TRACE(fmt_str, ...) ::log_fmt(::LogLevel::TRACE, FMT_STRING(fmt_str), ##__VA_ARGS__)
#define LOG_DEBUG(fmt_str, ...) ::log_fmt(::LogLevel::DEBUG, FMT_STRING(fmt_str), ##__VA_ARGS__)
#define LOG_INFO(fmt_str, ...)  ::log_fmt(::LogLevel::INFO,  FMT_STRING(fmt_str), ##__VA_ARGS__)
#define LOG_ERROR(fmt_str, ...) ::log_fmt(::LogLevel::ERROR, FMT_STRING(fmt_str), ##__VA_ARGS__)
