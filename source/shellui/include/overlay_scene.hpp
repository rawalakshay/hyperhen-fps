/* Copyright (C) 2025 OnionHEN
 *
 * Game performance overlay layout, three looks:
 *   Minimal  status dot + FPS pill
 *   Compact  one strip: FPS, frame time, AVG, LOW, mini frame-time chart
 *   Panel    corner panel: big FPS, AVG / LOW, FPS chart (10 s) and
 *            frame-time chart (one bar per frame sample); Full = Panel
 *
 * Pure layout: readings + history in, a flat list of rectangles and text runs
 * out (absolute RootWidget coordinates, back-to-front). The ShellUI renderer
 * (overlay_scene_renderer.cpp) reconciles that list into UI2.Panel /
 * UI2.Label widgets by id, so this file has no Mono dependency and is covered
 * by host tests. The renderer only draws rectangles and text, so both charts
 * are bar charts.
 *
 * Colour is reserved for state (under target, frame-time spikes); everything
 * else is grey labels and white values.
 */
#pragma once

#include "overlay_layout.hpp"
#include "overlay_text_metrics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace onion::overlay::scene {

struct Rgba {
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
  float a = 1.0f;
};

constexpr Rgba hex(uint32_t rgb, float a = 1.0f) {
  return {static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
          static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
          static_cast<float>(rgb & 0xFF) / 255.0f, a};
}

constexpr Rgba with_alpha(Rgba c, float a) { return {c.r, c.g, c.b, a}; }

/* Palette. */
inline constexpr Rgba kText = hex(0xEEF2F5);
/** Sub-headings: FPS, AVG, LOW, chart captions, MIN / MAX. */
inline constexpr Rgba kHeading = hex(0xC9D0D6);
/** Dim text: title id, target pill, units. */
inline constexpr Rgba kLabel = hex(0xA7B0B8);
inline constexpr uint32_t kSurfaceRgb = 0x0B0E13;
inline constexpr Rgba kHairline = hex(0xFFFFFF, 0.09f);
inline constexpr Rgba kSeparator = hex(0xFFFFFF, 0.14f);
inline constexpr Rgba kPillFill = hex(0xFFFFFF, 0.08f);
inline constexpr Rgba kBaseline = hex(0x3A434C);
inline constexpr Rgba kTargetLine = hex(0xEEF2F5, 0.45f);
inline constexpr Rgba kGuideLine = hex(0xEEF2F5, 0.22f);
/* State colours differ in lightness as well as hue. */
inline constexpr Rgba kGood = hex(0x5AA9E6);
inline constexpr Rgba kWarn = hex(0xF5B13D);
inline constexpr Rgba kBad = hex(0xFF7A59);
/* System row bars: a dim track with a neutral fill. */
inline constexpr Rgba kTrack = hex(0xFFFFFF, 0.10f);
inline constexpr Rgba kFill = hex(0xC9D2E0);

/* Shown in the panel header when Options::brand is on. */
inline constexpr const char *kBrandText = "4aLse Benchmarks";

/** Usage bars and usage values turn warn above this percentage. */
inline constexpr float kBarWarnPct = 85.0f;
/** Full scale of the GPU core power bar, watts. Provisional until a heavy
 *  scene has been read on a console. */
inline constexpr float kGpuPowerScaleW = 130.0f;

/** Thermal thresholds in °C: warn from `hot`, bad from `crit`. */
struct TempScale {
  int hot;
  int crit;
};
inline constexpr TempScale kCpuTempScale = {65, 80};
inline constexpr TempScale kGpuTempScale = {65, 85};

inline Rgba temp_color(int celsius, TempScale scale) {
  if (celsius >= scale.crit)
    return kBad;
  if (celsius >= scale.hot)
    return kWarn;
  return kText;
}

enum class FpsState : uint8_t { Good, Warn, Bad };

/** How close the frame rate is to the title's target. */
inline FpsState fps_state(float fps, int target) {
  const float t = static_cast<float>(target > 0 ? target : 60);
  if (fps >= 0.95f * t)
    return FpsState::Good;
  if (fps >= 0.8f * t)
    return FpsState::Warn;
  return FpsState::Bad;
}

inline Rgba fps_state_color(FpsState s) {
  switch (s) {
  case FpsState::Good:
    return kGood;
  case FpsState::Warn:
    return kWarn;
  case FpsState::Bad:
  default:
    return kBad;
  }
}

/* Large numbers stay white while things are fine. */
inline Rgba fps_value_color(FpsState s) {
  return s == FpsState::Good ? kText : fps_state_color(s);
}

/** Frame-time bar: on target up to 1.2× the target frame time, a spike up to
 *  2×, worse beyond that. */
inline Rgba frame_ms_color(float ms, int target) {
  const float t = 1000.0f / static_cast<float>(target > 0 ? target : 60);
  if (ms <= 1.2f * t)
    return kGood;
  if (ms <= 2.05f * t)
    return kWarn;
  return kBad;
}

/** Common frame caps, lowest first. */
inline constexpr std::array<int, 4> kFpsTargets = {30, 40, 60, 120};

/** The cap a title most likely runs at: the first cap its average fits
 *  under (with 5% slack). 60 when nothing is known yet. */
inline int infer_target(float avg) {
  if (!(avg > 0.0f) || !std::isfinite(avg))
    return 60;
  for (int t : kFpsTargets) {
    if (avg <= static_cast<float>(t) * 1.05f)
      return t;
  }
  return kFpsTargets.back();
}

/**
 * infer_target() with hysteresis: a new cap must win `hold` consecutive
 * updates before it replaces the current one, so a brief dip does not flip
 * the target line and every colour with it.
 */
class TargetTracker {
public:
  explicit TargetTracker(int hold = 20) : hold_(hold) {}

  int update(float avg) {
    const int t = infer_target(avg);
    if (t == target_) {
      pending_ = 0;
      streak_ = 0;
    } else if (t == pending_) {
      if (++streak_ >= hold_) {
        target_ = t;
        pending_ = 0;
        streak_ = 0;
      }
    } else {
      pending_ = t;
      streak_ = 1;
      if (streak_ >= hold_) {
        target_ = t;
        pending_ = 0;
        streak_ = 0;
      }
    }
    return target_;
  }
  int target() const { return target_; }
  void reset() { *this = TargetTracker(hold_); }

private:
  int hold_;
  int target_ = 60;
  int pending_ = 0;
  int streak_ = 0;
};

/** Fixed-size ring of the most recent samples, read oldest → newest. */
template <int Cap> class Ring {
public:
  static constexpr int kCap = Cap;

  void push(float v) {
    if (!std::isfinite(v))
      return;
    buf_[head_] = v;
    head_ = (head_ + 1) % Cap;
    if (count_ < Cap)
      ++count_;
  }
  void clear() {
    head_ = 0;
    count_ = 0;
  }
  int size() const { return count_; }
  /** i = 0 is the oldest retained sample. */
  float at(int i) const {
    const int start = (head_ - count_ + Cap) % Cap;
    return buf_[(start + i) % Cap];
  }

private:
  std::array<float, Cap> buf_{};
  int head_ = 0;
  int count_ = 0;
};

/* FPS chart: one bar per 250 ms, 10 s on screen. */
inline constexpr int kBucketMs = 250;
inline constexpr int kFpsBars = 40;
/* Frame-time chart: one bar per frame sample, newest on the right. */
inline constexpr int kFrameBars = 90;
/* Compact strip: the newest frame samples only. */
inline constexpr int kStripBars = 50;
/* Frame-time graph: one bar per sample, 7.5 s at a 33 ms publish.
 * shortcut: 225 Mono widgets untested on console; halve to 112 (two samples
 * per bar) if ShellUI stutters. */
inline constexpr int kLongBars = 225;

