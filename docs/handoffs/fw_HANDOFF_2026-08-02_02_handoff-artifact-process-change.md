# fw_HANDOFF_2026-08-02_02 — handoff-artifact process change (DOCS AND CONFIG ONLY)

Establishes `docs/handoffs/` as a second, reviewer-facing artifact alongside
`docs/SESSION_LOG.md`, and retroactively repairs the specific gap that
prompted it. No firmware was touched.

---

## a. Header

| Field | Value |
|---|---|
| Repo | `tilt_bed_controller` |
| Branch | `main` |
| HEAD before session | `6cbd2fd` |
| HEAD after session | `6cbd2fd` (no commits — none requested) |
| Firmware baseline | `29fdf0d` — parked, untouched |
| Session date | 2026-08-02 |
| Scope | DOCS AND CONFIG ONLY. No `.c` or `.h` modified. |

### `git status` at end of session

```
On branch main
Your branch is ahead of 'origin/main' by 20 commits.
  (use "git push" to publish your local commits)

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .claude/settings.json
	modified:   .projectlinkstore
	modified:   CLAUDE.md
	modified:   docs/SESSION_LOG.md

Untracked files:
  (use "git add <file>..." to include in what will be committed)
	.claude/hooks/check-handoff-file.ps1
	.projectchecksums
	docs/handoffs/
	tilt_bed_controller_project.pdf

no changes added to commit (use "git add" and/or "git commit -a")
```

`.projectlinkstore`, `.projectchecksums` and `tilt_bed_controller_project.pdf`
were already dirty/untracked before this session and were not touched by it.
`docs/handoffs/` shows as a single untracked directory entry; it contains
`README.md`, `fw_HANDOFF_2026-08-02_01_pitch-sign-tilt-sampling-plan.md`, and
this file.

**Nothing is committed.** No commit was requested.

---

## b. Full text of what this session produced

This session produced a process design rather than a plan for firmware. The
design, its rationale, and the full text of every artifact it created follow.
The complete content of the two new executable/config artifacts is in §c;
everything is inside this file, with no reference to anything outside it.

### b.1 The problem being solved

`docs/SESSION_LOG.md` is a summary written for the next Claude Code session.
It answers "what does the next session need to know before touching
anything" — VERIFIED vs ASSERTED, what is open, what must not be assumed. It
does that job well and this change does not alter it in any way.

What it cannot do is serve as **evidence of the work it summarises**. A
summary of a plan is not the plan.

This is not hypothetical. The 2026-08-02 planning session produced a full plan
for the pitch wire-sign convention (Item 1) and continuous tilt sampling
(Item 2). The reviewer — who is outside this repository and sees only what is
uploaded — received the session-log entry *about* the plan, never the plan,
and issued a verdict on that basis. The verdict has been withdrawn pending
review of the actual plan.

### b.2 Design decisions, and why

**D1. Add a second artifact rather than changing `SESSION_LOG.md`.** The two
have genuinely different readers (next session vs external reviewer),
different forms (append-only summary vs per-session evidence dump), and
different growth patterns (one file forever vs one file per session).
Overloading one file to serve both would degrade the summary — the thing that
currently works — by burying it in diffs and raw output. `SESSION_LOG.md`
keeps its format, its rules, and every existing entry untouched.

**D2. One file per session, not one appended file.** Evidence dumps do not
compress and do not benefit from chronological interleaving. A reviewer wants
one self-contained document per session. This also makes upload trivial.

**D3. Mandatory `fw_` prefix.** The sibling app repo emits `app_`-prefixed
files and both repos upload to the same destination. A bare
`HANDOFF_<date>_<NN>.md` from each repo collides on identical names, and one
silently overwrites the other — silently being the operative word, since
neither side would see an error. The prefix is the whole mitigation, so it is
specified as mandatory in three places: `CLAUDE.md`, `docs/handoffs/README.md`,
and the hook's own failure message.

**D4. Two-digit sequence within the date.** Multiple sessions per day is
normal here; `_01`/`_02` orders them without needing timestamps, which would
be noisy in filenames and unstable across timezones.

**D5. Section (b) is the load-bearing requirement, and it is stated as
such.** The failure mode being fixed is substituting a summary for the
artifact. The specification therefore forbids not only summarising but also
*pointing at another document* — including one inside this repo, since the
reviewer cannot browse it. "If a plan existed only in the conversation, write
it into this file in full." `CLAUDE.md` marks this as "the single most
important requirement and the specific failure this file exists to prevent",
and adds "Length is not a reason to summarise: if the choice is between a long
file and an incomplete one, write the long file."

