/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * fps-monitor game overlay: onionHEN 0.9.0's scene overlay (minimal pill,
 * compact line, corner panel, full panel) without the legacy edge bar or the
 * HyperHEN Toolbox polls. HyperHEN's own shellui keeps prx_overlay.cpp.
 */

#include "hooked_funcs.hpp"
#include "ipc.hpp"
#include "external_symbols.hpp"
#include "overlay_scene.hpp"
#include "overlay_scene_renderer.hpp"
#include <onion/fps_sample.h>
#include <onion/settings.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

extern MonoObject* rootWidget;
extern OverlayLayout g_overlay_layout;
extern onion::Settings g_settings;
#include "shellui_state.hpp"

struct OrbisKernelTimespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

struct Proc_Stats
{
    int32_t lo_data;								//0x00
    uint32_t td_tid;						//0x04
    OrbisKernelTimespec user_cpu_usage_time;	//0x08
    OrbisKernelTimespec system_cpu_usage_time;  //0x18
}; //0x28

extern "C" {
    int sceKernelGetSocSensorTemperature(int sensorId, int* soctime);
    int get_page_table_stats(int vm, int type, int* total, int* free);
    int sceKernelGetCpuUsage(struct Proc_Stats* out, int32_t* size);
    int sceKernelGetThreadName(uint32_t id, char* out);
	int sceKernelGetCpuTemperature(int* cputemp);
    int sceKernelClockGettime(int clockId, OrbisKernelTimespec* tp);
}

struct thread_usages
{
    OrbisKernelTimespec current_time;	//0x00
    int Thread_Count;					//0x10
    char padding0[0x4];					//0x14
    Proc_Stats Threads[3072];			//0x18
};

int Thread_Count = 0;
float Usage[8] = { 0 };

Proc_Stats Stat_Data[3072];
thread_usages gThread_Data[2];


bool Get_Page_Table_Stats(int vm, int type, int* Used, int* Free, int* Total)
{
    int _Total = 0, _Free = 0;

    if (get_page_table_stats(vm, type, &_Total, &_Free) == -1) {
        LOG_ERROR("get_page_table_stats() Failed.");
        return false;
    }

    if (Used)
        *Used = (_Total - _Free);

    if (Free)
        *Free = _Free;

    if (Total)
        *Total = _Total;
    return true;
}

void calc_usage(unsigned int idle_tid[8], thread_usages* cur, thread_usages* prev, float usage_out[8])
{
    if (cur->Thread_Count <= 0 || prev->Thread_Count <= 0) //Make sure our banks have threads
        return;

    //Calculate the Current time difference from the last bank to the current bank.
    float Current_Time_Total = ((prev->current_time.tv_sec + (prev->current_time.tv_nsec / 1000000000.0f)) - (cur->current_time.tv_sec + (cur->current_time.tv_nsec / 1000000000.0f)));

    //Here this could use to be improved but essetially what its doing is finding the thread information for the idle threads using their thread Index stored from before.
    struct Data_s
    {
        Proc_Stats* Cur;
        Proc_Stats* Prev;
    }Data[8] = {};

    for (int i = 0; i < cur->Thread_Count; i++)
    {
        for (int j = 0; j < 8; j++)
        {
            if (idle_tid[j] == cur->Threads[i].td_tid)
                Data[j].Cur = &cur->Threads[i];
        }
    }

    for (int i = 0; i < prev->Thread_Count; i++)
    {
        for (int j = 0; j < 8; j++)
        {
            if (idle_tid[j] == prev->Threads[i].td_tid)
                Data[j].Prev = &prev->Threads[i];
        }
    }

    //Here we loop through each core to calculate the total usage time as its split into user/sustem
    for (int i = 0; i < 8; i++)
    {
        //An idle thread missing from either bank leaves its core at the last value.
        if (!Data[i].Cur || !Data[i].Prev)
            continue;

        float Prev_Usage_Time = (Data[i].Prev->system_cpu_usage_time.tv_sec + (Data[i].Prev->system_cpu_usage_time.tv_nsec / 1000000.0f));
        Prev_Usage_Time += (Data[i].Prev->user_cpu_usage_time.tv_sec + (Data[i].Prev->user_cpu_usage_time.tv_nsec / 1000000.0f));

        float Cur_Usage_Time = (Data[i].Cur->system_cpu_usage_time.tv_sec + (Data[i].Cur->system_cpu_usage_time.tv_nsec / 1000000.0f));
        Cur_Usage_Time += (Data[i].Cur->user_cpu_usage_time.tv_sec + (Data[i].Cur->user_cpu_usage_time.tv_nsec / 1000000.0f));

        //We calculate the usage using usage time difference between the two samples divided by the current time difference.
        float Idle_Usage = ((Prev_Usage_Time - Cur_Usage_Time) / Current_Time_Total);

        if (Idle_Usage > 1.0f)
            Idle_Usage = 1.0f;

        if (Idle_Usage < 0.0f)
            Idle_Usage = 0.0f;

        //Get inverse of idle percentage and express in percent.
        usage_out[i] = (1.0f - Idle_Usage) * 100.0f;
    }
}

