#include "ExPlugin.h"
#include "ExEditor.h"

namespace ex
{
// ================================================================================================
void ExAlgorithm::set (int n, int v)
{
    auto* p = param (n);
    const auto& d = def (n);
    v = juce::jlimit (d.min, d.max, v);
    if (p->get() == v) return;
    p->beginChangeGesture();
    *p = v;
    p->endChangeGesture();
}

juce::String ExAlgorithm::valueText (int n, int v) const
{
    const auto& d = def (n);
    if (d.fmt) return d.fmt (v);
    if (! d.labels.isEmpty())
    {
        int i = v - d.min;
        if (i >= 0 && i < d.labels.size()) return d.labels[i];
    }
    return d.unit;
}

double ExAlgorithm::delayMs (int v) const
{
    if (delayUsesMidiClock)
        return delayDivisionBeats (v) * 60000.0 / juce::jlimit (20.0, 400.0, plugin.hostBpm);
    return (double) v;
}

void ExAlgorithm::drawPage (Oled& o, int cur)
{
    o.at (0, 0, headerText());
    const auto& d = def (cur);
    juce::String num = juce::String (cur).paddedLeft (' ', 2);
    o.at (0, 1, num + ":" + d.name);
    int base = pv (cur), eff = appliedValue (cur);
    juce::String vt = valueText (cur, base);
    o.right (5, 2, juce::String (base));
    o.right (11, 2, juce::String (eff));
    if (awaitingConfirm (cur)) o.right (21, 2, "PUSH V");
    else if (vt.isNotEmpty())
    {
        if (d.labels.isEmpty() && ! d.fmt && vt.length() <= 4) o.at (12, 2, vt);
        else o.right (21, 2, vt.substring (0, 9));
    }
    drawBottomLine (o, cur);
}

void ExAlgorithm::addStandardPolyMenu (std::vector<MenuItem>& items, bool hasDelay, bool hasParaGate)
{
    items.push_back (MenuItem::act ("All notes off", [this] { plugin.allNotesOffRequest = true; plugin.message ("All notes off"); }));
    std::vector<MenuItem> opts;
    opts.push_back (MenuItem::sub ("MIDI notes", {
        MenuItem::val ("Enable", 0, 1, [this] { return midiNotes.enable; }, [this] (int v) { midiNotes.enable = v; }),
        MenuItem::val ("Lowest note", 0, 127, [this] { return midiNotes.lowest; }, [this] (int v) { midiNotes.lowest = v; }, [] (int v) { return noteName (v); }),
        MenuItem::val ("Highest note", 0, 127, [this] { return midiNotes.highest; }, [this] (int v) { midiNotes.highest = v; }, [] (int v) { return noteName (v); }),
    }));
    if (hasParaGate)
        opts.push_back (MenuItem::val ("Paraphonic gate", 0, 1, [this] { return paraphonicGateMode; }, [this] (int v) { paraphonicGateMode = v; },
                                       [] (int v) { return v ? juce::String ("Keys held") : juce::String ("Voices playing"); }));
    if (hasDelay)
        opts.push_back (MenuItem::val ("Delay uses MIDI clk", 0, 1, [this] { return delayUsesMidiClock ? 1 : 0; }, [this] (int v) { delayUsesMidiClock = v != 0; },
                                       [] (int v) { return v ? juce::String ("Host tempo") : juce::String ("Off"); }));
    items.push_back (MenuItem::sub ("Options", opts));
}

// ================================================================================================
juce::AudioProcessor::BusesProperties ExPlugin::makeBuses (bool wantsInput)
{
    BusesProperties b;
    if (wantsInput) b = b.withInput ("Input", juce::AudioChannelSet::stereo(), true);
    b = b.withOutput ("Out 1/2", juce::AudioChannelSet::stereo(), true)
         .withOutput ("Out 3/4", juce::AudioChannelSet::stereo(), false);
    return b;
}

ExPlugin::ExPlugin (Identity id) : juce::AudioProcessor (makeBuses (id.wantsInput)), identity (std::move (id)) {}
ExPlugin::~ExPlugin()
{
    stopTimer();
    saveLastState();
}

// The plug-ins remember their last state: it is written to the settings folder whenever it changes
// (and when the plug-in closes), and a fresh instance starts from it. Hosts restoring a project still
// override it with the project's own state.
juce::File ExPlugin::lastStateFile() const
{
    return SdCard::globalSettings().getFile().getParentDirectory().getChildFile (identity.product + " - last state.xml");
}

void ExPlugin::saveLastState()
{
    if (! rememberLastState || algs.empty()) return;
    auto text = stateXml()->toString();
    if (text == lastSavedState) return;
    auto f = lastStateFile();
    f.getParentDirectory().createDirectory();
    if (f.replaceWithText (text)) lastSavedState = text;
}

void ExPlugin::timerCallback() { saveLastState(); }

void ExPlugin::finishSetup()
{
    for (auto& a : algs)
    {
        for (int i = 0; i < a->numParams(); ++i)
        {
            const auto d = a->defs[(size_t) i];
            auto* algp = a.get();
            const int n = i + 1;
            auto attrs = juce::AudioParameterIntAttributes().withStringFromValueFunction ([algp, n] (int v, int) {
                auto t = algp->valueText (n, v);
                return algp->def (n).labels.isEmpty() ? juce::String (v) + (t.isNotEmpty() ? " " + t : juce::String()) : t;
            });
            juce::String pid = "a" + juce::String (a->number) + "p" + juce::String (n);
            juce::String pname = (algs.size() > 1 ? juce::String (a->number) + "." : juce::String()) + juce::String (n) + " " + d.name;
            auto* p = new juce::AudioParameterInt (juce::ParameterID { pid, 1 }, pname, d.min, d.max, d.def, attrs);
            addParameter (p);
            a->params.push_back (p);
            if (d.confirm) a->applied[n] = d.def;
        }
    }
    if (rememberLastState)
    {
        if (auto x = juce::XmlDocument::parse (lastStateFile())) restoreXml (*x);
        lastSavedState = stateXml()->toString();
        startTimer (1500);
    }
}

bool ExPlugin::isBusesLayoutSupported (const BusesLayout& l) const
{
    if (l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    if (l.outputBuses.size() > 1)
    {
        auto aux = l.outputBuses[1];
        if (! aux.isDisabled() && aux != juce::AudioChannelSet::stereo()) return false;
    }
    if (! l.inputBuses.isEmpty())
    {
        auto in = l.getMainInputChannelSet();
        if (! in.isDisabled() && in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono()) return false;
    }
    return true;
}

void ExPlugin::prepareToPlay (double sr, int bs)
{
    sampleRate = sr;
    outBuf.setSize (4, juce::jmax (bs, 16));
    inBuf.setSize (2, juce::jmax (bs, 16));
    for (auto& a : algs) a->prepare (sr, bs);
}

void ExPlugin::selectAlgorithm (int index)
{
    index = juce::jlimit (0, (int) algs.size() - 1, index);
    if (index == current.load()) return;
    suspendProcessing (true);
    algs[(size_t) current.load()]->allNotesOff();
    current = index;
    algs[(size_t) index]->onSelected();
    suspendProcessing (false);
}

int ExPlugin::timbreForChannel (int ch, int numTimbres) const
{
    for (int t = 0; t < numTimbres; ++t)
        if (timbreChannel[t] > 0 && timbreChannel[t] == ch) return t;
    if (rxChannel == 0) return 0;
    for (int t = 0; t < numTimbres; ++t)
        if (timbreChannel[t] == 0 && rxChannel + t == ch) return t;
    return -1;
}

void ExPlugin::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    if (outBuf.getNumSamples() < n) { outBuf.setSize (4, n, false, false, true); inBuf.setSize (2, n, false, false, true); }
    // capture input
    inBuf.clear();
    if (getBusCount (true) > 0 && getBus (true, 0)->isEnabled())
    {
        auto in = getBusBuffer (buffer, true, 0);
        for (int c = 0; c < juce::jmin (2, in.getNumChannels()); ++c) inBuf.copyFrom (c, 0, in, c, 0, n);
        if (in.getNumChannels() == 1) inBuf.copyFrom (1, 0, in, 0, 0, n);
    }
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm()) hostBpm = *bpm;

