#pragma once
// Base for the algorithms that are not built on the polyphonic note engine (effects, Plaits/Rings ports):
// splits each block at its MIDI events and hands the pieces to render().
#include "ExPlugin.h"

namespace ex
{
class FxAlgorithm : public ExAlgorithm
{
public:
    using ExAlgorithm::ExAlgorithm;

    double sr = 48000;
    int maxBlock = 512;

    // ---- to implement ----
    virtual void configure() {}                                   // read parameters (once per block)
    virtual void midi (const juce::MidiMessage& m) { juce::ignoreUnused (m); }
    // in: stereo input, out: outputs 1-4 (cleared); n samples
    virtual void render (const float* inL, const float* inR, float* const* out, int n) = 0;

    void prepareFx (double sampleRate, int block) { sr = sampleRate; maxBlock = block; }

    // 0-based offset of a MIDI channel from the receive channel (0 for everything in omni); negative = not for us
    int channelOffset (int ch) const
    {
        if (plugin.rxChannel == 0) return 0;
        return ch - plugin.rxChannel;
    }

    void process (juce::AudioBuffer<float>& out, const juce::AudioBuffer<float>& in, juce::MidiBuffer& midiIn, juce::MidiBuffer&) override
    {
        configure();
        const int n = out.getNumSamples();
        float* ch[4] = { out.getWritePointer (0), out.getWritePointer (1), out.getWritePointer (2), out.getWritePointer (3) };
        const float* inL = in.getReadPointer (0);
        const float* inR = in.getReadPointer (1);
        int pos = 0;
        auto it = midiIn.cbegin();
        while (pos < n)
        {
            while (it != midiIn.cend() && (*it).samplePosition <= pos) { midi ((*it).getMessage()); ++it; }
            int next = n;
            if (it != midiIn.cend()) next = juce::jmin (next, (*it).samplePosition);
            int len = juce::jmax (1, next - pos);
            float* sub[4] = { ch[0] + pos, ch[1] + pos, ch[2] + pos, ch[3] + pos };
            render (inL + pos, inR + pos, sub, len);
            pos += len;
        }
        for (; it != midiIn.cend(); ++it) midi ((*it).getMessage());
    }
};
} // namespace ex
