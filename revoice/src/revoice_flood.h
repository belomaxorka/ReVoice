#pragma once

class CVoiceFloodGuard
{
public:
	static const int SAMPLE_RATE = 8000;
	static const int FRAME_SAMPLES = 160;

	void Reset()
	{
		m_NextExpectedTime = 0.0;
	}

	bool IsFlood(double now, double maxDeltaMs) const
	{
		if (maxDeltaMs <= 0.0)
			return false;

		return m_NextExpectedTime > now && (m_NextExpectedTime - now) * 1000.0 > maxDeltaMs;
	}

	void Advance(double now, int numSamples)
	{
		if (numSamples < FRAME_SAMPLES)
			numSamples = FRAME_SAMPLES;

		double frameLength = (double)numSamples / SAMPLE_RATE;
		m_NextExpectedTime = (m_NextExpectedTime > now ? m_NextExpectedTime : now) + frameLength;
	}

	int LeadMs(double now) const
	{
		if (m_NextExpectedTime <= now)
			return 0;

		return int((m_NextExpectedTime - now) * 1000.0);
	}

private:
	double m_NextExpectedTime = 0.0;
};