    keyboardState.processNextMidiBuffer (midi, 0, n, true);
    for (const auto m : midi)
    {
        auto msg = m.getMessage();
        if (msg.isMidiClock())
        {
            int64_t t = sampleClock + m.samplePosition;
            if (clockSamples > 0)
            {
                double iv = (double) (t - clockSamples);
                clockInterval = clockInterval <= 0 ? iv : clockInterval * 0.9 + iv * 0.1;
                if (clockInterval > 0) hostBpm = 60.0 * sampleRate / (clockInterval * 24.0);
            }
            clockSamples = t;
        }
        else if (! msg.isActiveSense()) midiInFlash = 6;
    }
    outBuf.clear();
    midiOutBuf.clear();
    if (allNotesOffRequest.exchange (false)) alg().allNotesOff();
    juce::AudioBuffer<float> outView (outBuf.getArrayOfWritePointers(), 4, n);
    juce::AudioBuffer<float> inView (inBuf.getArrayOfWritePointers(), 2, n);
    alg().process (outView, inView, midi, midiOutBuf);
    if (! midiOutBuf.isEmpty()) midiOutFlash = 6;

    buffer.clear();
    auto main = getBusBuffer (buffer, false, 0);
    for (int c = 0; c < juce::jmin (2, main.getNumChannels()); ++c) main.copyFrom (c, 0, outBuf, c, 0, n);
    if (getBusCount (false) > 1 && getBus (false, 1)->isEnabled())
    {
        auto aux = getBusBuffer (buffer, false, 1);
        for (int c = 0; c < juce::jmin (2, aux.getNumChannels()); ++c) aux.copyFrom (c, 0, outBuf, 2 + c, 0, n);
    }
    midi.clear();
    if (identity.midiOut) midi.swapWith (midiOutBuf);
    sampleClock += n;
}

