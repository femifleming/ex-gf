# EX-GF - instructions

## The screen

```
[SAVE][LOAD][SD]          title / algorithm tabs          [MIDI][PANIC][MENU]
[TIMBRE / VOICES]      +--------------------------+       [ACTIONS        ]
[voice meters    ]     |   module display (2x)    |       [buttons        ]
                       +--------------------------+
IN *                  status line - hover for help                     * OUT
+-------------+     [ section tabs ]                       +-------------+
| left list   |     [ tile ][ tile ]                       | right list  |
|             |     [ tile ][ tile ]                       |             |
+-------------+     [ tile ][ tile ]                       +-------------+
```

| Control | How |
|---|---|
| Parameter tile | Drag up/down or use the mouse wheel (shift = fine). Double-click resets it. Tiles with a small ▾ open a list of choices. |
| Lists | Click to load. The wheel scrolls. |
| Display | Wheel = next/previous parameter, shift+wheel = value. ↑/↓ = P encoder, ←/→ = V encoder (shift ×10), Enter = push P (module menu), Esc = push V (back / confirm). |
| Top bar | **SAVE**/**LOAD** presets (stored in `SD card/presets/<plug-in>`), **SD** chooses the card folder, **MIDI** sets channels, **PANIC** = all notes off, **MENU** = every module option. |
| Keyboard | **A W S E D F T G Y H U J K O L P ;** play notes, **Z/X** change octave. Click the window first so it has focus. |
| Window | Drag the corner to resize. It snaps to whole-pixel sizes (1×, 2×, 3×…) so pixels stay sharp. |

**Remembering state:** in a DAW, each instance saves with your project. The plug-ins also keep a *last state* file (in `~/Library/Application Support/EX-GF`, `%APPDATA%\EX-GF` or `~/.config/EX-GF`). New instances and the standalone apps start from where you left off.

**Outputs:** *Out 1/2* is the main stereo bus. *Out 3/4* is a second stereo bus. Enable it in your DAW to get per-timbre/per-voice outputs, the paraphonic gate and the mono mix, plus the auto-sampler's CV/gate.

**MIDI:** the receive channel defaults to 1 (Omni is available). Multi-timbral plug-ins use consecutive channels: timbre 1 on the receive channel, timbre 2 on the receive channel + 1, and so on. Each timbre can also be set to its own channel. Sustain (CC64), pitch bend and All Notes Off are supported, plus aftertouch in Poly Exciter.

## The SD card folder

It works like the module's MicroSD card. The default is `Music/EX SD Card`.

| Folder | Contents |
|---|---|
| `<any name>/` | **Multisample folders**: WAVs named `name_C4.wav` (natural pitch, sharps only, C4 = MIDI 60), `_V1`/`_V2`… velocity layers (V1 = softest), `_RR1`/`_RR2`… round robins (`name_C4_V2_RR1.wav`), optional `_SW<note>` switch point. Loops come from `smpl` or `cue ` chunks. A `playlist.txt` (disting playlist v1: `-natural=`, `-switch=`, `-loop=`, `-endLoopOnRelease=`) is optional. Files without a note name map chromatically from C3. |
| `wavetables/` | Each wavetable is a sub-folder of single-cycle WAVs (used alphabetically), or one WAV of concatenated 600-sample cycles (256 for WaveEdit banks, detected automatically). `playlist-wavetable.txt` with `-wavelength=` is optional. |
| `exciters/` | Each table is a folder of up to 128 short WAVs, or one WAV holding exactly 128 equal-length exciters. |
| `FMSYX/` | DX7 32-voice bank SysEx files (`.syx`, 4104 bytes). With none present, the three built-in Plaits banks are used. `MACRO2_1.SYX`-`MACRO2_3.SYX` are the banks of Macro Oscillator 2. |
| `scl/`, `kbm/` | Scala scales and keyboard maps (for the *Scala/MTS* and *Scala KBM* parameters). |
| `!RECORD/` | WAV Recorder recordings (`REC00001.WAV`…). |
| `AUTO0001/`… | Auto-sampler output (`060_C4_V1_RR1.wav`…). It's a normal multisample folder. |
| `!GRANUL8/` | Granulator buffers saved with SAVE (`GRAN0001.WAV`…). |
| `presets/` | Presets saved with SAVE. |

## EX Multisample

### 3 - SD Multisample
- **Left list:** sample folders. **Right list:** the samples in that folder; click one to hear it.
- **TIMBRE 1/2/3:** chooses which timbre the folder list sets. Timbres 2 and 3 can use their own folders or follow timbre 1.
- **Pages:**
  - **SOUND:** gain, saturation, voices, round-robin mode, output mode and spread
  - **ENV:** attack, decay, sustain, release (release 127 = infinite)
  - **TUNE:** octave, transpose, fine, bend range, Scala, per-voice detune
  - **CHORD:** chord generator, arpeggiator per timbre, broken chords (strum)
  - **FX:** stereo/ping-pong delay, bass/treble EQ
  - **KEYS:** sustain, sustain mode (synth/piano), velocity curve, note ranges per timbre
- **MENU → SD Multisample → Options:** MIDI note range, paraphonic gate mode, *Delay uses MIDI clk* (follows the host tempo), *Transpose limit*.

### 5 - WAV Recorder and auto-sampler
- **RECORD** starts/stops recording the plug-in's audio input to `!RECORD/RECnnnnn.WAV` (48/96 kHz, 16/24-bit, mono/stereo).
- **PLAY** plays the recording selected in the left list.
- **Auto-sampling an instrument:**
  1. Connect the instrument. In the **app**, open *Options → Audio/MIDI settings*: choose the MIDI output that drives the synth and the audio input it returns on. In a DAW, route the plug-in's MIDI out to the synth and the synth's audio back into the plug-in's input. Not every DAW passes MIDI out of an instrument plug-in, so the app is the easiest way.
  2. On the **AUTO-SAMPLER** page, set: note range and step, velocity layers (*AS Vel Sw*, between *AS Min/Max vel*), round robins, note length (*AS Coarse* s + *AS Fine* ms) and *AS Gap* (release time).
  3. Press **TEST**. A note plays and the status shows the measured latency in samples. Set *AS Latency* a little below that number.
  4. Press **AUTO**. Every note/velocity/round robin is played and recorded into a new `AUTOnnnn` folder. Press AUTO again to stop early.
  5. Click the folder in the right list (*Auto-sampled*) to play it straight away in SD Multisample.
- CV/gate for modular synths comes out of *Out 3/4* (1 V/oct at 0.1 per volt, MIDI note 48 = 0 V).

## EX Poly Wavetable
- **Left list:** wavetables. **Right list:** the waves in the current table; click one to jump the wave position to it.
- **Importing your own wavetables, including WaveEdit banks:**
  - **Drag & drop** `.wav` banks, single cycles or folders onto the window.
  - **IMPORT** opens a file picker (it starts in `~/Documents/WaveEdit/banks`).
  - **WAVEEDIT** imports every bank in `Documents/WaveEdit/banks` in one go. Press it again after editing in WaveEdit to update them.
  - Each import becomes a folder of single-cycle WAVs in `wavetables/`, which a real disting EX can use too. WaveEdit banks (64 × 256 samples) and 600-sample disting tables are recognised automatically. A set of single-cycle files becomes one table.
- **Pages:**
  - **OSC:** wave offset/spread, unison, voices, gain
  - **ENV 1/2:** with curve shapes; env 2 sustain can go negative
  - **FILTER:** off/LP/BP/HP with key, velocity, envelope and LFO tracking
  - **MOD:** velocity, pitch, envelopes and LFO to wave/filter/pitch; LFO speed, retrigger and spread
  - **CHORD**
  - **FX:** Juno chorus I/II, delay, output spread/mode (per-voice mode uses Out 1-4)
  - **KEYS:** tune, bend, sustain, trigger mode (gated/drone/triggered), Scala

## EX Poly FM
- **Left list:** banks (`FMSYX/*.syx`). **Right list:** the 32 voices of the bank, by name.
- **TIMBRE 1-4:** chooses which timbre the lists set. Timbres 2-4 can follow timbre 1.
- **Pages:**
  - **T1…T4:** per-timbre output, gain, pan, brightness (modulator depth), envelope scale
  - **VOICE:** max voices, unison, master gain, velocity→brightness, voice transpose, output mode
  - **CHORD**, **FX** (chorus/delay), **KEYS**
- **DX7 editors** (Dexed, Patch Base…): single-voice dumps, parameter edits and dump requests go to the timbre chosen by *SysEx target*. The display shows **VOICE EDITED**. Use MENU → Poly FM → *Write voice* / *Write bank to SD* / *Abandon edit*, or *Send* to dump to the editor.

## EX Poly Exciter
- **Left list:** exciter tables (*Internal* = Ramp, Noise burst, Noise ramp, Square, Silence). **Right list:** the exciters in the table.
- **Modes:**
  - *Strike*: the exciter plays once.
  - *Scrape*: it stops when the key is released.
  - *Strum*: it loops while held, and the level follows **pressure** (aftertouch or the *Pressure* tile).
- **Pages:**
  - **EXCITER:** mode, exciter, length, pressure, external input (feed audio into the plug-in input)
  - **RESONATOR:** tone, damp, release damp, low pass
  - **VELO:** velocity/pitch/pressure modulation
  - **ENV/LFO:** to pitch and exciter selection
  - **CHORD**, **FX**, **KEYS**

## EX Granulator
Granular synthesis on live input or a WAV file. It is an effect that is also played from MIDI: in a DAW put it where it gets both audio and notes (in Logic it appears under *MIDI-controlled Effects*).
- **Getting started:** feed audio in, press **RECORD** and switch on **DRONE 1**. Or load a WAV and play notes. Nothing sounds until the buffer holds audio.
- **Left list:** folders on the SD card that contain WAVs (plus `!GRANUL8` and `!RECORD`). **Right list:** the WAV files in that folder; click one to load it. You can also drag a WAV onto the window.
- **Buffer:** mono, up to 31.25 s. *Input feedback* echoes the input at the buffer length; *Main feedback* records the grains back in. The write position only moves while recording.
- **Notes and drones:** each MIDI note and each of the three drones makes its own grain cloud, with attack/release (100 ms - 30 s), opacity and level. *Natural pitch* is the note that plays the audio at its recorded pitch.
- **Spawn modes:**
  - *Stochastic*: random timing from *Rate mean/spread*.
  - *Mid-grain*: a new grain starts at the middle of the last one. With *Size spread* 0 and the *Equal power* shape this loops smoothly.
  - *Single*: one grain per note.
  - The *(fixed)* versions pin the grains to the audio that was at the write position when the note started. This only differs while recording.
- ***Size mean* 0** makes each grain one cycle of the note's pitch.
- **Pages:**
  - **BUFFER:** size, input gain, record, feedback
  - **GRAINS:** spawn mode, rate, size, shape, opacity, reverse, grain limit
  - **PITCH:** pitch mean/spread, *Pitch quantize*, pan
  - **POS:** *Delay mean/spread* (where in the buffer grains start, in % or ms) and the LFO that sweeps it; at speed 196 and depth 100% it moves at normal playback speed
  - **MOD**, **DRONES**, **FX** (delay, *Effects routing*), **REVERB**
- **Actions:** RECORD, VISUALS (waveform view with grain positions), DRONE 1-3, SAVE (writes `!GRANUL8/GRANnnnn.WAV`), CLEAR, NOTES OFF. MENU → Granulator has *Load WAV*, *Load WAV (raw)*, *View saved path*, *Clear buffer* and *Auto visuals*. A loaded file is reloaded with the preset or project.
- **Outputs:** Out 1/2 is the mix with effects; Out 3/4 is the grains alone.

## EX Multi-FX
A stereo multi-effects processor: a 3-band EQ feeding a pitch section, a delay and a reverb, all at once.
- **Left list (Programs):** 13 starting points, from *Init* to *Shimmer*, *Tape Echo* and *Lo-Fi 12k*. Choosing one resets every parameter and then applies the program.
- **Routing:** each of the pitch, delay and reverb sections takes a mix of two sources (*input 1*, *input 2*, *input mix*), chosen from Pre-EQ, Post-EQ, Pitch, Delay or Reverb. Sections can be chained in any order, run in parallel or fed back into each other. Feedback loops can howl, so route with care.
- **Pages:**
  - **MIX:** dry gain (pre- or post-EQ), effects gain, one gain per section, saturation, mono input. *Aux outputs* chooses what appears on Out 3/4.
  - **EQ:** band 1 is a high-pass or low shelf, band 2 a peak/notch with Q, band 3 a low-pass or high shelf. Frequencies are MIDI note numbers (69 = 440 Hz).
  - **PITCH:** chorus/flanger, phaser or pitch shifter. For chorus use a long *Chorus delay* and low depth; for a flanger a short delay, high depth and feedback. *Spread* 180° gives the classic stereo chorus.
  - **DELAY:** mono, stereo or ping-pong, each in a *fade* version (crossfades to a new time) and a *slew* version (glides like tape). TAP sets the time; MENU → Multi FX → Options → *Delay uses MIDI clk* follows the host tempo.
  - **REVERB:** four models, time 0.4-30 s, size, damping, modulation, early/diffuse levels, input low-pass.
  - **VSR (variable sample rate):** lowers the internal sample rate in semitones. Every EQ frequency drops, delay and reverb times stretch, and tails already in the buffers change pitch. -24 gives a 12 kHz retro sound with four times the delay time.
- **Actions:** BYPASS, CLEAR (empties the delay and reverb), TAP. The display shows CLIP when the output overloads.

## EX Resonator
Mutable Instruments Rings. It resonates the audio input, or plays its own exciter when notes arrive.
- **Left list:** the resonator mode: Modal, Sympathetic strings, String, FM voice, Sympathetic strings quantized, String + reverb, and String synth (the hidden "Disastrous Peace" mode). **Right list:** the chord (used by the chord modes), or the effect in String synth mode.
- **MODEL page:** polyphony (1-4), *Structure*, *Brightness*, *Damping*, *Position* as on Rings.
- **Playing:** *MIDI mode* chooses what notes do: set the pitch, strum, or both (the default). With *MIDI mode* Off or Pitch, Rings strums itself from pitch changes or from transients in the input, as the module does with nothing patched. **STRUM** strums by hand.
- **INPUT** off (*Audio input* None) uses Rings' internal exciter instead of the audio input.
- **MIX page:** input gain, noise gate, output gain, dry gain, and pan for the odd output, the even output and the dry signal. The dry signal is at 0 dB by default; press **DRY** to mute it.
- **Outputs:** Out 1/2 is the stereo mix; Out 3 is odd + even; Out 4 is a mono mix.

## EX Spectral Freeze
Captures the sound at one moment and sustains it for as long as you like. Four voices.
- **FREEZE** (or pushing L) freezes. With Target **A** (auto), the first press freezes the next free voice, the second releases it, and the third freezes a new one, so a double press crossfades to a fresh capture.
- **TARGET 1-4:** FREEZE toggles that voice, so you can stack up to four moments. The left **Voices** list shows each voice (FROZEN / LIVE / RELEASE / -); click a row to freeze or release it.
- **MOVE page:** *Movement* (Forwards, Backwards, Alternating, Random walk, Random skip; also the right list), *Rate* (0 = stopped, 100 = original speed), *Depth* (how much audio before the freeze point is used; 1 = completely static) and *Offset* (moves the freeze point back in time, up to about 11 s).
- ***Etherization*** below 127 progressively removes everything but the strongest harmonics.
- **LIVE** starts a voice that resynthesizes the live input instead of a frozen moment, so Etherization and pitch shift work on live audio. **UNFREEZE** releases everything.
- **VOICES / PITCH pages:** gain, pan and coarse/fine pitch shift per voice, added to the global shift. *Attack* and *Release* set the fade times.
- **MIX / FX pages:** dry and freeze gains, saturation, delay and reverb. Each effect can sit on the *Freeze output* only or on the whole *Output mix*.
- **VISUALS** switches the module display to the four-quadrant view.
- **MIDI** (VST3; Audio Unit hosts don't send MIDI to insert effects): C3/D3/E3/F3 select target voices 1-4 and B3 selects Auto; G3 = freeze toggle; A3 = freeze gate; C4/D4/E4/F4 toggle voices 1-4; C5/D5/E5/F5 freeze voices 1-4 while held.
- **Outputs:** Out 1/2 is the mix; Out 3/4 is the dry input.

## EX Macro Oscillator 2
Four instances of Mutable Instruments Plaits (firmware 1.2).
- **OSCILLATOR 1-4** chooses which oscillator the lists edit. **Left list:** the 24 models (the 16 classic ones first, then the 8 added in firmware 1.2). **Right list:** with a 6-op FM model, the 32 voices of its bank.
- **OSC pages:** *Harmonics*, *Timbre*, *Morph*, FM and modulation amounts, *LP gate* colour, *Time/decay*, tuning.
- **Out of the box it is a 4-voice synth:** all four oscillators are in one round-robin group (*MIDI option* = Poly) and share oscillator 1's settings (*Share set* = 1). **UNLINK** gives each oscillator its own settings, so every note of a chord can use a different model.
- ***MIDI option*** per oscillator: Off (drone), Pitch, Pitch/trig, Trig, Trig by one note (for drums) or Poly, on the receive channel or the next three.
- ***Envelope*:** *Trigger* pings the low-pass gate like a trigger into Plaits; *Gate* (**GATE ENV**) holds the sound while the key is down. Velocity sets the level, or the accent in the drum, string and FM models.
- **Outputs:** *Mixed* puts all main outputs on Out 1 (left) and all aux outputs on Out 2 (right). **SPLIT OUT** (*Individual*) assigns any oscillator's main or aux output to each of Out 1-4.
- **FM banks:** `FMSYX/MACRO2_1.SYX` to `MACRO2_3.SYX` on the SD card replace the three built-in banks. MENU → Macro Oscillator 2 → *Write FM banks to SD* creates them.

## Troubleshooting
- **macOS says the app/plug-in is damaged or can't be opened:** the builds aren't notarised. Run `xattr -dr com.apple.quarantine "/path/to/EX Poly FM.app"` (the zip's `install.command` and the Homebrew cask do this for you).
- **No sound in SD Multisample:** check that the SD folder contains sample folders (SD → Show in Finder).
- **Stuck notes:** press **PANIC**.
- **An effect is silent:** the four effects need audio at the plug-in input. In the standalone apps choose the input under *Options → Audio/MIDI settings* and un-mute it.
