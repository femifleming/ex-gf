#include "SpectralFreeze.h"

namespace ex
{
static float gainOrZero (int db) { return db <= -40 ? 0.0f : dbToGain ((float) db); }
static const int kNoteStep[SpectralFreeze::kVoices] = { 0, 2, 4, 5 }; // C, D, E, F

SpectralFreeze::SpectralFreeze (ExPlugin& p) : FxAlgorithm (p, 24, "Spectral Freeze")
{
    for (int i = 0; i < kVoices; ++i) { voiceReq[i] = 0; uiState[i] = 0; uiEnv[i] = 0.0f; uiPos[i] = 0.0f; }
    const juce::StringArray offOn { "Off", "On" };
    addAttenuverters();
    add ("Freeze toggle", 0, 1, 0, {}, offOn);                                         // 7
    add ("Freeze gate", 0, 1, 0, {}, offOn);                                           // 8
    add ("Freeze target", 0, 4, 2, {}, { "Auto", "1", "2", "3", "4" });                // 9
    for (int i = 1; i <= 4; ++i) add ("Voice " + juce::String (i) + " freeze", 0, 1, 0, {}, offOn); // 10-13
    add ("Movement", 0, 4, 2, {}, { "Forwards", "Backwards", "Alternating", "Random walk", "Random skip" }); // 14
    add ("Rate", 0, 100, 1);                                                           // 15
    defs.back().fmt = [] (int v) {
        if (v <= 0) return juce::String ("Stopped");
        if (v >= 100) return juce::String ("1x");
        return "1/" + juce::String (juce::roundToInt (std::pow (2.0, (100 - v) / 10.0))) + "x"; };
    add ("Depth", 1, 128, 2);                                                          // 16
    defs.back().fmt = [this] (int v) { return juce::String (juce::roundToInt (v * hop * 1000.0 / sr)) + "ms"; };
    add ("Offset", 0, kMaxOffset, 0);                                                  // 17
    add ("Etherization", 0, 127, 127);                                                 // 18
    add ("Attack", 0, 127, 10); add ("Release", 0, 127, 10);                           // 19, 20
    add ("Dry gain", -40, 6, 0, "dB"); add ("Freeze gain", -40, 6, 0, "dB");           // 21, 22
    for (int i = 1; i <= 4; ++i) add ("Voice " + juce::String (i) + " gain", -40, 6, 0, "dB");    // 23-26
    for (int i = 1; i <= 4; ++i) add ("Voice " + juce::String (i) + " pan", -100, 100, 0, "%");   // 27-30
    add ("Coarse shift", -48, 24, 0, "ST"); add ("Fine shift", -100, 100, 0, "cents"); // 31, 32
    for (int i = 1; i <= 4; ++i) add ("Voice " + juce::String (i) + " coarse", -48, 24, 0, "ST"); // 33-36
    for (int i = 1; i <= 4; ++i) add ("Voice " + juce::String (i) + " fine", -100, 100, 0, "cents"); // 37-40
    add ("Out delay mode", 0, 2, 0, {}, { "Off", "Stereo", "Ping-pong" });             // 41
    add ("Out delay level", -40, 0, -3, "dB");                                         // 42
    add ("Out delay time", 1, 2730, 500, "ms");                                        // 43
    add ("Out delay feedback", 0, 100, 50, "%");                                       // 44
    add ("Reverb mix", 0, 100, 0, "%");                                                // 45
    add ("Reverb time", 400, 30000, 1700, "ms");                                       // 46
    add ("Reverb model", 0, 3, 1); defs.back().fmt = [] (int v) { return juce::String (ExReverb::modelName (v)); }; // 47
    add ("Reverb size", 1, 100, 100, "%");                                             // 48
    add ("Reverb high damp", 0, 100, 60);                                              // 49
    add ("Reverb mod speed", 1, 500, 50);                                              // 50
    add ("Reverb mod depth", 0, 100, 10);                                              // 51
    add ("Reverb early gain", -40, 6, -12, "dB");                                      // 52
    add ("Reverb diff gain", -40, 6, -6, "dB");                                        // 53
    add ("Live process", 0, 1, 0, {}, offOn);                                          // 54
    add ("Unfreeze all", 0, 1, 0, {}, offOn);                                          // 55
    for (int i = 56; i <= 64; ++i) add ("Reserved " + juce::String (i), 0, 1, 0);      // 56-64 (not in the manual)
    add ("Mono input", 0, 1, 1, {}, { "Stereo", "Mono" });                             // 65
    add ("Delay routing", 0, 1, 0, {}, { "Freeze output", "Output mix" });             // 66
    add ("Reverb routing", 0, 1, 0, {}, { "Freeze output", "Output mix" });            // 67
    add ("Saturation", 0, 1, 0, {}, offOn);                                            // 68

    sections = {
        { "FREEZE", { 7, 8, 9, 54, 55, 18, 19, 20 } },
        { "MOVE", { 14, 15, 16, 17 } },
        { "VOICES", { 10, 11, 12, 13, 23, 24, 25, 26, 27, 28, 29, 30 } },
        { "PITCH", { 31, 32, 33, 37, 34, 38, 35, 39, 36, 40 } },
        { "MIX", { 21, 22, 65, 68, 66, 67 } },
        { "FX", { 41, 43, 44, 42, 45, 46, 47, 48, 49, 50, 51, 52, 53 } },
    };
    defaultL = 9;
}

void SpectralFreeze::prepare (double sampleRate, int block)
{
    prepareFx (sampleRate, block);
    const int order = sampleRate <= 50000.0 ? 11 : 12;
    fftSize = 1 << order; hop = fftSize / 4; bins = fftSize / 2 + 1;
    fft = std::make_unique<juce::dsp::FFT> (order);
    histSize = (kMaxOffset + kMaxDepth + 8) * hop + fftSize;
    hist.assign ((size_t) histSize, 0.0f);
    histW = 0; histCount = 0;
    window.resize ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * kPi * (float) i / (float) fftSize);
    bufA.assign ((size_t) fftSize * 2, 0.0f); bufB.assign ((size_t) fftSize * 2, 0.0f);
    mags.assign ((size_t) bins, 0.0f); peakScratch.assign ((size_t) bins, 0.0f); keep.assign ((size_t) bins, 0);
    for (int i = 0; i < kVoices; ++i)
    {
        auto& v = voices[i];
        v.snap.assign ((size_t) histSize, 0.0f);
        v.ola.assign ((size_t) fftSize, 0.0f);
        v.outBuf.assign ((size_t) hop + 1, 0.0f);
        v.phRe.assign ((size_t) bins, 1.0f); v.phIm.assign ((size_t) bins, 0.0f);
        v.gate = v.live = v.active = false; v.env = 0; v.seed = (uint32_t) (i * 7919 + 17);
        uiState[i] = Idle; uiEnv[i] = 0.0f; uiPos[i] = 0.0f;
    }
    autoHeld = gateVoice = liveVoice = -1;
    prevToggle = prevGate = prevLive = prevUnfreeze = 0;
    for (auto& pvv : prevVoice) pvv = 0;
    delay.prepare (sampleRate, 2.8f);
    reverb.prepare (sampleRate);
}