// ---------------------------------------------------------------------------------------- state
std::unique_ptr<juce::XmlElement> ExPlugin::stateXml() const
{
    auto x = std::make_unique<juce::XmlElement> ("EXSTATE");
    x->setAttribute ("product", identity.product);
    x->setAttribute ("alg", current.load());
    x->setAttribute ("rx", rxChannel); x->setAttribute ("tx", txChannel);
    x->setAttribute ("contrast", contrast);
    x->setAttribute ("mapL", mapL); x->setAttribute ("mapR", mapR);
    x->setAttribute ("potL", potValue[0]); x->setAttribute ("potR", potValue[1]);
    for (int t = 0; t < 4; ++t) x->setAttribute ("tch" + juce::String (t), timbreChannel[t]);
    x->setAttribute ("voices", PolyNoteEngine::kMaxVoices);
    for (size_t i = 0; i < algs.size(); ++i)
    {
        auto& a = *algs[i];
        auto* ax = x->createNewChildElement ("ALG");
        ax->setAttribute ("num", a.number);
        ax->setAttribute ("cur", curParam[i]);
        ax->setAttribute ("midiEnable", a.midiNotes.enable); ax->setAttribute ("midiLo", a.midiNotes.lowest); ax->setAttribute ("midiHi", a.midiNotes.highest);
        ax->setAttribute ("delayClk", a.delayUsesMidiClock); ax->setAttribute ("paraGate", a.paraphonicGateMode);
        for (int p = 1; p <= a.numParams(); ++p)
        {
            auto* px = ax->createNewChildElement ("P");
            px->setAttribute ("n", p); px->setAttribute ("v", a.pv (p));
        }
        a.saveExtra (*ax);
    }
    return x;
}

