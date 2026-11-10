#include "WavRecorder.h"

namespace ex
{
enum CmdType { CmdOpen = 1, CmdClose = 2 };

WavRecorder::WavRecorder (ExPlugin& p) : ExAlgorithm (p, 5, "WAV Recorder"), juce::Thread ("EX WAV writer")
{
    addAttenuverters();
    add ("Input gain", 0, 24, 0, "dB");                                   // 7
    add ("Sample rate", 0, 1, 0, {}, { "48kHz", "96kHz" });                // 8
    add ("Bit depth", 0, 1, 0, {}, { "16 bit", "24 bit" });                // 9
    add ("Channels", 1, 6, 2);                                            // 10
    add ("Rec CV Mode", 0, 1, 0, {}, { "Trigger", "Gate" });               // 11
    add ("Play CV Mode", 0, 1, 0, {}, { "Trigger", "Gate" });              // 12
    add ("Play As Loop", 0, 1, 0, {}, { "One-shot", "Loop" });             // 13
    add ("Outputs 1/2", 0, 2, 1, {}, { "Thru", "Thru/Play", "Play" });     // 14
    add ("Outputs 3/4", 0, 2, 0, {}, { "Thru", "Thru/Play", "Play" });     // 15
    add ("AS Enable", 0, 1, 0, {}, { "Off", "On" });                       // 16
    add ("AS Start Note", 0, 127, 21); defs.back().fmt = [] (int v) { return noteName (v); };   // 17
    add ("AS End Note", 0, 127, 108); defs.back().fmt = [] (int v) { return noteName (v); };    // 18
    add ("AS Note Step", 1, 127, 1);                                     // 19
    add ("AS Vel Sw", 1, 9, 1);                                          // 20
    add ("AS RRobins", 1, 9, 1);                                         // 21
    add ("AS Coarse", 0, 60, 1, "s");                                     // 22
    add ("AS Fine", -1000, 1000, 0, "ms");                                // 23
    add ("AS Gap", 1, 600, 1); defs.back().fmt = [] (int v) { return juce::String (v * 100) + "ms"; }; // 24
    add ("AS Latency", 0, 960, 0);                                       // 25
    for (int i = 1; i <= 6; ++i)                                         // 26-31
    {
        add ("Thru routing " + juce::String (i), 0, 6, i == 1 ? 1 : i == 2 ? 2 : 0, {}, { "None", "1", "2", "3", "4", "1&2", "3&4" });
    }
    add ("AS Min velocity", 0, 127, 25);                                 // 32
    add ("AS Max velocity", 0, 127, 127);                                // 33
    add ("Playback file", 0, 9999, 0);                                   // 34
    defs.back().fmt = [this] (int v) { return v == 0 ? juce::String ("Last") : v - 1 < recFiles.size() ? recFiles[v - 1].getFileNameWithoutExtension() : juce::String ("-"); };
    add ("Record enable", 0, 1, 1, {}, { "Locked", "On" });               // 35
    add ("Playback enable", 0, 1, 1, {}, { "Locked", "On" });             // 36
    add ("Playback gain", -40, 6, 0, "dB");                               // 37
    add ("AS Preview note", 0, 127, 48); defs.back().fmt = [] (int v) { return noteName (v); }; // 38
    defaultL = 35; defaultR = 36;
    ring.assign ((size_t) kRing * 2, 0.0f);
    lastRecording = scanLastRecording();
    startThread();
    startTimerHz (10);
    sections = {
        { "RECORD", { 7, 8, 9, 10, 13, 37, 14, 15, 26, 27 } },
        { "AUTO-SAMPLER", { 16, 17, 18, 19, 20, 21, 32, 33, 22, 23, 24, 25, 38 } },
    };
}

std::vector<BrowserSpec> WavRecorder::browsers()
{
    BrowserSpec b;
    b.title = "Recordings";
    b.items = [this] { juce::StringArray a { "(last recording)" }; for (auto& f : recFiles) a.add (f.getFileNameWithoutExtension()); return a; };
    b.selected = [this] { return pv (34); };
    b.choose = [this] (int i) { set (34, i); };
    BrowserSpec a;
    a.title = "Auto-sampled";
    a.items = [] { juce::StringArray s; for (auto& f : SdCard::sortedChildren (SdCard::root(), juce::File::findDirectories, "AUTO*")) s.add (f.getFileName()); return s; };
    a.selected = [] { return -1; };
    a.choose = [this] (int i) {
        auto dirs = SdCard::sortedChildren (SdCard::root(), juce::File::findDirectories, "AUTO*");
        if (i < 0 || i >= dirs.size()) return;
        int idx = SdCard::sampleFolders().indexOf (dirs[i]);
        if (idx < 0) { plugin.message ("Folder is empty"); return; }
        plugin.selectAlgorithm (0);
        plugin.algAt (0).onSelected();
        plugin.algAt (0).set (7, idx);
        plugin.message ("Playing " + dirs[i].getFileName()); };
    return { b, a };
}

std::vector<ActionSpec> WavRecorder::actions()
{
    return {
        { "Record", [this] { if (pv (16)) set (16, 0); if (recording) reqStopRecord = true; else reqRecord = true; }, [this] { return recording.load(); }, true },
        { "Play", [this] { if (playing) playing = false; else loadPlayback(); }, [this] { return playing.load(); } },
        { "Auto", [this] {
              if (state == Sampling) reqStopSampling = true;
              else if (state == Idle && ! recording) { set (16, 1); buildJobs(); reqStartSampling = true; } },
          [this] { return state.load() == Sampling; }, true },
        { "Test", [this] { if (state == Idle) reqPreview = true; }, [this] { return state.load() == Preview; } },
        { "Folder", [this] { recordDir().createDirectory(); recordDir().revealToUser(); }, {} },
    };
}

juce::String WavRecorder::statusText()
{
    if (state == Sampling) return "Auto-sampling " + currentFile + "  (" + juce::String ((int) jobs.size() - jobIndex) + " left)";
    if (state == Preview) return "Testing timing...";
    if (latencyMeasured >= 0 && pv (16)) return "Measured latency: " + juce::String (latencyMeasured) + " samples";
    if (recording) return "Recording REC" + juce::String (lastRecording).paddedLeft ('0', 5);
    if (pv (16)) return juce::String (jobsCount()) + " samples to record - MIDI out ch " + juce::String (plugin.txChannel) + ", CV/gate on Out 3/4";
    return {};
}

WavRecorder::~WavRecorder()
{
    stopTimer();
    signalThreadShouldExit();
    stopThread (4000);
    writer.reset();
    stopJobs();
}

void WavRecorder::prepare (double sampleRate, int)
{
    sr = sampleRate;
}

void WavRecorder::onSelected()
{
    lastRecording = scanLastRecording();
    recFiles = SdCard::sortedChildren (recordDir(), juce::File::findFiles, "REC*.WAV");
}

int WavRecorder::scanLastRecording() const
{
    int m = 0;
    for (auto& f : recordDir().findChildFiles (juce::File::findFiles, false, "REC*"))
        m = juce::jmax (m, f.getFileNameWithoutExtension().substring (3).getIntValue());
    return m;
}

void WavRecorder::pushCommand (int type, int64_t frame, const juce::String& path)
{
    int s1, n1, s2, n2;
    cmdFifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 + n2 < 1) return;
    auto& c = cmds[(size_t) (n1 > 0 ? s1 : s2)];
    c.type = type; c.frame = frame;
    path.copyToUTF8 (c.path, sizeof (c.path));
    c.channels = juce::jlimit (1, 2, pv (10));
    c.bits = pv (9) ? 24 : 16;
    c.rate = pv (8) ? 96000 : 48000;
    cmdFifo.finishedWrite (1);
}