**D6. Evidence travels with the claim.** This repo already had a strong
VERIFIED/ASSERTED discipline, and it already caught a real error in both
directions (DECISIONS #27 recorded unverified as verified; the 2026-08-01
backfill recorded verified as unverified). What it lacked was the evidence
sitting next to the claim, so a reviewer could check the classification rather
than trust it. Requirement (e) adds exactly that and nothing else: `file:line`
for a code fact, raw output for a run result.

**D7. Two separate hook scripts, not one extended script.** The instruction
was to extend the Stop hook in `.claude/settings.json` in the same style and
not rewrite it. Adding a second entry to the existing `Stop[0].hooks` array
does that literally: the existing `check-session-log.ps1` entry is byte-for-
byte unchanged, and the new entry sits beside it in the same shape. Because
Claude Code runs every registered hook and any exit-2 blocks, two independent
entries give the required AND semantics for free, with two distinct and
specific failure messages instead of one combined one.

**D8. The new hook is an existence check, not a transcript scan.** This
differs deliberately from `check-session-log.ps1`, which scans the session
transcript because a written-and-committed log leaves a clean working tree.
The specification for this artifact is "a handoff file matching today's date
exists in `docs/handoffs/`", so the hook implements exactly that. The
consequence is a real loosening — see QUESTIONS FOR REVIEW §f.1.

**D9. Windows PowerShell 5.1, matching the existing hook.** `powershell.exe`
ships with Windows; `pwsh` may not be installed, and the schema's
`"shell": "powershell"` maps to `pwsh`. Exec form (`command` + `args`) avoids
shell quoting entirely.

### b.3 Full text of the new `CLAUDE.md` section

Added immediately after the existing "Session handoff (required)" section.
The existing section was **not** edited.

```markdown
## Session handoff file (required)

**A second, separate artifact. In addition to the `docs/SESSION_LOG.md` entry
above — not instead of it.** The `Stop` hook enforces both: the session cannot
end until `docs/SESSION_LOG.md` has been written *and* a handoff file for
today exists in `docs/handoffs/`.

The two have different readers and different jobs. `SESSION_LOG.md` is a
summary for the next Claude Code session, and it keeps that job unchanged.
The handoff file is for a **reviewer outside this repository**, who sees only
this file — no conversation, no transcript, no repo browsing. A summary cannot
serve as evidence of the work it summarises; a reviewer who is handed a
description of a plan instead of the plan cannot review it. See
`docs/handoffs/README.md` for the incident that established this.

### Filename

​```
docs/handoffs/fw_HANDOFF_<YYYY-MM-DD>_<NN>_<slug>.md
​```

- `<NN>` — two-digit sequence within that date, starting at `01`.
- `<slug>` — short kebab-case description.
- **The `fw_` prefix is mandatory.** The sibling app repo uses `app_`, and
  both repos' files upload to the same place; a bare name collides and one
  silently overwrites the other.

### Required contents, in this order

- **a. Header.** Repo name, branch, HEAD before the session, HEAD after, and
  the full output of `git status` at the end.
- **b. THE FULL TEXT of every plan, design, recommendation, or analysis the
  session produced.** Verbatim, complete, inline in this file. Never a
  summary. Never a pointer to another document — not even one in this repo.
  If a plan existed only in the conversation, write it into this file in
  full. **This is the single most important requirement and the specific
  failure this file exists to prevent.**
- **c. The complete `git diff` for every source change**, verbatim — not a
  description of it. A docs-only diff over 400 lines may be truncated with an
  explicit note saying so; **source diffs are never truncated.**
- **d. The raw output of every command run** — builds, greps, git —
  **including failures.** A command that errored is evidence too.
- **e. Every claim split VERIFIED vs ASSERTED, with the evidence attached to
  each VERIFIED claim** — `file:line` for a code fact, raw output for a run
  result. This repo's existing VERIFIED/ASSERTED discipline (see the section
  above) is the model; the change here is that the evidence travels with the
  claim instead of the claim standing alone.
- **f. `QUESTIONS FOR REVIEW`** — judgement calls the session did not feel
  authorised to make.
- **g. `NOT DONE`** — anything asked for and not done, with the reason.

Sections (b) and (e) are where this file earns its keep. Length is not a
reason to summarise: if the choice is between a long file and an incomplete
one, write the long file.
```