void ExPlugin::restoreXml (const juce::XmlElement& x)
{
    rxChannel = x.getIntAttribute ("rx", 1); txChannel = x.getIntAttribute ("tx", 1);
    contrast = x.getIntAttribute ("contrast", 255);
    mapL = x.getIntAttribute ("mapL", -1); mapR = x.getIntAttribute ("mapR", 0);
    potValue[0] = (float) x.getDoubleAttribute ("potL", 0.5); potValue[1] = (float) x.getDoubleAttribute ("potR", 0.5);
    for (int t = 0; t < 4; ++t) timbreChannel[t] = x.getIntAttribute ("tch" + juce::String (t), 0);
    const int savedVoices = x.getIntAttribute ("voices", 8);
    for (auto* ax : x.getChildWithTagNameIterator ("ALG"))
    {
        int num = ax->getIntAttribute ("num");
        for (size_t i = 0; i < algs.size(); ++i)
        {
            auto& a = *algs[i];
            if (a.number != num) continue;
            curParam[i] = juce::jlimit (1, a.numParams(), ax->getIntAttribute ("cur", 7));
            a.midiNotes.enable = ax->getIntAttribute ("midiEnable", 1);
            a.midiNotes.lowest = ax->getIntAttribute ("midiLo", 0);
            a.midiNotes.highest = ax->getIntAttribute ("midiHi", 127);
            a.delayUsesMidiClock = ax->getBoolAttribute ("delayClk", false);
            a.paraphonicGateMode = ax->getIntAttribute ("paraGate", 0);
            for (auto* px : ax->getChildWithTagNameIterator ("P"))
            {
                int p = px->getIntAttribute ("n");
                if (p >= 1 && p <= a.numParams())
                {
                    a.set (p, px->getIntAttribute ("v"));
                    if (a.def (p).confirm) { a.applied[p] = a.pv (p); a.confirm (p); }
                }
            }
            // states saved when the limit was 8 voices: "all 8" becomes "all 16"
            if (savedVoices < PolyNoteEngine::kMaxVoices)
                for (int p = 1; p <= a.numParams(); ++p)
                    if (a.def (p).name == "Max voices" && a.pv (p) == savedVoices) a.set (p, PolyNoteEngine::kMaxVoices);
            a.loadExtra (*ax);
        }
    }
    selectAlgorithm (juce::jlimit (0, (int) algs.size() - 1, x.getIntAttribute ("alg", 0)));
}

void ExPlugin::getStateInformation (juce::MemoryBlock& mb)
{
    copyXmlToBinary (*stateXml(), mb);
}

void ExPlugin::setStateInformation (const void* data, int size)
{
    if (auto x = getXmlFromBinary (data, size)) restoreXml (*x);
}

// ---------------------------------------------------------------------------------------- controls
void ExPlugin::message (const juce::String& text, double seconds, bool big)
{
    msgText = text; msgBig = big;
    msgUntil = juce::Time::getMillisecondCounterHiRes() * 0.001 + seconds;
}

MenuItem* ExPlugin::menuNode (size_t depth)
{
    MenuItem* m = &menuRoot;
    for (size_t i = 0; i < depth && i < menuPath.size(); ++i)
    {
        if (menuPath[i] < 0 || menuPath[i] >= (int) m->children.size()) return m;
        m = &m->children[(size_t) menuPath[i]];
    }
    return m;
}

juce::File ExPlugin::presetDir() const
{
    return SdCard::root().getChildFile ("presets").getChildFile (identity.product);
}

