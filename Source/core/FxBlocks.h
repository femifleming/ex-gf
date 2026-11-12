#pragma once
// Blocks shared by the effect algorithms: the reverb used by Granulator / Multi-FX / Spectral Freeze, and an
// adapter that runs fixed-rate, fixed-block DSP (Plaits, Rings: 48 kHz) at any host sample rate.
#include "ExDsp.h"

namespace ex
{
// ------------------------------------------------------------------------------------------------
// Algorithmic reverb: early reflections (tapped delay) + an 8-line feedback delay network.
// Parameters follow the module: model 0-3, time (ms), size (%), high damp, mod speed/depth, early/diffuse gains.
struct ExReverb
{
    static constexpr int kLines = 8, kTaps = 6, kAp = 4;
    static inline const char* modelName (int m)
    {
        static const char* n[] = { "Room", "Hall", "Plate", "Cathedral" };
        return n[juce::jlimit (0, 3, m)];
    }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        size = 1; while (size < (int) (sr * 0.16)) size <<= 1;
        mask = size - 1;
        for (auto& l : line) l.assign ((size_t) size, 0.0f);
        for (auto& e : early) e.assign ((size_t) size, 0.0f);
        int apSize = 1; while (apSize < (int) (sr * 0.016)) apSize <<= 1;
        for (auto& a : ap) a.assign ((size_t) apSize, 0.0f);
        clear();
        model = -1;
    }
    void clear()
    {
        for (auto& l : line) std::fill (l.begin(), l.end(), 0.0f);
        for (auto& e : early) std::fill (e.begin(), e.end(), 0.0f);
        for (auto& a : ap) std::fill (a.begin(), a.end(), 0.0f);
        for (auto& z : lp) z = 0;
        w = 0; apW = 0;
    }

    // call once per block
    void set (int modelIn, float timeMs, float sizePct, float damp, float modCentiHz, float modDepth, float earlyDb, float diffDb)
    {
        static const float lens[4][kLines] = {
            { 11.3f, 14.9f, 18.1f, 22.3f, 26.9f, 31.1f, 35.3f, 41.9f },
            { 29.7f, 37.1f, 41.1f, 43.7f, 51.7f, 59.3f, 67.1f, 73.3f },
            { 17.3f, 19.1f, 22.9f, 25.3f, 28.7f, 30.1f, 33.7f, 36.1f },
            { 53.1f, 61.7f, 71.3f, 79.9f, 89.3f, 97.1f, 107.9f, 113.3f } };
        static const float taps[4][kTaps] = {
            { 3.1f, 7.3f, 11.9f, 17.7f, 23.3f, 29.9f },
            { 7.9f, 15.1f, 24.7f, 37.3f, 51.1f, 66.7f },
            { 1.3f, 2.9f, 4.7f, 7.1f, 9.7f, 13.1f },
            { 13.3f, 27.1f, 43.9f, 61.3f, 83.9f, 109.7f } };
        static const float apMs[kAp] = { 4.77f, 3.59f, 12.73f, 9.31f };
        modelIn = juce::jlimit (0, 3, modelIn);
        model = modelIn;
        const float sz = juce::jlimit (0.01f, 1.0f, sizePct / 100.0f);
        const float lineScale = 0.55f + 0.45f * sz;
        const float t60 = juce::jlimit (0.2f, 60.0f, timeMs * 0.001f);
        for (int i = 0; i < kLines; ++i)
        {
            float d = lens[model][i] * lineScale * 0.001f * (float) sr;
            len[i] = juce::jlimit (8.0f, (float) (size - 64), d);
            gain[i] = std::pow (10.0f, -3.0f * (len[i] / (float) sr) / t60);
        }
        for (int i = 0; i < kTaps; ++i)
        {
            tapL[i] = juce::jlimit (1, size - 2, (int) (taps[model][i] * sz * 0.001f * (float) sr));
            tapR[i] = juce::jlimit (1, size - 2, (int) (taps[model][i] * sz * 1.13f * 0.001f * (float) sr) + 3);
        }
        for (int i = 0; i < kAp; ++i) apLen[i] = juce::jlimit (4, (int) ap[0].size() - 1, (int) (apMs[i] * 0.001f * (float) sr));
        apG = model == 2 ? 0.7f : model == 0 ? 0.5f : 0.6f;
        // high damping: loop low-pass from ~18 kHz (0) down to ~700 Hz (100)
        float hz = 18000.0f * std::pow (0.04f, juce::jlimit (0.0f, 1.0f, damp / 100.0f));
        dampA = std::exp (-2.0f * kPi * juce::jmin (hz, (float) sr * 0.45f) / (float) sr);
        modInc = juce::jlimit (0.01f, 5.0f, modCentiHz * 0.01f) / (float) sr;
        modAmt = juce::jlimit (0.0f, 1.0f, modDepth / 100.0f) * 0.0009f * (float) sr;
        earlyG = earlyDb <= -40.0f ? 0.0f : dbToGain (earlyDb);
        diffG = diffDb <= -40.0f ? 0.0f : dbToGain (diffDb);
    }

