Mutable Instruments Plaits (all 24 engines, firmware 1.2), Rings (resonator + string synth) and the parts of
stmlib they need, by Emilie Gillet, from https://github.com/pichenettes/eurorack and
https://github.com/pichenettes/stmlib (MIT licence - see LICENSE and the file headers).

Only the DSP code and its lookup tables are included (no hardware drivers, UI or bootloader).
One change was made for EX-GF: plaits/dsp/voice.h/.cc gained set_custom_fm_bank() and trigger_state(), so DX7 banks
can be loaded from the SD card folder. plaits/user_data.h includes <cstdio>. Everything else is unmodified.