void ExPlugin::savePresetToSd()
{
    auto dir = presetDir();
    dir.createDirectory();
    int n = 1;
    while (dir.getChildFile (juce::String (n).paddedLeft ('0', 3) + ".xml").exists()) ++n;
    auto f = dir.getChildFile (juce::String (n).paddedLeft ('0', 3) + ".xml");
    stateXml()->writeTo (f);
    message ("Saved " + f.getFileName());
}

void ExPlugin::loadPresetFromSd (int index)
{
    if (index < 0 || index >= presetFiles.size()) return;
    loadPresetFile (presetFiles[index]);
}

void ExPlugin::loadPresetFile (const juce::File& f)
{
    if (auto x = juce::XmlDocument::parse (f))
    {
        restoreXml (*x);
        lastPresetName = f.getFileNameWithoutExtension();
        message ("Loaded " + lastPresetName);
    }
}

void ExPlugin::chooseSdFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose the SD card folder", SdCard::root());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc) {
        auto f = fc.getResult();
        if (f.isDirectory())
        {
            SdCard::setRoot (f);
            message ("SD: " + f.getFileName());
            if (onSdRootChanged) onSdRootChanged();
            for (auto& a : algs) a->onSelected();
        }
    });
}

void ExPlugin::buildMenu()
{
    menuRoot = MenuItem::sub ("", {});
    auto& r = menuRoot.children;
    if (algs.size() > 1)
    {
        std::vector<MenuItem> al;
        for (int i = 0; i < (int) algs.size(); ++i)
            al.push_back (MenuItem::act (juce::String (algs[(size_t) i]->number) + " " + algs[(size_t) i]->name, [this, i] { selectAlgorithm (i); menuOpen = false; }));
        r.push_back (MenuItem::sub ("Algorithms", al));
    }
    {
        std::vector<MenuItem> items;
        alg().addMenuItems (items);
        if (items.empty()) items.push_back (MenuItem::act ("All notes off", [this] { allNotesOffRequest = true; }));
        r.push_back (MenuItem::sub (alg().name, items));
    }
    presetFiles = SdCard::sortedChildren (presetDir(), juce::File::findFiles, "*.xml");
    r.push_back (MenuItem::sub ("Presets", {
        MenuItem::act ("Save to SD card", [this] { savePresetToSd(); menuOpen = false; }),
        MenuItem::val ("Load from SD card", 0, juce::jmax (0, presetFiles.size() - 1), [] { return 0; },
                       [this] (int) {}, [this] (int v) { return presetFiles.isEmpty() ? juce::String ("(none - push P)") : presetFiles[v].getFileNameWithoutExtension(); }),
        MenuItem::act ("Reset preset", [this] {
            auto& a = alg();
            for (int p = 1; p <= a.numParams(); ++p) { a.set (p, a.def (p).def); if (a.def (p).confirm) { a.applied[p] = a.def (p).def; a.confirm (p); } }
            message ("Preset reset"); menuOpen = false; }),
    }));
    // 'Load' is a value item: choose with P/V, the selection is applied when leaving with P
    loadPresetChoice = 0;
    r.back().children[1].get = [this] { return loadPresetChoice; };
    r.back().children[1].set = [this] (int v) { loadPresetChoice = v; };
    r.back().children[1].action = [this] { loadPresetFromSd (loadPresetChoice); };

    auto paramName = [this] (int v) { return v <= 0 ? juce::String ("None") : juce::String (v) + ":" + alg().def (juce::jlimit (1, alg().numParams(), v)).name; };
    r.push_back (MenuItem::sub ("Mappings", {
        MenuItem::val ("L knob", -1, alg().numParams(), [this] { return mapL; }, [this] (int v) { mapL = v; },
                       [this, paramName] (int v) { return v < 0 ? "Default " + paramName (alg().defaultL) : paramName (v); }),
        MenuItem::val ("R knob", 0, alg().numParams(), [this] { return mapR; }, [this] (int v) { mapR = v; },
                       [paramName] (int v) { return v == 0 ? juce::String ("Current param") : paramName (v); }),
    }));
    std::vector<MenuItem> midiItems {
        MenuItem::val ("RX channel", 0, 16, [this] { return rxChannel; }, [this] (int v) { rxChannel = v; }, [] (int v) { return v == 0 ? juce::String ("Omni") : juce::String (v); }),
        MenuItem::val ("TX channel", 0, 16, [this] { return txChannel; }, [this] (int v) { txChannel = v; }, [] (int v) { return v == 0 ? juce::String ("Off") : juce::String (v); }),
    };
    for (int t = 0; t < identity.numTimbreChannels && identity.numTimbreChannels > 1; ++t)
        midiItems.push_back (MenuItem::val ("Timbre " + juce::String (t + 1) + " channel", 0, 16, [this, t] { return timbreChannel[t]; },
                                            [this, t] (int v) { timbreChannel[t] = v; },
                                            [t] (int v) { return v == 0 ? "Default - RX+" + juce::String (t) : juce::String (v); }));
    r.push_back (MenuItem::sub ("Settings", {
        MenuItem::sub ("MIDI", midiItems),
        MenuItem::val ("Contrast", 0, 255, [this] { return contrast; }, [this] (int v) { contrast = v; }),
    }));
    r.push_back (MenuItem::sub ("SD card", {
        MenuItem::act ("Choose folder...", [this] { menuOpen = false; chooseSdFolder(); }),
        MenuItem::act ("Rescan", [this] { for (auto& a : algs) a->onSelected(); message ("Rescanned SD card"); menuOpen = false; }),
        MenuItem::act ("Show in Finder", [] { SdCard::root().revealToUser(); }),
    }));
    r.push_back (MenuItem::sub ("Misc", {
        MenuItem::act ("All notes off", [this] { allNotesOffRequest = true; message ("All notes off"); menuOpen = false; }),
        MenuItem::act ("About", [this] { message (identity.product + " v1.0", 2.5); menuOpen = false; }),
    }));
    // start on the algorithm's own menu (like the module, which opens near the algorithm entry)
    menuPath = { algs.size() > 1 ? 1 : 0 };
    valueEdit = false;
}