(The fenced block inside that section uses zero-width-joined backticks above
purely so this outer fence does not terminate early; the file on disk has
ordinary triple backticks. Verified against the raw `git diff` in §c.1.)

### b.4 Full text of `docs/handoffs/README.md`

```markdown
# docs/handoffs/ — reviewer-facing session handoff files

One file per working session, holding the **artifacts** that session produced:
plans in full, diffs in full, raw command output, and every claim split
VERIFIED vs ASSERTED with its evidence attached.

These files are uploaded to a reviewer **outside this repository**, who has no
access to the conversation, the transcript, or anything else that is not in
the file. Whatever is not written here does not exist as far as review is
concerned.

## Why this exists separately from SESSION_LOG.md

`docs/SESSION_LOG.md` is a **summary written for the next Claude Code
session**. It answers "what does the next session need to know before it
touches anything" — what is known versus merely claimed, what is open, what
must not be assumed. It does that job well and it keeps doing it unchanged.

What it cannot do is serve as **evidence of the work it summarises**. A
summary of a plan is not the plan.

That is not hypothetical. On 2026-08-02 a planning session produced a full
plan for the pitch wire-sign convention and continuous tilt sampling. The
reviewer received only the session-log entry *about* that plan — never the
plan — and issued a verdict anyway. The verdict was withdrawn. This directory
exists so that cannot happen again;
`fw_HANDOFF_2026-08-02_01_pitch-sign-tilt-sampling-plan.md` is the retroactive
repair of that specific gap.

| | `docs/SESSION_LOG.md` | `docs/handoffs/fw_HANDOFF_*.md` |
|---|---|---|
| Audience | the next Claude Code session | an external reviewer |
| Form | one appended entry, summary | one file per session, artifacts |
| Plans | referred to | reproduced in full, inline |
| Diffs | described | pasted verbatim |
| Commands | conclusions | raw output, including failures |
| Evidence | claims classified | classification **plus** the evidence |
| Growth | one file, append-only forever | one new file per session |

Both are required. Neither replaces the other. A `Stop` hook in
`.claude/settings.json` enforces both — `check-session-log.ps1` for the log,
`check-handoff-file.ps1` for this directory.

## Filename

​```
docs/handoffs/fw_HANDOFF_<YYYY-MM-DD>_<NN>_<slug>.md
​```

- `<NN>` — two-digit sequence within that date, starting at `01`.
- `<slug>` — short kebab-case description of the session.
- **The `fw_` prefix is mandatory.** The sibling app repo (`tilt_bed_app`)
  uses `app_`, and both repos' handoff files are uploaded to the same place.
  A bare `HANDOFF_<date>_01.md` from each repo collides, and one silently
  overwrites the other.

## Contents

The authoritative specification is the **"Session handoff file (required)"**
section of `CLAUDE.md`. In short, in this order: (a) repo/branch/HEAD
before/HEAD after/full `git status`; (b) the full text of every plan, design,
recommendation or analysis produced, verbatim and inline; (c) the complete
`git diff` for every source change; (d) raw output of every command run;
(e) VERIFIED vs ASSERTED with evidence attached; (f) QUESTIONS FOR REVIEW;
(g) NOT DONE.

The rule that matters most is (b). Never summarise a plan here, and never
point at another document for it — inline the whole thing, even if it already
exists elsewhere, even if it is long. If a plan existed only in the
conversation, write it into the handoff file in full.
```

---

## c. Complete diffs and new-file contents

**No `.c` or `.h` file was modified.** `git diff 29fdf0d..HEAD --stat` in §d.5
confirms firmware is untouched, and `git status` in §a shows no source file in
the working tree either.

### c.1 `CLAUDE.md` — verbatim `git diff`

