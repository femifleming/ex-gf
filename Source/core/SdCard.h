#pragma once
// The virtual MicroSD card: a folder laid out like a disting EX card, plus WAV reading/writing helpers.
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>
#include <memory>

namespace ex
{
struct SdCard
{
    static juce::File defaultRoot()
    {
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("EX SD Card");
    }
    static juce::PropertiesFile& globalSettings()
    {
        static std::unique_ptr<juce::PropertiesFile> pf;
        if (! pf)
        {
            juce::PropertiesFile::Options o;
            o.applicationName = "ex-girlfriend"; o.filenameSuffix = "settings"; o.folderName = "ex-girlfriend";
            o.osxLibrarySubFolder = "Application Support";
            // one-time migration from the pre-release name ("EX Suite")
            auto newDir = o.getDefaultFile().getParentDirectory();
            juce::PropertiesFile::Options old = o;
            old.applicationName = "EX Suite"; old.folderName = "EX Suite";
            auto oldDir = old.getDefaultFile().getParentDirectory();
            if (! newDir.isDirectory() && oldDir.isDirectory())
            {
                oldDir.copyDirectoryTo (newDir);
                auto oldSettings = newDir.getChildFile (old.getDefaultFile().getFileName());
                if (oldSettings.existsAsFile()) oldSettings.moveFileTo (o.getDefaultFile());
            }
            pf = std::make_unique<juce::PropertiesFile> (o);
        }
        return *pf;
    }
    static juce::File root()
    {
        auto env = juce::SystemStats::getEnvironmentVariable ("EX_SD_ROOT", {});
        if (env.isNotEmpty()) return juce::File (env);
        auto p = globalSettings().getValue ("sdRoot");
        juce::File f = p.isNotEmpty() ? juce::File (p) : defaultRoot();
        if (! f.isDirectory()) f.createDirectory();
        return f;
    }
    static void setRoot (const juce::File& f)
    {
        globalSettings().setValue ("sdRoot", f.getFullPathName());
        globalSettings().saveIfNeeded();
    }
    static bool isSpecialFolder (const juce::String& n)
    {
        static const char* special[] = { "wavetables", "exciters", "FMSYX", "!RECORD", "scl", "kbm", "MTS", "presets", "PRST", "System Volume Information" };
        for (auto* s : special) if (n.equalsIgnoreCase (s) || n.startsWithIgnoreCase ("PRST")) return true;
        return n.startsWith (".");
    }
    static juce::Array<juce::File> sortedChildren (const juce::File& dir, int what, const juce::String& pattern = "*")
    {
        auto a = dir.findChildFiles (what, false, pattern);
        std::sort (a.begin(), a.end(), [] (const juce::File& x, const juce::File& y) { return x.getFileName().compareNatural (y.getFileName()) < 0; });
        return a;
    }
    static bool isWav (const juce::File& f)
    {
        return f.hasFileExtension ("wav") && ! f.getFileName().startsWith ("._") && ! f.getFileName().startsWith (".");
    }
    // sample folders: top-level folders that contain at least one WAV, excluding the special folders
    static juce::Array<juce::File> sampleFolders()
    {
        juce::Array<juce::File> out;
        for (auto& d : sortedChildren (root(), juce::File::findDirectories))
        {
            if (isSpecialFolder (d.getFileName())) continue;
            for (auto& f : d.findChildFiles (juce::File::findFiles, false, "*.wav"))
                if (isWav (f)) { out.add (d); break; }
        }
        return out;
    }
    // wavetables / exciters: entries are sub-folders or single WAV files, optional playlist
    static juce::Array<juce::File> tableEntries (const juce::String& folderName, const juce::String& playlistName)
    {
        juce::Array<juce::File> out;
        auto dir = root().getChildFile (folderName);
        if (! dir.isDirectory()) return out;
        auto pl = dir.getChildFile (playlistName);
        if (playlistName.isNotEmpty() && pl.existsAsFile())
        {
            juce::StringArray lines; lines.addLines (pl.loadFileAsString());
            for (int i = 1; i < lines.size(); ++i)
            {
                auto l = lines[i].trim();
                if (l.isEmpty() || l.startsWith ("-")) continue;
                auto f = dir.getChildFile (l);
                if (f.exists()) out.add (f);
            }
            if (! out.isEmpty()) return out;
        }
        for (auto& f : sortedChildren (dir, juce::File::findFilesAndDirectories))
        {
            if (f.getFileName().startsWith (".")) continue;
            if (f.isDirectory() || isWav (f)) out.add (f);
        }
        return out;
    }
};

// ----------------------------------------------------------------------------------------------
// WAV file with loop metadata ('smpl' and 'cue ' chunks), read into float memory.
struct WavData
{
    int channels = 0;
    double sampleRate = 48000;
    int64_t frames = 0;
    std::vector<float> data; // interleaved
    int64_t loopStart = -1, loopEnd = -1; // loopEnd exclusive
    bool hasLoop() const { return loopStart >= 0 && loopEnd > loopStart; }
    float sample (int64_t frame, int ch) const { return data[(size_t) (frame * channels + std::min (ch, channels - 1))]; }
};

inline uint32_t rd32 (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
inline uint16_t rd16 (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }

inline bool loadWav (const juce::File& file, WavData& out, int64_t maxFrames = -1)
{
    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb) || mb.getSize() < 12) return false;
    const uint8_t* b = (const uint8_t*) mb.getData();
    const size_t n = mb.getSize();
    if (memcmp (b, "RIFF", 4) != 0 || memcmp (b + 8, "WAVE", 4) != 0) return false;
    int fmtTag = 0, ch = 0, bits = 0; uint32_t sr = 0;
    const uint8_t* dataPtr = nullptr; size_t dataLen = 0;
    std::vector<std::pair<uint32_t, uint32_t>> cues; // id, position
    std::vector<std::pair<uint32_t, uint32_t>> regions; // id, length
    int64_t smplStart = -1, smplEnd = -1;
    size_t pos = 12;
    while (pos + 8 <= n)
    {
        const uint8_t* c = b + pos;
        uint32_t len = rd32 (c + 4);
        size_t body = pos + 8;
        size_t avail = std::min ((size_t) len, n - body);
        if (memcmp (c, "fmt ", 4) == 0 && avail >= 16)
        {
            fmtTag = rd16 (b + body); ch = rd16 (b + body + 2); sr = rd32 (b + body + 4); bits = rd16 (b + body + 14);
            if (fmtTag == 0xFFFE && avail >= 26) fmtTag = rd16 (b + body + 24);
        }
        else if (memcmp (c, "data", 4) == 0) { dataPtr = b + body; dataLen = avail; }
        else if (memcmp (c, "smpl", 4) == 0 && avail >= 36)
        {
            uint32_t numLoops = rd32 (b + body + 28);
            if (numLoops > 0 && avail >= 36 + 24)
            {
                smplStart = rd32 (b + body + 36 + 8);
                smplEnd = (int64_t) rd32 (b + body + 36 + 12) + 1;
            }
        }
        else if (memcmp (c, "cue ", 4) == 0 && avail >= 4)
        {
            uint32_t num = rd32 (b + body);
            for (uint32_t i = 0; i < num && 4 + (i + 1) * 24 <= avail; ++i)
            {
                const uint8_t* e = b + body + 4 + i * 24;
                cues.push_back ({ rd32 (e), rd32 (e + 20) });
            }
        }
        else if (memcmp (c, "LIST", 4) == 0 && avail >= 4 && memcmp (b + body, "adtl", 4) == 0)
        {
            size_t p2 = body + 4;
            while (p2 + 8 <= body + avail)
            {
                uint32_t l2 = rd32 (b + p2 + 4);
                if (memcmp (b + p2, "ltxt", 4) == 0 && l2 >= 8) regions.push_back ({ rd32 (b + p2 + 8), rd32 (b + p2 + 12) });
                p2 += 8 + l2 + (l2 & 1);
            }
        }
        pos = body + len + (len & 1);
    }
    if (! dataPtr || ch <= 0 || bits <= 0) return false;
    const int bps = bits / 8;
    int64_t frames = (int64_t) (dataLen / (size_t) (bps * ch));
    if (maxFrames >= 0) frames = std::min (frames, maxFrames);
    out.channels = std::min (ch, 2);
    out.sampleRate = sr > 0 ? sr : 48000;
    out.frames = frames;
    out.data.resize ((size_t) (frames * out.channels));
    for (int64_t f = 0; f < frames; ++f)
        for (int c2 = 0; c2 < out.channels; ++c2)
        {
            const uint8_t* s = dataPtr + (size_t) ((f * ch + c2) * bps);
            float v = 0;
            if (fmtTag == 3 && bits == 32) { float x; memcpy (&x, s, 4); v = x; }
            else if (fmtTag == 3 && bits == 64) { double x; memcpy (&x, s, 8); v = (float) x; }
            else if (bits == 8) v = (s[0] - 128) / 128.0f;
            else if (bits == 16) v = (int16_t) rd16 (s) / 32768.0f;
            else if (bits == 24) { int32_t x = (int32_t) ((uint32_t) s[0] << 8 | (uint32_t) s[1] << 16 | (uint32_t) s[2] << 24); v = (float) (x / 2147483648.0); }
            else if (bits == 32) v = (float) ((int32_t) rd32 (s) / 2147483648.0);
            out.data[(size_t) (f * out.channels + c2)] = v;
        }
    // loops: cue regions/markers take the rules from the manual; smpl chunk as fallback
    out.loopStart = out.loopEnd = -1;
    if (! regions.empty())
    {
        for (auto& cpos : cues)
            if (cpos.first == regions[0].first) { out.loopStart = cpos.second; out.loopEnd = (int64_t) cpos.second + regions[0].second; }
    }
    if (out.loopStart < 0 && ! cues.empty())
    {
        std::vector<uint32_t> p; for (auto& c2 : cues) p.push_back (c2.second);
        std::sort (p.begin(), p.end());
        if (p.size() == 1) { out.loopStart = p[0]; out.loopEnd = frames; }
        else if (p.size() == 2) { out.loopStart = p[0]; out.loopEnd = p[1]; }
        else { out.loopStart = p[1]; out.loopEnd = p[2]; }
    }
    if (out.loopStart < 0 && smplStart >= 0) { out.loopStart = smplStart; out.loopEnd = smplEnd; }
    if (out.loopStart >= 0)
    {
        out.loopEnd = std::min (out.loopEnd, frames);
        if (out.loopEnd - out.loopStart < 8) out.loopStart = out.loopEnd = -1;
    }
    return true;
}

// mk4 style playlist ("disting playlist v1"): flags on lines after a file name apply to that file,
// flags before any file apply to all files.
struct PlaylistEntry { juce::String file; juce::StringPairArray flags; };
inline std::vector<PlaylistEntry> readPlaylist (const juce::File& f, juce::StringPairArray& globalFlags)
{
    std::vector<PlaylistEntry> out;
    juce::StringArray lines; lines.addLines (f.loadFileAsString());
    for (int i = 0; i < lines.size(); ++i)
    {
        auto l = lines[i].trim();
        if (i == 0 && l.startsWithIgnoreCase ("disting playlist")) continue;
        if (l.isEmpty()) continue;
        if (l.startsWith ("-"))
        {
            auto key = l.substring (1).upToFirstOccurrenceOf ("=", false, false).trim();
            auto val = l.containsChar ('=') ? l.fromFirstOccurrenceOf ("=", false, false).trim() : juce::String ("1");
            if (out.empty()) globalFlags.set (key, val); else out.back().flags.set (key, val);
        }
        else out.push_back ({ l, {} });
    }
    return out;
}
} // namespace ex