// ---------------------------------------------------------------------------------------------- writer thread
void WavRecorder::run()
{
    std::vector<float> outL, outR;
    auto writeRange = [&] (int64_t a, int64_t b)
    {
        if (b <= a) return;
        if (! writer) return;
        const double step = sr / (double) writerRate;
        outL.clear(); outR.clear();
        for (int64_t f = a; f < b; ++f)
        {
            size_t idx = (size_t) (f & (kRing - 1)) * 2;
            float x[2] = { ring[idx], ring[idx + 1] };
            if (std::abs (step - 1.0) < 1e-9) { outL.push_back (x[0]); outR.push_back (x[1]); continue; }
            for (int c = 0; c < 2; ++c) { auto* h = rsHist[c]; h[0] = h[1]; h[1] = h[2]; h[2] = h[3]; h[3] = x[c]; }
            while (rsPos < 1.0)
            {
                float fr = (float) rsPos;
                for (int c = 0; c < 2; ++c)
                {
                    auto* h = rsHist[c];
                    float c1 = 0.5f * (h[2] - h[0]), c2 = h[0] - 2.5f * h[1] + 2.0f * h[2] - 0.5f * h[3], c3 = 0.5f * (h[3] - h[0]) + 1.5f * (h[1] - h[2]);
                    float y = ((c3 * fr + c2) * fr + c1) * fr + h[1];
                    (c == 0 ? outL : outR).push_back (y);
                }
                rsPos += step;
            }
            rsPos -= 1.0;
        }
        const float* chans[2] = { outL.data(), outR.data() };
        writer->writeFromFloatArrays (chans, writerChannels, (int) outL.size());
    };

    while (! threadShouldExit())
    {
        const int64_t w = ringWrite.load();
        if (w - ringRead > kRing - 4096) ringRead = w - (kRing - 4096); // overflow: drop
        int s1, n1, s2, n2;
        cmdFifo.prepareToRead (1, s1, n1, s2, n2);
        if (n1 + n2 > 0)
        {
            const auto& c = cmds[(size_t) (n1 > 0 ? s1 : s2)];
            if (c.frame <= w)
            {
                writeRange (ringRead, juce::jmax (ringRead, c.frame));
                ringRead = juce::jmax (ringRead, c.frame);
                if (writer) { writer->flush(); writer.reset(); }
                if (c.type == CmdOpen)
                {
                    juce::File f (juce::String::fromUTF8 (c.path));
                    f.getParentDirectory().createDirectory();
                    f.deleteFile();
                    std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (f);
                    juce::WavAudioFormat fmt;
                    writer = fmt.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (c.rate).withNumChannels (c.channels).withBitsPerSample (c.bits));
                    writerChannels = c.channels; writerRate = c.rate;
                    rsPos = 0; std::memset (rsHist, 0, sizeof (rsHist));
                }
                cmdFifo.finishedRead (1);
                continue;
            }
        }
        writeRange (ringRead, w);
        ringRead = w;
        wait (5);
    }
}