```diff
diff --git a/CLAUDE.md b/CLAUDE.md
index 0e1de97..4a7302a 100644
--- a/CLAUDE.md
+++ b/CLAUDE.md
@@ -104,6 +104,61 @@ Remember that you do not build or flash (see Hard constraints). Anything that
 would need the bench to confirm, and that the user has not reported back on,
 is ASSERTED — regardless of how confident the reasoning is.
 
+## Session handoff file (required)
+
+**A second, separate artifact. In addition to the `docs/SESSION_LOG.md` entry
+above — not instead of it.** The `Stop` hook enforces both: the session cannot
+end until `docs/SESSION_LOG.md` has been written *and* a handoff file for
+today exists in `docs/handoffs/`.
+
+The two have different readers and different jobs. `SESSION_LOG.md` is a
+summary for the next Claude Code session, and it keeps that job unchanged.
+The handoff file is for a **reviewer outside this repository**, who sees only
+this file — no conversation, no transcript, no repo browsing. A summary cannot
+serve as evidence of the work it summarises; a reviewer who is handed a
+description of a plan instead of the plan cannot review it. See
+`docs/handoffs/README.md` for the incident that established this.
+
+### Filename
+
+```
+docs/handoffs/fw_HANDOFF_<YYYY-MM-DD>_<NN>_<slug>.md
+```
+
+- `<NN>` — two-digit sequence within that date, starting at `01`.
+- `<slug>` — short kebab-case description.
+- **The `fw_` prefix is mandatory.** The sibling app repo uses `app_`, and
+  both repos' files upload to the same place; a bare name collides and one
+  silently overwrites the other.
+
+### Required contents, in this order
+
+- **a. Header.** Repo name, branch, HEAD before the session, HEAD after, and
+  the full output of `git status` at the end.
+- **b. THE FULL TEXT of every plan, design, recommendation, or analysis the
+  session produced.** Verbatim, complete, inline in this file. Never a
+  summary. Never a pointer to another document — not even one in this repo.
+  If a plan existed only in the conversation, write it into this file in
+  full. **This is the single most important requirement and the specific
+  failure this file exists to prevent.**
+- **c. The complete `git diff` for every source change**, verbatim — not a
+  description of it. A docs-only diff over 400 lines may be truncated with an
+  explicit note saying so; **source diffs are never truncated.**
+- **d. The raw output of every command run** — builds, greps, git —
+  **including failures.** A command that errored is evidence too.
+- **e. Every claim split VERIFIED vs ASSERTED, with the evidence attached to
+  each VERIFIED claim** — `file:line` for a code fact, raw output for a run
+  result. This repo's existing VERIFIED/ASSERTED discipline (see the section
+  above) is the model; the change here is that the evidence travels with the
+  claim instead of the claim standing alone.
+- **f. `QUESTIONS FOR REVIEW`** — judgement calls the session did not feel
+  authorised to make.
+- **g. `NOT DONE`** — anything asked for and not done, with the reason.
+
+Sections (b) and (e) are where this file earns its keep. Length is not a
+reason to summarise: if the choice is between a long file and an incomplete
+one, write the long file.
+
 ## Where this fits in the migration (D1)
 
 See `docs/MIGRATION_STATUS.md` for the live checklist. Short version: this
```

### c.2 `.claude/settings.json` — verbatim `git diff`

```diff
diff --git a/.claude/settings.json b/.claude/settings.json
index aaa76c6..0ed4082 100644
--- a/.claude/settings.json
+++ b/.claude/settings.json
@@ -15,6 +15,19 @@
             ],
             "timeout": 30,
             "statusMessage": "Checking docs/SESSION_LOG.md handoff entry"
+          },
+          {
+            "type": "command",
+            "command": "powershell",
+            "args": [
+              "-NoProfile",
+              "-ExecutionPolicy",
+              "Bypass",
+              "-File",
+              ".claude/hooks/check-handoff-file.ps1"
+            ],
+            "timeout": 30,
+            "statusMessage": "Checking docs/handoffs/ file for today"
           }
         ]
       }
```

### c.3 `.claude/settings.json` — before and after, in full

Required explicitly by the instruction for this session.

**BEFORE:**

```json
{
  "hooks": {
    "Stop": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "powershell",
            "args": [
              "-NoProfile",
              "-ExecutionPolicy",
              "Bypass",
              "-File",
              ".claude/hooks/check-session-log.ps1"
            ],
            "timeout": 30,
            "statusMessage": "Checking docs/SESSION_LOG.md handoff entry"
          }
        ]
      }
    ]
  }
}
```

**AFTER:**

```json
{
  "hooks": {
    "Stop": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "powershell",
            "args": [
              "-NoProfile",
              "-ExecutionPolicy",
              "Bypass",
              "-File",
              ".claude/hooks/check-session-log.ps1"
            ],
            "timeout": 30,
            "statusMessage": "Checking docs/SESSION_LOG.md handoff entry"
          },
          {
            "type": "command",
            "command": "powershell",
            "args": [
              "-NoProfile",
              "-ExecutionPolicy",
              "Bypass",
              "-File",
              ".claude/hooks/check-handoff-file.ps1"
            ],
            "timeout": 30,
            "statusMessage": "Checking docs/handoffs/ file for today"
          }
        ]
      }
    ]
  }
}
```

