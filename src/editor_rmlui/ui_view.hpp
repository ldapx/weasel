#pragma once

// VIEW — renders the whole editor document from the model. The view reads
// model state (never input events) and writes DOM markup/classes only. All
// generated-HTML caches live here: they exist purely to avoid layout churn.

#include "ui_model.hpp"

#include <string>

#include "log_sink.hpp"

namespace Rml
{
class Context;
class ElementDocument;
class Element;
} // namespace Rml

namespace editor_rml
{

struct ui_view
{
  explicit ui_view (ui_model &model) : model_ (model) {}

  void attach (Rml::ElementDocument *document, Rml::Context *context);
  void detach ();
  void set_log_sink (log_sink *sink);

  /**
   * Throttled full refresh plus the mandatory post-change Context::Update.
   * Must run after any content changes and before Render.
   */
  void update (double dt);

  /** Rebuilds every panel unconditionally. */
  void refresh ();

  /** Rebuilds the console output immediately (command echo path). */
  void refresh_console ();

  // Shell chrome.
  void refresh_toolbar ();
  void refresh_status ();
  void refresh_panels ();
  void apply_layout ();
  void refresh_welcome ();
  void refresh_new_project_modal ();
  void log_tab_diagnostics ();

  // Left panel.
  void refresh_hierarchy ();
  void refresh_singletons ();
  void refresh_systems ();
  void append_entity_subtree (wsl::rsc::scene *scene, entt::registry &reg,
                              entt::entity entity, int depth, std::string &html,
                              std::unordered_set<std::uint32_t> &rendered);

  // Files + build tabs.
  void refresh_files ();
  void refresh_build ();

  // Inspector.
  void append_comp_header (std::string &html, std::uint64_t key,
                           const std::string &display_name,
                           const std::string &icon_src = {});
  std::string component_icon_src (const entt::meta_type &meta);
  void append_field_row (std::string &html, entt::id_type comp_type,
                         entt::id_type field_id, const entt::meta_data &data,
                         entt::meta_any value);
  void refresh_inspector ();

  // Bottom panel.
  void refresh_log ();
  void refresh_log_filters ();

  ui_model &model_;

  Rml::ElementDocument *document_ = nullptr;
  Rml::Context *context_ = nullptr;
  log_sink *sink_ = nullptr;

  // Generated-markup caches: SetInnerRML only runs when the content
  // changed, which avoids needless layout churn (and scroll resets).
  std::string hierarchy_html_cache;
  std::string singleton_html_cache;
  std::string singleton_title_cache;
  std::string inspector_html_cache;
  std::string log_html_cache;
  std::string console_html_cache;
  std::string recents_html_cache;
  std::string status_html_cache;
  std::string np_toggle_cache;
  std::string systems_html_cache;
  std::string files_html_cache;
  std::string build_html_cache;
  std::string log_autoscroll_cache;

  // Resolved component icon sources (empty = none), keyed by type id.
  std::unordered_map<entt::id_type, std::string> icon_src_cache;

  double refresh_timer = 0.0;
  bool logged_diagnostics = false;

  double refresh_timer_ = 0.0;
  bool logged_diagnostics_ = false;

  /**
   * Sets inner RML only when the markup changed, to avoid layout churn.
   */
  static void set_rml_if_changed (Rml::Element *el, const std::string &html,
                                  std::string &cache);

  /**
   * Returns ``true`` when keyboard focus sits inside the inspector (i.e. the
   * user is editing a field). Periodic refreshes must skip rebuilding then,
   * or typing would be wiped out from under the cursor.
   */
  bool inspector_input_focused ();
};

} // namespace editor_rml