// ------------------------------------------------------------------------------------------------ control
void SpectralFreeze::startVoice (int i, bool live)
{
    auto& v = voices[i];
    if (! live)
    {
        // linearise the history so the newest sample is last
        const size_t tail = (size_t) (histSize - histW);
        std::memcpy (v.snap.data(), hist.data() + histW, sizeof (float) * tail);
        std::memcpy (v.snap.data() + tail, hist.data(), sizeof (float) * (size_t) histW);
    }
    std::fill (v.ola.begin(), v.ola.end(), 0.0f);
    std::fill (v.outBuf.begin(), v.outBuf.end(), 0.0f);
    v.live = live; v.gate = true; v.active = true; v.fresh = true;
    v.rp = hop; v.u = winLen; v.dir = -1; v.skipAcc = 0;
    v.age = ++ageCounter;
}

void SpectralFreeze::releaseVoice (int i)
{
    voices[i].gate = false;
    if (autoHeld == i) autoHeld = -1;
    if (gateVoice == i) gateVoice = -1;
    if (liveVoice == i) liveVoice = -1;
}

int SpectralFreeze::nextVoice()
{
    for (int k = 1; k <= kVoices; ++k)
    {
        int v = (lastAuto + k) % kVoices;
        if (! voices[v].active) return lastAuto = v;
    }
    int best = -1;
    for (int v = 0; v < kVoices; ++v)
        if (! voices[v].gate && (best < 0 || voices[v].env < voices[best].env)) best = v;
    if (best < 0)
        for (int v = 0; v < kVoices; ++v)
            if (best < 0 || voices[v].age < voices[best].age) best = v;
    return lastAuto = best;
}