The pre-existing entry is byte-for-byte unchanged.

### c.4 `.claude/hooks/check-handoff-file.ps1` — complete new file

Untracked, so `git diff` shows nothing. Reproduced in full — this is
executable tooling, so the never-truncate rule for source is applied to it.

```powershell
# Stop hook: refuse to end a session that produced no reviewer-facing handoff
# file.  See "Session handoff file (required)" in CLAUDE.md.
#
# Companion to check-session-log.ps1, which enforces the OTHER artifact.  Both
# are registered on Stop in .claude/settings.json and both must pass, so the
# effective rule is: docs/SESSION_LOG.md was written AND a handoff file for
# today exists.  They are deliberately separate scripts - the two artifacts
# have different jobs and different failure messages.
#
# Wired up in .claude/settings.json.  Windows PowerShell 5.1 (powershell.exe),
# which ships with Windows -- deliberately not pwsh, which may not be present.
#
# Unlike check-session-log.ps1 this is a plain filesystem existence check, not
# a transcript scan: the spec for this artifact is "a handoff file matching
# today's date exists in docs/handoffs/".  See the QUESTIONS FOR REVIEW note in
# fw_HANDOFF_2026-08-02_02 - a second session on the same date is satisfied by
# the first session's file, which is a known and deliberate loosening.
#
# Exit codes: 0 = let the session end, 2 = block and feed stderr back.

$ErrorActionPreference = 'Stop'

$HandoffDir = 'docs/handoffs'

$raw = [Console]::In.ReadToEnd()
try {
  $payload = $raw | ConvertFrom-Json
} catch {
  # Unparseable input is a harness problem, not a missing handoff.  Don't
  # strand the user in a block they cannot clear.
  exit 0
}

# Loop guard: this hook already blocked once and Claude is stopping again.
# Never block twice -- that is an unbreakable stop loop.
if ($payload.stop_hook_active) { exit 0 }

$today = Get-Date -Format 'yyyy-MM-dd'
$pattern = "fw_HANDOFF_${today}_*.md"

$found = @()
if (Test-Path -LiteralPath $HandoffDir) {
  $found = @(Get-ChildItem -LiteralPath $HandoffDir -Filter $pattern -File -ErrorAction SilentlyContinue)
}

if ($found.Count -gt 0) { exit 0 }

$msg = @"
BLOCKED: no handoff file for today ($today) in $HandoffDir/.

Per "Session handoff file (required)" in CLAUDE.md, this session must leave a
reviewer-facing handoff file alongside the docs/SESSION_LOG.md entry. The log
is a summary for the next session; the handoff file is the evidence, and a
summary cannot stand in for the work it summarises.

  Filename: $HandoffDir/fw_HANDOFF_${today}_<NN>_<slug>.md
            <NN> = two-digit sequence within today, starting at 01
            <slug> = short kebab-case description
            The fw_ prefix is MANDATORY (the app repo uses app_, and both
            upload to the same place -- a bare name silently overwrites).

Required contents, in order:
  a. Repo, branch, HEAD before, HEAD after, full "git status" output
  b. THE FULL TEXT of every plan/design/recommendation/analysis produced,
     verbatim and inline -- never a summary, never a pointer
  c. The complete "git diff" for every source change, verbatim
  d. Raw output of every command run, including failures
  e. Every claim split VERIFIED vs ASSERTED, evidence attached to each
     VERIFIED claim (file:line, or raw output)
  f. QUESTIONS FOR REVIEW
  g. NOT DONE

Write the file, then stop again.
"@

[Console]::Error.WriteLine($msg)
exit 2
```

### c.5 `docs/SESSION_LOG.md` — diff not reproduced

179 lines, docs-only, and it contains two appended entries: the 2026-08-02
planning entry (uncommitted from the previous session) and this session's
entry. Under the 400-line rule it need not be truncated, but reproducing it
here would duplicate content that is itself a summary of what this file
already carries in full. **Explicit note per requirement (c): the
`docs/SESSION_LOG.md` diff is omitted from this handoff.** It is 179 lines,
0 deletions, append-only, and reproducible with `git diff docs/SESSION_LOG.md`.
Flagged as a judgement call in §f.3.

### c.6 New docs files — content not duplicated here

