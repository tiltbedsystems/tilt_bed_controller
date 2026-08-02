# fw_HANDOFF_2026-08-02_03 — Item 1 implemented; Item 2 recorded, not built

Implements the approved Item 1 (pitch wire sign), records Item 2's
prohibition and open filter question in `DECISIONS.md` without writing any
Item 2 code, and applies the process rulings from the same review.

Firmware moves off `29fdf0d` for the first time since the Stage C regression
record was signed. §c.1 carries that diff in full; §b.4 explains why a
regression re-run is not required for it.

---

## a. Header

| Field | Value |
|---|---|
| Repo | `tilt_bed_controller` |
| Branch | `main` |
| HEAD before session | `6cbd2fd` |
| HEAD after session | `9878d67` (plus this handoff's own commit, made after this file was written) |
| Previous firmware baseline | `29fdf0d` — **no longer current**, see §b.4 |
| Session date | 2026-08-02 |
| Scope | Item 1 implemented. Item 2 recorded only, no code. |

### Commits this session

```
9878d67 Handoff spec: per-session check that fails closed, plus review rulings
de9c848 DECISIONS #31/#32: record Item 2's prohibition and filter gap before any code
140c37b Pitch on the BLE wire is POSITIVE = head raised
```

Only `140c37b` touches firmware.

### `git status` at end of session

```
On branch main
Your branch is ahead of 'origin/main' by 23 commits.
  (use "git push" to publish your local commits)

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore
	modified:   docs/SESSION_LOG.md

Untracked files:
  (use "git add <file>..." to include in what will be committed)
	.projectchecksums
	docs/handoffs/fw_HANDOFF_2026-08-02_01_pitch-sign-tilt-sampling-plan.md
	docs/handoffs/fw_HANDOFF_2026-08-02_02_handoff-artifact-process-change.md
	docs/handoffs/fw_HANDOFF_2026-08-02_03_pitch-sign-item1-and-review-rulings.md
	tilt_bed_controller_project.pdf

no changes added to commit (use "git add" and/or "git commit -a")
```

Captured immediately before the final commit, which adds the three handoff
files and the `docs/SESSION_LOG.md` entry — per the new "a handoff is
committed separately and last" convention (§b.5). `.projectlinkstore`,
`.projectchecksums` and `tilt_bed_controller_project.pdf` were already
dirty/untracked before this session and were not touched by it.

---

## b. Full text of what this session produced

No new plan was produced — this session executed an existing, reviewed plan.
The full text of that plan is in
`fw_HANDOFF_2026-08-02_01_pitch-sign-tilt-sampling-plan.md`, uploaded in this
same batch, which the spec permits (a sibling handoff is visible to the
reviewer; the prohibition in requirement (b) is against pointing at documents
they cannot see). What follows is the design reasoning produced *this*
session, in full.

### b.1 What the review changed versus the plan as written

The plan was approved for Item 1 with five additions and one correction, all
of which were implemented. Recording them here because the delta is the part
a reviewer of *this* session needs, and it is not in `_01`:

1. **R8 was only half-mitigated in the plan.** The plan's risk table said
   "comment at both `read_accel()` and `bed_status.c`", but the proposed diff
   only contained the `bed_status.c` one. The `read_accel()` counterpart is
   the more important of the two — that is the site where a future reader
   would make the change. Both now exist and each points at the other.
2. **The roll convention was going to be written as specification.** It is
   not established. Corrected — see §b.2.
3. **The protocol-version question was not in the plan at all.** Added — see
   §b.3.
4. **The stale regression record needed stating in the commit.** Done — see
   §b.4.
5. **The CSV-printf note** was in the plan's prose but not its diff. Added.

### b.2 Why roll is marked PROVISIONAL and pitch is not

The plan proposed writing both conventions into `BLE_GATT_DESIGN.md` §6 as
specification. Only pitch has evidence behind it: the bench run reported the
pitch sign as wrong, which is a direct observation of pitch.

Roll has none. `POSITIVE = LEFT raised` is a read of the frame-diagram comment
at `auto_level.c:394-411` — a description of the intended mounting, not a
measurement. The bench run that caught pitch did not mention roll, and that
silence carries no information: **the run would have looked identical whether
roll was right or wrong, because nobody checked it.** Treating "not reported
as broken" as "confirmed working" is the same inference error as DECISIONS
#27, which recorded a fix as confirmed when nothing had confirmed it.

So §6 now carries pitch as specification and roll as an explicitly
provisional hypothesis, with the four-direction acceptance table and an
instruction to delete the marker only after it passes. The same caveat is
mirrored in `bed_status.h`'s field comment so it is visible from the code, not
only the doc.

### b.3 Protocol version — analysis and decision

This change **inverts what bytes mean while leaving the layout identical**.
Offset 6 is still an `i16` ×100. That is the one class of wire change a client
cannot detect: it reads without error and displays pitch upside down. The
Protocol Version characteristic exists precisely to let a client refuse a
format it does not understand, and a silent semantic change defeats it.

Decision, as directed: **no bump.** Verified rather than assumed —

- Firmware serves `01` from `config/btconf/gatt_configuration.btconf:117`, a
  `const` characteristic.
- The app's `kMaxSupportedProtocolVersion` is `1`
  (`protocol_version.dart:17`) and anything greater returns
  `ProtocolCompatibility.tooNewRefuse` (`protocol_version.dart:36-37`), which
  `ble_bed_transport.dart:138` turns into a refusal to connect.

So bumping to `02` would make current firmware refuse the current app, for a
change the app needs no edit to absorb — it decodes the wire value directly.
Amending v1 in place is correct **only because nothing has shipped**.

An additional point the review did not raise and worth recording: the version
value lives in a Simplicity Studio-generated config file, which per this
repo's hard constraints may not be hand-edited. A future bump therefore goes
through the configurator, not a text edit — a small extra reason not to treat
bumping as cheap.

The rule that applies afterwards is now written into §6: **after first
customer ship, any change to what a field MEANS requires a version bump even
when the byte layout is byte-for-byte identical.** Layout stability is not
compatibility.

### b.4 Why no regression re-run, and the stale record

`REGRESSION_RECORD_PRE_STAGEC.md` is signed against `29fdf0d`. Firmware is now
at `140c37b`, so that record is one commit stale, and the commit message says
so rather than leaving it silently out of date.

A full re-run is not required for this change. `bed_status_get()` is a pure
read of already-computed state into a struct. The change touches no timing
path, no safety path, no I2C, no interrupt context, and no control decision;
it cannot affect loop rate, so it cannot perturb anything in DECISIONS #29's
loop-pass-calibrated set. Everything else on that record — the Debug-only
caveat and the still-owed Release-config pass — is unaffected and still
stands.

What *is* required is the four-direction sign acceptance run (§b.2). Until it
runs, Item 1 is ASSERTED. It compiles-by-inspection only; see §e.

### b.5 Process rulings implemented

The hook tightening is the substantive one. As built last session it tested
only that a handoff file for today **existed**, so a second session on the
same date was satisfied by the first session's file and could end having
written nothing — a version of the exact failure the artifact exists to
prevent. It now:

- scans the session transcript for a write to
  `docs/handoffs/fw_HANDOFF_<today>_NN_*.md` **by this session**, and
- separately confirms the file is on disk.

And it **fails closed**, deliberately diverging from `check-session-log.ps1`.
The asymmetry is commented at the top of the script: a false block on the log
hook is annoying; a false pass here loses the evidence a reviewer needs, which
is the entire failure mode being designed against. Unparseable payload or
unreadable transcript now block.

The `stop_hook_active` guard is retained and is now **the only path that exits
0 without evidence**. That is not an oversight — without it, fail-closed on a
condition the model cannot fix (an unreadable transcript) would be an
unbreakable stop loop with no way out.

Remaining rulings, applied as directed: `SESSION_LOG.md` diffs never need
reproducing (generalised from last session's one-off judgement into a standing
exception); a redaction carve-out scoped to command output only, explicitly
not softening (b); handoffs describe preceding commits and are committed
separately and last; `docs/handoffs/` is version-controlled; retention
recorded as a deliberate non-decision; `docs/handoffs/` added to the Project
layout list; and the older section's now-incomplete "until the file has been
written" sentence amended to name both artifacts.

Note on the prior handoffs: `_01` and `_02` were **not** edited to reflect
these rulings. They are per-session artifacts, and this repo corrects the
record in a new entry rather than rewriting an old one (DECISIONS #27,
`SESSION_LOG.md`'s own rule). `_02`'s seven open questions are answered here
and in the commit messages, not retro-fitted into `_02`.

---

## c. Complete diffs

### c.1 Source diff — `140c37b`, verbatim, not truncated

```diff
diff --git a/auto_level.c b/auto_level.c
index 9061081..b445881 100644
--- a/auto_level.c
+++ b/auto_level.c
@@ -217,6 +217,26 @@ void read_accel(){
   accel_vector_mag = sqrt(x_accel_vector * x_accel_vector + y_accel_vector * y_accel_vector + z_accel_vector * z_accel_vector);
 
   // Calculate roll and pitch angles
+  //
+  // PITCH SIGN IS DELIBERATE - DO NOT "FIX" IT HERE.
+  // This produces NEGATIVE pitch when the HEAD is raised (see the frame
+  // diagram below set_auto_level_motor_direction()). That is the internal
+  // convention, it is fixed by how the accelerometer is mounted, and two
+  // things depend on it:
+  //
+  //   1. Auto-level's direction logic. latest_pitch_angle feeds the running
+  //      average -> check_level(PITCH) -> level_status[PITCH].positive ->
+  //      set_auto_level_motor_direction(), which decides whether HEAD or FOOT
+  //      drives up. Negating here inverts that - the bed would level away
+  //      from level. Safety-relevant.
+  //   2. Every zero-level baseline already stored in NVM3, on every bed
+  //      built. settings_mode.c stores returnAccelValue(PITCH) raw, in this
+  //      convention. Negating here would silently invalidate all of them and
+  //      there is no migration path for units already in the field.
+  //
+  // The product-facing convention (POSITIVE = head raised) is applied ONCE,
+  // at the wire boundary, in bed_status.c's bed_status_get(). That is the
+  // only place the two conventions meet. Roll is unaffected either way.
   latest_roll_angle = (float)(atan2(x_accel_vector, accel_vector_mag)) * (RAD_TO_DEG);
   latest_pitch_angle = (float)(atan2(y_accel_vector,z_accel_vector)) * (RAD_TO_DEG) - PITCH_OFFSET;
 
@@ -268,6 +288,14 @@ int16_t returnAccelValue(axis_t axis) {
   }
 }
 
+// NOTE ON SIGN, for the commented-out CSV lines below as well as the live
+// prints: every pitch value printed from this file is in the INTERNAL
+// convention (head raised => NEGATIVE), not the wire convention the app shows
+// (head raised => POSITIVE, applied in bed_status.c). If the CSV-style
+// printfs below are ever re-enabled for a tilt capture, the resulting log
+// will disagree in sign with the app for the same physical bed position.
+// Negate on the analysis side, or note the convention in the capture header -
+// do not "correct" it here (see the warning at read_accel()).
 void print_accel_value(axis_t axis){
   if (axis == ROLL){
     printf("ROLL Angle is: %d\r\n", accel_average_roll_angle);
diff --git a/bed_status.c b/bed_status.c
index 992202e..73f3fa5 100644
--- a/bed_status.c
+++ b/bed_status.c
@@ -90,7 +90,20 @@ void bed_status_get(bed_status_t *out)
   }
 
   out->roll_angle_scaled = baseline_accel_average_roll_angle;
-  out->pitch_angle_scaled = baseline_accel_average_pitch_angle;
+
+  // Product convention on the wire is POSITIVE = head raised. The internal
+  // value is the opposite sign (see auto_level.c's frame diagram: "If HEAD is
+  // high, PITCH is NEGATIVE"), and it must stay that way - latest_pitch_angle
+  // feeds check_level()/set_auto_level_motor_direction(), so flipping it
+  // upstream would invert which pair auto-level drives. The wire encoding is
+  // the correct boundary for the convention change. Roll is unaffected.
+  //
+  // This negation is the ONLY place the two conventions meet. Do not add a
+  // second one anywhere downstream - ble_bed_service.c just packs the int16.
+  // Negation cannot overflow: the source range is about -28500..+10500
+  // (atan2 minus PITCH_OFFSET, scaled, less a baseline clamped to +/-1500 by
+  // bed_settings.c), against INT16_MIN -32768.
+  out->pitch_angle_scaled = -baseline_accel_average_pitch_angle;
 
   out->any_collision = any_collision;
   out->in_settings_mode = app_in_settings_mode();
diff --git a/bed_status.h b/bed_status.h
index bc8eeb9..e0009a3 100644
--- a/bed_status.h
+++ b/bed_status.h
@@ -46,6 +46,14 @@ typedef struct {
   // continuously fresh while auto-level or the settings zero-level capture
   // is running - stale (last-known, possibly 0) otherwise. See
   // DECISIONS.md #17.
+  //
+  // SIGN CONVENTION - these are wire-facing values, and pitch does NOT match
+  // auto_level.c's internal sign. bed_status_get() negates pitch exactly once
+  // on the way out; see the comment there.
+  //   roll_angle_scaled  POSITIVE = LEFT side raised
+  //                      (derived from auto_level.c's frame diagram, NOT yet
+  //                      bench-confirmed - BLE_GATT_DESIGN.md #6)
+  //   pitch_angle_scaled POSITIVE = HEAD raised
   int16_t roll_angle_scaled;
   int16_t pitch_angle_scaled;
 
```

**The entire behavioural change is one line**: `out->pitch_angle_scaled =
-baseline_accel_average_pitch_angle;`. Everything else is comments.

### c.2 Docs diffs

Three docs files changed across the three commits, all docs-only and none
over the 400-line threshold. Reproduced by reference to their commits rather
than pasted, as they are large and non-behavioural:

| File | Commit | Size |
|---|---|---|
| `docs/BLE_GATT_DESIGN.md` | `140c37b` | +69 / −1 |
| `docs/DECISIONS.md` | `de9c848` | +101 / −0 (new #31, #32) |
| `CLAUDE.md` | `9878d67` | part of +330 / −2 |
| `docs/handoffs/README.md` | `9878d67` | same commit |
| `.claude/hooks/check-handoff-file.ps1` | `9878d67` | rewritten |
| `.claude/settings.json` | `9878d67` | +13 (second Stop entry) |

`git show <sha>` reproduces each. The `docs/SESSION_LOG.md` diff is omitted
under the standing exception added this session — the log ships alongside.

**Judgement call flagged:** `check-handoff-file.ps1` is executable tooling,
and the never-truncate rule for source arguably covers it. Its full text was
reproduced in `_02` §c.4 at its previous revision; the current revision is not
reproduced here. See §f.1.

---

## d. Raw output of every command run

No secrets appeared in any output, so no redaction was applied.

### d.1 `bed_status.h` region and protocol-version verification

```
$ grep -rn "protocol_version\|PROTOCOL_VERSION" --include=*.c --include=*.h . | grep -v autogen
=== firmware protocol version ===
(no output)
=== app max supported ===
…/tilt_bed_app/lib/protocol/protocol_version.dart:17:const int kMaxSupportedProtocolVersion = 1;
…/tilt_bed_app/lib/protocol/protocol_version.dart:21:  /// Version is within [kMinSupportedProtocolVersion, kMaxSupportedProtocolVersion].
…/tilt_bed_app/lib/protocol/protocol_version.dart:28:  tooNewRefuse,
…/tilt_bed_app/lib/protocol/protocol_version.dart:36:  if (version > kMaxSupportedProtocolVersion) {
…/tilt_bed_app/lib/protocol/protocol_version.dart:37:    return ProtocolCompatibility.tooNewRefuse;
…/tilt_bed_app/lib/transport/ble_bed_transport.dart:138:      if (compat == ProtocolCompatibility.tooNewRefuse) {
…/tilt_bed_app/lib/transport/ble_bed_transport.dart:141:          'supports (max v$kMaxSupportedProtocolVersion). Please update the app.',
…/tilt_bed_app/lib/transport/mock_bed_transport.dart:23:    int protocolVersion = kMaxSupportedProtocolVersion,
…/tilt_bed_app/lib/transport/mock_bed_transport.dart:205:    if (compat == ProtocolCompatibility.tooNewRefuse) {
…/tilt_bed_app/lib/transport/mock_bed_transport.dart:209:        'supports (max v$kMaxSupportedProtocolVersion). Please update the app.',
=== DECISIONS tail ===
`accel_alpha` any less loop-rate-dependent — the averaging window is still
"130 passes," not "N milliseconds." #29's time-based redesign of
`accel_running_average()`'s window is separate, control-behavior-changing
work, explicitly not part of this fix and not required to close out the BLE
drop.
```

The firmware grep returning **no output** is why the second grep below was
run — the value is not in C source at all.

```
$ grep -rni "protocol" --include=*.c --include=*.h --include=*.btconf --include=*.slcp . | grep -vi "autogen/sl_\|gecko\|bt_ll"
=== firmware protocol version (broad) ===
./autogen/gatt_db.h:28:#define gattdb_protocol_version               38
./autogen/gatt_db.h:45:#define gattdb_protocol_version_len           1
./config/btconf/gatt_configuration.btconf:115:    <!--Protocol Version-->
./config/btconf/gatt_configuration.btconf:116:    <characteristic const="true" id="protocol_version" name="Protocol Version" sourceId="" uuid="54b10006-ef39-4f03-a91d-eb12b0d067c9">
./config/psa_crypto_config.h:21:// <i> other software included in the application (e.g. wireless protocol stacks)
./config/sl_bgapi_config.h:3: * @brief BGAPI Protocol configuration
./config/sl_bgapi_config.h:38:// <i> Define the maximum payload size for BGAPI commands, responses, and events. The setting impacts all protocol stacks that use the BGAPI, i.e. Bluetooth host and Bluetooth mesh.
./config/sl_btctrl_scheduler_priority_config.h:152:// <i> coexistence and multiprotocol schedulers.
./config/sl_btctrl_scheduler_priority_config.h:154:// <i> and use this mapping window to adapt to other protocols.
./config/sl_btctrl_scheduler_priority_config.h:157:// <i> If coexistence or multiprotocol configuration is not used, then these
./main.c:43:  // Initialize Silicon Labs device, system, service(s) and protocol stack(s).
./tilt_bed_controller.slcp:143:  id: bgapi_protocol
./tilt_bed_controller.slcp:146:  id: bgapi_protocol_libraries_release
./tilt_bed_controller.slcp:548:  id: rail_lib_singleprotocol
./tilt_bed_controller.slcp:551:  id: rail_lib_singleprotocol_alpha_chips
./tilt_bed_controller.slcp:554:  id: rail_lib_singleprotocol_internal_chips_dummy
./tilt_bed_controller.slcp:629:  id: sli_protocol_crypto

$ sed -n '113,122p' config/btconf/gatt_configuration.btconf
    </characteristic>

    <!--Protocol Version-->
    <characteristic const="true" id="protocol_version" name="Protocol Version" sourceId="" uuid="54b10006-ef39-4f03-a91d-eb12b0d067c9">
      <value length="0" type="hex" variable_length="false">01</value>
      <properties>
        <read authenticated="false" bonded="true" encrypted="true"/>
      </properties>
    </characteristic>
  </service>
```

### d.2 Staging Item 1

```
$ git add bed_status.c bed_status.h auto_level.c docs/BLE_GATT_DESIGN.md && git status --short && git diff --cached --stat
warning: in the working copy of 'bed_status.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'bed_status.h', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/BLE_GATT_DESIGN.md', LF will be replaced by CRLF the next time Git touches it
 M .claude/settings.json
 M .projectlinkstore
 M CLAUDE.md
M  auto_level.c
M  bed_status.c
M  bed_status.h
M  docs/BLE_GATT_DESIGN.md
 M docs/SESSION_LOG.md
?? .claude/hooks/check-handoff-file.ps1
?? .projectchecksums
?? docs/handoffs/
?? tilt_bed_controller_project.pdf
=== source diff sanity ===
 auto_level.c            | 28 ++++++++++++++++++++
 bed_status.c            | 15 ++++++++++-
 bed_status.h            |  8 ++++++
 docs/BLE_GATT_DESIGN.md | 69 ++++++++++++++++++++++++++++++++++++++++++++++++-
 4 files changed, 118 insertions(+), 2 deletions(-)
```

### d.3 Commits

```
$ git commit -F - (Item 1)
[main 140c37b] Pitch on the BLE wire is POSITIVE = head raised
 4 files changed, 118 insertions(+), 2 deletions(-)

$ git commit -F - (DECISIONS #31/#32)
warning: in the working copy of 'docs/DECISIONS.md', LF will be replaced by CRLF the next time Git touches it
[main de9c848] DECISIONS #31/#32: record Item 2's prohibition and filter gap before any code
 1 file changed, 101 insertions(+)

$ git commit -F - (process rulings)
warning: in the working copy of '.claude/settings.json', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'CLAUDE.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of '.claude/hooks/check-handoff-file.ps1', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/handoffs/README.md', LF will be replaced by CRLF the next time Git touches it
[main 9878d67] Handoff spec: per-session check that fails closed, plus review rulings
 4 files changed, 330 insertions(+), 2 deletions(-)
 create mode 100644 .claude/hooks/check-handoff-file.ps1
 create mode 100644 docs/handoffs/README.md
```

The `LF will be replaced by CRLF` lines are git's standard autocrlf notices on
this Windows checkout, not errors.

### d.4 Tightened hook — five-case test, first run

```
$ (PowerShell) pipe synthetic Stop payloads into check-handoff-file.ps1
=== A: this session wrote handoff files today + on disk -> expect 0 ===
exit=0
=== B: TIGHTENING - old session never wrote one, files exist on disk -> expect 2 ===
powershell : BLOCKED: this session did not write a handoff file for today (2026-08-02) in docs/handoffs/.
At line:11 char:84
+ ... Compress) | powershell -NoProfile -ExecutionPolicy Bypass -File $s 2> ...
+                 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    + CategoryInfo          : NotSpecified: (BLOCKED: this s...docs/handoffs/.:String) [], RemoteException
    + FullyQualifiedErrorId : NativeCommandError
 
Per "Session handoff file (required)" in CLAUDE.md, this session must leave a
exit=2
=== C: FAIL CLOSED - unparseable stdin -> expect 2 ===
powershell : BLOCKED: the Stop hook payload could not be parsed, so this session's handoff file could not be confirmed 
(failing closed).
At line:14 char:21
+ ... n at all' | powershell -NoProfile -ExecutionPolicy Bypass -File $s 2> ...
+                 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    + CategoryInfo          : NotSpecified: (BLOCKED: the St...ailing closed).:String) [], RemoteException
    + FullyQualifiedErrorId : NativeCommandError
 
exit=-1
=== D: FAIL CLOSED - missing transcript -> expect 2 ===
powershell : BLOCKED: the session transcript could not be read, so this session's handoff file could not be confirmed 
(failing closed).
At line:17 char:100
+ ... Compress) | powershell -NoProfile -ExecutionPolicy Bypass -File $s 2> ...
+                 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    + CategoryInfo          : NotSpecified: (BLOCKED: the se...ailing closed).:String) [], RemoteException
    + FullyQualifiedErrorId : NativeCommandError
 
exit=-1
=== E: loop guard still exits 0 -> expect 0 ===
exit=0
```

**Cases C and D reported `exit=-1`, not the expected 2.** Diagnosed as a test
harness artifact, not a script fault: those two cases piped through
`Select-Object -First 2`, which tears the pipeline down before the native
process is reaped, so `$LASTEXITCODE` reflects the aborted pipeline. Case B
used the same construct but happened to complete first. Re-run without the
truncation to get true exit codes — see §d.5.

### d.5 Tightened hook — fail-closed cases re-tested cleanly

```
$ (PowerShell) re-run C and D without pipeline truncation
C unparseable stdin      -> exit=2 (expect 2)
D missing transcript     -> exit=2 (expect 2)
```

Harness reported "Exit code 2" for the block, which is the intended result.
All five cases now confirmed: A→0, B→2, C→2, D→2, E→0.

### d.6 Final source diff and commit list

```
$ git show 140c37b --format="" -- bed_status.c bed_status.h auto_level.c
```

Output reproduced verbatim in §c.1.

```
$ git log --oneline 6cbd2fd..HEAD
9878d67 Handoff spec: per-session check that fails closed, plus review rulings
de9c848 DECISIONS #31/#32: record Item 2's prohibition and filter gap before any code
140c37b Pitch on the BLE wire is POSITIVE = head raised
```

### d.7 Failures

One test-harness artifact (§d.4 cases C/D reporting `-1`), diagnosed and
re-run in §d.5. No command failed. No build was run — this session cannot
build or flash.

---

## e. VERIFIED vs ASSERTED, with evidence

### VERIFIED

| # | Claim | Evidence |
|---|---|---|
| V1 | The behavioural change is exactly one line; everything else is comments | §c.1 — sole non-comment `+` line is `out->pitch_angle_scaled = -baseline_accel_average_pitch_angle;` |
| V2 | Both R8 comments exist, and each references the other | `auto_level.c:220-239` ("applied ONCE … in bed_status.c") and `bed_status.c:93-105` ("see auto_level.c's frame diagram"); §c.1 |
| V3 | Firmware serves protocol version `01` | `config/btconf/gatt_configuration.btconf:117`, raw output §d.1 |
| V4 | App refuses versions above 1 via `tooNewRefuse` | `protocol_version.dart:17,36-37`; `ble_bed_transport.dart:138`; raw output §d.1 |
| V5 | The version value is in a Studio-generated config file, so a bump needs the configurator | `config/btconf/gatt_configuration.btconf` path + the repo's hard constraint on `config/` |
| V6 | Tightened hook rejects a session that did not itself write a handoff, even with files on disk | §d.4 case B → exit 2 |
| V7 | Tightened hook fails closed on unparseable payload and on unreadable transcript | §d.5 → both exit 2 |
| V8 | The loop guard still exits 0 | §d.4 case E |
| V9 | Three commits made; only `140c37b` touches firmware | §d.6 `git log`; §d.2 staged file list |
| V10 | `DECISIONS.md` gained #31 and #32 and nothing else | §d.3 — `1 file changed, 101 insertions(+)`, no deletions |
| V11 | No Item 2 code was written | §d.2/§d.6 — no sampler, no `tilt_fresh` change; `ble_bed_service.c` untouched in every commit |
| V12 | The auto-level sampling path is unmodified | `auto_level.c` diff (§c.1) contains only comment lines; `accel_running_average()`, `read_accel()`'s logic, and the buffer are byte-identical |

### ASSERTED

| # | Claim | Why only ASSERTED |
|---|---|---|
| A1 | **Item 1 works.** Pitch now reads positive with the head raised | Not built, not flashed, not measured. Acceptance run: raise HEAD → pitch positive; FOOT → negative; LEFT → roll positive; RIGHT → negative. |
| A2 | The code compiles | No build was run. The change is a unary minus on an `int16_t` and comment blocks, but "looks like it compiles" is not "compiles". |
| A3 | The overflow analysis (−28500..+10500 vs INT16_MIN) | Arithmetic from the frame geometry and `bed_settings.c:36`'s clamp. Not exercised at the extremes on hardware. |
| A4 | No regression re-run is needed for this change | Reasoning from what `bed_status_get()` touches (§b.4). Sound, but it is an argument, not a test. |
| A5 | The roll convention `POSITIVE = LEFT raised` | Read off a comment. This is exactly why it is marked PROVISIONAL rather than written as spec. |
| A6 | Both Stop hooks fire correctly in a real session | Pipe-tested only. `.claude/settings.json` and the hook changed mid-session, so the settings watcher may need a restart or one `/hooks` open. |
| A7 | The `fw_`/`app_` collision risk | From the user's account of the upload destination; not verified from this repo. |

---

## f. QUESTIONS FOR REVIEW

1. **`check-handoff-file.ps1`'s full text is not reproduced in this file.**
   It is executable tooling, so the never-truncate-source rule arguably
   applies; but it is also 130-odd lines of hook, reproduced at its previous
   revision in `_02` §c.4, and recoverable with `git show 9878d67`. I omitted
   it and flagged it rather than deciding the rule's scope unilaterally.
   Should hook scripts count as "source" for requirement (c)?
2. **The fail-closed hook has exactly one escape hatch: `stop_hook_active`.**
   If the transcript is ever genuinely unreadable, the first Stop blocks and
   the second passes. That is intentional — the alternative is an unbreakable
   loop — but it does mean fail-closed is really "fail-closed once". Is that
   the intended strength?
3. **Should the four-direction sign acceptance run be added to
   `REGRESSION.md`** as a permanent numbered check, rather than living only
   in `BLE_GATT_DESIGN.md` §6? It is a one-time confirmation for this change,
   but it is also the kind of thing worth re-checking after any accelerometer
   remount.
4. **`REGRESSION_RECORD_PRE_STAGEC.md` is now one commit stale and says
   nothing about it.** The commit message explains why no re-run is needed,
   but the record itself still reads as though `29fdf0d` is current. Should
   it get a pointer note, the way `REGRESSION_RECORD_D1.md` got one?
5. **DECISIONS #31 states the prohibition; it does not record the accepted
   O2.3 architecture, the piggyback ruling (Q4), or the `tilt_fresh`
   semantics (Q5).** Those are in this handoff and the session log only,
   because writing them into `DECISIONS.md` looked like the #17
   implementation entry that was explicitly deferred. Confirm that is the
   right split, or say where they should live in the meantime.

---

## g. NOT DONE

| Item | Reason |
|---|---|
| Item 2 implementation (sampler, `tilt_fresh`) | Explicitly not approved. "Do not write any Item 2 code this session." Nothing was written; V11/V12 are the evidence. |
| DECISIONS #17 implementation entry | Explicitly deferred: "That goes in when there is an implementation to record." |
| Designing the telemetry filter | Explicitly deferred: "Do not design it now." Recorded as open in DECISIONS #32. |
| Any change to the auto-level sampling path | Explicitly prohibited. The only `auto_level.c` edits are comments (V12). |
| Building or flashing | Repo hard constraint — the user drives the bench. Everything about runtime behaviour is ASSERTED (A1–A4). |
| The four-direction bench acceptance run | Requires the bench. Item 1 stays ASSERTED until it passes. |
| Promoting the roll convention from PROVISIONAL to spec | Waits on the same run. |
| App-side `TODO(tilt-fresh)` revert | Ruled to wait until Item 2(a) and 2(b) have both landed and been bench-verified. Neither exists. |
| Editing `_01` or `_02` to reflect these rulings | Deliberate: per-session artifacts are corrected in a new entry, not rewritten. See §b.5. |
| Reproducing the current `check-handoff-file.ps1` in full | Judgement call, raised as §f.1. |
| Pushing | Branch is 23 commits ahead of `origin/main`. No push was requested. |
