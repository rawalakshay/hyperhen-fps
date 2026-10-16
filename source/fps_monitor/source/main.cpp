/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Standalone FPS monitor host. Runs the daemon FPS sampler (daemon_fps.cpp)
 * and keeps the overlay-only ShellUI ELF injected across rest mode and
 * ShellUI restarts. No Toolbox, IPC, plugins or cheats.
 */
#include "daemon_ops.hpp"
#include "daemon_power_state.hpp"
#include "globalconf.hpp"
#include "toolbox_injection.hpp"

#include <onion/elfldr.h>
#include <onion/log.h>
#include <onion/notify.h>
#include <onion/proc_query.h>
#include <onion/ready.h>
#include <onion/settings.hpp>

#include <ps5/kernel.h>

#include <atomic>
#include <cstdint>
#include <ctime>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

typedef struct app_info {
  uint32_t app_id;
  uint64_t unknown1;
  uint32_t app_type;
  char title_id[10];
  char unknown2[0x3c];
} app_info_t;

extern "C" {
#include <ps5/payload.h>

bool Inject_Toolbox(int pid, uint8_t *elf);
extern uint8_t monitor_shellui_elf_start[];

int sceKernelSendNotificationRequest(int32_t device, void *req, size_t size,
                                     int32_t blocking);
int sceKernelGetProcessName(int pid, char *name);
int sceKernelGetAppInfo(int pid, app_info_t *info);
int sceSystemServiceGetAppIdOfRunningBigApp(void);
}

/* Symbols daemon_fps.cpp and libhijacker expect from their host process. */
onion::SettingsStore g_settings;
std::atomic_bool g_stack_shutting_down{false};
uintptr_t kernel_base = 0;

namespace {

constexpr const char *kLogDir = "/data/fps-monitor";
constexpr const char *kLogPath = "/data/fps-monitor/fps-monitor.log";
constexpr const char *kShellUiReadySprx[] = {
    "libSceNpTrophy.sprx",
    "libSceNpTrophy2.sprx",
};
constexpr int kSprxPollMax = 60;
constexpr useconds_t kSprxPollUs = 500 * 1000;
constexpr int kInjectTimeoutMs = 45 * 1000;
constexpr int kInjectPollMs = 250;

time_t g_config_mtime = 0;
bool g_config_loaded = false;

/* The monitor is the FPS bar: keep it on, and never show IP / fan. */
void apply_monitor_policy(onion::Settings &s) {
  s.overlay_enabled = true;
  s.overlay_fps = true;
  s.overlay_ip = false;
  s.overlay_fan = false;
}

bool alive_marker(const char *name) {
  pid_t pid = -1;
  return onion_ready_read_pid(name, &pid) && pid > 1 && pid != getpid() &&
         onion_proc_is_alive(pid);
}

bool hyperhen_running() {
  const pid_t shellui = onion_find_pid("SceShellUI");
  return alive_marker(ONION_READY_DAEMON) ||
         (shellui > 1 && onion_ready_matches_pid(ONION_READY_TOOLBOX, shellui));
}

bool sprx_loaded(pid_t pid, const char *name) {
  uint32_t handle = static_cast<uint32_t>(-1);
  return kernel_dynlib_handle(pid, name, &handle) == 0 &&
         handle != static_cast<uint32_t>(-1);
}

/* ShellUI accepts an ELF only after its trophy SPRX are mapped
 * (same gate as daemon_inject.cpp toolbox_wait_shellui_sprx). */
bool wait_shellui_ready(pid_t pid) {
  for (const char *name : kShellUiReadySprx) {
    int i = 0;
    for (; i < kSprxPollMax && !sprx_loaded(pid, name); ++i) {
      if (!onion_proc_is_alive(pid))
        return false;
      usleep(kSprxPollUs);
    }
    if (i == kSprxPollMax) {
      LOG_WARN("fps-monitor: %s missing in SceShellUI pid=%d", name,
               static_cast<int>(pid));
      return false;
    }
  }
  return true;
}

void ensure_overlay(onion::ToolboxInject &injector) {
  if (daemon_power_state_is_sleeping())
    return;
  const pid_t pid = onion_find_pid("SceShellUI");
  if (pid <= 1 || injector.is_ready_for(pid))
    return;
  if (!wait_shellui_ready(pid))
    return;

  LOG_INFO("fps-monitor: injecting overlay into SceShellUI pid=%d",
           static_cast<int>(pid));
  const onion::ToolboxInjectionOutcome outcome = injector.inject(
      []() -> pid_t { return onion_find_pid("SceShellUI"); },
      [](pid_t target) -> bool {
        return Inject_Toolbox(static_cast<int>(target),
                              monitor_shellui_elf_start);
      },
      kInjectTimeoutMs, kInjectPollMs, pid);
  if (outcome.ready()) {
    LOG_INFO("fps-monitor: overlay online pid=%d",
             static_cast<int>(outcome.pid));
    return;
  }
  LOG_ERROR("fps-monitor: overlay inject failed pid=%d result=%d",
            static_cast<int>(outcome.pid), static_cast<int>(outcome.result));
  onion_notify(0, "[FPS Monitor] overlay failed to load");
}

} // namespace

