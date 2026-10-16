/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Overlay-only ShellUI module for the standalone FPS monitor.
 * Trimmed boot of shellui/src/prx.cpp: resolve what the monitor bar needs,
 * hook PUI Application.Update, then keep the module alive. Drawing and
 * sampling live in the shared shellui/src/prx_overlay.cpp.
 */
#include "defs.h"
#include "detour.h"
#include "external_symbols.hpp"
#include "hook_lifecycle.hpp"
#include "hooked_funcs.hpp"
#include "proc.h"
#include "ps5/kernel.h"
#include "ucred.h"
#include <onion/log.h>
#include <onion/ready.h>
#include <onion/settings.hpp>

#include <cstdint>
#include <unistd.h>

/* Globals the shared overlay files expect (defined by prx.cpp in HyperHEN). */
onion::Settings g_settings;
OverlayLayout g_overlay_layout;
MonoImage *pui_img = nullptr;
MonoObject *Game = nullptr;
MonoObject *rootWidget = nullptr;
MonoObject *font = nullptr;
bool has_hv_bypass = false;
void (*OnRender_orig)(MonoObject *instance);

void OnRender_Hook(MonoObject *instance);

namespace {

constexpr size_t kMprotectProbeSize = 100;
constexpr int kProtRwx = 0x7; // PROT_READ | PROT_WRITE | PROT_EXEC
constexpr unsigned kKeepAliveSleepSec = 0x100000;
bool hooked = false;

struct AuthIdGuard {
  pid_t pid;
  uintptr_t old_authid;
  ~AuthIdGuard() { set_proc_authid(pid, old_authid); }
};

bool resolve_symbols(pid_t pid) {
  const int libkernelsys = get_module_handle(pid, "libkernel_sys.sprx");
  KERNEL_DLSYM(libkernelsys, sceKernelMprotect);

  const int libmono = get_module_handle(pid, "libmonosgen-2.0.sprx");
  KERNEL_DLSYM(libmono, mono_get_root_domain);
  KERNEL_DLSYM(libmono, mono_thread_attach);
  KERNEL_DLSYM(libmono, mono_domain_assembly_open);
  KERNEL_DLSYM(libmono, mono_assembly_get_image);
  KERNEL_DLSYM(libmono, mono_class_from_name);
  KERNEL_DLSYM(libmono, mono_class_get_method_from_name);
  KERNEL_DLSYM(libmono, mono_compile_method);
  KERNEL_DLSYM(libmono, mono_runtime_invoke);
  KERNEL_DLSYM(libmono, mono_string_new);
  KERNEL_DLSYM(libmono, mono_domain_get);
  KERNEL_DLSYM(libmono, mono_class_get_property_from_name);
  KERNEL_DLSYM(libmono, mono_property_get_get_method);
  KERNEL_DLSYM(libmono, mono_property_get_set_method);
  KERNEL_DLSYM(libmono, mono_object_new);
  KERNEL_DLSYM(libmono, mono_object_unbox);
  KERNEL_DLSYM(libmono, mono_runtime_object_init);
  const bool ok =
      sceKernelMprotect && mono_get_root_domain && mono_thread_attach &&
      mono_domain_assembly_open && mono_assembly_get_image &&
      mono_class_from_name && mono_class_get_method_from_name &&
      mono_compile_method && mono_runtime_invoke && mono_string_new &&
      mono_domain_get && mono_class_get_property_from_name &&
      mono_property_get_get_method && mono_property_get_set_method &&
      mono_object_new && mono_object_unbox && mono_runtime_object_init;
  if (!ok)
    LOG_ERROR("fps-monitor: symbol resolution failed");
  return ok;
}

/* Same policy as the host: always the FPS bar, never IP / fan. */
void load_settings() {
  onion::Settings s{};
  (void)onion::settings_load(&s); // false = file missing, defaults kept
  s.overlay_enabled = true;
  s.overlay_fps = true;
  s.overlay_ip = false;
  s.overlay_fan = false;
  g_settings = s;
  apply_overlay_layout();
}

/* prx.cpp resolve_game_container: the in-game layer the bar attaches to. */
bool resolve_game_container() {
  MonoImage *app_system = getDLLimage("Sce.Vsh.ShellUI.AppSystem.dll");
  if (!app_system)
    return false;
  MonoClass *layer_manager = mono_class_from_name(
      app_system, "Sce.Vsh.ShellUI.AppSystem", "LayerManager");
  MonoMethod *find =
      layer_manager ? mono_class_get_method_from_name(
                          layer_manager, "FindContainerSceneByPath", 1)
                    : nullptr;
  if (!find) {
    LOG_ERROR("fps-monitor: LayerManager.FindContainerSceneByPath missing");
    return false;
  }
  void *args[1] = {mono_string_new(mono_domain_get(), "Game")};
  MonoObject *exception = nullptr;
  Game = mono_runtime_invoke(find, nullptr, args, &exception);
  if (exception || !Game) {
    LOG_ERROR("fps-monitor: Game scene lookup failed");
    return false;
  }
  return true;
}

bool install_render_hook() {
  char probe[kMprotectProbeSize];
  has_hv_bypass = (sceKernelMprotect(probe, sizeof(probe), kProtRwx) == 0);
  const uint64_t addr = Get_Address_of_Method(pui_img, "Sce.PlayStation.PUI",
                                              "Application", "Update", 0);
  if (!addr ||
      !InstallDetour(addr, reinterpret_cast<void *>(&OnRender_Hook),
                     reinterpret_cast<void **>(&OnRender_orig))) {
    LOG_ERROR("fps-monitor: Application.Update hook failed");
    return false;
  }
  return true;
}

} // namespace

int main(int argc, char const *argv[]) {
  (void)argc;
  (void)argv;
  if (hooked)
    return 0;

  const pid_t pid = getpid();
  {
    AuthIdGuard auth{pid, set_ucred_to_ptrace()};
    /* ShellUI's sandbox sees /user/data, not /data (see prx.cpp main). */
    onion_log_configure("FPSMon", "/user/data/fps-monitor/fps-monitor.log");

    if (!resolve_symbols(pid))
      return -1;
    load_settings();

    Root_Domain = mono_get_root_domain();
    if (!Root_Domain)
      return -1;
    mono_thread_attach(Root_Domain);

    pui_img = getDLLimage("Sce.PlayStation.PUI.dll");
    if (!pui_img || !resolve_game_container())
      return -1;

    shellui_hooks_begin_install();
    if (!install_render_hook()) {
      shellui_hooks_publish_failed();
      return -1;
    }
  } // restore authid before long-running work

  shellui_hooks_publish_ready();
  hooked = true;
  LOG_INFO("fps-monitor: overlay hooked in SceShellUI pid=%d",
           static_cast<int>(pid));

  /* Host waits on this marker; the thread must never return. */
  onion_ready_signal_pid(ONION_READY_FPS_MONITOR_UI, pid);
  while (true)
    sleep(kKeepAliveSleepSec);
}
