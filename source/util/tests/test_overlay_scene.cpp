#include "test_harness.h"

#include "overlay_scene.hpp"

#include <cmath>
#include <limits>
#include <string>

namespace {

namespace sc = onion::overlay::scene;
using onion::overlay::BarAlign;
using onion::overlay::BarEdge;

constexpr float kW = 1920.0f;
constexpr float kH = 1080.0f;
const char *const kDash = "\xE2\x80\x94";

int as_int(float value) { return static_cast<int>(std::lround(value)); }

sc::Options base_options(sc::Detail detail = sc::Detail::Panel) {
  sc::Options o{};
  o.screen_w = kW;
  o.screen_h = kH;
  o.edge = BarEdge::Top;
  o.align = BarAlign::Left;
  o.detail = detail;
  o.font_pt = 18;
  o.margin = 0.0f;
  o.opacity = 85;
  return o;
}

sc::Readings live_readings() {
  sc::Readings r{};
  r.fps_valid = true;
  r.fps = 60.0f;
  r.frame_ms = 1000.0f / 60.0f;
  r.frame_ms_max = 33.4f;
  r.fps_avg = 59.4f;
  r.fps_low = 48.0f;
  r.fps_target = 60;
  r.title_id = "PPSA01234";
  return r;
}

/* A plausible system row: mixed core loads, warm but fine temperatures. */
sc::SystemReadings live_sys() {
  sc::SystemReadings s{};
  s.cpu_valid = true;
  const float cores[8] = {10, 20, 30, 40, 50, 60, 70, 90};
  float sum = 0.0f;
  for (int i = 0; i < 8; ++i) {
    s.cpu_core[i] = cores[i];
    sum += cores[i];
  }
  s.cpu_pct = sum / 8.0f; /* 46.25 */
  s.cpu_temp_valid = true;
  s.cpu_temp = 58;
  s.soc_temp_valid = true;
  s.soc_temp = 70;
  s.power_valid = true;
  s.gpu_power_w = 65.0f;
  s.ram_valid = true;
  s.ram_used_mb = 9830;
  s.ram_total_mb = 16000;
  return s;
}

sc::Readings live_readings_with_sys() {
  sc::Readings r = live_readings();
  r.sys = live_sys();
  return r;
}

/* Full charts: 40 FPS buckets and 90 frame samples at a steady rate. */
sc::Histories flat_history(float fps = 60.0f) {
  sc::Histories h{};
  for (int i = 0; i < sc::kFpsBars; ++i)
    h.fps.push(fps);
  for (int i = 0; i < sc::kFrameBars; ++i)
    h.frame_ms.push(1000.0f / fps);
  for (int i = 0; i < sc::kLongBars; ++i)
    h.long_ms.push(1000.0f / fps);
  return h;
}

const sc::Text *find_text(const sc::Scene &s, const std::string &id) {
  for (const sc::Text &t : s.texts)
    if (t.id == id)
      return &t;
  return nullptr;
}

const sc::Rect *find_rect(const sc::Scene &s, const std::string &id) {
  for (const sc::Rect &r : s.rects)
    if (r.id == id)
      return &r;
  return nullptr;
}

int count_rect_prefix(const sc::Scene &s, const std::string &prefix) {
  int n = 0;
  for (const sc::Rect &r : s.rects)
    if (r.id.compare(0, prefix.size(), prefix) == 0)
      ++n;
  return n;
}

bool ids_unique(const sc::Scene &s) {
  for (size_t i = 0; i < s.rects.size(); ++i)
    for (size_t j = i + 1; j < s.rects.size(); ++j)
      if (s.rects[i].id == s.rects[j].id)
        return false;
  for (size_t i = 0; i < s.texts.size(); ++i)
    for (size_t j = i + 1; j < s.texts.size(); ++j)
      if (s.texts[i].id == s.texts[j].id)
        return false;
  return true;
}

bool same_color(const sc::Rgba &a, const sc::Rgba &b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

/* Widgets the renderer creates: one Panel per rect, cell + label per text. */
int widget_count(const sc::Scene &s) {
  return static_cast<int>(s.rects.size() + 2 * s.texts.size());
}

const char *text_of(const sc::Scene &s, const std::string &id) {
  const sc::Text *t = find_text(s, id);
  return t ? t->text.c_str() : nullptr;
}

/* Everything inside the scene box. */
bool contained(const sc::Scene &s) {
  for (const sc::Rect &r : s.rects)
    if (r.x < s.x - 0.5f || r.y < s.y - 0.5f || r.x + r.w > s.x + s.w + 0.5f ||
        r.y + r.h > s.y + s.h + 0.5f)
      return false;
  for (const sc::Text &t : s.texts)
    if (t.x < s.x - 0.5f || t.y < s.y - 0.5f || t.x + t.w > s.x + s.w + 0.5f ||
        t.y + t.h > s.y + s.h + 0.5f)
      return false;
  return true;
}

int test_empty_without_screen(void) {
  sc::Options o = base_options();
  o.screen_w = 0.0f;
  sc::Scene s = sc::build_scene(o, live_readings(), flat_history());
  TEST_ASSERT_TRUE(s.rects.empty() && s.texts.empty());
  return 0;
}

int test_corner_placement_and_margin(void) {
  for (float margin : {0.0f, 5.0f, 10.0f, 20.0f}) {
    sc::Options o = base_options();
    o.margin = margin;
    sc::Scene tl = sc::build_scene(o, live_readings(), flat_history());
    TEST_ASSERT_EQ_INT(as_int(margin), as_int(tl.x));
    TEST_ASSERT_EQ_INT(as_int(margin), as_int(tl.y));
    TEST_ASSERT_TRUE(contained(tl));

    o.edge = BarEdge::Bottom;
    o.align = BarAlign::Right;
    sc::Scene br = sc::build_scene(o, live_readings(), flat_history());
    TEST_ASSERT_EQ_INT(as_int(kW - margin), as_int(br.x + br.w));
    TEST_ASSERT_EQ_INT(as_int(kH - margin), as_int(br.y + br.h));

    o.align = BarAlign::Center;
    sc::Scene c = sc::build_scene(o, live_readings(), flat_history());
    TEST_ASSERT_EQ_INT(as_int((kW - c.w) * 0.5f), as_int(c.x));
  }
  return 0;
}

int test_opacity(void) {
  sc::Options o = base_options();
  o.opacity = 0;
  sc::Scene none = sc::build_scene(o, live_readings(), flat_history());
  TEST_ASSERT_TRUE(find_rect(none, "id_osc_bg") == nullptr);
  TEST_ASSERT_TRUE(find_rect(none, "id_osc_edge_t") == nullptr);
  TEST_ASSERT_TRUE(find_text(none, "id_osc_fps_v") != nullptr);

  for (int pct : {40, 65, 85}) {
    o.opacity = pct;
    sc::Scene s = sc::build_scene(o, live_readings(), flat_history());
    const sc::Rect &bg = s.rects.front(); /* drawn first, under everything */
    TEST_ASSERT_STREQ("id_osc_bg", bg.id.c_str());
    TEST_ASSERT_EQ_INT(pct, as_int(bg.color.a * 100.0f));
    TEST_ASSERT_EQ_INT(as_int(s.w), as_int(bg.w));
    TEST_ASSERT_EQ_INT(as_int(s.h), as_int(bg.h));
  }
  return 0;
}

int test_panel_values(void) {
  sc::Histories h = flat_history();
  h.fps.push(44.0f);
  sc::Scene s = sc::build_scene(base_options(), live_readings(), h);
  TEST_ASSERT_TRUE(ids_unique(s));
  TEST_ASSERT_STREQ("PPSA01234", text_of(s, "id_osc_head_l"));
  TEST_ASSERT_STREQ("TARGET 60", text_of(s, "id_osc_head_r"));
  TEST_ASSERT_STREQ("60", text_of(s, "id_osc_fps_v"));
  TEST_ASSERT_STREQ("FPS", text_of(s, "id_osc_fps_l"));
  TEST_ASSERT_STREQ("AVG", text_of(s, "id_osc_avg_l"));
  TEST_ASSERT_STREQ("59.4", text_of(s, "id_osc_avg_v"));
  TEST_ASSERT_STREQ("48.0", text_of(s, "id_osc_low_v"));
  TEST_ASSERT_STREQ("FPS \xC2\xB7 LAST 10 S", text_of(s, "id_osc_fc_l"));
  TEST_ASSERT_STREQ("MIN", text_of(s, "id_osc_fc_min_l"));
  TEST_ASSERT_STREQ("44", text_of(s, "id_osc_fc_min_v"));
  TEST_ASSERT_STREQ("MAX", text_of(s, "id_osc_fc_max_l"));
  TEST_ASSERT_STREQ("60", text_of(s, "id_osc_fc_max_v"));
  TEST_ASSERT_STREQ("FRAME TIME", text_of(s, "id_osc_tc_l"));
  TEST_ASSERT_STREQ("16.7", text_of(s, "id_osc_ft_v"));
  TEST_ASSERT_STREQ("ms", text_of(s, "id_osc_ft_u"));
  TEST_ASSERT_STREQ("33.4", text_of(s, "id_osc_ft_max_v"));
  TEST_ASSERT_STREQ("60", text_of(s, "id_osc_ftarget_t"));
  TEST_ASSERT_STREQ("16.7", text_of(s, "id_osc_tg1_t"));
  TEST_ASSERT_STREQ("33.3", text_of(s, "id_osc_tg2_t"));
  TEST_ASSERT_TRUE(contained(s));

  /* Type scale: hero > AVG/LOW > frame time > sub-headings > header. */
  const int hero = find_text(s, "id_osc_fps_v")->pt;
  const int stat = find_text(s, "id_osc_avg_v")->pt;
  const int ft = find_text(s, "id_osc_ft_v")->pt;
  const int label = find_text(s, "id_osc_tc_l")->pt;
  const int head = find_text(s, "id_osc_head_l")->pt;
  TEST_ASSERT_TRUE(hero > stat && stat > ft && ft > label && label > head);
  /* Every sub-heading shares one size and colour. */
  for (const char *id : {"id_osc_fps_l", "id_osc_avg_l", "id_osc_low_l",
                         "id_osc_fc_l", "id_osc_fc_min_l", "id_osc_fc_max_l",
                         "id_osc_tc_l", "id_osc_ft_max_l"}) {
    const sc::Text *t = find_text(s, id);
    TEST_ASSERT_TRUE(t != nullptr);
    TEST_ASSERT_EQ_INT(label, t->pt);
    TEST_ASSERT_TRUE(same_color(sc::kHeading, t->color));
  }

  /* A three-digit FPS never runs into the AVG column, at any font size. */
  for (int pt : {14, 18, 24}) {
    sc::Options o = base_options();
    o.font_pt = pt;
    sc::Readings fast = live_readings();
    fast.fps = 144.0f;
    fast.fps_target = 120;
    sc::Scene f = sc::build_scene(o, fast, flat_history(120.0f));
    const sc::Text *unit = find_text(f, "id_osc_fps_l");
    const sc::Text *avg = find_text(f, "id_osc_avg_l");
    TEST_ASSERT_TRUE(unit && avg);
    TEST_ASSERT_TRUE(unit->x + sc::detail::advance("FPS", unit->pt) < avg->x);
  }
  return 0;
}

int test_low_label(void) {
  sc::Readings r = live_readings();
  sc::Scene s = sc::build_scene(base_options(), r, flat_history());
  TEST_ASSERT_STREQ("LOW", text_of(s, "id_osc_low_l"));
  r.low_per_frame = true;
  s = sc::build_scene(base_options(), r, flat_history());
  TEST_ASSERT_STREQ("1% LOW", text_of(s, "id_osc_low_l"));
  s = sc::build_scene(base_options(sc::Detail::Compact), r, flat_history());
  TEST_ASSERT_STREQ("1% LOW", text_of(s, "id_osc_low_l"));
  return 0;
}

int test_brand_header(void) {
  sc::Options o = base_options();
  sc::Scene plain = sc::build_scene(o, live_readings(), flat_history());
  for (const sc::Text &t : plain.texts)
    TEST_ASSERT_TRUE(t.text != sc::kBrandText); /* off by default */
  o.brand = true;
  sc::Scene s = sc::build_scene(o, live_readings(), flat_history());
  TEST_ASSERT_STREQ(sc::kBrandText, text_of(s, "id_osc_head_l"));
  TEST_ASSERT_STREQ("TARGET 60", text_of(s, "id_osc_head_r"));
  return 0;
}

int test_no_game(void) {
  sc::Scene live = sc::build_scene(base_options(), live_readings(), flat_history());
  sc::Readings r = live_readings();
  r.fps_valid = false;
  sc::Scene s = sc::build_scene(base_options(), r, flat_history());
  TEST_ASSERT_STREQ("NO GAME", text_of(s, "id_osc_head_l"));
  TEST_ASSERT_STREQ("NO GAME DETECTED", text_of(s, "id_osc_fps_v"));
  TEST_ASSERT_TRUE(find_text(s, "id_osc_head_r") == nullptr);
  TEST_ASSERT_TRUE(find_text(s, "id_osc_avg_v") == nullptr);
  TEST_ASSERT_EQ_INT(0, count_rect_prefix(s, "id_osc_fbar"));
  TEST_ASSERT_EQ_INT(0, count_rect_prefix(s, "id_osc_tbar"));
  TEST_ASSERT_TRUE(s.h < live.h);
  TEST_ASSERT_EQ_INT(as_int(live.w), as_int(s.w));
  return 0;
}

int test_non_finite(void) {
  sc::Readings r = live_readings();
  r.fps = std::numeric_limits<float>::infinity();
  sc::Scene s = sc::build_scene(base_options(), r, flat_history());
  TEST_ASSERT_STREQ("NO GAME DETECTED", text_of(s, "id_osc_fps_v"));

  r = live_readings();
  r.frame_ms_max = std::numeric_limits<float>::quiet_NaN();
  s = sc::build_scene(base_options(), r, flat_history());
  TEST_ASSERT_STREQ(kDash, text_of(s, "id_osc_ft_max_v"));
  TEST_ASSERT_STREQ("60", text_of(s, "id_osc_fps_v"));
  return 0;
}

int test_fps_state_colors(void) {
  TEST_ASSERT_TRUE(sc::fps_state(59.0f, 60) == sc::FpsState::Good);
  TEST_ASSERT_TRUE(sc::fps_state(50.0f, 60) == sc::FpsState::Warn);
  TEST_ASSERT_TRUE(sc::fps_state(40.0f, 60) == sc::FpsState::Bad);
  TEST_ASSERT_TRUE(sc::fps_state(29.0f, 30) == sc::FpsState::Good);

  sc::Readings r = live_readings();
  sc::Scene s = sc::build_scene(base_options(), r, flat_history());
  TEST_ASSERT_TRUE(same_color(sc::kText, find_text(s, "id_osc_fps_v")->color));
  TEST_ASSERT_TRUE(same_color(sc::kText, find_text(s, "id_osc_avg_v")->color));
  /* 48 against a 60 cap is a warning; values only colour when off-target */
  TEST_ASSERT_TRUE(same_color(sc::kWarn, find_text(s, "id_osc_low_v")->color));
  r.fps = 35.0f;
  r.frame_ms = 1000.0f / 35.0f;
  s = sc::build_scene(base_options(), r, flat_history());
  TEST_ASSERT_TRUE(same_color(sc::kBad, find_text(s, "id_osc_fps_v")->color));
  TEST_ASSERT_TRUE(same_color(sc::kBad, find_text(s, "id_osc_ft_v")->color));

  TEST_ASSERT_TRUE(same_color(sc::kGood, sc::frame_ms_color(16.7f, 60)));
  TEST_ASSERT_TRUE(same_color(sc::kGood, sc::frame_ms_color(19.9f, 60)));
  TEST_ASSERT_TRUE(same_color(sc::kWarn, sc::frame_ms_color(25.0f, 60)));
  TEST_ASSERT_TRUE(same_color(sc::kWarn, sc::frame_ms_color(33.4f, 60)));
  TEST_ASSERT_TRUE(same_color(sc::kBad, sc::frame_ms_color(50.0f, 60)));
  TEST_ASSERT_TRUE(same_color(sc::kGood, sc::frame_ms_color(33.4f, 30)));
  return 0;
}

int test_fps_chart(void) {
  sc::Histories h{};
  for (int i = 0; i < sc::kFpsBars; ++i)
    h.fps.push(i == sc::kFpsBars - 1 ? 30.0f : 60.0f);
  sc::Scene s = sc::build_scene(base_options(), live_readings(), h);
  TEST_ASSERT_EQ_INT(sc::kFpsBars, count_rect_prefix(s, "id_osc_fbar"));
  const sc::Rect *base = find_rect(s, "id_osc_fbase");
  const sc::Rect *line = find_rect(s, "id_osc_ftarget");
  const sc::Rect *full = find_rect(s, "id_osc_fbar0");
  const sc::Rect *dip = find_rect(s, "id_osc_fbar39");
  TEST_ASSERT_TRUE(base && line && full && dip);
  /* 0 .. 1.25× target: a bar at the target touches the target line. */
  TEST_ASSERT_EQ_INT(as_int(line->y), as_int(full->y));
  TEST_ASSERT_EQ_INT(as_int(base->y), as_int(full->y + full->h));
  TEST_ASSERT_EQ_INT(as_int(full->h * 0.5f), as_int(dip->h));
  TEST_ASSERT_TRUE(same_color(sc::kGood, full->color));
  TEST_ASSERT_TRUE(same_color(sc::kBad, dip->color));
  /* Newest bar ends at the right edge, minus the gap. */
  TEST_ASSERT_TRUE(dip->x + dip->w <= base->x + base->w);
  TEST_ASSERT_TRUE(dip->x + dip->w >= base->x + base->w - 3.0f);

  /* A short history fills from the right; ids follow the slot. */
  sc::Histories few{};
  few.fps.push(60.0f);
  few.fps.push(60.0f);
  s = sc::build_scene(base_options(), live_readings(), few);
  TEST_ASSERT_EQ_INT(2, count_rect_prefix(s, "id_osc_fbar"));
  TEST_ASSERT_TRUE(find_rect(s, "id_osc_fbar39") != nullptr);
  TEST_ASSERT_TRUE(find_rect(s, "id_osc_fbar38") != nullptr);
  TEST_ASSERT_TRUE(find_rect(s, "id_osc_fbar0") == nullptr);
  /* No buckets yet: no MIN / MAX. */
  s = sc::build_scene(base_options(), live_readings(), sc::Histories{});
  TEST_ASSERT_TRUE(find_text(s, "id_osc_fc_min_v") == nullptr);
  return 0;
}

int test_frame_time_chart(void) {
  sc::Histories h = flat_history();
  h.frame_ms.push(25.0f);  /* a spike under 2× target */
  h.frame_ms.push(50.0f);  /* 3× target: the top of the chart */
  h.frame_ms.push(120.0f); /* off the scale: clamped */
  sc::Scene s = sc::build_scene(base_options(), live_readings(), h);
  TEST_ASSERT_EQ_INT(sc::kFrameBars, count_rect_prefix(s, "id_osc_tbar"));
  const sc::Rect *base = find_rect(s, "id_osc_tbase");
  const sc::Rect *g1 = find_rect(s, "id_osc_tg1");
  const sc::Rect *g2 = find_rect(s, "id_osc_tg2");
  const sc::Rect *flat = find_rect(s, "id_osc_tbar80");
  const sc::Rect *warn = find_rect(s, "id_osc_tbar87");
  const sc::Rect *top = find_rect(s, "id_osc_tbar88");
  const sc::Rect *clip = find_rect(s, "id_osc_tbar89");
  TEST_ASSERT_TRUE(base && g1 && g2 && flat && warn && top && clip);
  /* 0 .. 3× target: 16.7 ms reaches the first guide. */
  TEST_ASSERT_TRUE(std::fabs(g1->y - flat->y) <= 1.0f);
  TEST_ASSERT_TRUE(g2->y < g1->y);
  TEST_ASSERT_EQ_INT(as_int(top->h), as_int(clip->h));
  TEST_ASSERT_TRUE(same_color(sc::kGood, flat->color));
  TEST_ASSERT_TRUE(same_color(sc::kWarn, warn->color));
  TEST_ASSERT_TRUE(same_color(sc::kBad, top->color));
  TEST_ASSERT_TRUE(same_color(sc::kBad, clip->color));
  /* Guide labels sit on a patch of panel colour above their line. */
  const sc::Rect *patch = find_rect(s, "id_osc_tg1_bg");
  TEST_ASSERT_TRUE(patch != nullptr);
  TEST_ASSERT_EQ_INT(as_int(g1->y), as_int(patch->y + patch->h));
  return 0;
}

int test_compact_strip(void) {
  sc::Options o = base_options(sc::Detail::Compact);
  o.align = BarAlign::Center;
  o.margin = 10.0f;
  sc::Scene s = sc::build_scene(o, live_readings(), flat_history());
  TEST_ASSERT_TRUE(ids_unique(s));
  TEST_ASSERT_STREQ("60", text_of(s, "id_osc_fps_v"));
  TEST_ASSERT_STREQ("16.7", text_of(s, "id_osc_ft_v"));
  TEST_ASSERT_STREQ("59.4", text_of(s, "id_osc_avg_v"));
  TEST_ASSERT_STREQ("LOW", text_of(s, "id_osc_low_l"));
  TEST_ASSERT_STREQ("48.0", text_of(s, "id_osc_low_v"));
  TEST_ASSERT_EQ_INT(sc::kStripBars, count_rect_prefix(s, "id_osc_sbar"));
  TEST_ASSERT_TRUE(contained(s));
  /* With the system groups the strip passes half the screen; what matters is
   * that it fits between the margins at the largest font. */
  sc::Options big = o;
  big.font_pt = 24;
  TEST_ASSERT_TRUE(sc::build_scene(big, live_readings_with_sys(), flat_history()).w <=
                   kW - 2.0f * o.margin);
  TEST_ASSERT_EQ_INT(as_int((kW - s.w) * 0.5f), as_int(s.x));
  TEST_ASSERT_EQ_INT(10, as_int(s.y));

  /* Values sit in fixed slots: nothing moves as digits change. */
  sc::Readings lo = live_readings();
  lo.fps = 9.0f;
  lo.frame_ms = 111.1f;
  lo.fps_avg = 9.5f;
  lo.fps_low = 4.0f;
  sc::Readings hi = live_readings();
  hi.fps = 120.0f;
  hi.frame_ms = 8.3f;
  hi.fps_avg = 119.9f;
  hi.fps_low = 101.2f;
  sc::Scene a = sc::build_scene(o, lo, flat_history());
  sc::Scene b = sc::build_scene(o, hi, flat_history());
  TEST_ASSERT_EQ_INT(as_int(a.w), as_int(b.w));
  TEST_ASSERT_EQ_INT(static_cast<int>(a.texts.size()),
                     static_cast<int>(b.texts.size()));
  for (size_t i = 0; i < a.texts.size(); ++i) {
    TEST_ASSERT_STREQ(a.texts[i].id.c_str(), b.texts[i].id.c_str());
    TEST_ASSERT_EQ_INT(as_int(a.texts[i].x), as_int(b.texts[i].x));
  }
  /* Losing the game does not move anything either. */
  sc::Readings gone = live_readings();
  gone.fps_valid = false;
  sc::Scene c = sc::build_scene(o, gone, flat_history());
  TEST_ASSERT_EQ_INT(as_int(a.w), as_int(c.w));
  TEST_ASSERT_STREQ(kDash, text_of(c, "id_osc_fps_v"));
  TEST_ASSERT_EQ_INT(0, count_rect_prefix(c, "id_osc_sbar"));
  return 0;
}

int test_minimal_pill(void) {
  sc::Options o = base_options(sc::Detail::Minimal);
  sc::Scene s = sc::build_scene(o, live_readings(), flat_history());
  TEST_ASSERT_STREQ("60", text_of(s, "id_osc_fps_v"));
  TEST_ASSERT_STREQ("FPS", text_of(s, "id_osc_fps_l"));
  TEST_ASSERT_TRUE(find_text(s, "id_osc_avg_v") == nullptr);
  const sc::Rect *dot = find_rect(s, "id_osc_dot");
  TEST_ASSERT_TRUE(dot && same_color(sc::kGood, dot->color));

  sc::Readings r = live_readings();
  r.fps = 30.0f;
  s = sc::build_scene(o, r, flat_history());
  TEST_ASSERT_TRUE(same_color(sc::kBad, find_rect(s, "id_osc_dot")->color));
  r.fps_target = 30;
  s = sc::build_scene(o, r, flat_history());
  TEST_ASSERT_TRUE(same_color(sc::kGood, find_rect(s, "id_osc_dot")->color));

  r.fps_valid = false;
  s = sc::build_scene(o, r, flat_history());
  TEST_ASSERT_STREQ(kDash, text_of(s, "id_osc_fps_v"));
  TEST_ASSERT_TRUE(same_color(sc::kLabel, find_rect(s, "id_osc_dot")->color));
  return 0;
}

/* The frame-time graph: a third box along the bottom edge, every level. */
int test_frame_graph(void) {
  sc::Scene plain = sc::build_scene(base_options(), live_readings(), flat_history());
  TEST_ASSERT_EQ_INT(0, count_rect_prefix(plain, "id_osc_graph_"));

  sc::Histories h = flat_history();
  for (int i = 0; i < sc::kLongBars; ++i)
    h.long_ms.push(i == 100 ? 33.4f : 16.7f);
  sc::Options o = base_options(); /* panel, top-left */
  o.frame_graph = true;
  sc::Scene s = sc::build_scene(o, live_readings(), h);
  TEST_ASSERT_TRUE(ids_unique(s));
  TEST_ASSERT_EQ_INT(as_int(plain.w), as_int(s.w));
  const sc::Rect *bg = find_rect(s, "id_osc_graph_bg");
  TEST_ASSERT_TRUE(bg != nullptr);
  const float w = std::round(0.7f * kW);
  TEST_ASSERT_EQ_INT(as_int(w), as_int(bg->w));
  TEST_ASSERT_EQ_INT(as_int((kW - w) * 0.5f), as_int(bg->x));
  TEST_ASSERT_EQ_INT(116, as_int(bg->h));
  TEST_ASSERT_EQ_INT(as_int(kH - 24.0f), as_int(bg->y + bg->h));
  TEST_ASSERT_EQ_INT(sc::kLongBars, count_rect_prefix(s, "id_osc_graph_bar"));
  TEST_ASSERT_TRUE(find_rect(s, "id_osc_graph_g1") != nullptr);
  TEST_ASSERT_TRUE(find_rect(s, "id_osc_graph_g2") != nullptr);
  TEST_ASSERT_STREQ("33.4", text_of(s, "id_osc_graph_max_v"));
  TEST_ASSERT_STREQ("16.7", text_of(s, "id_osc_graph_g1_t"));
  TEST_ASSERT_STREQ("33.3", text_of(s, "id_osc_graph_g2_t"));

  /* contained() only knows the main box; check the graph against its own,
   * and pin its widget count (one Panel per rect, cell + label per text). */
  int widgets = 0;
  for (const sc::Rect &rc : s.rects) {
    if (rc.id.compare(0, 13, "id_osc_graph_") != 0)
      continue;
    ++widgets;
    TEST_ASSERT_TRUE(rc.x >= bg->x - 0.5f && rc.x + rc.w <= bg->x + bg->w + 0.5f);
    TEST_ASSERT_TRUE(rc.y >= bg->y - 0.5f && rc.y + rc.h <= bg->y + bg->h + 0.5f);
  }
  for (const sc::Text &t : s.texts)
    if (t.id.compare(0, 13, "id_osc_graph_") == 0)
      widgets += 2;
  TEST_ASSERT_TRUE(widgets <= 250);

  /* Every level, Minimal included. */
  o = base_options(sc::Detail::Minimal);
  o.frame_graph = true;
  s = sc::build_scene(o, live_readings(), h);
  TEST_ASSERT_TRUE(find_rect(s, "id_osc_graph_bg") != nullptr);

  /* An overlay on the bottom edge sends the graph to the top. */
  o = base_options();
  o.frame_graph = true;
  o.edge = BarEdge::Bottom;
  s = sc::build_scene(o, live_readings(), h);
  bg = find_rect(s, "id_osc_graph_bg");
  TEST_ASSERT_TRUE(bg != nullptr);
  TEST_ASSERT_EQ_INT(24, as_int(bg->y));
  return 0;
}

int test_companion_fps_pill(void) {
  sc::Scene plain = sc::build_scene(base_options(), live_readings(), flat_history());
  TEST_ASSERT_TRUE(find_text(plain, "id_osc_pill_fps_v") == nullptr);

  sc::Options o = base_options(); /* panel, top-left */
  o.fps_pill = true;
  o.margin = 10.0f;
  sc::Scene s = sc::build_scene(o, live_readings(), flat_history());
  TEST_ASSERT_TRUE(ids_unique(s));
  TEST_ASSERT_EQ_INT(as_int(plain.w), as_int(s.w));
  TEST_ASSERT_STREQ("60", text_of(s, "id_osc_pill_fps_v"));
  const sc::Rect *bg = find_rect(s, "id_osc_pill_bg");
  TEST_ASSERT_TRUE(bg != nullptr);
  TEST_ASSERT_EQ_INT(as_int(kW - 10.0f), as_int(bg->x + bg->w));
  TEST_ASSERT_EQ_INT(10, as_int(bg->y));

  o.align = BarAlign::Right;
  o.edge = BarEdge::Bottom;
  s = sc::build_scene(o, live_readings(), flat_history());
  bg = find_rect(s, "id_osc_pill_bg");
  TEST_ASSERT_TRUE(bg != nullptr);
  TEST_ASSERT_EQ_INT(10, as_int(bg->x));
  TEST_ASSERT_EQ_INT(as_int(kH - 10.0f), as_int(bg->y + bg->h));

  /* The minimal level already is the pill: no second copy. */
  o = base_options(sc::Detail::Minimal);
  o.fps_pill = true;
  s = sc::build_scene(o, live_readings(), flat_history());
  TEST_ASSERT_TRUE(find_text(s, "id_osc_pill_fps_v") == nullptr);
  return 0;
}

int test_full_matches_panel(void) {
  sc::Scene p = sc::build_scene(base_options(sc::Detail::Panel), live_readings(),
                                flat_history());
  sc::Scene f = sc::build_scene(base_options(sc::Detail::Full), live_readings(),
                                flat_history());
  TEST_ASSERT_EQ_INT(static_cast<int>(p.rects.size()),
                     static_cast<int>(f.rects.size()));
  TEST_ASSERT_EQ_INT(static_cast<int>(p.texts.size()),
                     static_cast<int>(f.texts.size()));
  TEST_ASSERT_EQ_INT(as_int(p.h), as_int(f.h));
  return 0;
}

int test_target_inference(void) {
  TEST_ASSERT_EQ_INT(60, sc::infer_target(0.0f));
  TEST_ASSERT_EQ_INT(60, sc::infer_target(std::numeric_limits<float>::quiet_NaN()));
  TEST_ASSERT_EQ_INT(30, sc::infer_target(29.8f));
  TEST_ASSERT_EQ_INT(30, sc::infer_target(31.0f));
  TEST_ASSERT_EQ_INT(40, sc::infer_target(39.5f));
  TEST_ASSERT_EQ_INT(60, sc::infer_target(45.0f));
  TEST_ASSERT_EQ_INT(60, sc::infer_target(62.0f));
  TEST_ASSERT_EQ_INT(120, sc::infer_target(90.0f));
  TEST_ASSERT_EQ_INT(120, sc::infer_target(200.0f));

  sc::TargetTracker t{3};
  TEST_ASSERT_EQ_INT(60, t.target());
  TEST_ASSERT_EQ_INT(60, t.update(30.0f));
  TEST_ASSERT_EQ_INT(60, t.update(30.0f));
  TEST_ASSERT_EQ_INT(60, t.update(59.0f)); /* a 60 reading resets the streak */
  TEST_ASSERT_EQ_INT(60, t.update(30.0f));
  TEST_ASSERT_EQ_INT(60, t.update(30.0f));
  TEST_ASSERT_EQ_INT(30, t.update(30.0f)); /* three in a row: switch */
  t.reset();
  TEST_ASSERT_EQ_INT(60, t.target());

  /* A 30 cap moves the header pill and the chart guides. */
  sc::Readings r = live_readings();
  r.fps = r.fps_avg = 30.0f;
  r.frame_ms = 1000.0f / 30.0f;
  r.fps_target = 30;
  sc::Scene s = sc::build_scene(base_options(), r, flat_history(30.0f));
  TEST_ASSERT_STREQ("TARGET 30", text_of(s, "id_osc_head_r"));
  TEST_ASSERT_STREQ("33.3", text_of(s, "id_osc_tg1_t"));
  TEST_ASSERT_STREQ("66.7", text_of(s, "id_osc_tg2_t"));
  return 0;
}

int test_formatting(void) {
  TEST_ASSERT_STREQ("60", sc::fmt_fps(59.6f).c_str()); /* never "59.6" */
  TEST_ASSERT_STREQ("59.6", sc::fmt_fps1(59.6f).c_str());
  TEST_ASSERT_STREQ("16.7", sc::fmt_ms(1000.0f / 60.0f).c_str());
  TEST_ASSERT_STREQ("999.9", sc::fmt_ms(5000.0f).c_str());
  TEST_ASSERT_STREQ("46%", sc::fmt_pct(46.25f).c_str());
  TEST_ASSERT_STREQ("100%", sc::fmt_pct(140.0f).c_str());
  TEST_ASSERT_STREQ("58\xC2\xB0", sc::fmt_temp(58).c_str());
  TEST_ASSERT_STREQ("65 W", sc::fmt_watts(65.4f).c_str());
  TEST_ASSERT_STREQ("9.6", sc::fmt_gb(9830).c_str());
  return 0;
}

/* Panel: three columns under the charts with the values, the core
 * segments, the power and memory bars and the total in the heading row. */
int test_system_row_panel(void) {
  const sc::Options o = base_options();
  const sc::Scene s = sc::build_scene(o, live_readings_with_sys(), flat_history());
  TEST_ASSERT_TRUE(find_rect(s, "id_osc_sys_rule") != nullptr);
  TEST_ASSERT_STREQ("CPU", text_of(s, "id_osc_cpu_l"));
  TEST_ASSERT_STREQ("46%", text_of(s, "id_osc_cpu_v"));
  TEST_ASSERT_STREQ("58\xC2\xB0", text_of(s, "id_osc_cpu_t"));
  TEST_ASSERT_STREQ("GPU", text_of(s, "id_osc_gpu_l"));
  TEST_ASSERT_STREQ("70\xC2\xB0", text_of(s, "id_osc_gpu_v"));
  TEST_ASSERT_STREQ("65 W", text_of(s, "id_osc_gpu_t"));
  TEST_ASSERT_STREQ("MEMORY", text_of(s, "id_osc_mem_l"));
  TEST_ASSERT_STREQ("9.6", text_of(s, "id_osc_mem_v"));
  TEST_ASSERT_STREQ("GB", text_of(s, "id_osc_mem_u"));

  /* Eight core segments; the 90 % core is warn, the 10 % core neutral. */
  TEST_ASSERT_EQ_INT(8, count_rect_prefix(s, "id_osc_cpu_seg"));
  const sc::Rect *seg0 = find_rect(s, "id_osc_cpu_seg0");
  const sc::Rect *seg7 = find_rect(s, "id_osc_cpu_seg7");
  TEST_ASSERT_TRUE(seg0 && seg7);
  TEST_ASSERT_TRUE(same_color(sc::kFill, seg0->color));
  TEST_ASSERT_TRUE(same_color(sc::kWarn, seg7->color));
  TEST_ASSERT_TRUE(seg7->w > seg0->w);

  /* GPU bar is power over the fixed scale. */
  const sc::Rect *gtrack = find_rect(s, "id_osc_gpu_track");
  const sc::Rect *gfill = find_rect(s, "id_osc_gpu_fill");
  TEST_ASSERT_TRUE(gtrack && gfill);
  TEST_ASSERT_EQ_INT(as_int(std::round(gtrack->w * 65.0f / sc::kGpuPowerScaleW)),
                     as_int(gfill->w));

  /* Memory bar is used / total. */
  const sc::Rect *mtrack = find_rect(s, "id_osc_mem_track");
  const sc::Rect *mfill = find_rect(s, "id_osc_mem_fill");
  TEST_ASSERT_TRUE(mtrack && mfill);
  TEST_ASSERT_EQ_INT(as_int(std::round(mtrack->w * 9830.0f / 16000.0f)),
                     as_int(mfill->w));
  TEST_ASSERT_TRUE(same_color(sc::kFill, mfill->color));

  /* The row sits under the frame-time chart and adds 80 px at 18 pt. */
  const sc::Rect *tbase = find_rect(s, "id_osc_tbase");
  const sc::Rect *rule = find_rect(s, "id_osc_sys_rule");
  TEST_ASSERT_TRUE(tbase && rule && rule->y > tbase->y);
  sc::Options off = o;
  off.show_system = false;
  const sc::Scene without = sc::build_scene(off, live_readings_with_sys(), flat_history());
  TEST_ASSERT_EQ_INT(as_int(without.h) + 80, as_int(s.h));

  TEST_ASSERT_TRUE(contained(s));
  TEST_ASSERT_TRUE(ids_unique(s));
  return 0;
}

int test_system_row_colors(void) {
  sc::Readings r = live_readings_with_sys();
  auto color_of = [&](const char *id) {
    const sc::Scene s = sc::build_scene(base_options(), r, flat_history());
    const sc::Text *t = find_text(s, id);
    return t ? t->color : sc::Rgba{};
  };
  TEST_ASSERT_TRUE(same_color(sc::kText, color_of("id_osc_cpu_t")));
  r.sys.cpu_temp = 65;
  TEST_ASSERT_TRUE(same_color(sc::kWarn, color_of("id_osc_cpu_t")));
  r.sys.cpu_temp = 80;
  TEST_ASSERT_TRUE(same_color(sc::kBad, color_of("id_osc_cpu_t")));
  r.sys.soc_temp = 84;
  TEST_ASSERT_TRUE(same_color(sc::kWarn, color_of("id_osc_gpu_v")));
  r.sys.soc_temp = 85;
  TEST_ASSERT_TRUE(same_color(sc::kBad, color_of("id_osc_gpu_v")));
  r.sys.soc_temp = 50;
  TEST_ASSERT_TRUE(same_color(sc::kText, color_of("id_osc_gpu_v")));
  /* Load and memory values turn warn past 85 %. */
  TEST_ASSERT_TRUE(same_color(sc::kText, color_of("id_osc_cpu_v")));
  r.sys.cpu_pct = 90.0f;
  TEST_ASSERT_TRUE(same_color(sc::kWarn, color_of("id_osc_cpu_v")));
  r.sys.ram_used_mb = 15000;
  TEST_ASSERT_TRUE(same_color(sc::kWarn, color_of("id_osc_mem_v")));
  const sc::Scene s = sc::build_scene(base_options(), r, flat_history());
  const sc::Rect *mfill = find_rect(s, "id_osc_mem_fill");
  TEST_ASSERT_TRUE(mfill && same_color(sc::kWarn, mfill->color));
  return 0;
}

/* Nothing sampled yet, or a NaN: dashes, tracks only, no total. */
int test_system_row_invalid(void) {
  sc::Readings r = live_readings(); /* sys left at defaults */
  sc::Scene s = sc::build_scene(base_options(), r, flat_history());
  TEST_ASSERT_STREQ(kDash, text_of(s, "id_osc_cpu_v"));
  TEST_ASSERT_STREQ(kDash, text_of(s, "id_osc_cpu_t"));
  TEST_ASSERT_STREQ(kDash, text_of(s, "id_osc_gpu_v"));
  TEST_ASSERT_STREQ(kDash, text_of(s, "id_osc_gpu_t"));
  TEST_ASSERT_STREQ(kDash, text_of(s, "id_osc_mem_v"));
  TEST_ASSERT_STREQ("GB", text_of(s, "id_osc_mem_u"));
  TEST_ASSERT_EQ_INT(0, count_rect_prefix(s, "id_osc_cpu_seg"));
  TEST_ASSERT_TRUE(find_rect(s, "id_osc_gpu_fill") == nullptr);
  TEST_ASSERT_TRUE(find_rect(s, "id_osc_mem_fill") == nullptr);
  TEST_ASSERT_TRUE(find_rect(s, "id_osc_cpu_track") != nullptr);
  TEST_ASSERT_TRUE(find_rect(s, "id_osc_gpu_track") != nullptr);
  TEST_ASSERT_TRUE(find_rect(s, "id_osc_mem_track") != nullptr);

  r = live_readings_with_sys();
  r.sys.cpu_pct = std::numeric_limits<float>::quiet_NaN();
  r.sys.gpu_power_w = std::numeric_limits<float>::infinity();
  r.sys.ram_total_mb = 0;
  s = sc::build_scene(base_options(), r, flat_history());
  TEST_ASSERT_STREQ(kDash, text_of(s, "id_osc_cpu_v"));
  TEST_ASSERT_STREQ(kDash, text_of(s, "id_osc_gpu_t"));
  TEST_ASSERT_STREQ(kDash, text_of(s, "id_osc_mem_v"));
  TEST_ASSERT_TRUE(contained(s));
  return 0;
}

/* The row does not need a game; the toggle removes it everywhere. */
int test_system_row_no_game_and_toggle(void) {
  sc::Readings r = live_readings_with_sys();
  r.fps_valid = false;
  sc::Scene s = sc::build_scene(base_options(), r, flat_history());
  TEST_ASSERT_STREQ("NO GAME", text_of(s, "id_osc_head_l"));
  TEST_ASSERT_STREQ("46%", text_of(s, "id_osc_cpu_v"));
  TEST_ASSERT_TRUE(find_rect(s, "id_osc_sys_rule") != nullptr);
  const sc::Scene live = sc::build_scene(base_options(), live_readings_with_sys(),
                                         flat_history());
  TEST_ASSERT_TRUE(s.h < live.h);
  TEST_ASSERT_TRUE(contained(s));

  for (sc::Detail d : {sc::Detail::Panel, sc::Detail::Compact}) {
    sc::Options o = base_options(d);
    o.show_system = false;
    s = sc::build_scene(o, live_readings_with_sys(), flat_history());
    TEST_ASSERT_TRUE(find_rect(s, "id_osc_sys_rule") == nullptr);
    TEST_ASSERT_TRUE(find_text(s, "id_osc_cpu_v") == nullptr);
    TEST_ASSERT_TRUE(find_text(s, "id_osc_scpu_v") == nullptr);
    TEST_ASSERT_TRUE(find_rect(s, "id_osc_sep1") == nullptr);
  }
  /* Minimal never has it, whatever the option says. */
  s = sc::build_scene(base_options(sc::Detail::Minimal), live_readings_with_sys(),
                      flat_history());
  TEST_ASSERT_TRUE(find_text(s, "id_osc_cpu_v") == nullptr);
  TEST_ASSERT_TRUE(find_text(s, "id_osc_scpu_v") == nullptr);
  return 0;
}

/* Compact: three separated groups after the mini chart, in fixed slots. */
int test_system_row_compact(void) {
  const sc::Options o = base_options(sc::Detail::Compact);
  const sc::Scene s = sc::build_scene(o, live_readings_with_sys(), flat_history());
  TEST_ASSERT_STREQ("CPU", text_of(s, "id_osc_scpu_l"));
  TEST_ASSERT_STREQ("46%", text_of(s, "id_osc_scpu_v"));
  TEST_ASSERT_STREQ("58\xC2\xB0", text_of(s, "id_osc_scpu_t"));
  TEST_ASSERT_STREQ("GPU", text_of(s, "id_osc_sgpu_l"));
  TEST_ASSERT_STREQ("70\xC2\xB0", text_of(s, "id_osc_sgpu_v"));
  TEST_ASSERT_STREQ("65 W", text_of(s, "id_osc_sgpu_t"));
  TEST_ASSERT_STREQ("MEMORY", text_of(s, "id_osc_smem_l"));
  TEST_ASSERT_STREQ("9.6", text_of(s, "id_osc_smem_v"));
  TEST_ASSERT_STREQ("GB", text_of(s, "id_osc_smem_u"));
  TEST_ASSERT_EQ_INT(3, count_rect_prefix(s, "id_osc_sep") - 1); /* sep0 + 3 */
  const sc::Rect *base = find_rect(s, "id_osc_sbase");
  const sc::Rect *sep1 = find_rect(s, "id_osc_sep1");
  TEST_ASSERT_TRUE(base && sep1 && sep1->x > base->x + base->w);
  TEST_ASSERT_TRUE(contained(s));
  TEST_ASSERT_TRUE(ids_unique(s));

  /* Width and positions do not depend on the values. */
  const sc::Scene none = sc::build_scene(o, live_readings(), flat_history());
  TEST_ASSERT_EQ_INT(as_int(s.w), as_int(none.w));
  const sc::Text *a = find_text(s, "id_osc_sgpu_l");
  const sc::Text *b = find_text(none, "id_osc_sgpu_l");
  TEST_ASSERT_TRUE(a && b && as_int(a->x) == as_int(b->x));
  TEST_ASSERT_STREQ(kDash, text_of(none, "id_osc_scpu_v"));
  return 0;
}

/* Renderer cost with full charts. Each bar is a widget, so the panel is
 * dominated by its 130 bars plus the system row's 34. */
int test_widget_budget(void) {
  struct Case {
    sc::Detail detail;
    int max_widgets;
  } cases[] = {{sc::Detail::Minimal, 10},
               {sc::Detail::Compact, 100},
               {sc::Detail::Panel, 230},
               {sc::Detail::Full, 230}};
  for (const Case &c : cases) {
    sc::Options o = base_options(c.detail);
    o.brand = true;
    sc::Scene s = sc::build_scene(o, live_readings_with_sys(), flat_history());
    TEST_ASSERT_TRUE(!s.rects.empty());
    TEST_ASSERT_TRUE(ids_unique(s));
    if (widget_count(s) > c.max_widgets)
      return onion_test_fail(__FILE__, __LINE__, "level %d uses %d widgets > %d",
                             static_cast<int>(c.detail), widget_count(s),
                             c.max_widgets);
  }
  return 0;
}

int test_large_font_scales(void) {
  sc::Options o = base_options();
  sc::Scene medium = sc::build_scene(o, live_readings(), flat_history());
  o.font_pt = 24;
  sc::Scene large = sc::build_scene(o, live_readings(), flat_history());
  TEST_ASSERT_EQ_INT(300, as_int(medium.w));
  TEST_ASSERT_EQ_INT(400, as_int(large.w));
  TEST_ASSERT_TRUE(large.h > medium.h);
  TEST_ASSERT_TRUE(contained(large));
  TEST_ASSERT_TRUE(find_text(large, "id_osc_fps_v")->pt >
                   find_text(medium, "id_osc_fps_v")->pt);
  /* Small font still fits. */
  o.font_pt = 14;
  TEST_ASSERT_TRUE(contained(sc::build_scene(o, live_readings(), flat_history())));
  return 0;
}

int test_frame_count_window(void) {
  sc::FrameCountWindow w;
  const uint64_t t0 = 10000000000ull;
  TEST_ASSERT_TRUE(w.take(1, 1000000, t0) < 0.0f);
  TEST_ASSERT_EQ_INT(60, as_int(w.take(1, 1015000, t0 + 250000000ull)));
  TEST_ASSERT_TRUE(w.take(1, 1016000, t0 + 300000000ull) < 0.0f);
  TEST_ASSERT_EQ_INT(30, as_int(w.take(1, 1022500, t0 + 500000000ull)));
  TEST_ASSERT_EQ_INT(0, as_int(w.take(1, 1022500, t0 + 750000000ull)));
  TEST_ASSERT_TRUE(w.take(2, 500, t0 + 1000000000ull) < 0.0f);
  TEST_ASSERT_EQ_INT(40, as_int(w.take(2, 10500, t0 + 1250000000ull)));
  TEST_ASSERT_TRUE(w.take(2, 100, t0 + 1500000000ull) < 0.0f);
  w.reset();
  TEST_ASSERT_TRUE(w.take(2, 20000, t0 + 1750000000ull) < 0.0f);
  return 0;
}

int test_ring(void) {
  sc::Ring<8> h;
  TEST_ASSERT_EQ_INT(0, h.size());
  for (int i = 0; i < 8 + 5; ++i)
    h.push(static_cast<float>(i));
  h.push(std::numeric_limits<float>::quiet_NaN()); /* ignored */
  TEST_ASSERT_EQ_INT(8, h.size());
  TEST_ASSERT_EQ_INT(5, as_int(h.at(0)));
  TEST_ASSERT_EQ_INT(12, as_int(h.at(7)));
  h.clear();
  TEST_ASSERT_EQ_INT(0, h.size());
  return 0;
}

/* Daemon publishes every 1/30 s; at 60 fps that is 2 frames per span. */
constexpr uint64_t kTick = 33333333ull;

int test_timeline_steady(void) {
  sc::FrameTimeline tl;
  uint64_t t = 5000000000ull, frames = 0;
  TEST_ASSERT_EQ_INT(0, tl.push(1, frames, t)); /* first read: base only */
  TEST_ASSERT_TRUE(tl.rate(sc::kFpsWindowNs) < 0.0f);
  TEST_ASSERT_EQ_INT(0, tl.push(1, frames, t)); /* same read twice */
  for (int i = 0; i < 300; ++i) {
    t += kTick;
    frames += 2000;
    TEST_ASSERT_EQ_INT(1, tl.push(1, frames, t));
  }
  TEST_ASSERT_EQ_INT(60, as_int(tl.rate(sc::kFpsWindowNs)));
  TEST_ASSERT_EQ_INT(60, as_int(tl.rate(sc::kStatsWindowNs)));
  TEST_ASSERT_EQ_INT(17, as_int(tl.max_frame_ms(sc::kMaxWindowNs)));
  TEST_ASSERT_EQ_INT(60, as_int(tl.low_fps(sc::kStatsWindowNs)));
  TEST_ASSERT_EQ_INT(17, as_int(tl.at(tl.size() - 1).frame_ms));

  /* A new epoch (game restarted) re-bases instead of differencing. */
  TEST_ASSERT_EQ_INT(0, tl.push(2, 10, t + kTick));
  /* So does a count that went backwards. */
  TEST_ASSERT_EQ_INT(0, tl.push(2, 5, t + 2 * kTick));
  tl.reset();
  TEST_ASSERT_EQ_INT(0, tl.size());
  return 0;
}

int test_timeline_stall(void) {
  sc::FrameTimeline tl;
  uint64_t t = 5000000000ull, frames = 0;
  tl.push(1, frames, t);
  for (int i = 0; i < 60; ++i) {
    t += kTick;
    frames += 2000;
    tl.push(1, frames, t);
  }
  /* Three publishes with no new frame: a 100 ms freeze in progress. */
  for (int i = 0; i < 3; ++i) {
    t += kTick;
    TEST_ASSERT_EQ_INT(1, tl.push(1, frames, t));
  }
  TEST_ASSERT_EQ_INT(100, as_int(tl.max_frame_ms(sc::kMaxWindowNs)));
  /* FPS over 0.5 s drops: 24 frames in 15 spans, 48 fps. */
  TEST_ASSERT_TRUE(tl.rate(sc::kFpsWindowNs) < 55.0f);

  /* The freeze ends: the first frame carries it, the second is normal. */
  t += kTick;
  frames += 2000;
  TEST_ASSERT_EQ_INT(2, tl.push(1, frames, t));
  const sc::FrameTimeline::Span &spike = tl.at(tl.size() - 2);
  const sc::FrameTimeline::Span &after = tl.at(tl.size() - 1);
  TEST_ASSERT_EQ_INT(117, as_int(spike.frame_ms)); /* 100 + 16.7 */
  TEST_ASSERT_EQ_INT(1, as_int(spike.frames));
  TEST_ASSERT_EQ_INT(17, as_int(after.frame_ms));
  TEST_ASSERT_EQ_INT(117, as_int(tl.max_frame_ms(sc::kMaxWindowNs)));
  /* Time is conserved: the window still sums to real elapsed time. */
  TEST_ASSERT_EQ_INT(as_int(static_cast<float>(after.end_ns - spike.end_ns)),
                     as_int(static_cast<float>(after.dur_ns)));
  return 0;
}

int test_timeline_low(void) {
  /* 10 s at 60 fps with one 117 ms hitch: under 1% of frames, LOW stays at
   * the steady rate. */
  sc::FrameTimeline tl;
  uint64_t t = 5000000000ull, frames = 0;
  tl.push(1, frames, t);
  auto steady = [&](int n) {
    for (int i = 0; i < n; ++i) {
      t += kTick;
      frames += 2000;
      tl.push(1, frames, t);
    }
  };
  auto hitch = [&]() {
    for (int i = 0; i < 3; ++i) {
      t += kTick;
      tl.push(1, frames, t);
    }
  };
  steady(150);
  hitch();
  steady(150);
  TEST_ASSERT_EQ_INT(60, as_int(tl.low_fps(sc::kStatsWindowNs)));
  /* A hitch every second: 10 slow frames of ~600 is over 1%, so LOW drops
   * to the hitch rate (1000 / 116.7 ≈ 8.6). */
  for (int i = 0; i < 10; ++i) {
    hitch();
    steady(27);
  }
  TEST_ASSERT_EQ_INT(9, as_int(tl.low_fps(sc::kStatsWindowNs)));
  TEST_ASSERT_TRUE(tl.rate(sc::kStatsWindowNs) < 60.0f);
  return 0;
}

} // namespace

extern "C" int test_overlay_scene_suite(void) {
  int failures = 0;
  failures += onion_test_run("overlay_scene.empty", test_empty_without_screen);
  failures += onion_test_run("overlay_scene.placement", test_corner_placement_and_margin);
  failures += onion_test_run("overlay_scene.opacity", test_opacity);
  failures += onion_test_run("overlay_scene.panel_values", test_panel_values);
  failures += onion_test_run("overlay_scene.low_label", test_low_label);
  failures += onion_test_run("overlay_scene.brand", test_brand_header);
  failures += onion_test_run("overlay_scene.no_game", test_no_game);
  failures += onion_test_run("overlay_scene.non_finite", test_non_finite);
  failures += onion_test_run("overlay_scene.fps_colors", test_fps_state_colors);
  failures += onion_test_run("overlay_scene.fps_chart", test_fps_chart);
  failures += onion_test_run("overlay_scene.frame_chart", test_frame_time_chart);
  failures += onion_test_run("overlay_scene.compact", test_compact_strip);
  failures += onion_test_run("overlay_scene.minimal", test_minimal_pill);
  failures += onion_test_run("overlay_scene.fps_pill", test_companion_fps_pill);
  failures += onion_test_run("overlay_scene.full_is_panel", test_full_matches_panel);
  failures += onion_test_run("overlay_scene.target", test_target_inference);
  failures += onion_test_run("overlay_scene.format", test_formatting);
  failures += onion_test_run("overlay_scene.system_panel", test_system_row_panel);
  failures += onion_test_run("overlay_scene.system_colors", test_system_row_colors);
  failures += onion_test_run("overlay_scene.system_invalid", test_system_row_invalid);
  failures += onion_test_run("overlay_scene.system_toggle", test_system_row_no_game_and_toggle);
  failures += onion_test_run("overlay_scene.system_compact", test_system_row_compact);
  failures += onion_test_run("overlay_scene.budget", test_widget_budget);
  failures += onion_test_run("overlay_scene.frame_graph", test_frame_graph);
  failures += onion_test_run("overlay_scene.large_font", test_large_font_scales);
  failures += onion_test_run("overlay_scene.frame_window", test_frame_count_window);
  failures += onion_test_run("overlay_scene.ring", test_ring);
  failures += onion_test_run("overlay_scene.timeline_steady", test_timeline_steady);
  failures += onion_test_run("overlay_scene.timeline_stall", test_timeline_stall);
  failures += onion_test_run("overlay_scene.timeline_low", test_timeline_low);
  return failures;
}