// ---------------------------------------------------------------------------------------------- audio
int64_t WavRecorder::noteLenSamples() const
{
    double s = pv (22) + pv (23) / 1000.0;
    return (int64_t) (juce::jmax (0.01, s) * sr);
}
int64_t WavRecorder::gapSamples() const { return (int64_t) (pv (24) * 0.1 * sr); }

void WavRecorder::sendNote (juce::MidiBuffer& out, int pos, int note, int vel, bool on)
{
    int ch = plugin.txChannel;
    if (ch <= 0) return;
    out.addEvent (on ? juce::MidiMessage::noteOn (ch, note, (juce::uint8) juce::jlimit (1, 127, vel)) : juce::MidiMessage::noteOff (ch, note), pos);
}

void WavRecorder::buildJobs()
{
    int n = 1;
    while (SdCard::root().getChildFile ("AUTO" + juce::String (n).paddedLeft ('0', 4)).exists()) ++n;
    autoFolder = SdCard::root().getChildFile ("AUTO" + juce::String (n).paddedLeft ('0', 4));
    autoFolder.createDirectory();
    jobs.clear();
    const int nVel = pv (20), nRR = pv (21), lo = pv (17), hi = pv (18), step = pv (19);
    for (int note = lo; note <= hi; note += step)
        for (int v = 1; v <= nVel; ++v)
            for (int rr = 1; rr <= nRR; ++rr)
            {
                int vel = nVel == 1 ? pv (33) : juce::roundToInt (pv (32) + (pv (33) - pv (32)) * (v - 1) / (double) (nVel - 1));
                juce::String name = juce::String (note).paddedLeft ('0', 3) + "_" + noteName (note);
                if (nVel > 1) name << "_V" << v;
                if (nRR > 1) name << "_RR" << rr;
                jobs.push_back ({ note, vel, autoFolder.getChildFile (name + ".wav").getFullPathName() });
            }
}

