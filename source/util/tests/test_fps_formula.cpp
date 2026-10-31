/* Host unit tests for skip-hook FPS composition (no PS5 SDK). */
#include "test_harness.h"

#include <onion/fps_formula.hpp>
#include <onion/fps_sample.h>

#include <cstring>

static int test_hz_from_delta(void) {
  TEST_ASSERT_TRUE(onion::fps::hz_from_delta(0, 0.033) == 0.0);
  TEST_ASSERT_TRUE(onion::fps::hz_from_delta(2, 0.0) == 0.0);
  const double hz = onion::fps::hz_from_delta(2, 1.0 / 30.0);
  TEST_ASSERT_TRUE(hz > 59.0 && hz < 61.0);
  TEST_ASSERT_TRUE(onion::fps::hz_from_delta(1, 0.001) > 999.0); /* raw uncapped rate */
  return 0;
}

static int test_rolling_mean(void) {
  const float s[] = {60.f, 60.f, 60.f, 60.f, 60.f};
  TEST_ASSERT_TRUE(onion::fps::rolling_mean(s, 5) > 59.9f);
  TEST_ASSERT_TRUE(onion::fps::rolling_mean(nullptr, 5) == 0.f);
  TEST_ASSERT_TRUE(onion::fps::rolling_mean(s, 0) == 0.f);
  return 0;
}

static int test_compose_hybrid_and_multipass(void) {
  onion::fps::HybridIn in;
  in.scanout_ok = true;
  in.scanout = 60.f;
  in.ring_ok = true;
  in.ring = 60.f;
  auto out = onion::fps::compose(in);
  TEST_ASSERT_TRUE(out.valid);
  TEST_ASSERT_TRUE(out.fps >= 59.f && out.fps <= 61.f);

  in.ring = 120.f;
  out = onion::fps::compose(in);
  TEST_ASSERT_TRUE(out.valid);
  TEST_ASSERT_TRUE(out.fps >= 119.f); /* uncap hybrid max */

  in.ring_ok = false;
  in.global_ok = true;
  in.global = 180.f; /* 3x scanout → multi-pass */
  in.calibration_ready = true;
  in.multipass = true;
  out = onion::fps::compose(in);
  TEST_ASSERT_TRUE(out.valid);
  TEST_ASSERT_TRUE(out.fps >= 59.f && out.fps <= 61.f);
  TEST_ASSERT_TRUE((out.source & ONION_FPS_SRC_MULTIPASS) != 0);
  return 0;
}

static int test_compose_dead_ticks(void) {
  onion::fps::HybridIn in;
  in.dead_ticks = onion::fps::kDeadTicks - 1;
  auto out = onion::fps::compose(in);
  TEST_ASSERT_TRUE(!out.valid);
  TEST_ASSERT_EQ_INT(onion::fps::kDeadTicks, out.dead_ticks);
  return 0;
}

static int test_title_prefixes(void) {
  TEST_ASSERT_TRUE(onion::fps::is_ps5_native_title("PPSA12345"));
  TEST_ASSERT_TRUE(onion::fps::is_ps5_native_title("PPSB00001"));
  TEST_ASSERT_TRUE(!onion::fps::is_ps5_native_title("ECAS00006"));
  TEST_ASSERT_TRUE(!onion::fps::is_ps5_native_title("CUSA12345"));
  TEST_ASSERT_TRUE(onion::fps::is_ps4_bc_title("CUSA12345"));
  TEST_ASSERT_TRUE(onion::fps::is_ps4_bc_title("PCAS00001"));
  TEST_ASSERT_TRUE(onion::fps::is_ps4_bc_title("CUHJ10000"));
  TEST_ASSERT_TRUE(!onion::fps::is_ps4_bc_title("PLJM80005"));
  TEST_ASSERT_TRUE(!onion::fps::is_ps4_bc_title("PPSA12345"));
  TEST_ASSERT_TRUE(!onion::fps::is_ps5_native_title(""));
  return 0;
}

static int test_sample_size(void) {
  TEST_ASSERT_EQ_INT(ONION_FPS_SAMPLE_BYTES,
                     static_cast<int>(sizeof(OnionFpsSample)));
  return 0;
}

