/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Bar layout from g_settings. Separate from settings_ui.cpp so the standalone
 * FPS monitor can link it without IPC / i18n.
 */

#include "hooked_funcs.hpp"
#include <onion/settings.hpp>

void apply_overlay_layout() {
  const float screen_w = g_overlay_layout.screen_w;
  const float screen_h = g_overlay_layout.screen_h;
  onion::overlay::Metrics metrics{};
  metrics.enabled = g_settings.overlay_enabled;
  metrics.show_fps = g_settings.overlay_fps;
  metrics.show_cpu = g_settings.overlay_cpu;
  metrics.show_gpu = g_settings.overlay_gpu;
  metrics.show_ram = g_settings.overlay_ram;
  metrics.show_ip = g_settings.overlay_ip;
  metrics.show_fan = g_settings.overlay_fan;
  metrics.per_core_cpu = g_settings.all_cpu_usage;
  metrics.font_size_pt = onion::overlay_font_pt(g_settings.overlay_font_size);
  metrics.order = g_settings.overlay_order;
  g_overlay_layout = onion::overlay::compute_overlay_layout(
      screen_w, screen_h,
      onion::overlay::bar_edge_from_pos(g_settings.overlay_pos),
      onion::overlay::bar_align_from_value(g_settings.overlay_align), metrics);
}

void apply_overlay_layout(float screen_w, float screen_h) {
  g_overlay_layout.screen_w = screen_w;
  g_overlay_layout.screen_h = screen_h;
  apply_overlay_layout();
}
