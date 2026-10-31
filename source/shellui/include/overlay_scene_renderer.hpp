/* Copyright (C) 2025 OnionHEN
 *
 * Mono side of the game monitor overlay: reconciles an overlay_scene.hpp
 * Scene into UI2.Panel / UI2.Label widgets on the Game scene RootWidget.
 * Widgets are created on first use, updated only when a property changed,
 * moved off-screen when a frame no longer mentions them, and destroyed by
 * remove_all(). UI thread only. Built into the fps-monitor ShellUI module only.
 */
#pragma once

#include "overlay_scene.hpp"

/** Apply one frame. Returns false if the RootWidget is not available. */
bool overlay_scene_apply(const onion::overlay::scene::Scene &scene);

/** Destroy every overlay widget (detail change, overlay off, font change). */
void overlay_scene_remove_all();

/** Drop the frame-time history and FPS session stats (monitor_overlay.cpp),
 *  so a detail change does not show stale points across the gap. */
void overlay_scene_reset_state();