void WavRecorder::process (juce::AudioBuffer<float>& out, const juce::AudioBuffer<float>& in, juce::MidiBuffer& midi, juce::MidiBuffer& midiOut)
{
    juce::ignoreUnused (midi);
    const int n = out.getNumSamples();
    const float g = dbToGain ((float) pv (7));
    const int64_t now = plugin.sampleClock;
    const int64_t w0 = ringWrite.load();
    const int64_t latency = pv (25);

    // requests from the UI
    if (reqRecord.exchange (false) && ! recording && state == Idle)
    {
        ++lastRecording;
        pushCommand (CmdOpen, w0, recordDir().getChildFile ("REC" + juce::String (lastRecording).paddedLeft ('0', 5) + ".WAV").getFullPathName());
        recording = true; recStartFrame = now;
    }
    if (reqStopRecord.exchange (false) && recording) { pushCommand (CmdClose, w0, {}); recording = false; }
    if (reqStopSampling.exchange (false) && state != Idle)
    {
        if (noteIsOn) sendNote (midiOut, 0, state == Preview ? previewNote : jobs[(size_t) juce::jmin (jobIndex, (int) jobs.size() - 1)].note, 0, false);
        noteIsOn = false;
        if (state == Sampling) pushCommand (CmdClose, w0, {});
        state = Idle;
    }
    if (reqStartSampling.exchange (false) && state == Idle && ! jobs.empty() && ! recording)
    {
        state = Sampling; jobIndex = 0; jobStart = now; samplingStart = now;
        sendNote (midiOut, 0, jobs[0].note, jobs[0].vel, true); noteIsOn = true;
        currentFile = juce::File (jobs[0].path).getFileName();
        pushCommand (CmdOpen, w0 + latency, jobs[0].path);
    }
    if (reqPreview.exchange (false) && state == Idle)
    {
        state = Preview; jobStart = now; previewNote = pv (38); latencyMeasured = -1;
        sendNote (midiOut, 0, previewNote, pv (33), true); noteIsOn = true;
    }

    const int64_t len = noteLenSamples(), gap = gapSamples();
    float pk[2] = { 0, 0 };
    auto pb = std::atomic_load (&playback);
    const bool loop = pv (13) != 0;
    const float pg = dbToGain ((float) pv (37));
    const double pbStep = pb ? pb->sampleRate / sr : 1.0;
    int curNote = 48;
    if (state == Sampling && jobIndex < (int) jobs.size()) curNote = jobs[(size_t) jobIndex].note;
    if (state == Preview) curNote = previewNote;

    for (int i = 0; i < n; ++i)
    {
        float x[2] = { in.getSample (0, i) * g, in.getSample (1, i) * g };
        if (pv (10) == 1) x[1] = x[0];
        size_t idx = (size_t) ((w0 + i) & (kRing - 1)) * 2;
        ring[idx] = x[0]; ring[idx + 1] = x[1];
        pk[0] = std::max (pk[0], std::abs (x[0])); pk[1] = std::max (pk[1], std::abs (x[1]));

        // ---- auto-sampler timeline ----
        if (state != Idle)
        {
            const int64_t t = now + i - jobStart;
            if (t == len && noteIsOn) { sendNote (midiOut, i, curNote, 0, false); noteIsOn = false; }
            if (state == Preview)
            {
                if (latencyMeasured < 0 && t < len + gap && (std::abs (x[0]) > 0.01f || std::abs (x[1]) > 0.01f)) latencyMeasured = t;
                if (t >= len + gap) state = Idle;
            }
            else if (t >= len + gap + latency)
            {
                pushCommand (CmdClose, w0 + i, {});
                ++jobIndex;
                if (jobIndex >= (int) jobs.size()) { state = Idle; }
                else
                {
                    jobStart = now + i;
                    curNote = jobs[(size_t) jobIndex].note;
                    currentFile = juce::File (jobs[(size_t) jobIndex].path).getFileName();
                    sendNote (midiOut, i, curNote, jobs[(size_t) jobIndex].vel, true); noteIsOn = true;
                    pushCommand (CmdOpen, w0 + i + latency, jobs[(size_t) jobIndex].path);
                }
            }
        }

        // ---- outputs ----
        float play[2] = { 0, 0 };
        if (playing && pb)
        {
            int64_t ip = (int64_t) playPos;
            if (ip >= pb->frames) { if (loop) { playPos = 0; ip = 0; } else { playing = false; } }
            if (playing)
            {
                play[0] = pb->sample (ip, 0) * pg; play[1] = pb->sample (ip, 1) * pg;
                playPos += pbStep;
            }
        }
        float thru[4] = { 0, 0, 0, 0 };
        for (int c = 0; c < 2; ++c)
        {
            int r = pv (26 + c);
            if (r >= 1 && r <= 4) thru[r - 1] += x[c];
            else if (r == 5) { thru[0] += x[c]; thru[1] += x[c]; }
            else if (r == 6) { thru[2] += x[c]; thru[3] += x[c]; }
        }
        for (int pair = 0; pair < 2; ++pair)
        {
            int mode = pv (14 + pair);
            for (int c = 0; c < 2; ++c)
            {
                float v = 0;
                if (mode == 0) v = thru[pair * 2 + c];
                else if (mode == 1) v = playing ? play[c] : thru[pair * 2 + c];
                else v = playing ? play[c] : 0.0f;
                out.setSample (pair * 2 + c, i, v);
            }
        }
        if (state != Idle) // CV / gate for the instrument being sampled
        {
            out.setSample (2, i, (float) (curNote - 48) / 120.0f);
            out.setSample (3, i, noteIsOn ? 0.5f : 0.0f);
        }
    }
    ringWrite = w0 + n;
    peak[0] = std::max (peak[0].load(), pk[0]);
    peak[1] = std::max (peak[1].load(), pk[1]);
}