    // one sample: writes the wet signal only
    void process (float inL, float inR, float& outL, float& outR)
    {
        if (model < 0) { outL = outR = 0; return; }
        // early reflections
        early[0][(size_t) w] = inL; early[1][(size_t) w] = inR;
        static const float tg[kTaps] = { 0.84f, -0.71f, 0.62f, 0.51f, -0.43f, 0.36f };
        float eL = 0, eR = 0;
        for (int i = 0; i < kTaps; ++i)
        {
            eL += tg[i] * early[0][(size_t) ((w - tapL[i]) & mask)];
            eR += tg[i] * early[1][(size_t) ((w - tapR[i]) & mask)];
        }
        // input diffusion (two all-passes per channel)
        float dL = inL + 0.4f * eL, dR = inR + 0.4f * eR;
        const int apMask = (int) ap[0].size() - 1;
        for (int i = 0; i < kAp; ++i)
        {
            float& x = (i & 1) ? dR : dL;
            float z = ap[i][(size_t) ((apW - apLen[i]) & apMask)];
            float v = x + apG * z;
            ap[i][(size_t) apW] = v;
            x = z - apG * v;
        }
        apW = (apW + 1) & apMask;
        // feedback delay network
        modPhase += modInc; if (modPhase >= 1.0f) modPhase -= 1.0f;
        float o[kLines], sum = 0;
        for (int i = 0; i < kLines; ++i)
        {
            float d = len[i];
            if (modAmt > 0.0f) d += modAmt * (1.0f + std::sin (2.0f * kPi * (modPhase + (float) i * 0.125f)));
            float rp = (float) w - d;
            int i0 = (int) std::floor (rp); float fr = rp - (float) i0;
            float a = line[i][(size_t) (i0 & mask)], b = line[i][(size_t) ((i0 + 1) & mask)];
            float v = a + fr * (b - a);
            lp[i] = v + dampA * (lp[i] - v);
            o[i] = lp[i] * gain[i];
            sum += o[i];
        }
        sum *= 2.0f / kLines; // Householder reflection
        float wl = 0, wr = 0;
        for (int i = 0; i < kLines; ++i)
        {
            float fb = o[i] - sum;
            line[i][(size_t) w] = ((i & 1) ? dR : dL) * 0.5f + fb;
            if (i & 1) wr += o[i]; else wl += o[i];
        }
        w = (w + 1) & mask;
        outL = earlyG * eL + diffG * wl * 0.7f;
        outR = earlyG * eR + diffG * wr * 0.7f;
    }

private:
    double sr = 48000;
    int size = 0, mask = 0, w = 0, apW = 0, model = -1;
    std::vector<float> line[kLines], early[2], ap[kAp];
    float len[kLines] {}, gain[kLines] {}, lp[kLines] {};
    int tapL[kTaps] {}, tapR[kTaps] {}, apLen[kAp] {};
    float apG = 0.6f, dampA = 0, modInc = 0, modAmt = 0, modPhase = 0, earlyG = 0.25f, diffG = 0.5f;
};

// ------------------------------------------------------------------------------------------------
// Runs block-based DSP that only works at one sample rate (and one block size) inside a host running at any
// rate: the input is resampled into blocks, processBlock() is called, and its output is resampled back.
// Adds a few milliseconds of latency.
struct RateAdapter
{
    // in[numIn][block] -> out[numOut][block]
    std::function<void (float* const* in, float* const* out)> processBlock;

    void prepare (double hostSr, double internalSr, int numIn, int numOut, int blockSize)
    {
        nin = numIn; nout = numOut; block = blockSize;
        ratio = internalSr / hostSr;
        step = hostSr / internalSr;
        lat = block + 3 * (int) std::ceil (ratio) + 6;
        cap = (int) (kChunk * ratio) + lat + 2 * block + 32;
        hist.assign ((size_t) juce::jmax (1, nin), std::vector<float> (kHist + kChunk, 0.0f));
        inBlk.assign ((size_t) juce::jmax (1, nin), std::vector<float> ((size_t) block, 0.0f));
        outBlk.assign ((size_t) nout, std::vector<float> ((size_t) block, 0.0f));
        fifo.assign ((size_t) nout, std::vector<float> ((size_t) cap, 0.0f));
        inPtr.resize ((size_t) juce::jmax (1, nin)); outPtr.resize ((size_t) nout);
        for (size_t c = 0; c < inPtr.size(); ++c) inPtr[c] = inBlk[c].data();
        for (size_t c = 0; c < outPtr.size(); ++c) outPtr[c] = outBlk[c].data();
        inCount = 0; fifoCount = lat; tIn = 0; rPos = 1.0;
    }

