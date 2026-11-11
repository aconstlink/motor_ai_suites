# Audio Onset Sculptures

The engine's `motor::audio::onset_detector` drives three colored sculptures:
low (orange, 40-250 Hz), mid (green, 250-2000 Hz) and high (violet, 2000-12000 Hz).
A detected onset sets a visual pulse to one. The pulse decays exponentially;
it lifts/scales a cube and brightens/expands its rings. Small energy pillars
show the detector envelope separately, so a sustained tone is not confused with
a new onset. This is onset detection, not beat tracking or kick classification.

## Run

```powershell
.\build\bin\Release\30_audio_onset.exe
.\build\bin\Release\30_audio_onset.exe --d3d-only
.\build\bin\Release\30_audio_onset.exe --synthetic
.\build\bin\Release\30_audio_onset.exe --synthetic --demo --youtube-short vertical
```

Default input is motor's `what_u_hear` capture of the system output. Play music
on the default output device. The application does not record files, request
microphone capture or play audio itself. Capture availability depends on the
platform/backend. Use synthetic mode when loopback capture is unavailable.

`--demo` only hides the UI; combine it with `--synthetic` for a self-running
silent demonstration, or leave capture active for a music-reactive recording.
Square and vertical output use the existing suite capture options. Camera FOV
preserves horizontal scene coverage in portrait. One window uses gl4 by default.

## Signal Path

- The existing audio backend provides squared-amplitude FFT bins. The test
  sums them over three non-overlapping, half-open bands; it does not normalize
  each FFT frame, take a square root or replace the engine FFT.
- Every band uses the engine detector and its internal attack/release envelope,
  moving reference, minimum, trigger ratio and rearm ratio. Defaults match the
  current motor-suites experiment. Parameters can be edited in the meter popup.
- The visual release is separate from the detector release. It changes only
  how long the visible event lasts, not which onsets are detected.
- Relative thresholds help across levels, but this is not completely gain
  invariant: the absolute minimum still matters. The meter display scale and
  decorative energy-pillar scale do not alter detection.
- Synthetic mode feeds known band-energy pulses at a fixed 5 ms cadence. It
  bypasses capture and FFT and does not generate audible sound.

Audio updates and UI edits share a test-local mutex. Graphics copies only the
three pulse/envelope values. The live path follows the existing capture example:
queue capture and consume the last completed spectrum. The capture API has no
sequence identifier here, so the test cannot distinguish a repeated block from
an unchanged newly captured block.

The onset meter is a test-local copy of Alex Link's motor-suites widget, with
includes and namespace adapted. The current ai-suites motor submodule has the
detector but not yet that widget; no engine files are modified.

## Verification

```powershell
ctest --test-dir build -C Release -R "graphics_audio_onset_cpu|graphics_capture_30_" --output-on-failure
```

Windowless checks cover sub-threshold noise, a rising onset, a sustained level,
rearming after silence, zero dt, reset, band boundaries, missing bin spacing,
known synthetic onset counts and timestep-independent visual decay.
These tests do not verify physical loopback capture or GPU rendering.

Windows Debug and Release builds succeeded; all five targeted CTests passed
in each configuration. No visual desktop or live-audio test was performed.
