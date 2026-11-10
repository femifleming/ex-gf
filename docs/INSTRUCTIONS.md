# EX Suite - instructions

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

**Remembering state:** in a DAW, each instance saves with your project. The plug-ins also keep a *last state* file (in `~/Library/Application Support/EX Suite`, `%APPDATA%\EX Suite` or `~/.config/EX Suite`). New instances and the standalone apps start from where you left off.

**Outputs:** *Out 1/2* is the main stereo bus. *Out 3/4* is a second stereo bus. Enable it in your DAW to get per-timbre/per-voice outputs, the paraphonic gate and the mono mix, plus the auto-sampler's CV/gate.

**MIDI:** the receive channel defaults to 1 (Omni is available). Multi-timbral plug-ins use consecutive channels: timbre 1 on the receive channel, timbre 2 on the receive channel + 1, and so on. Each timbre can also be set to its own channel. Sustain (CC64), pitch bend and All Notes Off are supported, plus aftertouch in Poly Exciter.

## The SD card folder

It works like the module's MicroSD card. The default is `Music/EX SD Card`.

| Folder | Contents |
|---|---|
| `<any name>/` | **Multisample folders**: WAVs named `name_C4.wav` (natural pitch, sharps only, C4 = MIDI 60), `_V1`/`_V2`… velocity layers (V1 = softest), `_RR1`/`_RR2`… round robins (`name_C4_V2_RR1.wav`), optional `_SW<note>` switch point. Loops come from `smpl` or `cue ` chunks. A `playlist.txt` (disting playlist v1: `-natural=`, `-switch=`, `-loop=`, `-endLoopOnRelease=`) is optional. Files without a note name map chromatically from C3. |
| `wavetables/` | Each wavetable is a sub-folder of single-cycle WAVs (used alphabetically), or one WAV of concatenated 600-sample cycles (256 for WaveEdit banks, detected automatically). `playlist-wavetable.txt` with `-wavelength=` is optional. |
| `exciters/` | Each table is a folder of up to 128 short WAVs, or one WAV holding exactly 128 equal-length exciters. |
| `FMSYX/` | DX7 32-voice bank SysEx files (`.syx`, 4104 bytes). With none present, the three built-in Plaits banks are used. |
| `scl/`, `kbm/` | Scala scales and keyboard maps (for the *Scala/MTS* and *Scala KBM* parameters). |
| `!RECORD/` | WAV Recorder recordings (`REC00001.WAV`…). |
| `AUTO0001/`… | Auto-sampler output (`060_C4_V1_RR1.wav`…). It's a normal multisample folder. |
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

## Troubleshooting
- **macOS says the app/plug-in is damaged or can't be opened:** the builds aren't notarised. Run `xattr -dr com.apple.quarantine "/path/to/EX Poly FM.app"` (the zip's `install.command` and the Homebrew cask do this for you).
- **No sound in SD Multisample:** check that the SD folder contains sample folders (SD → Show in Finder).
- **Stuck notes:** press **PANIC**.
