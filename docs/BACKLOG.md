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

## BLE pairing/connection is still too fragile

**Why**: even after the v1.1.0 fix (disable NimBLE host privacy + renegotiate
connection params on connect), pairing with the band from the console is
still unreliable in day-to-day use — a lot of difficulty getting and keeping
a connection, not a one-off. Symptoms seen across today's testing:

- Connection attempts intermittently fail at the link layer with no host
  event at all (`Reattempt advertising; reason: 0x3e` — connection
  establishment failure) — first-attempt-after-boot usually works, retries
  after that are hit or miss.
- A connection that does establish can drop mid-session
  (`app disconnected, reason=520` = `0x208`, supervision timeout) — after
  which the *next* several reconnect attempts from the phone/browser fail
  outright for a minute or more, with nothing at all on serial (not even a
  failed-attempt log), and a full board reset (not just re-opening the
  browser's device picker) was the only thing that reliably recovered it in
  today's session.
- Firmware comments already document this as a known single-radio C3
  limitation (concurrent scan+advertising kills the link; the v1.1.0 fix
  narrowed the window but didn't close it).

**Scope for whoever picks this up**: this needs real investigation, not
another guess-and-flash cycle — e.g. capture the actual HCI-level
disconnect/reconnect sequence (btmon on the Android/desktop side, not just
our own ESP_LOG lines) to see what the central is doing right before a
connection attempt silently fails, and decide whether the fix is firmware
(radio timing, advertising parameters), a recovery behavior (e.g.
auto-`esp_restart()` after N consecutive failed connection attempts, since a
reset is what unblocks it today), or both.

## Chrome on Linux needs a manual flag for Web Bluetooth

Not a project bug, but worth a README note (apps/invoke-console) since it
cost real debugging time today: on desktop Linux, Chrome ships Web
Bluetooth *disabled by default* — `navigator.bluetooth` is fully absent from
the page, with no console error to point at it. Fix: `chrome://flags` →
search "bluetooth" → **Web Bluetooth** (`#enable-web-bluetooth`) → Enabled →
Relaunch. Windows/Mac/ChromeOS/Android don't need this. Add this to
apps/invoke-console/README.md's "Run locally" section.