// ---------------------------------------------------------------------------------------------- controls
bool WavRecorder::pushL()
{
    if (pv (35) == 0) { plugin.message ("LOCKED", 1.0, true); return true; }
    if (pv (16))
    {
        if (state == Sampling) { reqStopSampling = true; plugin.message ("Auto-sampling stopped"); }
        else if (state == Idle) { buildJobs(); reqStartSampling = true; }
        return true;
    }
    if (recording) reqStopRecord = true;
    else reqRecord = true;
    return true;
}

bool WavRecorder::pushR (int cur)
{
    juce::ignoreUnused (cur);
    if (pv (36) == 0) { plugin.message ("LOCKED", 1.0, true); return true; }
    if (pv (16)) { if (state == Idle) reqPreview = true; return true; }
    if (playing) { playing = false; return true; }
    loadPlayback();
    return true;
}

void WavRecorder::loadPlayback()
{
    recFiles = SdCard::sortedChildren (recordDir(), juce::File::findFiles, "REC*.WAV");
    if (recFiles.isEmpty()) { plugin.message ("No recordings"); return; }
    int v = pv (34);
    auto f = v == 0 || v - 1 >= recFiles.size() ? recFiles.getLast() : recFiles[v - 1];
    runAsync ([this, f] {
        auto w = std::make_shared<WavData>();
        if (loadWav (f, *w) && w->frames > 0)
        {
            std::atomic_store (&playback, w);
            playPos = 0; playing = true;
        }
    });
}

void WavRecorder::addMenuItems (std::vector<MenuItem>& items)
{
    items.push_back (MenuItem::act ("Stop auto-sampling", [this] { reqStopSampling = true; }));
    items.push_back (MenuItem::act ("Show recordings", [this] { recordDir().createDirectory(); recordDir().revealToUser(); }));
}

void WavRecorder::timerCallback()
{
    static bool wasBusy = false;
    bool busy = recording || state != Idle;
    if (wasBusy && ! busy) recFiles = SdCard::sortedChildren (recordDir(), juce::File::findFiles, "REC*.WAV");
    wasBusy = busy;
}

// ---------------------------------------------------------------------------------------------- display
static juce::String hms (double secs)
{
    int ms = (int) (secs * 1000.0);
    int h = ms / 3600000, m = (ms / 60000) % 60, s = (ms / 1000) % 60, f = ms % 1000;
    return juce::String (h).paddedLeft ('0', 2) + ":" + juce::String (m).paddedLeft ('0', 2) + ":" + juce::String (s).paddedLeft ('0', 2) + "." + juce::String (f).paddedLeft ('0', 3);
}

