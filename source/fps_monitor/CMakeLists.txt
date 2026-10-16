cmake_minimum_required (VERSION 3.20)

# Standalone FPS monitor payload: fps-monitor.elf embeds monitor_shellui.elf.
# Both reuse existing sources (daemon FPS sampler, shellui overlay) unchanged.
project(fps_monitor C CXX ASM)

onion_module_prologue(C_STANDARD 11 CXX_STANDARD 20)

set(D_CWD "${CMAKE_CURRENT_SOURCE_DIR}")
onion_output_dirs()
set(D_SHELLUI "${PROJECT_ROOT}/shellui")
set(D_DAEMON "${PROJECT_ROOT}/daemon")

set(CMAKE_C_FLAGS "${ONION_PS5_TARGET_FLAGS} ")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-nested-anon-types -Wno-gnu-anonymous-struct -Wno-zero-length-array -Wno-keyword-macro -fPIC -fPIE -march=znver2 -Wall -Werror -fstack-protector-all -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -gfull -gdwarf-2 -pedantic -pedantic-errors")
set(CMAKE_C_FLAGS_RELEASE "-Os -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-Os -DNDEBUG")

# --- monitor_shellui.elf: injected into SceShellUI, draws the bar only -------
add_executable(fps_monitor_shellui)
set_target_properties(fps_monitor_shellui PROPERTIES OUTPUT_NAME "monitor_shellui.elf")
target_link_options(fps_monitor_shellui PRIVATE "-Wl,--gc-sections" "-Wl,--icf=safe")
target_sources(fps_monitor_shellui PRIVATE
	"${D_CWD}/shellui/main.cpp"
	"${D_SHELLUI}/src/prx_overlay.cpp"
	"${D_SHELLUI}/src/shellui_overlay_widgets.cpp"
	"${D_SHELLUI}/src/mono_widgets.cpp"
	"${D_SHELLUI}/src/external_symbols.cpp"
	"${D_SHELLUI}/src/hook_lifecycle.cpp"
	"${D_SHELLUI}/src/overlay_layout_apply.cpp"
)
target_compile_definitions(fps_monitor_shellui PRIVATE HYPERHEN_FPS_MONITOR=1)
onion_target_debug_define(fps_monitor_shellui)
target_include_directories(fps_monitor_shellui PRIVATE
	"${D_SHELLUI}/include"
	"${HYPERHEN_THIRD_PARTY_DIR}/cjson"
	"${PROJECT_ROOT}/libonion_ipc/include"
	"${PROJECT_ROOT}/libonion_settings/include"
	"${PROJECT_ROOT}/libonion_detour/include"
	"${PROJECT_ROOT}/libonion_proc/include"
	"${PROJECT_ROOT}/libonion_platform/include"
	"${PROJECT_ROOT}/libonion_fps/include"
	"${PROJECT_ROOT}/libonion_payload/include"
	"${PROJECT_ROOT}/libonion_plugin_ui/include"
	"${PROJECT_ROOT}/libonion_plugin_bridge/include"
	"${PROJECT_ROOT}/include"
)
target_link_directories(fps_monitor_shellui PUBLIC
	"${HYPERHEN_PS5_STUB_DIR}"
	"${PS5_PAYLOAD_SDK}/target/lib"
)
add_dependencies(fps_monitor_shellui onion_settings onion_detour onion_proc onion_platform onion_ready)
target_link_libraries(fps_monitor_shellui PUBLIC onion_detour onion_proc onion_platform onion_settings onion_ready SceNetCtl)
onion_strip_debug(fps_monitor_shellui)

# --- fps-monitor.elf: sampler threads + ShellUI injector/watchdog -----------
set(_monitor_shellui_elf "${HYPERHEN_OUT_BIN}/monitor_shellui.elf")
add_executable(fps_monitor)
set_target_properties(fps_monitor PROPERTIES OUTPUT_NAME "fps-monitor.elf")
target_link_options(fps_monitor PRIVATE "-Wl,--gc-sections" "-Wl,--icf=safe")
target_sources(fps_monitor PRIVATE
	"${D_CWD}/source/main.cpp"
	"${D_CWD}/source/embedded.c"
	"${D_DAEMON}/source/daemon_fps.cpp"
	"${D_DAEMON}/source/running_app.cpp"
	"${D_DAEMON}/source/daemon_power_state.cpp"
)
# .incbin is invisible to the dependency scanner; re-embed on rebuild.
set_source_files_properties("${D_CWD}/source/embedded.c"
	PROPERTIES OBJECT_DEPENDS "${_monitor_shellui_elf}")
target_compile_definitions(fps_monitor PRIVATE
	FPS_MONITOR_SHELLUI_ELF="${_monitor_shellui_elf}")
onion_target_debug_define(fps_monitor)
target_include_directories(fps_monitor PRIVATE
	"${D_DAEMON}/include"
	"${HYPERHEN_THIRD_PARTY_DIR}/cjson"
	"${PROJECT_ROOT}/libonion_settings/include"
	"${PROJECT_ROOT}/libonion_ipc/include"
	"${PROJECT_ROOT}/libonion_proc/include"
	"${PROJECT_ROOT}/libonion_fps/include"
	"${PROJECT_ROOT}/libonion_platform/include"
	"${PROJECT_ROOT}/libonion_elfldr/include"
	"${PROJECT_ROOT}/include"
)
target_link_directories(fps_monitor PUBLIC "${HYPERHEN_PS5_STUB_DIR}")
add_dependencies(fps_monitor fps_monitor_shellui NineS hijacker onion_settings onion_proc onion_fps onion_platform onion_ready onion_elfldr)
target_link_libraries(fps_monitor PUBLIC onion_fps onion_settings onion_proc onion_platform onion_ready onion_elfldr hijacker NineS SceSystemService kernel_sys)
onion_strip_debug(fps_monitor)