/** Windows the numbers are measured over. */
inline constexpr uint64_t kFpsWindowNs = 500000000ull;    /* FPS, frame time */
inline constexpr uint64_t kMaxWindowNs = 1000000000ull;   /* frame-time MAX */
inline constexpr uint64_t kStatsWindowNs = 10000000000ull; /* AVG, LOW */

/**
 * Exact frame rate between successive reads of the daemon's running frame
 * count (OnionFpsSample frames_milli / frames_mono_ns / ext_epoch), used for
 * the 250 ms FPS chart buckets.
 */
class FrameCountWindow {
public:
  /** Shorter windows are too coarse at a 30 Hz daemon tick. */
  static constexpr uint64_t kMinWindowNs = 100000000ull;

  /**
   * Frames per second since the previous successful take() (0 = a real
   * freeze), or a negative value when there is no usable window yet: first
   * read, new epoch, count went backwards, or less than kMinWindowNs elapsed
   * (the base is kept for the next call).
   */
  float take(uint32_t epoch, uint64_t frames_milli, uint64_t mono_ns) {
    if (!have_ || epoch != epoch_ || mono_ns < ns_ || frames_milli < frames_) {
      have_ = true;
      epoch_ = epoch;
      frames_ = frames_milli;
      ns_ = mono_ns;
      return -1.0f;
    }
    const uint64_t dns = mono_ns - ns_;
    if (dns < kMinWindowNs)
      return -1.0f;
    const double fps = (static_cast<double>(frames_milli - frames_) / 1000.0) /
                       (static_cast<double>(dns) / 1e9);
    frames_ = frames_milli;
    ns_ = mono_ns;
    return static_cast<float>(fps);
  }
  void reset() { have_ = false; }

private:
  bool have_ = false;
  uint32_t epoch_ = 0;
  uint64_t frames_ = 0;
  uint64_t ns_ = 0;
};

/**
 * The daemon's running frame count turned into a timeline of spans, one per
 * publish (~33 ms), each with its frame count and the mean frame time inside
 * it. FPS, AVG, LOW and the frame-time bars all come from here.
 *
 * Not per-frame: a span averages the frames inside it. A stall (spans with no
 * frames) is carried forward and charged to the first frame after it, so a
 * 100 ms hitch shows as one ~100 ms frame instead of vanishing.
 */
class FrameTimeline {
public:
  static constexpr int kCap = 720; /* > 20 s of 33 ms publishes */

  struct Span {
    uint64_t end_ns = 0;
    uint64_t dur_ns = 0;
    float frames = 0.0f;   /* frames that finished inside the span */
    float frame_ms = 0.0f; /* their mean frame time; 0 for a stall */
  };

  /**
   * Feed one read of the running count. Returns how many spans it appended
   * (0 for the first read, a repeated read, or a re-base on a new epoch or a
   * count that went backwards). Newest spans are at(size() - 1) and down.
   */
  int push(uint32_t epoch, uint64_t frames_milli, uint64_t mono_ns) {
    if (!have_ || epoch != epoch_ || mono_ns < ns_ || frames_milli < frames_) {
      have_ = true;
      epoch_ = epoch;
      frames_ = frames_milli;
      ns_ = mono_ns;
      stall_ns_ = 0;
      return 0;
    }
    if (mono_ns == ns_)
      return 0;
    const uint64_t dns = mono_ns - ns_;
    const float frames =
        static_cast<float>(static_cast<double>(frames_milli - frames_) / 1000.0);
    frames_ = frames_milli;
    ns_ = mono_ns;

    if (!(frames >= kMinFrames)) {
      /* No frame finished: time passes, nothing to draw yet. */
      stall_ns_ += dns;
      append({mono_ns, dns, 0.0f, 0.0f});
      return 1;
    }
    const double per_ns = static_cast<double>(dns) / frames;
    if (stall_ns_ == 0) {
      append({mono_ns, dns, frames, static_cast<float>(per_ns / 1e6)});
      return 1;
    }
    /* The first frame after a stall took the stall plus its share. */
    const double first_ns = static_cast<double>(stall_ns_) + per_ns;
    stall_ns_ = 0;
    if (frames <= 1.0f) {
      append({mono_ns, dns, frames, static_cast<float>(first_ns / 1e6)});
      return 1;
    }
    const uint64_t first_dur = static_cast<uint64_t>(per_ns);
    append({mono_ns - (dns - first_dur), first_dur, 1.0f,
            static_cast<float>(first_ns / 1e6)});
    append({mono_ns, dns - first_dur, frames - 1.0f,
            static_cast<float>(per_ns / 1e6)});
    return 2;
  }

  void reset() {
    have_ = false;
    head_ = 0;
    count_ = 0;
    stall_ns_ = 0;
  }
  int size() const { return count_; }
  /** i = 0 is the oldest retained span. */
  const Span &at(int i) const {
    const int start = (head_ - count_ + kCap) % kCap;
    return buf_[(start + i) % kCap];
  }

  /** Frames ÷ time over the spans ending in the last `window_ns`; < 0 when
   *  there is no data. Stalls count as time with no frames. */
  float rate(uint64_t window_ns) const {
    double frames = 0.0, ns = 0.0;
    for_window(window_ns, [&](const Span &s) {
      frames += s.frames;
      ns += static_cast<double>(s.dur_ns);
    });
    if (!(ns > 0.0))
      return -1.0f;
    return static_cast<float>(frames / (ns / 1e9));
  }

  /** Longest frame time in the window, including a stall still in
   *  progress; < 0 when there is no data. */
  float max_frame_ms(uint64_t window_ns) const {
    float worst = -1.0f;
    for_window(window_ns, [&](const Span &s) {
      if (s.frames > 0.0f)
        worst = std::max(worst, s.frame_ms);
    });
    if (stall_ns_ > 0)
      worst = std::max(worst, static_cast<float>(stall_ns_ / 1e6));
    return worst;
  }

  /**
   * The frame rate the slowest 1% of frames run at: walk spans from the
   * longest frame time down, weighted by their frame counts, until 1% of the
   * window's frames are covered. < 0 when there is no data.
   */
  float low_fps(uint64_t window_ns) const {
    std::vector<std::pair<float, float>> spans; /* frame_ms, frames */
    double total = 0.0;
    for_window(window_ns, [&](const Span &s) {
      if (s.frames > 0.0f && s.frame_ms > 0.0f) {
        spans.emplace_back(s.frame_ms, s.frames);
        total += s.frames;
      }
    });
    if (spans.empty() || !(total > 0.0))
      return -1.0f;
    std::sort(spans.begin(), spans.end(),
              [](const auto &a, const auto &b) { return a.first > b.first; });
    const double need = total * 0.01;
    double seen = 0.0;
    for (const auto &s : spans) {
      seen += s.second;
      if (seen >= need)
        return 1000.0f / s.first;
    }
    return 1000.0f / spans.back().first;
  }

private:
  static constexpr float kMinFrames = 0.001f;

  void append(const Span &s) {
    buf_[head_] = s;
    head_ = (head_ + 1) % kCap;
    if (count_ < kCap)
      ++count_;
  }

  template <typename F> void for_window(uint64_t window_ns, F &&f) const {
    if (count_ == 0)
      return;
    const uint64_t newest = at(count_ - 1).end_ns;
    const uint64_t from = newest > window_ns ? newest - window_ns : 0;
    for (int i = count_ - 1; i >= 0; --i) {
      const Span &s = at(i);
      if (s.end_ns <= from)
        break;
      f(s);
    }
  }

  std::array<Span, kCap> buf_{};
  int head_ = 0;
  int count_ = 0;
  bool have_ = false;
  uint32_t epoch_ = 0;
  uint64_t frames_ = 0;
  uint64_t ns_ = 0;
  uint64_t stall_ns_ = 0;
};

