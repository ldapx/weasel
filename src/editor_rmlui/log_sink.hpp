#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <spdlog/sinks/base_sink.h>

namespace editor_rml
{

/**
 * spdlog sink that keeps the last N messages in memory so the editor UI can
 * render a live log panel without touching the loggers' console sinks.
 *
 * Entries keep their level and logger name so the panel can filter by
 * severity and category (mirroring the ImGui editor's logger panel).
 *
 * Thread-safe: loggers may write from worker threads while the UI reads.
 */
class log_sink final : public spdlog::sinks::base_sink<std::mutex>
{
public:
  /** One captured log record. */
  struct entry
  {
    /** Lowercase level name: trace/debug/info/warn/error/fatal/off. */
    std::string level;
    /** Logger name (core/gfx/rsc/sys/editor/cli/phys/net/xmake/...). */
    std::string category;
    /** Raw payload; may contain embedded newlines. */
    std::string message;
  };

  explicit log_sink (std::size_t capacity = 1024);

  /** Returns up to `max_lines` most recent lines, oldest first. */
  std::vector<std::string> recent (std::size_t max_lines) const;

  /** Returns up to `max_lines` most recent entries, oldest first. */
  std::vector<entry> entries (std::size_t max_lines) const;

  /** Drops all stored lines. */
  void clear ();

protected:
  void sink_it_ (const spdlog::details::log_msg &msg) override;
  void
  flush_ () override
  {
  }

private:
  static const char *level_name (spdlog::level::level_enum level);

  std::size_t m_capacity;
  mutable std::mutex m_lines_mutex;
  std::vector<entry> m_lines;
  std::size_t m_next = 0;
  std::size_t m_count = 0;
};

} // namespace editor_rml
