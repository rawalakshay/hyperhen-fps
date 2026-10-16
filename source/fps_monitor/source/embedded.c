/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Overlay-only ShellUI ELF, injected by the FPS monitor host.
 * Path comes from CMake (see daemon/source/embedded_payloads.c).
 */
#ifndef FPS_MONITOR_SHELLUI_ELF
#define FPS_MONITOR_SHELLUI_ELF "../../bin/monitor_shellui.elf"
#endif

__asm__(
    ".global monitor_shellui_elf_start\n"
    ".type   monitor_shellui_elf_start, @object\n"
    ".align  16\n"
    "monitor_shellui_elf_start:\n"
    ".incbin \"" FPS_MONITOR_SHELLUI_ELF "\"\n"
    "monitor_shellui_elf_end:\n");