/* daemon_fps.cpp polls this once per second; re-read only when the file
 * changed. Unlike the daemon, never create a default config. */
bool LoadSettings(bool force) {
  if (!force && g_config_loaded &&
      !onion::settings_config_is_newer_than(g_config_mtime))
    return true;
  onion::Settings s{};
  (void)onion::settings_load(&s); // false = file missing, defaults kept
  apply_monitor_policy(s);
  g_settings.store(s);
  g_config_mtime = onion::settings_config_newest_mtime();
  g_config_loaded = true;
  return true;
}

int main() {
  (void)syscall(SYS_thr_set_name, -1, "fps-monitor.elf");
  payload_args_t *args = payload_get_args();
  if (args)
    kernel_base = args->kdata_base_addr;
  (void)elfldr_raise_privileges(getpid());

  mkdir(kLogDir, 0777);
  onion_log_configure("FPSMon", kLogPath);
  onion_notify_set_send(reinterpret_cast<onion_notify_send_fn>(
      sceKernelSendNotificationRequest));

  if (alive_marker(ONION_READY_FPS_MONITOR)) {
    LOG_WARN("fps-monitor: already running; exiting");
    onion_notify(0, "[FPS Monitor] already running");
    return 0;
  }
  if (hyperhen_running()) {
    LOG_WARN("fps-monitor: HyperHEN is running; its overlay owns the bar");
    onion_notify(0, "[FPS Monitor] HyperHEN is running, use its monitor bar");
    return 0;
  }
  onion_ready_signal_pid(ONION_READY_FPS_MONITOR, getpid());

  (void)LoadSettings(true);
  onion_proc_set_sce_hooks(
      [](int pid, char *name) -> int {
        return sceKernelGetProcessName(pid, name);
      },
      [](pid_t pid, void *info) -> int {
        return sceKernelGetAppInfo(pid, static_cast<app_info_t *>(info));
      },
      []() -> int { return sceSystemServiceGetAppIdOfRunningBigApp(); });

  pthread_t fps_thr = nullptr;
  pthread_t vsync_thr = nullptr;
  pthread_create(&fps_thr, nullptr, fps_sampler_thread, nullptr);
  pthread_create(&vsync_thr, nullptr, vsync_fps_sampler_thread, nullptr);

  LOG_INFO("fps-monitor: started pid=%d", static_cast<int>(getpid()));
  onion_notify(0, "[FPS Monitor] started");

  onion::ToolboxInject injector(ONION_READY_FPS_MONITOR_UI);
  while (!g_stack_shutting_down.load(std::memory_order_acquire)) {
    ensure_overlay(injector);
    sleep(1);
  }
  return 0;
}