/** System metrics sampled by the shell; every *_valid = false renders as a
 *  dash and an empty bar. */
struct SystemReadings {
  bool cpu_valid = false;
  /** Busy percentage per core, 0..100. */
  float cpu_core[8] = {};
  /** Mean of cpu_core. */
  float cpu_pct = 0.0f;
  bool cpu_temp_valid = false;
  int cpu_temp = 0; /* °C */
  bool soc_temp_valid = false;
  int soc_temp = 0; /* °C, the APU sensor: shown as GPU */
  bool power_valid = false;
  /** GPU core rail, watts. The console has no GPU busy counter, so this is
   *  the load proxy. */
  float gpu_power_w = 0.0f;
  bool ram_valid = false;
  int ram_used_mb = 0;
  int ram_total_mb = 0;
};

/** Latest numbers; fps_valid = false shows "no game". */
struct Readings {
  bool fps_valid = false;
  /** Over the last 0.5 s. */
  float fps = 0.0f;
  /** Mean frame time over the last 0.5 s, ms. */
  float frame_ms = 0.0f;
  /** Longest frame in the last 1 s, ms; < 0 = unknown. */
  float frame_ms_max = -1.0f;
  /** Frames ÷ time over the last 10 s. */
  float fps_avg = 0.0f;
  /** Slowest-1% frame rate over the last 10 s. */
  float fps_low = 0.0f;
  /** true only when LOW comes from per-frame times; otherwise the label
   *  says "LOW", never "1% LOW". */
  bool low_per_frame = false;
  /** Frame cap the colours and the target lines compare against. */
  int fps_target = 60;
  std::string title_id;
  /** CPU / GPU / MEM row; independent of fps_valid. */
  SystemReadings sys;
};

struct Histories {
  /** FPS per kBucketMs bucket, oldest → newest. */
  Ring<kFpsBars> fps;
  /** Frame time in ms per frame sample, oldest → newest. */
  Ring<kFrameBars> frame_ms;
  /** The same samples, kept long enough for the frame-time graph. */
  Ring<kLongBars> long_ms;
};

enum class Detail : uint8_t { Minimal, Compact, Panel, Full };

struct Options {
  float screen_w = 0.0f;
  float screen_h = 0.0f;
  BarEdge edge = BarEdge::Top;
  BarAlign align = BarAlign::Center;
  Detail detail = Detail::Panel;
  int font_pt = static_cast<int>(kFontH);
  /** Background opacity in percent; 0 = no background or outline. */
  int opacity = 85;
  /** Gap from the screen edges the overlay is anchored to (overlay.margin). */
  float margin = 10.0f;
  bool brand = false;
  /** Also draw the FPS pill in the opposite corner (compact / panel). */
  bool fps_pill = false;
  /** CPU / GPU / MEM row under the panel charts and after the compact
   *  strip (overlay.system). Minimal never shows it. */
  bool show_system = true;
  /** Frame-time graph box along the bottom edge (overlay.frame_graph). */
  bool frame_graph = false;
};

enum class TextAlign : uint8_t { Left, Right };

struct Rect {
  std::string id;
  float x, y, w, h;
  Rgba color;
};

/** A text run: the renderer places a transparent cell at x/y/w/h and the
 *  label inside it at margin_top, in a font of `pt` / `weight`. */
struct Text {
  std::string id;
  float x, y, w, h;
  float margin_top;
  std::string text;
  Rgba color;
  int pt;
  int weight;
  TextAlign align;
};

struct Scene {
  float screen_w = 0.0f, screen_h = 0.0f;
  float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
  std::vector<Rect> rects; /* back to front */
  std::vector<Text> texts;
};

/**
 * true: values use the label's own right alignment over the slot width.
 * false: values are left-aligned at an estimated x (fallback if
 * HorizontalAlignment=Right ever misbehaves in ShellUI).
 */
inline constexpr bool kNativeRightAlign = true;
inline constexpr int kHAlignLeft = 0;
inline constexpr int kHAlignRight = 2;

/*
 * Font weights handed to UI2.UIFont (style 1). On hardware (2026-10-11)
 * 600/700/800 all rendered in the same light face; 900 is what the original
 * edge bar used, so values and the big FPS number use it. Labels stay light
 * on purpose: they are the dim, secondary text.
 */
inline constexpr int kWeightLabel = 600;
inline constexpr int kWeightValue = 900;
inline constexpr int kWeightHero = 900;

/** Every length is designed at 18 pt and scales with the font setting. */
struct Metrics {
  float k;
  int value_pt;   /* compact values */
  int head_pt;    /* header: title id, target pill */
  int label_pt;   /* sub-headings */
  int stat_pt;    /* AVG / LOW values */
  int ft_pt;      /* frame-time value */
  int hero_pt;    /* panel FPS */
  int strip_pt;   /* compact FPS */
  int axis_pt;    /* chart guide labels */
  int pill_pt;    /* minimal FPS */
  float pad_x, pad_y, panel_w, head_h, hero_h, caption_h, chart_h, chart_gap;
  float section_gap, col_gap, fps_bar_gap, ft_bar_gap;
  float line_h, gap, sep_gap, strip_chart_w, strip_chart_h, dot;
  /* System row: value row height, bar height / gap, column gap, core
   * segment gap. */
  float sys_val_h, sys_bar_h, sys_bar_gap, sys_col_gap, sys_seg_gap;

  explicit Metrics(int font_pt) {
    value_pt = font_pt > 0 ? font_pt : static_cast<int>(kFontH);
    k = static_cast<float>(value_pt) / 18.0f;
    auto px = [this](float v) { return std::round(v * k); };
    auto pt = [this](float v) { return static_cast<int>(std::lround(v * k)); };
    head_pt = pt(11.0f);
    label_pt = pt(13.0f);
    stat_pt = pt(21.0f);
    ft_pt = pt(16.0f);
    hero_pt = pt(58.0f);
    strip_pt = pt(24.0f);
    axis_pt = std::max(8, pt(9.0f));
    pill_pt = pt(22.0f);
    pad_x = px(13.0f);
    pad_y = px(12.0f);
    panel_w = px(300.0f);
    head_h = px(20.0f);
    hero_h = px(62.0f);
    caption_h = px(22.0f);
    chart_h = px(57.0f);
    chart_gap = px(5.0f);
    section_gap = px(11.0f);
    col_gap = px(14.0f);
    fps_bar_gap = std::max(1.0f, px(2.0f));
    ft_bar_gap = 1.0f;
    line_h = px(42.0f);
    gap = px(6.0f);
    sep_gap = px(12.0f);
    strip_chart_w = px(120.0f);
    strip_chart_h = px(24.0f);
    dot = px(10.0f);
    sys_val_h = px(28.0f);
    sys_bar_h = std::max(2.0f, px(3.0f));
    sys_bar_gap = px(4.0f);
    sys_col_gap = px(12.0f);
    sys_seg_gap = std::max(1.0f, px(2.0f));
  }

  /** Top inset that puts a run of `pt` on the baseline a run of `ref_pt`
   *  would have when centred in a row of height `row_h`. */
  static float text_top(float row_h, int pt, int ref_pt) {
    const float box = static_cast<float>(ref_pt) * 1.25f;
    const float top = (row_h - box) * 0.5f;
    return std::round(top + static_cast<float>(ref_pt - pt) * 0.85f);
  }
};