namespace {

constexpr int kMaxProcThreads = 3072;
constexpr int kCpuCores = 8;
constexpr int kOverlayInitDelayFrames = 30;
constexpr int kFpsUpdateIntervalFrames = 2;

constexpr int kSceneUpdateIntervalFrames = 4;
/* CPU / GPU / MEM row: about twice a second at 60 Hz. */
constexpr int kSystemUpdateIntervalFrames = 30;

constexpr int kClockIdRealtime = 4;
constexpr int kVmSystem = 1;
constexpr int kPageTableRam = 1;

namespace scene = onion::overlay::scene;

/* Latest numeric readings and the frame-time history the scene draws. */
scene::Readings g_readings;
scene::Histories g_history;
/* Frame spans from the daemon's running frame count: FPS, AVG, LOW, frame
 * times. */
scene::FrameTimeline g_timeline;
/* 20 buckets × 250 ms: a new frame cap must hold ~5 s before it sticks. */
scene::TargetTracker g_fps_target{20};
/* Exact per-bucket rate from the daemon's running frame count. */
scene::FrameCountWindow g_frame_window;
/* Older daemons publish only a smoothed rate: integrate it into a running
 * count of our own so the same timeline serves both. */
uint64_t g_synth_frames_milli = 0;
uint64_t g_synth_ns = 0;
/* The game the FPS stats belong to (title + pid, so a relaunch starts over). */
std::string g_fps_title;
int32_t g_fps_pid = 0;
bool g_fps_tracking = false;
int64_t g_fps_start_s = 0;
int64_t g_fps_last_valid_s = 0;
int64_t g_fps_last_bucket_ms = 0;
/* Daemon publish counter of the last sample folded into the stats; the
 * daemon and this loop both run near 30 Hz, so the same sample can be read
 * twice or skipped, and only a new seq is a new measurement. */
uint32_t g_fps_last_seq = 0;

/* A dropped sample (loading screen, brief stall) keeps the last reading this
 * long before the FPS section collapses to "NO GAME DETECTED". */
constexpr int64_t kFpsGraceSeconds = 2;
/* After this long without frames the session is over; the same title coming
 * back counts as a new session. */
constexpr int64_t kFpsSessionEndSeconds = 10;

bool detail_is(int detail) { return g_settings.overlay_detail == detail; }

/* The overlay is performance-only: FPS whenever it is on. */
bool wants_fps() { return g_settings.overlay_enabled; }
/* The scene overlay's CPU / GPU / MEM row (compact, panel, full). */
bool wants_system() {
  return g_settings.overlay_enabled && g_settings.overlay_system &&
         !detail_is(onion::kOverlayDetailMinimal);
}

scene::Detail scene_detail() {
  switch (g_settings.overlay_detail) {
  case onion::kOverlayDetailMinimal:
    return scene::Detail::Minimal;
  case onion::kOverlayDetailPanel:
    return scene::Detail::Panel;
  case onion::kOverlayDetailFull:
    return scene::Detail::Full;
  default:
    return scene::Detail::Compact;
  }
}

/* Clock id 4 is CLOCK_MONOTONIC on the PS5 (FreeBSD numbering). */
int64_t monotonic_seconds() {
  OrbisKernelTimespec ts{};
  sceKernelClockGettime(kClockIdRealtime, &ts);
  return ts.tv_sec;
}

int64_t monotonic_ms() {
  OrbisKernelTimespec ts{};
  sceKernelClockGettime(kClockIdRealtime, &ts);
  return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

uint64_t monotonic_ns() {
  OrbisKernelTimespec ts{};
  sceKernelClockGettime(kClockIdRealtime, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
         static_cast<uint64_t>(ts.tv_nsec);
}

void discover_idle_thread_ids(unsigned int idle_tid[kCpuCores]) {
  int count = kMaxProcThreads;
  if (sceKernelGetCpuUsage(reinterpret_cast<Proc_Stats*>(&Stat_Data), &count) ||
      count <= 0)
    return;

  char thread_name[0x40];
  int core = 0;
  for (int i = 0; i < count; i++) {
    if (!sceKernelGetThreadName(Stat_Data[i].td_tid, thread_name) &&
        sscanf(thread_name, "SceIdleCpu%d", &core) == 1 && core >= 0 &&
        core < kCpuCores) {
      idle_tid[core] = Stat_Data[i].td_tid;
    }
  }
}

bool init_overlay_once(unsigned int idle_tid[kCpuCores]) {
  rootWidget = Get_Property<MonoObject*>(pui_img, "Sce.PlayStation.PUI.UI2", "Scene",
                                         Game, "RootWidget");
  if (!rootWidget)
    return false;

  float screen_w = 0.0f;
  float screen_h = 0.0f;
  if (!resolve_root_dimensions(rootWidget, &screen_w, &screen_h))
    return false;

  apply_overlay_layout(screen_w, screen_h);
  discover_idle_thread_ids(idle_tid);
  if (!g_settings.overlay_enabled)
    return true;

  /* Widgets are created on the first scene_render(). */
  overlay_scene_remove_all();
  return true;
}

/**
 * Dual-bank CPU sample into the global Usage[]: the first successful
 * GetCpuUsage seeds the other bank, the next one can compute a delta. Bound
 * retries so a stuck/failing probe cannot block the rest of the render
 * tick. False when no delta was available (a core whose idle thread is
 * missing keeps its previous value, see calc_usage).
 */
bool sample_cpu_cores(unsigned int idle_tid[kCpuCores], int& current_bank) {
  constexpr int kMaxAttempts = 4;
  for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
    gThread_Data[current_bank].Thread_Count = kMaxProcThreads;
    if (sceKernelGetCpuUsage(
            reinterpret_cast<Proc_Stats*>(&gThread_Data[current_bank].Threads),
            &gThread_Data[current_bank].Thread_Count))
      continue;

    Thread_Count = gThread_Data[current_bank].Thread_Count;
    sceKernelClockGettime(kClockIdRealtime, &gThread_Data[current_bank].current_time);
    current_bank = !current_bank;

    if (gThread_Data[current_bank].Thread_Count <= 0)
      continue;

    calc_usage(idle_tid, &gThread_Data[!current_bank], &gThread_Data[current_bank],
               Usage);
    return true;
  }
  return false;
}

/** Read the daemon's FPS sample into the frame timeline, then derive FPS,
 *  frame time, AVG, LOW and both chart histories from it. */
void sample_fps() {
  scene::Readings &r = g_readings;
  const int64_t now = monotonic_seconds();
  OnionFpsSample sample{};
  if (!wants_fps() || onion_fps_read(&sample) != 0 || !sample.valid ||
      sample.fps < 1.f) {
    if (g_fps_tracking && now - g_fps_last_valid_s > kFpsSessionEndSeconds)
      g_fps_tracking = false;
    if (r.fps_valid && now - g_fps_last_valid_s <= kFpsGraceSeconds)
      return; /* hold the last reading through a short gap */
    r.fps_valid = false;
    r.title_id.clear();
    return;
  }

  const std::string title(sample.title_id,
                          strnlen(sample.title_id, sizeof(sample.title_id)));
  if (!g_fps_tracking || title != g_fps_title || sample.pid != g_fps_pid) {
    /* New game or relaunch: every window, chart and the target restart. */
    g_fps_title = title;
    g_fps_pid = sample.pid;
    g_fps_tracking = true;
    g_fps_start_s = now;
    g_timeline.reset();
    g_history = scene::Histories{};
    g_fps_target.reset();
    g_frame_window.reset();
    g_fps_last_bucket_ms = 0;
    g_synth_ns = 0;
    r.fps = sample.fps;
    r.frame_ms = 1000.0f / sample.fps;
    r.frame_ms_max = -1.0f;
    r.fps_avg = sample.fps;
    r.fps_low = sample.fps;
    r.fps_target = scene::infer_target(sample.fps);
  }
  g_fps_last_valid_s = now;

  /* Daemons that publish the running frame count feed the timeline
   * directly; for older ones, integrate the smoothed rate per new publish. */
  const bool frame_count = sample.ext_layout == ONION_FPS_EXT_FRAMES;
  uint32_t epoch = sample.ext_epoch;
  uint64_t frames_milli = sample.frames_milli;
  uint64_t mono_ns = sample.frames_mono_ns;
  if (!frame_count) {
    if (sample.seq == g_fps_last_seq && g_synth_ns != 0)
      return;
    g_fps_last_seq = sample.seq;
    const uint64_t t = monotonic_ns();
    if (g_synth_ns != 0 && t > g_synth_ns)
      g_synth_frames_milli += static_cast<uint64_t>(
          static_cast<double>(sample.fps) * static_cast<double>(t - g_synth_ns) /
          1e6);
    g_synth_ns = t;
    epoch = 0;
    frames_milli = g_synth_frames_milli;
    mono_ns = t;
  }

  const int added = g_timeline.push(epoch, frames_milli, mono_ns);
  for (int i = g_timeline.size() - added; i < g_timeline.size(); ++i) {
    const scene::FrameTimeline::Span &s = g_timeline.at(i);
    if (s.frames > 0.0f) {
      g_history.frame_ms.push(s.frame_ms);
      g_history.long_ms.push(s.frame_ms);
    }
  }

  const float fps = g_timeline.rate(scene::kFpsWindowNs);
  if (fps > 0.0f) {
    r.fps = fps;
    r.frame_ms = 1000.0f / fps;
  }
  r.frame_ms_max = g_timeline.max_frame_ms(scene::kMaxWindowNs);
  r.fps_valid = r.fps >= 1.0f;
  r.title_id = title;
  /* Spans average ~33 ms of frames, so LOW is not a per-frame 1% low. */
  r.low_per_frame = false;

  const int64_t now_ms = monotonic_ms();
  if (now_ms - g_fps_last_bucket_ms >= scene::kBucketMs) {
    g_fps_last_bucket_ms = now_ms;
    float bucket_fps = sample.fps;
    if (frame_count) {
      const float exact = g_frame_window.take(
          sample.ext_epoch, sample.frames_milli, sample.frames_mono_ns);
      /* No usable window yet: skip the bucket rather than invent one. */
      bucket_fps = exact >= 0.0f ? exact : -1.0f;
    }
    if (bucket_fps >= 0.0f)
      g_history.fps.push(bucket_fps);
    /* Window stats at bucket rate, not every read (sorting). */
    const float avg = g_timeline.rate(scene::kStatsWindowNs);
    const float low = g_timeline.low_fps(scene::kStatsWindowNs);
    if (avg > 0.0f)
      r.fps_avg = avg;
    if (low > 0.0f)
      r.fps_low = low;
    r.fps_target = g_fps_target.update(r.fps_avg);
  }
}

/**
 * CPU load and temperature, SoC temperature, GPU core power and system
 * memory into g_readings.sys. Each source is independent: one that fails
 * renders as a dash while the others keep updating.
 */
void sample_system(unsigned int idle_tid[kCpuCores], int& current_bank) {
  scene::SystemReadings &s = g_readings.sys;

  /* The first success spans the microseconds between the two seed
   * snapshots, so it is dropped. Two failures in a row blank the column
   * rather than leave a stale number that looks live. */
  static bool cpu_seeded = false;
  static int cpu_failures = 0;
  bool have_idle = false;
  for (int i = 0; i < kCpuCores; ++i)
    have_idle = have_idle || idle_tid[i] != 0;
  if (have_idle && sample_cpu_cores(idle_tid, current_bank)) {
    cpu_failures = 0;
    if (cpu_seeded) {
      float sum = 0.0f;
      for (int i = 0; i < kCpuCores; ++i) {
        s.cpu_core[i] = Usage[i];
        sum += Usage[i];
      }
      s.cpu_pct = sum / static_cast<float>(kCpuCores);
      s.cpu_valid = std::isfinite(s.cpu_pct);
    }
    cpu_seeded = true;
  } else if (++cpu_failures >= 2) {
    s.cpu_valid = false;
  }

  int t = 0;
  s.cpu_temp_valid = sceKernelGetCpuTemperature(&t) == 0 && t > 0 && t < 150;
  if (s.cpu_temp_valid)
    s.cpu_temp = t;
  t = 0;
  s.soc_temp_valid =
      sceKernelGetSocSensorTemperature(0, &t) == 0 && t > 0 && t < 150;
  if (s.soc_temp_valid)
    s.soc_temp = t;

  int used = 0, free_mb = 0, total = 0;
  s.ram_valid = Get_Page_Table_Stats(kVmSystem, kPageTableRam, &used, &free_mb,
                                     &total) &&
                total > 0;
  if (s.ram_valid) {
    s.ram_used_mb = used;
    s.ram_total_mb = total;
  }

  /* 0x70 bytes of u32: eight rails × (power mW, voltage mV, current mA);
   * rail 0 is the GPU core. Decoding follows ps5-hwinfo, so the raw words
   * are logged once for a check on the console. */
  static bool power_logged = false;
  constexpr int kPowerWords = 0x70 / 4;
  /* The record size is community-sourced; the buffer is oversized so a
   * larger firmware record cannot overrun the shell's stack. */
  uint32_t raw[0x200 / 4] = {};
  s.power_valid = false;
  if (sceKernelGetSocPowerConsumption) {
    const int rc = sceKernelGetSocPowerConsumption(raw);
    if (rc == 0) {
      if (!power_logged) {
        power_logged = true;
        char line[kPowerWords * 11 + 1];
        size_t n = 0;
        for (int i = 0; i < kPowerWords && n < sizeof(line); ++i)
          n += static_cast<size_t>(snprintf(line + n, sizeof(line) - n, "%s%u",
                                            i ? " " : "", raw[i]));
        LOG_INFO("soc power raw[%d]: %s", kPowerWords, line);
      }
      s.gpu_power_w = static_cast<float>(raw[0]) / 1000.0f;
      s.power_valid = raw[0] > 0;
    } else if (!power_logged) {
      power_logged = true;
      LOG_INFO("sceKernelGetSocPowerConsumption failed: 0x%x", rc);
    }
  } else if (!power_logged) {
    power_logged = true;
    LOG_INFO("sceKernelGetSocPowerConsumption not resolved; GPU power hidden");
  }
}

void scene_render() {
  scene::Readings &r = g_readings;
  scene::Options o{};
  o.screen_w = g_overlay_layout.screen_w;
  o.screen_h = g_overlay_layout.screen_h;
  o.edge = onion::overlay::bar_edge_from_pos(g_settings.overlay_pos);
  o.align = onion::overlay::bar_align_from_value(g_settings.overlay_align);
  o.detail = scene_detail();
  o.font_pt = onion::overlay_font_pt(g_settings.overlay_font_size);
  o.opacity = g_settings.overlay_opacity;
  o.brand = g_settings.overlay_brand;
  o.fps_pill = g_settings.overlay_fps_pill;
  o.show_system = g_settings.overlay_system;
  o.frame_graph = g_settings.overlay_frame_graph;
  o.margin = static_cast<float>(g_settings.overlay_margin);
  overlay_scene_apply(scene::build_scene(o, r, g_history));
}

} // namespace

void overlay_scene_reset_state() {
  g_history = scene::Histories{};
  g_timeline.reset();
  g_fps_target.reset();
  g_frame_window.reset();
  g_fps_tracking = false;
  g_fps_last_bucket_ms = 0;
  g_synth_ns = 0;
  g_readings.fps_valid = false;
  g_readings.sys = scene::SystemReadings{};
}

void OnRender_Hook(MonoObject* instance) {
  if (!shellui_hooks_are_ready()) {
    if (OnRender_orig)
      OnRender_orig(instance);
    return;
  }

  static bool inited = false;
  static unsigned int idle_thread_id[kCpuCores] = {};
  static int current_bank = 0;
  static int frames_until_fps = 0;
  static int frames_until_scene = 0;
  static int frames_until_system = 0;
  static int frames_until_init = kOverlayInitDelayFrames;

  if (!inited) {
    if (frames_until_init > 0) {
      --frames_until_init;
    } else if (init_overlay_once(idle_thread_id)) {
      inited = true;
    }
    if (!inited) {
      OnRender_orig(instance);
      return;
    }
  }

  if (frames_until_system <= 0) {
    if (wants_system())
      sample_system(idle_thread_id, current_bank);
    else if (g_readings.sys.cpu_valid || g_readings.sys.ram_valid ||
             g_readings.sys.cpu_temp_valid || g_readings.sys.soc_temp_valid)
      g_readings.sys = scene::SystemReadings{};
    frames_until_system = kSystemUpdateIntervalFrames;
  } else {
    frames_until_system--;
  }
  if (frames_until_fps <= 0) {
    sample_fps();
    frames_until_fps = kFpsUpdateIntervalFrames;
  } else {
    frames_until_fps--;
  }
  if (frames_until_scene <= 0) {
    if (g_settings.overlay_enabled)
      scene_render();
    else
      overlay_scene_remove_all();
    frames_until_scene = kSceneUpdateIntervalFrames;
  } else {
    frames_until_scene--;
  }

  OnRender_orig(instance);
}