void SpectralFreeze::doToggle()
{
    const int t = target();
    if (t > 0) { toggleVoice (t - 1); return; }
    if (autoHeld >= 0 && voices[autoHeld].gate) { releaseVoice (autoHeld); return; }
    const int v = nextVoice();
    startVoice (v, false);
    autoHeld = v;
}

void SpectralFreeze::gateChange (bool on)
{
    if (on)
    {
        const int t = target();
        const int v = t > 0 ? t - 1 : nextVoice();
        startVoice (v, false);
        gateVoice = v;
    }
    else if (gateVoice >= 0) releaseVoice (gateVoice);
}

void SpectralFreeze::liveChange (bool on)
{
    if (on)
    {
        const int t = target();
        const int v = t > 0 ? t - 1 : nextVoice();
        startVoice (v, true);
        liveVoice = v;
    }
    else if (liveVoice >= 0) releaseVoice (liveVoice);
}

void SpectralFreeze::configure()
{
    // settings first: starting a voice uses winLen
    const int depth = pv (16);
    winLen = (double) (depth - 1);
    offsetFrames = pv (17);
    movement = pv (14);
    const int r = pv (15);
    speed = r <= 0 ? 0.0 : std::pow (2.0, (r - 100) / 10.0);
    ether = pv (18);
    attackInc = (float) (1.0 / (attackSeconds (pv (19), false) * sr));
    releaseInc = (float) (1.0 / (releaseSeconds (pv (20)) * sr));
    dryGain = gainOrZero (pv (21));
    const float fg = gainOrZero (pv (22));
    for (int v = 0; v < kVoices; ++v)
    {
        // limited to +2 octaves: every octave up doubles the FFT work
        float semis = juce::jlimit (-48.0f, 24.0f, (float) (pv (31) + pv (33 + v)) + 0.01f * (float) (pv (32) + pv (37 + v)));
        ratio[v] = std::pow (2.0, semis / 12.0);
        const float g = fg * gainOrZero (pv (23 + v));
        const float a = ((float) pv (27 + v) / 100.0f + 1.0f) * 0.25f * kPi;
        gainL[v] = g * std::cos (a) * 1.41421356f; gainR[v] = g * std::sin (a) * 1.41421356f;
    }
    reverb.set (pv (47), (float) pv (46), (float) pv (48), (float) pv (49), (float) pv (50), (float) pv (51), (float) pv (52), (float) pv (53));
    if (clearFxReq.exchange (0) > 0) { delay.clear(); reverb.clear(); }

    // edges
    if (pv (9) != prevTargetParam) { prevTargetParam = pv (9); targetOverride = -1; }
    int unfreeze = unfreezeReq.exchange (0);
    if (pv (55) != prevUnfreeze) { prevUnfreeze = pv (55); if (prevUnfreeze) ++unfreeze; }
    if (unfreeze > 0) for (int v = 0; v < kVoices; ++v) releaseVoice (v);
    for (int v = 0; v < kVoices; ++v)
    {
        if (pv (10 + v) != prevVoice[v]) { prevVoice[v] = pv (10 + v); if (prevVoice[v]) startVoice (v, false); else releaseVoice (v); }
        if (voiceReq[v].exchange (0) & 1) toggleVoice (v);
    }
    int toggles = toggleReq.exchange (0);
    if (pv (7) != prevToggle) { prevToggle = pv (7); if (prevToggle) ++toggles; }
    while (toggles-- > 0) doToggle();
    if (pv (8) != prevGate) { prevGate = pv (8); gateChange (prevGate != 0); }
    if (pv (54) != prevLive) { prevLive = pv (54); liveChange (prevLive != 0); }
    if (liveReq.exchange (0) & 1) liveChange (! (liveVoice >= 0 && voices[liveVoice].gate));
}