    void process (const float* const* hostIn, float* const* hostOut, int n)
    {
        for (int pos = 0; pos < n; pos += kChunk)
        {
            const int len = juce::jmin (kChunk, n - pos);
            const float* in[8] {}; float* out[8] {};
            for (int c = 0; c < nin; ++c) in[c] = hostIn[c] + pos;
            for (int c = 0; c < nout; ++c) out[c] = hostOut[c] + pos;
            chunk (in, out, len);
        }
    }

private:
    static constexpr int kChunk = 256, kHist = 4;
    int nin = 0, nout = 2, block = 24, lat = 0, cap = 0, inCount = 0, fifoCount = 0;
    double ratio = 1, step = 1, tIn = 0, rPos = 1;
    std::vector<std::vector<float>> hist, inBlk, outBlk, fifo;
    std::vector<float*> inPtr, outPtr;

    static float hermite (float xm1, float x0, float x1, float x2, float t)
    {
        float c = 0.5f * (x1 - xm1), v = x0 - x1, w2 = c + v, a = w2 + v + 0.5f * (x2 - x0), b = w2 + a;
        return ((a * t - b) * t + c) * t + x0;
    }

    void chunk (const float* const* in, float* const* out, int n)
    {
        for (int c = 0; c < nin; ++c) std::memcpy (hist[(size_t) c].data() + kHist, in[c], sizeof (float) * (size_t) n);
        // host -> internal
        while (std::floor (tIn) + 2 <= n - 1)
        {
            const int i0 = (int) std::floor (tIn);
            const float fr = (float) (tIn - i0);
            for (int c = 0; c < nin; ++c)
            {
                const float* h = hist[(size_t) c].data() + kHist + i0;
                inBlk[(size_t) c][(size_t) inCount] = hermite (h[-1], h[0], h[1], h[2], fr);
            }
            tIn += step;
            if (++inCount == block)
            {
                inCount = 0;
                if (processBlock) processBlock (inPtr.data(), outPtr.data());
                if (fifoCount + block <= cap)
                {
                    for (int c = 0; c < nout; ++c)
                        std::memcpy (fifo[(size_t) c].data() + fifoCount, outBlk[(size_t) c].data(), sizeof (float) * (size_t) block);
                    fifoCount += block;
                }
            }
        }
        tIn -= n;
        for (int c = 0; c < nin; ++c)
        {
            auto* h = hist[(size_t) c].data();
            std::memmove (h, h + n, sizeof (float) * kHist);
        }
        // internal -> host
        for (int i = 0; i < n; ++i)
        {
            int i0 = (int) rPos;
            if (i0 + 2 >= fifoCount) { for (int c = 0; c < nout; ++c) out[c][i] = 0.0f; continue; } // underrun (should not happen)
            const float fr = (float) (rPos - i0);
            for (int c = 0; c < nout; ++c)
            {
                const float* f = fifo[(size_t) c].data() + i0;
                out[c][i] = hermite (f[-1], f[0], f[1], f[2], fr);
            }
            rPos += ratio;
        }
        const int drop = (int) rPos - 1;
        if (drop > 0 && drop <= fifoCount)
        {
            for (int c = 0; c < nout; ++c)
                std::memmove (fifo[(size_t) c].data(), fifo[(size_t) c].data() + drop, sizeof (float) * (size_t) (fifoCount - drop));
            fifoCount -= drop; rPos -= drop;
        }
    }
};

// The Mutable Instruments classes have empty constructors and expect zeroed memory (static storage on the
// hardware). Value-initialising this wrapper (std::make_unique<Zeroed<T>>()) zero-fills the object first.
template <typename T> struct Zeroed { T obj; };

// level meter helper for the display (peak with decay), written on the audio thread
struct PeakMeter
{
    std::atomic<float> value { 0.0f };
    void push (float peak, int n, double sr)
    {
        float v = value.load (std::memory_order_relaxed) * std::exp (-(float) n / (0.25f * (float) sr));
        value.store (std::max (v, peak), std::memory_order_relaxed);
    }
};
} // namespace ex