static int test_seqlock_roundtrip(void) {
  OnionFpsSample shared{};
  OnionFpsSample in{};
  in.magic = ONION_FPS_MAGIC;
  in.valid = 1;
  in.fps = 59.5f;
  in.seq = 12345u; /* writer owns seq; the caller's value is ignored */
  std::memcpy(in.title_id, "PPSA01234", 10);
  onion_fps_seqlock_store(&shared, &in);
  TEST_ASSERT_EQ_INT(2, static_cast<int>(shared.seq));
  onion_fps_seqlock_store(&shared, &in);
  TEST_ASSERT_EQ_INT(4, static_cast<int>(shared.seq));

  OnionFpsSample out{};
  TEST_ASSERT_EQ_INT(0, onion_fps_seqlock_load(&shared, &out));
  TEST_ASSERT_EQ_INT(4, static_cast<int>(out.seq));
  TEST_ASSERT_TRUE(out.fps == 59.5f);
  TEST_ASSERT_STREQ("PPSA01234", out.title_id);

  /* A writer caught mid-update (odd seq) is never accepted. */
  shared.seq = 5u;
  TEST_ASSERT_EQ_INT(-1, onion_fps_seqlock_load(&shared, &out));
  return 0;
}

static int test_frame_accumulator(void) {
  using onion::fps::FrameAccumulator;
  constexpr uint64_t kTick = 33333333ull; /* 30 Hz daemon tick */
  FrameAccumulator acc;
  TEST_ASSERT_TRUE(!acc.started());
  const uint32_t epoch0 = acc.epoch();

  uint64_t t = 5000000000ull;
  acc.add(60.f, t); /* first tick only sets the time base */
  TEST_ASSERT_TRUE(acc.started());
  TEST_ASSERT_EQ_U64(0, acc.frames_milli());
  for (int i = 0; i < 30; ++i)
    acc.add(60.f, t += kTick);
  /* 30 ticks × 33.3 ms at 60 fps ≈ 60 frames */
  TEST_ASSERT_TRUE(acc.frames_milli() > 59900 && acc.frames_milli() < 60100);

  /* The source switching (ring → scanout) changes the rate, not the count. */
  const uint64_t before = acc.frames_milli();
  acc.add(30.f, t += kTick);
  TEST_ASSERT_TRUE(acc.frames_milli() > before);
  TEST_ASSERT_EQ_U64(t, acc.last_ns());

  /* A stalled tick adds nothing but moves the clock: the next tick is not
   * stretched over it. */
  const uint64_t stalled = acc.frames_milli();
  acc.add(0.f, t += kTick);
  TEST_ASSERT_EQ_U64(stalled, acc.frames_milli());
  acc.add(60.f, t += kTick);
  TEST_ASSERT_TRUE(acc.frames_milli() - stalled < 2100); /* ~2 frames, not 4 */

  /* A gap over a second restarts the count under a new epoch. */
  acc.add(60.f, t += 2000000000ull);
  TEST_ASSERT_EQ_U64(0, acc.frames_milli());
  TEST_ASSERT_EQ_INT(static_cast<int>(epoch0 + 1), static_cast<int>(acc.epoch()));

  acc.restart(); /* new game */
  TEST_ASSERT_TRUE(!acc.started());
  TEST_ASSERT_EQ_INT(static_cast<int>(epoch0 + 2), static_cast<int>(acc.epoch()));
  return 0;
}

static int test_sample_extension_layout(void) {
  /* Older daemons zero-fill the record: that must read as "no extension". */
  OnionFpsSample s{};
  TEST_ASSERT_EQ_INT(ONION_FPS_EXT_NONE, s.ext_layout);
  TEST_ASSERT_EQ_INT(48, static_cast<int>(offsetof(OnionFpsSample, ext_layout)));
  TEST_ASSERT_EQ_INT(56, static_cast<int>(offsetof(OnionFpsSample, frames_milli)));
  TEST_ASSERT_EQ_INT(128, static_cast<int>(sizeof(OnionFpsSample)));
  return 0;
}

extern "C" int test_fps_formula_suite(void) {
  int failures = 0;
  failures += onion_test_run("fps_hz_from_delta", test_hz_from_delta);
  failures += onion_test_run("fps_rolling_mean", test_rolling_mean);
  failures += onion_test_run("fps_compose_hybrid_multipass",
                             test_compose_hybrid_and_multipass);
  failures += onion_test_run("fps_compose_dead_ticks", test_compose_dead_ticks);
  failures += onion_test_run("fps_title_prefixes", test_title_prefixes);
  failures += onion_test_run("fps_sample_size", test_sample_size);
  failures += onion_test_run("fps_seqlock_roundtrip", test_seqlock_roundtrip);
  failures += onion_test_run("fps_frame_accumulator", test_frame_accumulator);
  failures += onion_test_run("fps_sample_extension", test_sample_extension_layout);
  return failures;
}