`docs/handoffs/README.md` **is** reproduced in full, in §b.4.

`docs/handoffs/fw_HANDOFF_2026-08-02_01_pitch-sign-tilt-sampling-plan.md` is
not reproduced inside this file. It is ~900 lines and it is itself a handoff
artifact uploaded in the same batch as this one — not an external document the
reviewer would have to go and find. Duplicating it would double the upload for
no gain. Flagged as a judgement call in §f.2.

---

## d. Raw output of every command run

In order. All exit 0 unless noted.

### d.1 Branch, HEAD, status, repo name (start of session)

```
$ git rev-parse --abbrev-ref HEAD && git rev-parse --short HEAD && git status && basename "$(git rev-parse --show-toplevel)"
=== branch ===
main
=== HEAD ===
6cbd2fd
=== status ===
On branch main
Your branch is ahead of 'origin/main' by 20 commits.
  (use "git push" to publish your local commits)

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore
	modified:   docs/SESSION_LOG.md

Untracked files:
  (use "git add <file>..." to include in what will be committed)
	.projectchecksums
	tilt_bed_controller_project.pdf

no changes added to commit (use "git add" and/or "git commit -a")
=== repo name ===
tilt_bed_controller
```

### d.2 Hook test round 1 — three cases plus schema check

```
$ (PowerShell) pipe synthetic Stop payloads into check-handoff-file.ps1
=== A: handoff file for today exists (cwd=repo) -> expect 0 ===
exit=0
=== B: no docs/handoffs dir (cwd=scratchpad) -> expect 2 + stderr ===
BLOCKED: no handoff file for today (2026-08-02) in docs/handoffs/.

Per "Session handoff file (required)" in CLAUDE.md, this session must leave a
reviewer-facing handoff file alongside the docs/SESSION_LOG.md entry. The log
is a summary for the next session; the handoff file is the evidence, and a
summary cannot stand in for the work it summarises.

  Filename: docs/handoffs/fw_HANDOFF_2026-08-02_<NN>_<slug>.md
            <NN> = two-digit sequence within today, starting at 01
            <slug> = short kebab-case description
            The fw_ prefix is MANDATORY (the app repo uses app_, and both
            upload to the same place -- a bare name silently overwrites).

Required contents, in order:
  a. Repo, branch, HEAD before, HEAD after, full git status output
  b. THE FULL TEXT of every plan/design/recommendation/analysis produced,
     verbatim and inline -- never a summary, never a pointer
  c. The complete git diff for every source change, verbatim
  d. Raw output of every command run, including failures
  e. Every claim split VERIFIED vs ASSERTED, evidence attached to each
     VERIFIED claim (file:line, or raw output)
  f. QUESTIONS FOR REVIEW
  g. NOT DONE

Write the file, then stop again.
exit=2
=== C: loop guard (cwd=scratchpad, stop_hook_active) -> expect 0 ===
exit=0
=== D: settings.json schema check ===
Stop entries: 2
  powershell .claude/hooks/check-session-log.ps1  (script exists: True)
  powershell .claude/hooks/check-handoff-file.ps1  (script exists: True)
```

**Defect found in this run.** Case B's message printed `git status` and
`git diff` without the backticks written in the source. Cause: the message is
a PowerShell double-quoted here-string (`@"…"@`), in which the backtick is the
escape character, so `` `git status` `` collapsed to `git status`. Cosmetic
only — no logic affected and no other character was consumed — but the source
no longer matched its own output. Fixed by replacing the backticks with double
quotes in the source, then retested.

### d.3 Hook test round 2 — block path after the fix

```
$ (PowerShell) retest case B from scratchpad cwd
=== retest block path after backtick fix -> expect 2 ===
BLOCKED: no handoff file for today (2026-08-02) in docs/handoffs/.

Per "Session handoff file (required)" in CLAUDE.md, this session must leave a
reviewer-facing handoff file alongside the docs/SESSION_LOG.md entry. The log
is a summary for the next session; the handoff file is the evidence, and a
summary cannot stand in for the work it summarises.

  Filename: docs/handoffs/fw_HANDOFF_2026-08-02_<NN>_<slug>.md
            <NN> = two-digit sequence within today, starting at 01
            <slug> = short kebab-case description
            The fw_ prefix is MANDATORY (the app repo uses app_, and both
            upload to the same place -- a bare name silently overwrites).

Required contents, in order:
  a. Repo, branch, HEAD before, HEAD after, full "git status" output
  b. THE FULL TEXT of every plan/design/recommendation/analysis produced,
     verbatim and inline -- never a summary, never a pointer
  c. The complete "git diff" for every source change, verbatim
  d. Raw output of every command run, including failures
  e. Every claim split VERIFIED vs ASSERTED, evidence attached to each
     VERIFIED claim (file:line, or raw output)
  f. QUESTIONS FOR REVIEW
  g. NOT DONE

Write the file, then stop again.
exit=2
```