void SpectralFreeze::midi (const juce::MidiMessage& m)
{
    if (! (m.isNoteOn() || m.isNoteOff())) return;
    if (channelOffset (m.getChannel()) != 0) return;
    const int note = m.getNoteNumber();
    if (! acceptsNote (note)) return;
    const bool on = m.isNoteOn();
    for (int v = 0; v < kVoices; ++v)
    {
        if (note == 48 + kNoteStep[v] && on) targetOverride = v + 1;
        if (note == 60 + kNoteStep[v] && on) toggleVoice (v);
        if (note == 72 + kNoteStep[v]) { if (on) startVoice (v, false); else releaseVoice (v); }
    }
    if (note == 55 && on) doToggle();
    if (note == 57) gateChange (on);
    if (note == 59 && on) targetOverride = 0;
}

// ------------------------------------------------------------------------------------------------ audio
void SpectralFreeze::fetch (const Voice& v, int64_t end, float* dst) const
{
    if (v.live)
    {
        int idx = (int) (((end - fftSize) % histSize + histSize) % histSize);
        for (int j = 0; j < fftSize; ++j)
        {
            dst[j] = hist[(size_t) idx] * window[(size_t) j];
            if (++idx >= histSize) idx = 0;
        }
    }
    else
    {
        const float* src = v.snap.data() + (end - fftSize);
        for (int j = 0; j < fftSize; ++j) dst[j] = src[j] * window[(size_t) j];
    }
    std::fill (dst + fftSize, dst + 2 * fftSize, 0.0f);
}

void SpectralFreeze::advance (Voice& v, double step)
{
    if (winLen <= 0.0) { v.u = 0; return; }
    auto rnd = [&v] { v.seed = v.seed * 1664525u + 1013904223u; return (double) (v.seed >> 8) / 16777216.0; };
    switch (movement)
    {
        case 0: v.u += step; while (v.u >= winLen) v.u -= winLen; break;
        case 1: v.u -= step; while (v.u < 0.0) v.u += winLen; break;
        case 3: if (rnd() < 0.04) v.dir = -v.dir; [[fallthrough]];
        case 2:
            v.u += step * v.dir;
            if (v.u >= winLen) { v.u = winLen; v.dir = -1; }
            else if (v.u <= 0.0) { v.u = 0; v.dir = 1; }
            break;
        default:
            v.skipAcc += step;
            if (v.skipAcc >= 1.0) { v.skipAcc -= std::floor (v.skipAcc); v.u = rnd() * winLen; }
            break;
    }
    v.u = juce::jlimit (0.0, winLen, v.u);
}

