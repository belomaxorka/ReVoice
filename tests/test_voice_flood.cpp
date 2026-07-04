#include "revoice_flood.h"
#include <cstdio>

static int g_failures = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                    \
			printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
			++g_failures;                                                 \
		}                                                                 \
	} while (0)

static const double MAX_DELTA_MS = 200.0;
static const int FRAME = CVoiceFloodGuard::FRAME_SAMPLES;   // one 20 ms frame

static bool between(int v, int lo, int hi)
{
	return v >= lo && v <= hi;
}

// A client speaking in real time (one frame every 20 ms) never floods,
// no matter how long it keeps talking.
static void test_realtime_never_floods()
{
	CVoiceFloodGuard g;
	double now = 100.0;

	for (int i = 0; i < 5000; i++) {
		CHECK(!g.IsFlood(now, MAX_DELTA_MS));
		g.Advance(now, FRAME);
		now += (double)FRAME / CVoiceFloodGuard::SAMPLE_RATE;
		CHECK(g.LeadMs(now) == 0);
	}
}

// Packets fired at the same instant (speedhack) are throttled once the clock
// runs past the delta budget: 200 ms / 20 ms = 10 frames allowed, 11th trips.
static void test_burst_flood_trips()
{
	CVoiceFloodGuard g;
	double now = 50.0;

	int floodedAt = -1;
	for (int i = 0; i < 100; i++) {
		if (g.IsFlood(now, MAX_DELTA_MS)) {
			floodedAt = i;
			break;
		}
		g.Advance(now, FRAME);
	}

	CHECK(between(floodedAt, 10, 11));
}

// A bundle of frames arriving together (jitter) stays under the budget as long
// as it is below ~200 ms worth; 9 frames = 180 ms must still pass.
static void test_jitter_within_budget_ok()
{
	CVoiceFloodGuard g;
	double now = 0.0;

	for (int i = 0; i < 9; i++) {
		CHECK(!g.IsFlood(now, MAX_DELTA_MS));
		g.Advance(now, FRAME);
	}

	CHECK(between(g.LeadMs(now), 178, 182));
	CHECK(!g.IsFlood(now, MAX_DELTA_MS));
}

// Empty / DTX packets (0 samples) still advance the clock by a 20 ms floor,
// so a flood of empty packets is throttled just like real audio.
static void test_empty_packets_floored()
{
	CVoiceFloodGuard g;
	double now = 10.0;

	g.Advance(now, 0);
	CHECK(between(g.LeadMs(now), 19, 20));

	CVoiceFloodGuard g2;
	int floodedAt = -1;
	for (int i = 0; i < 100; i++) {
		if (g2.IsFlood(now, MAX_DELTA_MS)) {
			floodedAt = i;
			break;
		}
		g2.Advance(now, 0);
	}

	CHECK(between(floodedAt, 10, 11));
}

// Once the flood stops, elapsed real time drains the lead and packets pass again.
static void test_recovery_after_silence()
{
	CVoiceFloodGuard g;
	double now = 0.0;

	for (int i = 0; i < 20; i++)
		g.Advance(now, FRAME);

	CHECK(g.IsFlood(now, MAX_DELTA_MS));

	now += 0.5;
	CHECK(!g.IsFlood(now, MAX_DELTA_MS));
	CHECK(g.LeadMs(now) == 0);
}

// Real hpp6 "OPUS method": each ~253-byte packet decodes to ~2160 samples
// (270 ms of audio) instead of one 20 ms frame. A single packet already pushes
// the clock past the 200 ms budget, so the attacker firing as fast as it can is
// throttled to roughly real time -- a handful of packets per second -- matching
// what was captured live.
static void test_oversized_sample_packet_throttled()
{
	const int EXPLOIT_SAMPLES = 2160;   // 270 ms crammed into one packet

	CVoiceFloodGuard g;
	double now = 0.0;

	CHECK(!g.IsFlood(now, MAX_DELTA_MS));
	g.Advance(now, EXPLOIT_SAMPLES);
	CHECK(between(g.LeadMs(now), 268, 272));
	CHECK(g.IsFlood(now, MAX_DELTA_MS));

	for (int i = 0; i < 100; i++)
		CHECK(g.IsFlood(now, MAX_DELTA_MS));

	CVoiceFloodGuard g2;
	double t = 0.0;
	int accepted = 0;
	for (int i = 0; i < 1000; i++) {          // attacker fires ~1 packet/ms for 1 s
		if (!g2.IsFlood(t, MAX_DELTA_MS)) {
			g2.Advance(t, EXPLOIT_SAMPLES);
			accepted++;
		}
		t += 0.001;
	}
	CHECK(between(accepted, 3, 6));
}

// maxDelta of 0 disables flood protection entirely.
static void test_disabled_when_zero()
{
	CVoiceFloodGuard g;
	double now = 0.0;

	for (int i = 0; i < 100; i++)
		g.Advance(now, FRAME);

	CHECK(g.LeadMs(now) > 1000);
	CHECK(!g.IsFlood(now, 0.0));
	CHECK(g.IsFlood(now, MAX_DELTA_MS));
}

int main()
{
	test_realtime_never_floods();
	test_burst_flood_trips();
	test_jitter_within_budget_ok();
	test_empty_packets_floored();
	test_oversized_sample_packet_throttled();
	test_recovery_after_silence();
	test_disabled_when_zero();

	if (g_failures == 0) {
		printf("all voice-flood tests passed\n");
		return 0;
	}

	printf("%d voice-flood test(s) failed\n", g_failures);
	return 1;
}