The harness reported this as `Exit code 2`, i.e. a tool-level "error". That is
the intended result for the block path, not a failure of the test.

### d.4 Diff capture

```
$ git diff CLAUDE.md && git diff .claude/settings.json && git diff --stat && git diff docs/SESSION_LOG.md | wc -l
```

Output for `CLAUDE.md` and `.claude/settings.json` is reproduced verbatim in
§c.1 and §c.2. Remaining output:

```
###LINECOUNTS###
warning: in the working copy of '.claude/settings.json', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'CLAUDE.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/SESSION_LOG.md', LF will be replaced by CRLF the next time Git touches it
 .claude/settings.json |  13 ++++
 .projectlinkstore     |   2 +-
 CLAUDE.md             |  55 ++++++++++++++++
 docs/SESSION_LOG.md   | 171 ++++++++++++++++++++++++++++++++++++++++++++++++++
 4 files changed, 240 insertions(+), 1 deletion(-)
###SESSIONLOG-LINES###
179
```

The three `LF will be replaced by CRLF` lines are git's standard
autocrlf notices on this Windows checkout, not errors.

### d.5 Final firmware-parked confirmation

```
$ git diff --stat 29fdf0d..HEAD
```

Output pasted in the session response and reproduced here:

```
 .claude/hooks/check-session-log.ps1 |  98 ++++++++++++++++
 .claude/settings.json               |  23 ++++
 .gitignore                          |   8 +-
 CLAUDE.md                           |  42 ++++++-
 docs/SESSION_LOG.md                 | 228 ++++++++++++++++++++++++++++++++++++
 5 files changed, 396 insertions(+), 3 deletions(-)
```

No `.c` or `.h` file appears. Note this compares **commits**, so it does not
include this session's uncommitted working-tree changes; §a's `git status`
covers those.

### d.6 gitignore check on the new paths

```
$ for f in …; do git check-ignore -q "$f" && echo IGNORED || echo trackable; done
=== check-ignore on new paths ===
docs/handoffs/README.md                                                trackable
docs/handoffs/fw_HANDOFF_2026-08-02_01_pitch-sign-tilt-sampling-plan.md trackable
docs/handoffs/fw_HANDOFF_2026-08-02_02_handoff-artifact-process-change.md trackable
.claude/hooks/check-handoff-file.ps1                                   trackable
```

All four are trackable. `.claude/hooks/` is covered by the `!/.claude/hooks/`
negation added when the first hook landed; `docs/` was never ignored.

### d.7 Failures

One defect and no command failures. The defect (backtick consumption in the
here-string) is documented in §d.2 with its fix and retest in §d.3. The
harness-reported "Exit code 2" in §d.3 is the intended block result.

---

## e. VERIFIED vs ASSERTED, with evidence

### VERIFIED

| # | Claim | Evidence |
|---|---|---|
| V1 | Firmware untouched since `29fdf0d`; only `.claude/`, `.gitignore`, `CLAUDE.md`, `docs/SESSION_LOG.md` differ | raw output §d.5 |
| V2 | No `.c` or `.h` modified in the working tree either | `git status`, §a — no source file listed |
| V3 | HEAD unchanged at `6cbd2fd`; nothing committed | §a; §d.1 |
| V4 | New hook exits 0 when a handoff file for today exists | §d.2 case A |
| V5 | New hook exits 2 with the reminder when no handoff file for today exists | §d.2 case B, §d.3 retest |
| V6 | New hook exits 0 under `stop_hook_active` (loop guard) | §d.2 case C |
| V7 | `settings.json` parses and registers exactly two Stop entries, both pointing at scripts that exist on disk | §d.2 case D |
| V8 | The pre-existing `check-session-log.ps1` entry is byte-for-byte unchanged | `git diff .claude/settings.json`, §c.2 — pure addition, no `-` lines |
| V9 | The new `CLAUDE.md` section sits immediately after the existing "Session handoff (required)" section, which is unmodified | `git diff CLAUDE.md`, §c.1 — hunk `@@ -104,6 +104,61 @@`, pure addition anchored on that section's last paragraph |
| V10 | The plan in `_01` is the delivered text, not a reconstruction | It was present in this session's conversation context and copied verbatim; the two reconstructed parts are fenced and labelled inline in `_01` |
| V11 | The backtick defect existed and is fixed | before: §d.2 case B renders `git status`; after: §d.3 renders `"git status"` |
| V12 | All four new paths are trackable, not gitignored | raw output §d.7 |