void SpectralFreeze::synthHop (Voice& v, int index)
{
    v.outBuf[0] = v.outBuf[(size_t) hop];
    int64_t end;
    if (v.live) end = histCount;
    else
    {
        v.u = juce::jlimit (0.0, winLen, v.u);
        const double back = juce::jlimit (0.0, (double) (kMaxOffset + kMaxDepth - 1), (double) offsetFrames + (winLen - v.u));
        end = (int64_t) histSize - (int64_t) std::llround (back * hop);
        end = juce::jlimit ((int64_t) (fftSize + hop), (int64_t) histSize, end);
    }
    fetch (v, end - hop, bufA.data());
    fetch (v, end, bufB.data());
    fft->performRealOnlyForwardTransform (bufA.data(), true);
    fft->performRealOnlyForwardTransform (bufB.data(), true);

    // advance each bin's phase by the phase difference between the two frames (one hop apart)
    for (int k = 0; k < bins; ++k)
    {
        const float ar = bufA[(size_t) k * 2], ai = bufA[(size_t) k * 2 + 1], br = bufB[(size_t) k * 2], bi = bufB[(size_t) k * 2 + 1];
        const float mag = std::sqrt (br * br + bi * bi);
        mags[(size_t) k] = mag;
        float pr = v.phRe[(size_t) k], pi = v.phIm[(size_t) k];
        if (v.fresh)
        {
            if (mag > 1.0e-12f) { pr = br / mag; pi = bi / mag; } else { pr = 1.0f; pi = 0.0f; }
        }
        else
        {
            const float cr = br * ar + bi * ai, ci = bi * ar - br * ai;
            const float cm = std::sqrt (cr * cr + ci * ci);
            if (cm > 1.0e-20f)
            {
                const float nr = (pr * cr - pi * ci) / cm, ni = (pr * ci + pi * cr) / cm;
                const float nm = std::sqrt (nr * nr + ni * ni);
                if (nm > 1.0e-12f) { pr = nr / nm; pi = ni / nm; }
            }
        }
        v.phRe[(size_t) k] = pr; v.phIm[(size_t) k] = pi;
    }
    v.fresh = false;

    // etherization: keep only the strongest spectral peaks and the bins around them
    if (ether < 127)
    {
        int numPeaks = 0;
        for (int k = 1; k < bins - 1; ++k)
            if (mags[(size_t) k] > mags[(size_t) k - 1] && mags[(size_t) k] >= mags[(size_t) k + 1]) peakScratch[(size_t) numPeaks++] = mags[(size_t) k];
        const float e = (float) ether / 127.0f;
        const int wanted = juce::jmax (2, (int) (0.25f * (float) bins * e * e * e));
        float thr = 0.0f;
        if (numPeaks > wanted)
        {
            std::nth_element (peakScratch.begin(), peakScratch.begin() + (wanted - 1), peakScratch.begin() + numPeaks, std::greater<float>());
            thr = peakScratch[(size_t) wanted - 1];
        }
        std::fill (keep.begin(), keep.end(), (uint8_t) 0);
        for (int k = 1; k < bins - 1; ++k)
            if (mags[(size_t) k] >= thr && mags[(size_t) k] > mags[(size_t) k - 1] && mags[(size_t) k] >= mags[(size_t) k + 1])
                for (int j = juce::jmax (0, k - 2); j <= juce::jmin (bins - 1, k + 2); ++j) keep[(size_t) j] = 1;
        for (int k = 0; k < bins; ++k) if (! keep[(size_t) k]) mags[(size_t) k] = 0.0f;
    }

    for (int k = 0; k < bins; ++k)
    {
        bufB[(size_t) k * 2] = mags[(size_t) k] * v.phRe[(size_t) k];
        bufB[(size_t) k * 2 + 1] = mags[(size_t) k] * v.phIm[(size_t) k];
    }
    std::fill (bufB.begin() + (size_t) bins * 2, bufB.end(), 0.0f);
    fft->performRealOnlyInverseTransform (bufB.data());
    const float norm = 2.0f / 3.0f; // Hann analysis x Hann synthesis at 75% overlap sums to 1.5
    for (int j = 0; j < fftSize; ++j) v.ola[(size_t) j] += bufB[(size_t) j] * window[(size_t) j] * norm;
    std::memcpy (v.outBuf.data() + 1, v.ola.data(), sizeof (float) * (size_t) hop);
    std::memmove (v.ola.data(), v.ola.data() + hop, sizeof (float) * (size_t) (fftSize - hop));
    std::fill (v.ola.end() - hop, v.ola.end(), 0.0f);

    // pitch shifting synthesizes more (or fewer) hops per second: scale the step so real-time speed is unchanged
    if (! v.live) advance (v, speed / ratio[index]);
}

void SpectralFreeze::applyReverb (float* L, float* R, int n, float mix)
{
    if (mix <= 0.0f) return;
    for (int i = 0; i < n; ++i)
    {
        float wl, wr;
        reverb.process (L[i], R[i], wl, wr);
        L[i] += mix * (wl - L[i]); R[i] += mix * (wr - R[i]);
    }
}