namespace detail {

inline std::string fmt(const char *format, double v) {
  char buf[48];
  std::snprintf(buf, sizeof(buf), format, v);
  return buf;
}

inline constexpr const char *kDash = "\xE2\x80\x94";   /* — */
inline constexpr const char *kMidDot = "\xC2\xB7";     /* · */
inline constexpr const char *kDeg = "\xC2\xB0";        /* ° */

/** NaN/Inf readings render as missing, never "nan". */
inline Readings sanitized(const Readings &in) {
  Readings r = in;
  auto ok = [](float v) { return std::isfinite(v); };
  r.fps_valid = r.fps_valid && ok(r.fps) && ok(r.fps_avg) && ok(r.fps_low) &&
                ok(r.frame_ms) && r.fps > 0.0f;
  if (!ok(r.frame_ms_max))
    r.frame_ms_max = -1.0f;
  if (r.fps_target <= 0)
    r.fps_target = 60;
  SystemReadings &s = r.sys;
  for (float &core : s.cpu_core)
    if (!ok(core))
      core = 0.0f;
  s.cpu_valid = s.cpu_valid && ok(s.cpu_pct);
  s.power_valid = s.power_valid && ok(s.gpu_power_w) && s.gpu_power_w >= 0.0f;
  s.ram_valid = s.ram_valid && s.ram_total_mb > 0 && s.ram_used_mb >= 0;
  return r;
}

} // namespace detail

/* Value formatting shared by every level. */
inline std::string fmt_fps(float fps) {
  return detail::fmt("%.0f", std::clamp(fps, 0.0f, 999.0f));
}
/** AVG / LOW: one decimal, "59.8". */
inline std::string fmt_fps1(float fps) {
  return detail::fmt("%.1f", std::clamp(fps, 0.0f, 999.9f));
}
inline std::string fmt_ms(float ms) {
  return detail::fmt("%.1f", std::clamp(ms, 0.0f, 999.9f));
}
inline std::string fmt_pct(float pct) {
  return detail::fmt("%.0f%%", std::clamp(pct, 0.0f, 100.0f));
}
inline std::string fmt_temp(int celsius) {
  return std::to_string(celsius) + detail::kDeg;
}
/** Watts, whole numbers: "65 W". */
inline std::string fmt_watts(float w) {
  return detail::fmt("%.0f W", std::clamp(w, 0.0f, 999.0f));
}
/** MB → "9.6" (GB, one decimal). */
inline std::string fmt_gb(int mb) {
  return detail::fmt("%.1f", static_cast<double>(std::max(mb, 0)) / 1024.0);
}

namespace detail {

/** Accumulates rects/texts in overlay-local coordinates. */
class Builder {
public:
  Builder(const Metrics &m, Scene &scene) : m_(m), scene_(scene) {}

  const Metrics &m() const { return m_; }

  void rect(std::string id, float x, float y, float w, float h, Rgba c) {
    /* negated so NaN sizes are dropped too */
    if (!(w > 0.0f) || !(h > 0.0f))
      return;
    scene_.rects.push_back({std::move(id), x, y, w, h, c});
  }

  /**
   * Text in a row starting at `row_y` of height `row_h`, baseline-aligned
   * with a run of `ref_pt`. Left runs start at x; right runs end at x + w.
   */
  void text(std::string id, float x, float w, float row_y, float row_h,
            std::string s, Rgba c, int pt, int weight, TextAlign align,
            int ref_pt) {
    if (align == TextAlign::Right && !kNativeRightAlign) {
      const float est = text_advance(s.c_str(), static_cast<float>(pt));
      x = x + w - est;
      w = est;
      align = TextAlign::Left;
    }
    scene_.texts.push_back({std::move(id), x, row_y, w, row_h,
                            Metrics::text_top(row_h, pt, ref_pt), std::move(s),
                            c, pt, weight, align});
  }