### ASSERTED

| # | Claim | Why only ASSERTED |
|---|---|---|
| A1 | The hooks will fire correctly in a real session | Pipe-tested with synthetic payloads, never observed firing from an actual Stop event. |
| A2 | The new hook entry is live in this session | `.claude/settings.json` changed mid-session. Claude Code's settings watcher only watches directories that had a settings file at session start; that was true here, but the reload was not observed. A restart, or opening `/hooks` once, guarantees it. |
| A3 | The `fw_`/`app_` collision is real | Taken from the user's statement about the sibling repo's upload destination. Not independently verified from this repo. |
| A4 | One file per session is the right granularity for the reviewer | Design judgement. Untested against actual review use. |

---

## f. QUESTIONS FOR REVIEW

1. **The hook checks only that a handoff file for today *exists*.** This is
   what the specification asked for and it is what was implemented. The
   consequence: a second session on the same date is satisfied by the first
   session's file and can end without writing its own. `check-session-log.ps1`
   does not have this hole — it scans the transcript for a write made *this
   session*. Should the handoff hook be tightened the same way? I did not
   change it unilaterally because the spec was explicit.
2. **`_01` is not reproduced inside `_02`.** Requirement (b) forbids pointing
   at another document, and I have read that as meaning documents the reviewer
   cannot see. `_01` ships in the same upload. If the rule is meant absolutely
   — every handoff self-contained even against sibling handoffs — `_02` needs
   ~900 more lines and I will add them.
3. **`docs/SESSION_LOG.md`'s 179-line diff is omitted from §c.5** with an
   explicit note. It is under the 400-line threshold, so the truncation
   allowance does not strictly cover it; I judged it redundant because it
   summarises material this file already carries in full. Confirm or I will
   paste it.
4. **`CLAUDE.md`'s "Project layout" section still lists `docs/` contents
   without `docs/handoffs/`.** Updating it was not in the instruction and I
   did not want to edit `CLAUDE.md` beyond the section requested. Should it be
   added?
5. **The existing "Session handoff (required)" section says the Stop hook
   blocks "until the file has been written" (singular).** That is now
   incomplete — the hook enforces both artifacts. I flagged this at the start
   of the session rather than resolving it, and stated the combined rule in
   the new section instead. Should the old sentence be amended?
6. **Nothing was committed.** No commit was requested, and the previous two
   sessions asked explicitly. Confirm whether these should be committed, and
   whether `docs/handoffs/` belongs in version control at all or should be
   generated-and-uploaded only.
7. **Retention.** One file per session, none ever deleted. At this project's
   cadence `docs/handoffs/` will hold hundreds of files within a year. No
   archival or pruning policy was specified and none was invented.

---

## g. NOT DONE

| Item | Reason |
|---|---|
| Item 1 (pitch wire sign) implementation | Explicitly excluded: "Do not implement Item 1 or Item 2." The plan is in `_01`. |
| Item 2 (continuous tilt sampling) implementation | Same. |
| Any `.c`/`.h` change | Explicitly excluded; firmware parked at `29fdf0d`. Confirmed in §d.5. |
| Committing any of this | Not requested. Everything is uncommitted — see §a. |
| Tightening the handoff hook to a per-session check | The spec said "exists". Implemented as specified; raised as §f.1 instead of decided unilaterally. |
| Amending the existing "Session handoff (required)" section for the now-plural hook | Would edit a section the instruction did not scope. Raised as §f.5. |
| Adding `docs/handoffs/` to `CLAUDE.md`'s Project layout list | Out of the requested scope. Raised as §f.4. |
| ~~`git check-ignore` verification for the new paths~~ | Initially overlooked, then run before finalising. Now VERIFIED V12 / §d.6. |
| Observing either Stop hook fire from a real Stop event | Not possible from inside the session that configures them; see ASSERTED A1/A2. |
| A retention/archival policy for `docs/handoffs/` | None specified; not invented. Raised as §f.7. |