void SpectralFreeze::chunk (const float* inL, const float* inR, float* const* out, int n)
{
    float fL[kChunk], fR[kChunk];
    const bool mono = pv (65) != 0;
    for (int i = 0; i < n; ++i)
    {
        hist[(size_t) histW] = mono ? inL[i] : 0.5f * (inL[i] + inR[i]);
        if (++histW >= histSize) histW = 0;
        ++histCount;
        float l = 0, r = 0;
        for (int vi = 0; vi < kVoices; ++vi)
        {
            auto& v = voices[vi];
            if (! v.active) continue;
            if (v.rp >= (double) hop) { v.rp -= (double) hop; synthHop (v, vi); }
            const int i0 = (int) v.rp;
            const float fr = (float) (v.rp - i0);
            float s = v.outBuf[(size_t) i0] + fr * (v.outBuf[(size_t) i0 + 1] - v.outBuf[(size_t) i0]);
            v.rp += ratio[vi];
            if (v.gate) v.env = std::min (1.0f, v.env + attackInc);
            else
            {
                v.env -= releaseInc;
                if (v.env <= 0.0f) { v.env = 0.0f; v.active = false; }
            }
            s *= v.env;
            l += s * gainL[vi]; r += s * gainR[vi];
        }
        fL[i] = l; fR[i] = r;
    }
    const bool delayOnMix = pv (66) != 0, reverbOnMix = pv (67) != 0;
    const int dMode = pv (41);
    const float dLevel = (float) pv (42), dTime = (float) pv (43), dFb = (float) pv (44) / 100.0f, rMix = (float) pv (45) / 100.0f;
    if (! delayOnMix) delay.process (fL, fR, n, dMode, dLevel, dTime, dFb);
    if (! reverbOnMix) applyReverb (fL, fR, n, rMix);
    for (int i = 0; i < n; ++i)
    {
        fL[i] += inL[i] * dryGain;
        fR[i] += (mono ? inL[i] : inR[i]) * dryGain;
    }
    if (delayOnMix) delay.process (fL, fR, n, dMode, dLevel, dTime, dFb);
    if (reverbOnMix) applyReverb (fL, fR, n, rMix);
    const bool sat = pv (68) != 0;
    for (int i = 0; i < n; ++i)
    {
        out[2][i] = inL[i]; out[3][i] = inR[i]; // before out[0]/out[1], in case the host buffers alias
        out[0][i] = sat ? softSat (fL[i]) : fL[i];
        out[1][i] = sat ? softSat (fR[i]) : fR[i];
    }
}

void SpectralFreeze::render (const float* inL, const float* inR, float* const* out, int n)
{
    for (int pos = 0; pos < n; pos += kChunk)
    {
        float* sub[4] = { out[0] + pos, out[1] + pos, out[2] + pos, out[3] + pos };
        chunk (inL + pos, inR + pos, sub, juce::jmin (kChunk, n - pos));
    }
    for (int v = 0; v < kVoices; ++v)
    {
        const auto& vc = voices[v];
        uiState[v] = ! vc.active ? Idle : ! vc.gate ? Releasing : vc.live ? Live : Frozen;
        uiEnv[v] = vc.env;
        uiPos[v] = vc.live || winLen <= 0.0 ? 1.0f : (float) (vc.u / winLen);
    }
}

// ------------------------------------------------------------------------------------------------ UI
ExAlgorithm::Meter SpectralFreeze::meter (int index)
{
    Meter m;
    m.on = uiState[index].load() == Frozen || uiState[index].load() == Live;
    m.level = uiEnv[index].load();
    m.label = juce::String (index + 1) + (target() == index + 1 ? "T" : "");
    return m;
}

juce::String SpectralFreeze::statusText()
{
    const int t = target();
    return "Target " + (t == 0 ? juce::String ("Auto") : juce::String (t));
}

std::vector<BrowserSpec> SpectralFreeze::browsers()
{
    BrowserSpec t;
    t.title = "Target"; t.compact = true;
    t.items = [] { return juce::StringArray { "A", "1", "2", "3", "4" }; };
    t.selected = [this] { return target(); };
    t.choose = [this] (int i) { set (9, i); targetOverride = -1; };
    BrowserSpec vs;
    vs.title = "Voices";
    vs.items = [this] {
        juce::StringArray a;
        static const char* names[] = { "-", "FROZEN", "LIVE", "RELEASE" };
        for (int v = 0; v < kVoices; ++v) a.add (juce::String (v + 1) + " " + names[juce::jlimit (0, 3, uiState[v].load())]);
        return a; };
    vs.selected = [this] { return target() - 1; };
    vs.choose = [this] (int i) {
        if (i < 0 || i >= kVoices) return;
        if (pv (10 + i) != 0) set (10 + i, 0); else voiceReq[i].fetch_add (1); };
    BrowserSpec mv;
    mv.title = "Movement";
    mv.items = [this] { return def (14).labels; };
    mv.selected = [this] { return pv (14); };
    mv.choose = [this] (int i) { set (14, i); };
    return { t, vs, mv };
}

