#include "log_sink.hpp"

#include <spdlog/spdlog.h>

namespace editor_rml
{

log_sink::log_sink (std::size_t capacity)
    : m_capacity (capacity > 0 ? capacity : 1), m_lines (m_capacity)
{
}

void
log_sink::sink_it_ (const spdlog::details::log_msg &msg)
{
  entry rec;
  rec.level = level_name (msg.level);
  rec.category.assign (msg.logger_name.data (), msg.logger_name.size ());
  rec.message.assign (msg.payload.data (), msg.payload.size ());

  std::lock_guard<std::mutex> lock (m_lines_mutex);
  m_lines[m_next] = std::move (rec);
  m_next = (m_next + 1) % m_capacity;
  if (m_count < m_capacity) {
    ++m_count;
  }
}

std::vector<std::string>
log_sink::recent (std::size_t max_lines) const
{
  std::vector<std::string> out;
  for (const entry &rec : entries (max_lines)) {
    std::string line;
    line.reserve (rec.level.size () + rec.category.size () + rec.message.size ()
                  + 4);
    line += '[';
    line += rec.level;
    line += "] ";
    line += rec.category;
    line += ": ";
    line += rec.message;
    out.push_back (std::move (line));
  }
  return out;
}

std::vector<log_sink::entry>
log_sink::entries (std::size_t max_lines) const
{
  std::lock_guard<std::mutex> lock (m_lines_mutex);

  const std::size_t count = max_lines < m_count ? max_lines : m_count;
  std::vector<entry> out;
  out.reserve (count);

  const std::size_t start = (m_count == m_capacity) ? m_next : 0;
  for (std::size_t i = 0; i < count; ++i) {
    out.push_back (m_lines[(start + i) % m_capacity]);
  }
  return out;
}

void
log_sink::clear ()
{
  std::lock_guard<std::mutex> lock (m_lines_mutex);
  m_count = 0;
  m_next = 0;
}

const char *
log_sink::level_name (spdlog::level::level_enum level)
{
  switch (level) {
  case spdlog::level::trace:
    return "trace";
  case spdlog::level::debug:
    return "debug";
  case spdlog::level::info:
    return "info";
  case spdlog::level::warn:
    return "warn";
  case spdlog::level::err:
    return "error";
  case spdlog::level::critical:
    return "fatal";
  case spdlog::level::off:
  default:
    return "off";
  }
}

} // namespace editor_rml