void ExPlugin::turnP (int d)
{
    if (menuOpen)
    {
        if (valueEdit)
        {
            auto* item = menuNode (menuPath.size());
            if (item->isValue && item->set) item->set (juce::jlimit (item->vmin, item->vmax, item->get() + d));
            return;
        }
        auto* parent = menuNode (menuPath.size() - 1);
        auto& idx = menuPath.back();
        idx = juce::jlimit (0, juce::jmax (0, (int) parent->children.size() - 1), idx + d);
        return;
    }
    auto& cur = curParam[current.load()];
    cur = juce::jlimit (1, alg().numParams(), cur + d);
}

void ExPlugin::turnV (int d)
{
    if (menuOpen)
    {
        if (valueEdit) turnP (d);
        return;
    }
    int cur = curParam[current.load()];
    alg().set (cur, alg().pv (cur) + d);
}

void ExPlugin::pushP()
{
    if (! menuOpen) { buildMenu(); menuOpen = true; return; }
    auto* item = menuNode (menuPath.size());
    if (valueEdit)
    {
        valueEdit = false;
        if (item->action) { item->action(); menuOpen = false; }
        return;
    }
    if (item->isValue) { valueEdit = true; return; }
    if (! item->children.empty()) { menuPath.push_back (0); return; }
    if (item->action) item->action();
}

void ExPlugin::pushV()
{
    if (menuOpen)
    {
        if (valueEdit) { valueEdit = false; return; }
        if (menuPath.size() > 1) menuPath.pop_back();
        else menuOpen = false;
        return;
    }
    int cur = curParam[current.load()];
    auto& a = alg();
    if (a.awaitingConfirm (cur)) { a.applied[cur] = a.pv (cur); a.confirm (cur); }
}

