# Backlog

## Position calibration ("aferição") mode

**Why**: gesture detection (`tilt_direction()` in `invoke_game.c`) currently
assumes a fixed axis mapping (X sign → left/right, Y sign → up/down, relative
to a flat/level rest pose). On the bench this doesn't match how the band
actually sits when worn — reaching all 4 answers needs close to 360° of wrist
rotation instead of a small natural tilt (see the raw-accel diagnostic added
in `main.c` for v1.1.0, still uncommitted-for-removal, and the investigation
that stalled when we moved on to screen appearance). Hardcoding a "correct"
mapping means guessing at wear position/handedness/strap orientation, which
won't hold for every student. A per-session calibration sidesteps that
entirely: record whatever 4 physical positions the teacher actually asks for.

**Flow** (teacher-driven, from the console, before starting questions):

1. Teacher puts a band into **calibration mode** (new console action — a
   button or panel, not part of the question form).
2. Teacher asks the student to hold some position of their choosing — no
   fixed meaning, whatever the teacher decides is "A" this session.
3. Teacher fires the label for that position (A, B, C, or D) from the
   console. On receiving it, the device snapshots its *current* accelerometer
   reading and stores it as that label's reference vector.
4. Repeat for the remaining letters, in whatever order the teacher wants.
5. **All 4 positions are required** — calibration isn't valid (can't be
   exited / used) with fewer than 4 distinct recorded labels. Needs a
   distinctness check too (reject/warn if two labels get a near-identical
   reading — otherwise two answers become indistinguishable).

**Once calibrated**, answer detection changes from the fixed-axis heuristic
to "which of the 4 stored reference vectors is the current reading closest
to" (e.g. cosine similarity or Euclidean distance in accel space) — replacing
`tilt_direction()`'s role for that band.

**Open questions for implementation:**
- Wire protocol: new message type(s) on the existing NUS RX JSON channel
  (proxy-only interaction, no mesh needed) — e.g. `{"t":"cal","cmd":"start"}`,
  `{"t":"cal","cmd":"set","label":"A"}`, `{"t":"cal","cmd":"done"}`.
- Persistence: save calibration to NVS so it survives a reboot, or is it
  expected fresh every session? (Leaning toward persist — recalibrating every
  boot defeats the point.)
- Console UI: a "Calibração" panel — start button, 4 "gravar A/B/C/D" actions
  fired one at a time, progress indicator for which labels are done, a way to
  cancel/redo a single label without restarting all 4.
- Fallback: what happens for a band that was never calibrated — keep
  `tilt_direction()`'s fixed mapping as a default, or refuse to run questions
  until calibrated?
- Mesh-relayed bands (not directly connected to the app) still need their
  *own* calibration done via whichever app they're proxied through — this is
  a per-band, per-wearer thing, not something one calibration pass covers
  for the whole mesh.

**Related cleanup while in this code**: remove the temporary raw-accel
diagnostic log in `main.c` (`// TEMP: axis-mapping diagnostic`) once this
replaces the need for it, or once the fixed-mapping fallback is separately
retuned.