  /** Width to give a left-aligned run so its real glyphs never clip. */
  static float left_w(const std::string &s, int pt) {
    return std::ceil(text_advance(s.c_str(), static_cast<float>(pt)) * 1.25f +
                     6.0f);
  }

private:
  const Metrics &m_;
  Scene &scene_;
};

inline float advance(const std::string &s, int pt) {
  return text_advance(s.c_str(), static_cast<float>(pt));
}

/** Width reserved for a value slot: the widest of the worst-case strings, so
 *  changing digits never move anything. */
inline float slot_w(std::initializer_list<const char *> worst, int pt) {
  float w = 0.0f;
  for (const char *s : worst)
    w = std::max(w, text_advance(s, static_cast<float>(pt)));
  return std::ceil(w + 2.0f);
}

inline constexpr const char *kIdPrefix = "id_osc_";
/* The companion FPS pill keeps its own ids so it can sit next to a level
 * that uses the plain ones. */
inline constexpr const char *kPillIdPrefix = "id_osc_pill_";
inline constexpr const char *kGraphIdPrefix = "id_osc_graph_";

/** Background + 1 px outline; nothing at opacity 0. */
inline void chrome(Scene &out, float w, float h, int opacity,
                   const std::string &prefix) {
  if (opacity <= 0)
    return;
  const float a = std::clamp(static_cast<float>(opacity) / 100.0f, 0.0f, 1.0f);
  out.rects.push_back({prefix + "bg", 0.0f, 0.0f, w, h, hex(kSurfaceRgb, a)});
  out.rects.push_back({prefix + "edge_t", 0.0f, 0.0f, w, 1.0f, kHairline});
  out.rects.push_back({prefix + "edge_b", 0.0f, h - 1.0f, w, 1.0f, kHairline});
  out.rects.push_back({prefix + "edge_l", 0.0f, 1.0f, 1.0f, h - 2.0f, kHairline});
  out.rects.push_back(
      {prefix + "edge_r", w - 1.0f, 1.0f, 1.0f, h - 2.0f, kHairline});
}

/** Anchor a w×h overlay on screen per edge / align / margin. */
inline void place_on_screen(Scene &s, const Options &opt, float w, float h) {
  s.w = w;
  s.h = h;
  const float margin = std::max(0.0f, opt.margin);
  switch (opt.align) {
  case BarAlign::Left:
    s.x = margin;
    break;
  case BarAlign::Right:
    s.x = std::round(opt.screen_w - margin - w);
    break;
  case BarAlign::Center:
  default:
    s.x = std::round((opt.screen_w - w) * 0.5f);
    break;
  }
  s.y = opt.edge == BarEdge::Bottom ? std::round(opt.screen_h - margin - h)
                                    : margin;
}

/** Chrome behind content, then everything shifted to screen coordinates. */
inline Scene finish(const Options &opt, Scene content, float w, float h,
                    const std::string &prefix = kIdPrefix) {
  Scene out{};
  out.screen_w = opt.screen_w;
  out.screen_h = opt.screen_h;
  w = std::round(w);
  h = std::round(h);
  place_on_screen(out, opt, w, h);
  chrome(out, w, h, opt.opacity, prefix);
  for (Rect &rc : content.rects)
    out.rects.push_back(std::move(rc));
  out.texts = std::move(content.texts);
  for (Rect &rc : out.rects) {
    rc.x += out.x;
    rc.y += out.y;
  }
  for (Text &t : out.texts) {
    t.x += out.x;
    t.y += out.y;
  }
  return out;
}

/** One chart bar: its height as a fraction of the chart, and its colour. */
struct Bar {
  float frac;
  Rgba color;
};

/**
 * Bars bottom-aligned on a chart of `slots` fixed positions, newest at the
 * right; with fewer values than slots the left stays empty. Ids follow the
 * slot, so a full chart only changes heights and colours from frame to
 * frame.
 */
inline void bars(Builder &b, const std::string &prefix, int slots,
                 const std::vector<Bar> &vals, float x, float bottom, float w,
                 float h, float gap) {
  const int n = std::min(slots, static_cast<int>(vals.size()));
  const int first = static_cast<int>(vals.size()) - n;
  const int offset = slots - n;
  const float pitch = w / static_cast<float>(slots);
  for (int i = 0; i < n; ++i) {
    const int s = offset + i;
    const float x0 = x + std::round(s * pitch);
    const float x1 = x + std::round((s + 1) * pitch) - gap;
    const float bw = std::max(1.0f, x1 - x0);
    const float frac = std::clamp(vals[first + i].frac, 0.0f, 1.0f);
    const float bh = std::max(2.0f, std::round(frac * h));
    b.rect(prefix + std::to_string(s), x0, bottom - bh, bw, bh,
           vals[first + i].color);
  }
}

/** A horizontal guide across a chart with its value just above it at the
 *  left edge (the oldest bars; the newest, on the right, stay visible), on a
 *  patch of panel colour so bars never run through it. */
inline void guide(Builder &b, const Metrics &m, const std::string &id, float x,
                  float w, float y, Rgba line, const std::string &label,
                  int opacity) {
  b.rect(id, x, y, w, 1.0f, line);
  const float lw = std::ceil(advance(label, m.axis_pt)) + std::round(6.0f * m.k);
  const float lh = std::round(static_cast<float>(m.axis_pt) * 1.3f);
  const float ly = y - lh;
  const float a =
      std::clamp(static_cast<float>(std::max(opacity, 60)) / 100.0f, 0.0f, 1.0f);
  b.rect(id + "_bg", x, ly, lw, lh, hex(kSurfaceRgb, a));
  b.text(id + "_t", x + std::round(3.0f * m.k), lw, ly, lh, label, kHeading,
         m.axis_pt, kWeightLabel, TextAlign::Left, m.axis_pt);
}

/* ------------------------------------------------------------------ */
/* Minimal: [dot] 60 FPS                                               */
/* ------------------------------------------------------------------ */

inline Scene build_minimal(const Options &opt, const Readings &r,
                           const std::string &prefix = kIdPrefix) {
  const Metrics m(opt.font_pt);
  Scene content{};
  Builder b(m, content);
  const float h = std::round(36.0f * m.k);
  float x = std::round(14.0f * m.k);

  const Rgba dot = r.fps_valid
                       ? fps_state_color(fps_state(r.fps, r.fps_target))
                       : kLabel;
  b.rect(prefix + "dot", x, std::round((h - m.dot) * 0.5f), m.dot, m.dot, dot);
  x += m.dot + std::round(8.0f * m.k);

  const float vw = slot_w({"888"}, m.pill_pt);
  b.text(prefix + "fps_v", x, vw, 0.0f, h,
         r.fps_valid ? fmt_fps(r.fps) : kDash, kText, m.pill_pt,
         kWeightHero, TextAlign::Right, m.pill_pt);
  x += vw + std::round(5.0f * m.k);
  const std::string unit = "FPS";
  b.text(prefix + "fps_l", x, Builder::left_w(unit, m.label_pt), 0.0f, h, unit,
         kHeading, m.label_pt, kWeightLabel, TextAlign::Left, m.pill_pt);
  x += advance(unit, m.label_pt) + std::round(14.0f * m.k);
  return finish(opt, std::move(content), x, h, prefix);
}

/** Opposite horizontal side on the same edge; a centred overlay pairs with
 *  a pill on the right. */
inline BarAlign opposite_align(BarAlign align) {
  return align == BarAlign::Right ? BarAlign::Left : BarAlign::Right;
}

/** Adds the companion FPS pill to an already placed scene. */
inline void add_fps_pill(Scene &main, const Options &opt, const Readings &r) {
  Options pill = opt;
  pill.align = opposite_align(opt.align);
  Scene p = build_minimal(pill, r, kPillIdPrefix);
  for (Rect &rc : p.rects)
    main.rects.push_back(std::move(rc));
  for (Text &t : p.texts)
    main.texts.push_back(std::move(t));
}

inline const char *low_label(const Readings &r) {
  return r.low_per_frame ? "1% LOW" : "LOW";
}

/** The newest `n` frame samples as bars on a 0 .. 3× target scale. */
template <int N>
inline std::vector<Bar> frame_bars(const Ring<N> &ring, int target, int n) {
  const float top = 3.0f * 1000.0f / static_cast<float>(target);
  const int count = std::min(n, ring.size());
  std::vector<Bar> out;
  out.reserve(static_cast<size_t>(count));
  for (int i = ring.size() - count; i < ring.size(); ++i) {
    const float ms = ring.at(i);
    out.push_back({ms / top, frame_ms_color(ms, target)});
  }
  return out;
}

/* ------------------------------------------------------------------ */
/* Frame-time graph: FRAME TIME · LAST 7.5 S ......... MAX 33.4 ms     */
/* ------------------------------------------------------------------ */

/**
 * The reference design's box: 70 % of the screen wide, centred, 24 px off
 * the bottom edge (the top edge when the overlay itself sits at the
 * bottom), a header with the window's MAX, and one bar per sample on a
 * 0 .. 3× target scale with guides at 1× and 2×. Appended to an already
 * placed scene like the FPS pill; the box uses the overlay opacity.
 */
inline void add_frame_graph(Scene &main, const Options &opt, const Readings &r,
                            const Histories &h) {
  const Metrics m(opt.font_pt);
  auto px = [&m](float v) { return std::round(v * m.k); };
  const float w = std::round(0.7f * opt.screen_w);
  const float bh = px(116.0f);
  const float x0 = px(16.0f), iw = w - 2.0f * x0;
  const int target = r.fps_target;
  const float target_ms = 1000.0f / static_cast<float>(target);

  float max_ms = -1.0f;
  for (int i = 0; i < h.long_ms.size(); ++i)
    max_ms = std::max(max_ms, h.long_ms.at(i));

  Scene content{};
  Builder b(m, content);
  const float y = px(10.0f), ch = m.caption_h;
  b.text("id_osc_graph_l", x0, iw * 0.5f, y, ch,
         std::string("FRAME TIME ") + kMidDot + " LAST 7.5 S", kHeading,
         m.label_pt, kWeightLabel, TextAlign::Left, m.ft_pt);
  float rx = x0 + iw;
  const float uw = std::ceil(advance("ms", m.label_pt));
  b.text("id_osc_graph_u", rx - uw, uw + 2.0f, y, ch, "ms", kLabel, m.label_pt,
         kWeightLabel, TextAlign::Right, m.ft_pt);
  rx -= uw + std::round(4.0f * m.k);
  const float vw = slot_w({"888.8"}, m.ft_pt);
  b.text("id_osc_graph_max_v", rx - vw, vw, y, ch,
         max_ms >= 0.0f ? fmt_ms(max_ms) : kDash,
         max_ms >= 0.0f ? frame_ms_color(max_ms, target) : kText, m.ft_pt,
         kWeightValue, TextAlign::Right, m.ft_pt);
  rx -= vw + std::round(4.0f * m.k);
  const float mw = std::ceil(advance("MAX", m.label_pt));
  b.text("id_osc_graph_max_l", rx - mw, mw + 2.0f, y, ch, "MAX", kHeading,
         m.label_pt, kWeightLabel, TextAlign::Right, m.ft_pt);

  const float top = px(32.0f), gh = px(72.0f), bottom = top + gh;
  if (r.fps_valid)
    bars(b, "id_osc_graph_bar", kLongBars,
         frame_bars(h.long_ms, target, kLongBars), x0, bottom, iw, gh,
         m.ft_bar_gap);
  b.rect("id_osc_graph_base", x0, bottom, iw, 1.0f, kBaseline);
  guide(b, m, "id_osc_graph_g1", x0, iw, bottom - std::round(gh / 3.0f),
        kTargetLine, fmt_ms(target_ms), opt.opacity);
  guide(b, m, "id_osc_graph_g2", x0, iw, bottom - std::round(gh * 2.0f / 3.0f),
        kGuideLine, fmt_ms(2.0f * target_ms), opt.opacity);

  Options box = opt;
  box.align = BarAlign::Center;
  box.margin = px(24.0f);
  box.edge = opt.edge == BarEdge::Bottom ? BarEdge::Top : BarEdge::Bottom;
  Scene g = finish(box, std::move(content), w, bh, kGraphIdPrefix);
  for (Rect &rc : g.rects)
    main.rects.push_back(std::move(rc));
  for (Text &t : g.texts)
    main.texts.push_back(std::move(t));
}

/* ------------------------------------------------------------------ */
/* System row: CPU 42% 58°  |  GPU 61° 74 W  |  MEMORY 9.6 GB            */
/* ------------------------------------------------------------------ */

/**
 * Three equal columns under the panel charts, each a heading row, a value
 * row (big value + small value on one baseline) and a 3 px bar. Starts
 * with a hairline rule. Returns the y just under the bars.
 */
inline float system_block(Builder &b, const Metrics &m, float x0, float iw,
                          float y, const SystemReadings &s) {
  y += m.section_gap;
  b.rect("id_osc_sys_rule", x0, y, iw, 1.0f, kHairline);
  y += 1.0f + m.section_gap;

  const float cw = std::floor((iw - 2.0f * m.sys_col_gap) / 3.0f);
  const float cx[3] = {x0, x0 + cw + m.sys_col_gap,
                       x0 + 2.0f * (cw + m.sys_col_gap)};
  const float cwl = x0 + iw - cx[2]; /* last column takes the remainder */
  const float hy = y;
  const float vy = y + m.caption_h;
  const float by = vy + m.sys_val_h + m.sys_bar_gap;
  const float vg = std::round(5.0f * m.k);

  auto heading = [&](const char *id, float x, const char *text) {
    b.text(id, x, Builder::left_w(text, m.label_pt), hy, m.caption_h, text,
           kHeading, m.label_pt, kWeightLabel, TextAlign::Left, m.label_pt);
  };
  /* Big value at stat_pt, the small one at label_pt just after its glyphs;
   * both sit on the stat baseline. */
  auto values = [&](const char *big_id, const char *small_id, float x,
                    const std::string &big, Rgba big_c,
                    const std::string &small, Rgba small_c) {
    b.text(big_id, x, Builder::left_w(big, m.stat_pt), vy, m.sys_val_h, big,
           big_c, m.stat_pt, kWeightValue, TextAlign::Left, m.stat_pt);
    const float sx = x + advance(big, m.stat_pt) + vg;
    b.text(small_id, sx, Builder::left_w(small, m.label_pt), vy, m.sys_val_h,
           small, small_c, m.label_pt, kWeightLabel, TextAlign::Left,
           m.stat_pt);
  };

  /* CPU: load, temperature, one segment per core. */
  heading("id_osc_cpu_l", cx[0], "CPU");
  values("id_osc_cpu_v", "id_osc_cpu_t", cx[0],
         s.cpu_valid ? fmt_pct(s.cpu_pct) : kDash,
         s.cpu_valid && s.cpu_pct > kBarWarnPct ? kWarn : kText,
         s.cpu_temp_valid ? fmt_temp(s.cpu_temp) : kDash,
         s.cpu_temp_valid ? temp_color(s.cpu_temp, kCpuTempScale) : kLabel);
  b.rect("id_osc_cpu_track", cx[0], by, cw, m.sys_bar_h, kTrack);
  if (s.cpu_valid) {
    const int n = static_cast<int>(sizeof(s.cpu_core) / sizeof(s.cpu_core[0]));
    const float pitch = (cw + m.sys_seg_gap) / static_cast<float>(n);
    for (int i = 0; i < n; ++i) {
      const float sx0 = cx[0] + std::round(i * pitch);
      const float sx1 = cx[0] + std::round((i + 1) * pitch) - m.sys_seg_gap;
      const float sw = std::max(1.0f, sx1 - sx0);
      const float load = std::clamp(s.cpu_core[i], 0.0f, 100.0f);
      const float fw = load > 0.0f ? std::max(1.0f, std::round(sw * load / 100.0f))
                                   : 0.0f;
      /* Builder drops a zero-width rect, so an idle core leaves its slot
       * showing the track. */
      b.rect("id_osc_cpu_seg" + std::to_string(i), sx0, by, fw, m.sys_bar_h,
             load > kBarWarnPct ? kWarn : kFill);
    }
  }

  /* GPU: SoC temperature, core power; the bar is power on a fixed scale. */
  heading("id_osc_gpu_l", cx[1], "GPU");
  values("id_osc_gpu_v", "id_osc_gpu_t", cx[1],
         s.soc_temp_valid ? fmt_temp(s.soc_temp) : kDash,
         s.soc_temp_valid ? temp_color(s.soc_temp, kGpuTempScale) : kText,
         s.power_valid ? fmt_watts(s.gpu_power_w) : kDash, kLabel);
  b.rect("id_osc_gpu_track", cx[1], by, cw, m.sys_bar_h, kTrack);
  if (s.power_valid)
    b.rect("id_osc_gpu_fill", cx[1], by,
           std::round(cw * std::clamp(s.gpu_power_w / kGpuPowerScaleW, 0.0f,
                                      1.0f)),
           m.sys_bar_h, kFill);

  /* MEM: used GB; the bar shows it against the total. */
  heading("id_osc_mem_l", cx[2], "MEMORY");
  const float mem_pct =
      s.ram_valid ? 100.0f * static_cast<float>(s.ram_used_mb) /
                        static_cast<float>(s.ram_total_mb)
                  : 0.0f;
  values("id_osc_mem_v", "id_osc_mem_u", cx[2],
         s.ram_valid ? fmt_gb(s.ram_used_mb) : kDash,
         s.ram_valid && mem_pct > kBarWarnPct ? kWarn : kText, "GB", kLabel);
  b.rect("id_osc_mem_track", cx[2], by, cwl, m.sys_bar_h, kTrack);
  if (s.ram_valid)
    b.rect("id_osc_mem_fill", cx[2], by,
           std::round(cwl * std::clamp(mem_pct, 0.0f, 100.0f) / 100.0f),
           m.sys_bar_h, mem_pct > kBarWarnPct ? kWarn : kFill);

  return by + m.sys_bar_h;
}

/* ------------------------------------------------------------------ */
/* Compact: 60 FPS | 16.7 ms  AVG 59.9  LOW 58.6  ▁▁▁▇▁▁              */
/* ------------------------------------------------------------------ */

inline Scene build_compact(const Options &opt, const Readings &r,
                           const Histories &h) {
  const Metrics m(opt.font_pt);
  Scene content{};
  Builder b(m, content);
  const float lh = m.line_h;
  const float ug = std::round(4.0f * m.k);  /* value → its unit */
  const float vg = std::round(12.0f * m.k); /* group → group */
  float x = std::round(14.0f * m.k);

  const FpsState st = fps_state(r.fps, r.fps_target);
  const float fw = slot_w({"888"}, m.strip_pt);
  b.text("id_osc_fps_v", x, fw, 0.0f, lh, r.fps_valid ? fmt_fps(r.fps) : kDash,
         r.fps_valid ? fps_value_color(st) : kText, m.strip_pt, kWeightHero,
         TextAlign::Right, m.strip_pt);
  x += fw + ug;
  b.text("id_osc_fps_l", x, Builder::left_w("FPS", m.label_pt), 0.0f, lh, "FPS",
         kHeading, m.label_pt, kWeightLabel, TextAlign::Left, m.strip_pt);
  x += advance("FPS", m.label_pt) + m.sep_gap;
  const float sh = std::round(24.0f * m.k);
  b.rect("id_osc_sep0", x, std::round((lh - sh) * 0.5f), 1.0f, sh, kSeparator);
  x += 1.0f + m.sep_gap;

  const float vw = slot_w({"888.8"}, m.value_pt);
  b.text("id_osc_ft_v", x, vw, 0.0f, lh,
         r.fps_valid ? fmt_ms(r.frame_ms) : kDash, kText, m.value_pt,
         kWeightValue, TextAlign::Right, m.strip_pt);
  x += vw + ug;
  b.text("id_osc_ft_u", x, Builder::left_w("ms", m.label_pt), 0.0f, lh, "ms",
         kLabel, m.label_pt, kWeightLabel, TextAlign::Left, m.strip_pt);
  x += advance("ms", m.label_pt) + vg;

  auto stat = [&](const char *key, const char *label, float v, Rgba c) {
    b.text(std::string("id_osc_") + key + "_l", x,
           Builder::left_w(label, m.label_pt), 0.0f, lh, label, kHeading,
           m.label_pt, kWeightLabel, TextAlign::Left, m.strip_pt);
    x += advance(label, m.label_pt) + ug;
    b.text(std::string("id_osc_") + key + "_v", x, vw, 0.0f, lh,
           r.fps_valid ? fmt_fps1(v) : kDash, r.fps_valid ? c : kText,
           m.value_pt, kWeightValue, TextAlign::Right, m.strip_pt);
    x += vw + vg;
  };
  stat("avg", "AVG", r.fps_avg, fps_value_color(fps_state(r.fps_avg, r.fps_target)));
  stat("low", low_label(r), r.fps_low,
       fps_value_color(fps_state(r.fps_low, r.fps_target)));

  /* Mini frame-time chart. */
  const float cw = m.strip_chart_w, ch = m.strip_chart_h;
  const float bottom = std::round((lh + ch) * 0.5f);
  if (r.fps_valid)
    bars(b, "id_osc_sbar", kStripBars,
         frame_bars(h.frame_ms, r.fps_target, kStripBars), x, bottom, cw, ch,
         m.ft_bar_gap);
  b.rect("id_osc_sbase", x, bottom, cw, 1.0f, kBaseline);
  b.rect("id_osc_starget", x, bottom - std::round(ch / 3.0f), cw, 1.0f,
         kTargetLine);
  x += cw;

  /* System groups: | CPU 42% 58° | GPU 61° 74 W | MEM 9.6 GB. Fixed slots so
   * nothing shifts as the numbers change. */
  if (opt.show_system) {
    const SystemReadings &s = r.sys;
    const float tail_w = slot_w({"100\xC2\xB0", "128 W", "GB"}, m.label_pt);
    auto sep = [&](const char *id) {
      x += m.sep_gap;
      b.rect(id, x, std::round((lh - sh) * 0.5f), 1.0f, sh, kSeparator);
      x += 1.0f + m.sep_gap;
    };
    auto group = [&](const char *key, const char *label, const std::string &v,
                     Rgba vc, std::initializer_list<const char *> worst,
                     const std::string &tail, Rgba tc, const char *tail_suffix) {
      const std::string p = std::string("id_osc_s") + key;
      b.text(p + "_l", x, Builder::left_w(label, m.label_pt), 0.0f, lh, label,
             kHeading, m.label_pt, kWeightLabel, TextAlign::Left, m.strip_pt);
      x += advance(label, m.label_pt) + ug;
      const float w = slot_w(worst, m.value_pt);
      b.text(p + "_v", x, w, 0.0f, lh, v, vc, m.value_pt, kWeightValue,
             TextAlign::Right, m.strip_pt);
      x += w + ug;
      b.text(p + tail_suffix, x, tail_w, 0.0f, lh, tail, tc, m.label_pt,
             kWeightLabel, TextAlign::Left, m.strip_pt);
      x += tail_w;
    };
    sep("id_osc_sep1");
    group("cpu", "CPU", s.cpu_valid ? fmt_pct(s.cpu_pct) : kDash,
          s.cpu_valid && s.cpu_pct > kBarWarnPct ? kWarn : kText, {"100%"},
          s.cpu_temp_valid ? fmt_temp(s.cpu_temp) : kDash,
          s.cpu_temp_valid ? temp_color(s.cpu_temp, kCpuTempScale) : kLabel,
          "_t");
    sep("id_osc_sep2");
    group("gpu", "GPU", s.soc_temp_valid ? fmt_temp(s.soc_temp) : kDash,
          s.soc_temp_valid ? temp_color(s.soc_temp, kGpuTempScale) : kText,
          {"100\xC2\xB0"}, s.power_valid ? fmt_watts(s.gpu_power_w) : kDash,
          kLabel, "_t");
    sep("id_osc_sep3");
    group("mem", "MEMORY", s.ram_valid ? fmt_gb(s.ram_used_mb) : kDash, kText,
          {"88.8"}, "GB", kLabel, "_u");
  }
  x += std::round(14.0f * m.k);
  return finish(opt, std::move(content), x, lh);
}

/* ------------------------------------------------------------------ */
/* Panel                                                               */
/* ------------------------------------------------------------------ */

inline Scene build_panel(const Options &opt, const Readings &r,
                         const Histories &h) {
  const Metrics m(opt.font_pt);
  Scene content{};
  Builder b(m, content);
  const float x0 = m.pad_x;
  const float iw = m.panel_w - 2.0f * m.pad_x;
  float y = m.pad_y;
  const int target = r.fps_target;

  /* Header: title id ............ [TARGET 60] */
  {
    std::string left = r.fps_valid ? r.title_id : "NO GAME";
    if (opt.brand)
      left = kBrandText;
    const float hh = m.head_h;
    if (!left.empty())
      b.text("id_osc_head_l", x0, iw * 0.6f, y, hh, left,
             opt.brand ? kText : kLabel, m.head_pt,
             opt.brand ? kWeightValue : kWeightLabel, TextAlign::Left,
             m.head_pt);
    if (r.fps_valid) {
      const std::string pill = "TARGET " + std::to_string(target);
      const float pw = std::ceil(advance(pill, m.head_pt)) + std::round(12.0f * m.k);
      b.rect("id_osc_head_pill", x0 + iw - pw, y, pw, hh, kPillFill);
      /* Right-aligned to the pill's inner edge. */
      b.text("id_osc_head_r", x0 + iw - pw, pw - std::round(6.0f * m.k), y, hh,
             pill, kLabel, m.head_pt, kWeightLabel, TextAlign::Right,
             m.head_pt);
    }
    y += hh + m.section_gap;
  }

  if (!r.fps_valid) {
    const float lh = m.caption_h + std::round(6.0f * m.k);
    b.text("id_osc_fps_v", x0, iw, y, lh, "NO GAME DETECTED", kLabel,
           m.label_pt, kWeightLabel, TextAlign::Left, m.label_pt);
    y += lh;
    /* The system row does not need a game. */
    if (opt.show_system)
      y = system_block(b, m, x0, iw, y, r.sys);
    return finish(opt, std::move(content), m.panel_w, y + m.pad_y);
  }

  /* Hero: 60 FPS ............ AVG  LOW / 59.8  52.3 */
  {
    const float hh = m.hero_h;
    const FpsState st = fps_state(r.fps, target);
    const std::string fps = fmt_fps(r.fps);
    b.text("id_osc_fps_v", x0, Builder::left_w(fps, m.hero_pt), y, hh, fps,
           fps_value_color(st), m.hero_pt, kWeightHero, TextAlign::Left,
           m.hero_pt);
    const float ux = x0 + advance(fps, m.hero_pt) + std::round(6.0f * m.k);
    b.text("id_osc_fps_l", ux, Builder::left_w("FPS", m.label_pt), y, hh, "FPS",
           kHeading, m.label_pt, kWeightLabel, TextAlign::Left, m.hero_pt);

    const float cw =
        std::max(slot_w({"888.8"}, m.stat_pt), slot_w({"1% LOW"}, m.label_pt));
    const float lx = x0 + iw - cw;               /* LOW column */
    const float ax = lx - m.col_gap - cw;        /* AVG column */
    const float top_h = std::round(hh * 0.42f);  /* labels */
    const float bot_h = hh - top_h;              /* values */
    b.text("id_osc_avg_l", ax, cw, y, top_h, "AVG", kHeading, m.label_pt,
           kWeightLabel, TextAlign::Right, m.label_pt);
    b.text("id_osc_low_l", lx, cw, y, top_h, low_label(r), kHeading,
           m.label_pt, kWeightLabel, TextAlign::Right, m.label_pt);
    b.text("id_osc_avg_v", ax, cw, y + top_h, bot_h, fmt_fps1(r.fps_avg),
           fps_value_color(fps_state(r.fps_avg, target)), m.stat_pt,
           kWeightValue, TextAlign::Right, m.stat_pt);
    b.text("id_osc_low_v", lx, cw, y + top_h, bot_h, fmt_fps1(r.fps_low),
           fps_value_color(fps_state(r.fps_low, target)), m.stat_pt,
           kWeightValue, TextAlign::Right, m.stat_pt);
    y += hh + m.section_gap;
  }

  /* FPS · LAST 10 S ............ MIN 44 · MAX 60, then the chart. */
  {
    const float ch = m.caption_h;
    const std::string cap = std::string("FPS ") + kMidDot + " LAST 10 S";
    b.text("id_osc_fc_l", x0, iw * 0.55f, y, ch, cap, kHeading, m.label_pt,
           kWeightLabel, TextAlign::Left, m.label_pt);
    if (h.fps.size() > 0) {
      float lo = 1e9f, hi = 0.0f;
      for (int i = 0; i < h.fps.size(); ++i) {
        lo = std::min(lo, h.fps.at(i));
        hi = std::max(hi, h.fps.at(i));
      }
      const float sw = slot_w({"888"}, m.label_pt);
      const float lg = std::round(4.0f * m.k);
      float rx = x0 + iw; /* right edge, walking left */
      b.text("id_osc_fc_max_v", rx - sw, sw, y, ch, fmt_fps(hi), kText,
             m.label_pt, kWeightValue, TextAlign::Right, m.label_pt);
      rx -= sw + lg;
      const float mw = std::ceil(advance("MAX", m.label_pt));
      b.text("id_osc_fc_max_l", rx - mw, mw + 2.0f, y, ch, "MAX", kHeading,
             m.label_pt, kWeightLabel, TextAlign::Right, m.label_pt);
      rx -= mw + std::round(10.0f * m.k);
      b.text("id_osc_fc_min_v", rx - sw, sw, y, ch, fmt_fps(lo), kText,
             m.label_pt, kWeightValue, TextAlign::Right, m.label_pt);
      rx -= sw + lg;
      const float nw = std::ceil(advance("MIN", m.label_pt));
      b.text("id_osc_fc_min_l", rx - nw, nw + 2.0f, y, ch, "MIN", kHeading,
             m.label_pt, kWeightLabel, TextAlign::Right, m.label_pt);
    }
    y += ch + m.chart_gap;

    const float gh = m.chart_h;
    const float bottom = y + gh;
    const float top_fps = 1.25f * static_cast<float>(target);
    std::vector<Bar> vals;
    vals.reserve(static_cast<size_t>(h.fps.size()));
    for (int i = 0; i < h.fps.size(); ++i) {
      const float f = h.fps.at(i);
      vals.push_back({f / top_fps, fps_state_color(fps_state(f, target))});
    }
    bars(b, "id_osc_fbar", kFpsBars, vals, x0, bottom, iw, gh, m.fps_bar_gap);
    b.rect("id_osc_fbase", x0, bottom, iw, 1.0f, kBaseline);
    guide(b, m, "id_osc_ftarget", x0, iw,
          bottom - std::round(gh / 1.25f), kTargetLine, std::to_string(target),
          opt.opacity);
    y = bottom + 1.0f + m.section_gap;
  }

  /* FRAME TIME ............ MAX 33.4   16.7 ms, then the chart. */
  {
    const float ch = m.caption_h;
    b.text("id_osc_tc_l", x0, iw * 0.5f, y, ch, "FRAME TIME", kHeading,
           m.label_pt, kWeightLabel, TextAlign::Left, m.ft_pt);
    float rx = x0 + iw;
    const float uw = std::ceil(advance("ms", m.label_pt));
    b.text("id_osc_ft_u", rx - uw, uw + 2.0f, y, ch, "ms", kLabel, m.label_pt,
           kWeightLabel, TextAlign::Right, m.ft_pt);
    rx -= uw + std::round(4.0f * m.k);
    const float vw = slot_w({"888.8"}, m.ft_pt);
    b.text("id_osc_ft_v", rx - vw, vw, y, ch, fmt_ms(r.frame_ms),
           fps_value_color(fps_state(r.fps, target)), m.ft_pt, kWeightValue,
           TextAlign::Right, m.ft_pt);
    rx -= vw + std::round(10.0f * m.k);
    const float mvw = slot_w({"888.8"}, m.label_pt);
    b.text("id_osc_ft_max_v", rx - mvw, mvw, y, ch,
           r.frame_ms_max >= 0.0f ? fmt_ms(r.frame_ms_max) : kDash, kText,
           m.label_pt, kWeightValue, TextAlign::Right, m.ft_pt);
    rx -= mvw + std::round(4.0f * m.k);
    const float mw = std::ceil(advance("MAX", m.label_pt));
    b.text("id_osc_ft_max_l", rx - mw, mw + 2.0f, y, ch, "MAX", kHeading,
           m.label_pt, kWeightLabel, TextAlign::Right, m.ft_pt);
    y += ch + m.chart_gap;

    const float gh = m.chart_h;
    const float bottom = y + gh;
    const float target_ms = 1000.0f / static_cast<float>(target);
    bars(b, "id_osc_tbar", kFrameBars, frame_bars(h.frame_ms, target, kFrameBars),
         x0, bottom, iw, gh, m.ft_bar_gap);
    b.rect("id_osc_tbase", x0, bottom, iw, 1.0f, kBaseline);
    /* 0 .. 3× target: the guides sit at 1/3 and 2/3 of the height. */
    guide(b, m, "id_osc_tg1", x0, iw, bottom - std::round(gh / 3.0f),
          kTargetLine, fmt_ms(target_ms), opt.opacity);
    guide(b, m, "id_osc_tg2", x0, iw, bottom - std::round(gh * 2.0f / 3.0f),
          kGuideLine, fmt_ms(2.0f * target_ms), opt.opacity);
    y = bottom + 1.0f;
  }

  if (opt.show_system)
    y = system_block(b, m, x0, iw, y, r.sys);

  return finish(opt, std::move(content), m.panel_w, y + m.pad_y);
}

} // namespace detail

/** Build one frame. Returns an empty scene when the screen size is
 *  unknown. */
inline Scene build_scene(const Options &opt, const Readings &readings,
                         const Histories &hist) {
  const Readings r = detail::sanitized(readings);
  if (!valid_screen_dimension(opt.screen_w) ||
      !valid_screen_dimension(opt.screen_h))
    return Scene{};
  Scene s{};
  switch (opt.detail) {
  case Detail::Minimal:
    s = detail::build_minimal(opt, r);
    break;
  case Detail::Compact:
    s = detail::build_compact(opt, r, hist);
    break;
  case Detail::Panel:
  case Detail::Full:
  default:
    s = detail::build_panel(opt, r, hist);
    break;
  }
  /* The minimal level already is the pill: no second copy. */
  if (opt.fps_pill && opt.detail != Detail::Minimal)
    detail::add_fps_pill(s, opt, r);
  if (opt.frame_graph)
    detail::add_frame_graph(s, opt, r, hist);
  return s;
}

} // namespace onion::overlay::scene