void ExPlugin::holdV() { menuOpen = false; valueEdit = false; }

void ExPlugin::setPot (int which, float v01)
{
    potValue[which] = juce::jlimit (0.0f, 1.0f, v01);
    auto& a = alg();
    int p = which == 1 ? (mapR > 0 ? mapR : (a.defaultR > 0 ? a.defaultR : curParam[current.load()])) : (mapL < 0 ? a.defaultL : mapL);
    if (p <= 0 || p > a.numParams()) return;
    const auto& d = a.def (p);
    a.set (p, d.min + juce::roundToInt (potValue[which] * (float) (d.max - d.min)));
    if (which == 1 && mapR == 0 && a.defaultR == 0 && ! menuOpen) curParam[current.load()] = p;
}

void ExPlugin::pushPot (int which)
{
    auto& a = alg();
    if (which == 0) { a.pushL(); return; }
    int cur = curParam[current.load()];
    if (! a.pushR (cur)) a.set (cur, a.def (cur).def);
}

// ---------------------------------------------------------------------------------------- display
void ExPlugin::renderMenu (Oled& o)
{
    auto* item = menuNode (menuPath.size());
    if (valueEdit && item->isValue)
    {
        o.hline (0, 127, 0);
        o.text (2, 2, item->label);
        int v = item->get();
        juce::String t = item->fmt ? item->fmt (v) : juce::String();
        if (item->fmt && (t == juce::String (v))) t = {};
        o.text (2, 16, juce::String (v));
        if (t.isNotEmpty()) o.text (juce::jmax (26, 2 + Oled::textWidth (juce::String (v)) + 12), 16, t.substring (0, 16));
        o.hline (0, 127, 31);
        return;
    }
    auto* parent = menuNode (menuPath.size() - 1);
    o.hline (0, 127, 0);
    o.centre (0, parent->label);
    o.set (0, 0); // keep line visible under title
    // items row: current centred, neighbours either side
    const int y = 15;
    const int idx = menuPath.back();
    auto label = [&] (int i) { return parent->children[(size_t) i].label; };
    juce::String c = label (idx);
    int cx = (Oled::W - Oled::textWidth (c)) / 2;
    o.text (cx, y, c);
    int xr = cx + Oled::textWidth (c) + 12;
    for (int i = idx + 1; i < (int) parent->children.size() && xr < Oled::W; ++i) { o.text (xr, y, label (i)); xr += Oled::textWidth (label (i)) + 12; }
    int xl = cx - 12;
    for (int i = idx - 1; i >= 0 && xl > 0; --i) { int w = Oled::textWidth (label (i)); o.text (xl - w, y, label (i)); xl -= w + 12; }
    o.hline (0, 127, 31);
}

void ExPlugin::render (Oled& o)
{
    o.clear();
    if (menuOpen) renderMenu (o);
    else alg().drawPage (o, curParam[current.load()]);

    // MIDI activity indicators (top right)
    if (! menuOpen)
    {
        if (midiInFlash.load() > 0) { --midiInFlash; for (int y = 1; y < 6; ++y) o.hline (120, 120 + (y < 3 ? y : 5 - y), y); }
        if (midiOutFlash.load() > 0) { --midiOutFlash; for (int y = 1; y < 6; ++y) o.hline (127 - (y < 3 ? y : 5 - y), 127, y); }
    }
    double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (now < msgUntil && msgText.isNotEmpty())
    {
        if (msgBig)
        {
            int w = msgText.length() * 12;
            int x = (Oled::W - w) / 2;
            o.fillRect (x - 2, 3, w + 4, 26, false);
            o.bigText (x, 6, msgText);
        }
        else
        {
            o.fillRect (0, 0, 128, 8, false);
            o.centre (0, msgText.substring (0, 21));
        }
    }
}

juce::AudioProcessorEditor* ExPlugin::createEditor() { return new ExEditor (*this); }
} // namespace ex
