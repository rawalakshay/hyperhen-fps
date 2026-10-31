/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Seqlock record published by the daemon FPS sampler and read by ShellUI.
 * Header-only: ShellUI must not link libonion_fps (no DMAP / ioctl).
 */
#pragma once

#include <onion/system_tmp.h>

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ONION_FPS_MAGIC 0x4F465053u /* 'OFPS' */
#define ONION_FPS_SAMPLE_BYTES 128
#define ONION_FPS_STALE_NS 2000000000ull /* 2 s */

enum OnionFpsSource {
  ONION_FPS_SRC_NONE = 0,
  ONION_FPS_SRC_SCANOUT = 1u << 0,
  ONION_FPS_SRC_RING = 1u << 1,
  ONION_FPS_SRC_GLOBAL = 1u << 2,
  ONION_FPS_SRC_HYBRID = 1u << 3,
  ONION_FPS_SRC_MULTIPASS = 1u << 4,
  ONION_FPS_SRC_BC = 1u << 5
};

/* ext_layout values. 0 (what older daemons leave there) = no extension. */
#define ONION_FPS_EXT_NONE 0u
#define ONION_FPS_EXT_FRAMES 1u

struct OnionFpsSample {
  uint32_t magic;
  uint32_t seq;
  int32_t pid;
  uint8_t valid;
  uint8_t source;
  uint8_t pad[2];
  float fps; /* smoothed display rate (rolling mean) */
  uint32_t pad_align;
  uint64_t unix_ns;
  char title_id[16];
  /*
   * ONION_FPS_EXT_FRAMES: a running count of displayed frames, so readers can
   * take exact rates over their own windows instead of averaging `fps` again.
   * frames_milli is frames × 1000 at frames_mono_ns (daemon CLOCK_MONOTONIC);
   * both only grow within one ext_epoch. A new epoch (new game, gap) means
   * the count restarted and earlier readings must not be differenced.
   */
  uint16_t ext_layout;
  uint16_t ext_flags;
  uint32_t ext_epoch;
  uint64_t frames_milli;
  uint64_t frames_mono_ns;
  float raw_fps; /* this tick's unsmoothed rate */
  uint8_t reserved[52];
};

static inline uint64_t onion_fps_realtime_ns(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
    return 0;
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static inline int onion_fps_seqlock_load(const volatile struct OnionFpsSample *src,
                                         struct OnionFpsSample *dst) {
  if (!src || !dst)
    return -1;
  for (int i = 0; i < 8; ++i) {
    const uint32_t s1 = __atomic_load_n(&src->seq, __ATOMIC_ACQUIRE);
    if (s1 & 1u)
      continue;
    memcpy(dst, (const void *)src, sizeof(*dst));
    /* Keep the payload reads above the re-check of seq. */
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    const uint32_t s2 = __atomic_load_n(&src->seq, __ATOMIC_ACQUIRE);
    if (s1 == s2 && (s2 & 1u) == 0)
      return 0;
  }
  return -1;
}

/**
 * Writer side: seq goes odd before any payload byte changes and even only
 * after all of them land, so a reader never accepts a torn record.
 */
static inline void onion_fps_seqlock_store(volatile struct OnionFpsSample *dst,
                                           const struct OnionFpsSample *src) {
  if (!dst || !src)
    return;
  const uint32_t seq = __atomic_load_n(&dst->seq, __ATOMIC_RELAXED);
  __atomic_store_n(&dst->seq, seq + 1u, __ATOMIC_RELAXED);
  __atomic_thread_fence(__ATOMIC_RELEASE);
  struct OnionFpsSample tmp = *src;
  tmp.seq = seq + 1u; /* the copy must not publish an even seq early */
  memcpy((void *)dst, &tmp, sizeof(tmp));
  __atomic_thread_fence(__ATOMIC_RELEASE);
  __atomic_store_n(&dst->seq, seq + 2u, __ATOMIC_RELAXED);
}

static inline struct OnionFpsSample *onion_fps_map_ro(void) {
  const int fd = open(ONION_SYSTEM_TMP_FPS_SAMPLE, O_RDONLY);
  if (fd < 0)
    return (struct OnionFpsSample *)MAP_FAILED;
  void *p = mmap(NULL, ONION_FPS_SAMPLE_BYTES, PROT_READ, MAP_SHARED, fd, 0);
  close(fd);
  return (struct OnionFpsSample *)p;
}

static inline int onion_fps_read(struct OnionFpsSample *out) {
  static struct OnionFpsSample *map;
  if (!out)
    return -1;
  memset(out, 0, sizeof(*out));
  if (map == NULL || map == (struct OnionFpsSample *)MAP_FAILED) {
    map = onion_fps_map_ro();
    if (map == (struct OnionFpsSample *)MAP_FAILED) {
      map = NULL;
      return -1;
    }
  }
  struct OnionFpsSample local;
  if (onion_fps_seqlock_load(map, &local) != 0)
    return -1;
  if (local.magic != ONION_FPS_MAGIC || !local.valid)
    return -1;
  const uint64_t now = onion_fps_realtime_ns();
  if (local.unix_ns == 0 || now < local.unix_ns ||
      now - local.unix_ns > ONION_FPS_STALE_NS)
    return -1;
  *out = local;
  return 0;
}

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
static_assert(sizeof(OnionFpsSample) == ONION_FPS_SAMPLE_BYTES,
              "OnionFpsSample must stay 128 bytes");
static_assert(offsetof(OnionFpsSample, unix_ns) == 24,
              "unix_ns must be 8-byte aligned");
/* The extension sits in what used to be reserved[80]; older readers ignore
 * it and older writers leave it zeroed. */
static_assert(offsetof(OnionFpsSample, ext_layout) == 48,
              "extension must start where reserved[] began");
static_assert(offsetof(OnionFpsSample, frames_milli) == 56,
              "frames_milli must be 8-byte aligned");
static_assert(offsetof(OnionFpsSample, frames_mono_ns) == 64,
              "frames_mono_ns must be 8-byte aligned");
static_assert(offsetof(OnionFpsSample, raw_fps) == 72, "raw_fps offset");
#endif