std::vector<ActionSpec> SpectralFreeze::actions()
{
    auto any = [this] (int state) { for (auto& s : uiState) if (s.load() == state) return true; return false; };
    return {
        { "Freeze", [this] { toggleReq.fetch_add (1); }, [any] { return any (Frozen); } },
        { "Live", [this] { liveReq.fetch_add (1); }, [any] { return any (Live); } },
        { "Unfreeze", [this] { unfreezeReq.fetch_add (1); for (int v = 0; v < kVoices; ++v) set (10 + v, 0); }, {} },
        { "Visuals", [this] { visuals = ! visuals; }, [this] { return visuals; } },
        { "Clear FX", [this] { clearFxReq.fetch_add (1); plugin.message ("Delay + reverb cleared"); }, {} },
    };
}

void SpectralFreeze::addMenuItems (std::vector<MenuItem>& items)
{
    items.push_back (MenuItem::act ("Unfreeze all", [this] { unfreezeReq.fetch_add (1); plugin.message ("Unfreeze all"); }));
    items.push_back (MenuItem::val ("Visuals", 0, 1, [this] { return visuals ? 1 : 0; }, [this] (int v) { visuals = v != 0; },
                                    [] (int v) { return v ? juce::String ("On") : juce::String ("Off"); }));
    items.push_back (MenuItem::sub ("MIDI notes", {
        MenuItem::val ("Enable", 0, 1, [this] { return midiNotes.enable; }, [this] (int v) { midiNotes.enable = v; }),
        MenuItem::val ("Lowest note", 0, 127, [this] { return midiNotes.lowest; }, [this] (int v) { midiNotes.lowest = v; }, [] (int v) { return noteName (v); }),
        MenuItem::val ("Highest note", 0, 127, [this] { return midiNotes.highest; }, [this] (int v) { midiNotes.highest = v; }, [] (int v) { return noteName (v); }),
    }));
}

void SpectralFreeze::drawBottomLine (Oled& o, int)
{
    static const char* st[] = { "-", "F", "L", "r" };
    const int t = target();
    for (int v = 0; v < kVoices; ++v)
        o.at (v * 5, 3, juce::String (v + 1) + (t == v + 1 ? "T" : " ") + st[juce::jlimit (0, 3, uiState[v].load())]);
}

void SpectralFreeze::drawPage (Oled& o, int cur)
{
    if (! visuals) { FxAlgorithm::drawPage (o, cur); return; }
    const int t = target();
    const int depth = pv (16);
    for (int v = 0; v < kVoices; ++v)
    {
        const int x0 = (v % 2) * 64, y0 = (v / 2) * 16;
        const int state = uiState[v].load();
        o.text (x0 + 4, y0 + 4, juce::String (v + 1) + (t == v + 1 ? "T" : "") + (state == Live ? "L" : ""));
        const int h = juce::jlimit (0, 14, (int) std::round (uiEnv[v].load() * 14.0f));
        if (h > 0) o.fillRect (x0, y0 + 15 - h, 2, h);
        // the segment of audio in use (its width follows Depth) and the play position inside it
        const int xs = x0 + 24, full = 36;
        for (int x = xs; x <= xs + full; x += 2) o.set (x, y0 + 13);
        if (state != Idle)
        {
            const int w = juce::jmax (2, full * depth / kMaxDepth);
            const int xe = xs + full, xa = xe - w;
            o.vline (xa, y0 + 7, y0 + 13); o.vline (xe, y0 + 7, y0 + 13);
            const int xp = xa + (int) std::round (juce::jlimit (0.0f, 1.0f, uiPos[v].load()) * (float) w);
            o.vline (xp, y0 + 3, y0 + 13);
        }
    }
    for (int x = 0; x < Oled::W; x += 4) o.set (x, 16);
    for (int y = 0; y < Oled::H; y += 4) o.set (63, y);
}
} // namespace ex