void WavRecorder::drawPage (Oled& o, int cur)
{
    const double t = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const juce::String num = juce::String (lastRecording).paddedLeft ('0', 5);
    const int numX = 128 - 30;
    const int st = state.load();
    if (st == Sampling)
    {
        o.text (0, 0, hms ((double) (plugin.sampleClock - samplingStart) / sr));
        for (int y = 1; y < 7; ++y) o.hline (78 + (y == 1 || y == 6 ? 1 : 0), 83 - (y == 1 || y == 6 ? 1 : 0), y);
        o.text (numX, 0, autoFolder.getFileName().substring (4).paddedLeft ('0', 5));
        int phase = (int) (t * 20.0);
        for (int x = numX; x < 128; ++x) for (int y = 0; y < 8; ++y) if (((x + y + phase) / 3) % 2 == 0) o.flip (x, y);
        o.at (0, 1, "Auto-sampling");
        o.right (21, 1, juce::String (juce::jmax (0, (int) jobs.size() - jobIndex)).paddedLeft ('0', 4));
        o.at (0, 2, currentFile.substring (0, 21));
    }
    else if (st == Preview || (pv (16) && latencyMeasured >= 0 && cur == 25))
    {
        o.at (0, 0, headerText()); o.text (numX, 0, num);
        o.at (0, 1, "Auto-sampling");
        o.at (0, 2, "Testing timing");
        if (latencyMeasured >= 0) o.right (21, 2, juce::String (latencyMeasured));
    }
    else
    {
        ExAlgorithm::drawPage (o, cur);
        o.fillRect (0, 24, 128, 8, false);
        o.fillRect (numX - 6, 0, 36, 8, false);
        if (recording)
        {
            o.fillRect (0, 0, numX, 8, false);
            o.text (0, 0, hms ((double) (plugin.sampleClock - recStartFrame) / sr));
            for (int y = 1; y < 7; ++y) o.hline (88 + (y == 1 || y == 6 ? 1 : 0), 93 - (y == 1 || y == 6 ? 1 : 0), y);
            o.text (numX, 0, num);
            int phase = (int) (t * 20.0);
            for (int x = numX; x < 128; ++x) for (int y = 0; y < 8; ++y) if (((x + y + phase) / 3) % 2 == 0) o.flip (x, y);
        }
        else if (playing)
        {
            auto pb = std::atomic_load (&playback);
            o.fillRect (0, 0, numX, 8, false);
            if (pb) o.text (0, 0, hms (playPos / pb->sampleRate));
            for (int y = 1; y < 7; ++y) o.hline (88, 88 + (y < 4 ? y : 7 - y), y);
            o.text (numX, 0, num);
            if (pb) o.invertRect (numX, 0, (int) (30.0 * playPos / juce::jmax ((double) 1, (double) pb->frames)), 8);
        }
        else o.text (numX, 0, num);
    }
    // level meters with peak hold and clip indicators
    for (int c = 0; c < 2; ++c)
    {
        float p = peak[c].exchange (0.0f);
        meterDisp[c] = std::max (p, meterDisp[c] * 0.85f);
        if (p >= holdVal[c] || t > holdUntil[c]) { holdVal[c] = p; holdUntil[c] = t + 1.5; }
        if (p >= 0.999f) clipUntil[c] = t + 2.0;
        auto toX = [] (float v) { float db = 20.0f * std::log10 (std::max (v, 1e-5f)); return juce::jlimit (0, 118, (int) ((db + 60.0f) / 60.0f * 118.0f)); };
        int y = 25 + c * 4;
        int x = toX (meterDisp[c]);
        if (x > 0) { o.hline (0, x - 1, y); o.hline (0, x - 1, y + 1); }
        int hx = toX (holdVal[c]);
        if (hx > 0) { o.set (hx, y); o.set (hx, y + 1); }
        o.set (122, y); // scale end tick
        if (t < clipUntil[c]) o.fillRect (124, y - 1, 4, 3);
    }
}
} // namespace ex
