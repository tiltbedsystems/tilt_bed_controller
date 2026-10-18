# fw_HANDOFF_2026-10-17_03_single-switch-sampler-plan

PLAN-ONLY SESSION. No .c or .h file was modified. The design for review is in
section (b).

## (a) Header

- Repo: `tilt_bed_controller`
- Branch: `main`
- HEAD before session: `1046a14` (.gitignore: ignore Studio-generated .projectchecksums and project PDF) — local only, origin/main = `ae6f13a`
- HEAD after session: (filled in at end)
- End-of-session `git status`: (filled in at end)

### Part A confirmation (raw)

```
$ date && git log --oneline -3 && git status && git rev-parse HEAD origin/main && git diff --stat
Sat Oct 17 21:02:51 PDT 2026
1046a14 .gitignore: ignore Studio-generated .projectchecksums and project PDF
ae6f13a Handoff for 3f0d409: fail-safe hardware read, double press, swallowed events
3f0d409 Fail-safe reads motor enables from hardware; double press = two switches down; recover swallowed switch events
On branch main
Your branch is ahead of 'origin/main' by 1 commit.
  (use "git push" to publish your local commits)

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore

no changes added to commit (use "git add" and/or "git commit -a")
1046a14d0a74421a850a33ac053922955b34c45d
ae6f13a1b28ad9feba898a5682b47bac8c3f5c40
 .projectlinkstore | 2 +-
 1 file changed, 1 insertion(+), 1 deletion(-)
```

```
$ git diff 3f0d409 --stat
 .gitignore                                         |   7 +
 .projectlinkstore                                  |   2 +-
 docs/SESSION_LOG.md                                |  31 ++
 ...7_02_failsafe-hardware-read-and-double-press.md | 380 +++++++++++++++++++++
 4 files changed, 419 insertions(+), 1 deletion(-)
$ git diff 3f0d409 -- '*.c' '*.h'
(no output)
$ git show 3f0d409:switch_input.c | grep -n "define BED_BENCH_PROVOKE_RUNAWAY"
91:#define BED_BENCH_PROVOKE_RUNAWAY 0
```

`.projectlinkstore` diff is a Simplicity Studio recent-items hit-counter
reordering (JSON, one line) — Studio housekeeping, not touched by this session,
not committed by it.

Note on the RESTART NOTE: it says the user set `BED_BENCH_PROVOKE_RUNAWAY` back
to 0 after 3f0d409. VERIFIED it is 0 both at 3f0d409 and in the working tree
(`switch_input.c:91`); there is no uncommitted .c/.h change. So it was already
0 in the committed tree — no difference in outcome.

### This session (restart, started 22:00 PDT) — confirmation and first action (raw)

This handoff was begun by an earlier, cut-off session (raw output above, 21:02).
This session continued it. `date` still reads 2026-10-17, so per the RESTART
NOTE the 03 file is continued rather than a new one created.

```
$ date && git log --oneline -3 && git status && git log --oneline origin/main -1
Sat Oct 17 22:00:33 PDT 2026
1046a14 .gitignore: ignore Studio-generated .projectchecksums and project PDF
ae6f13a Handoff for 3f0d409: fail-safe hardware read, double press, swallowed events
3f0d409 Fail-safe reads motor enables from hardware; double press = two switches down; recover swallowed switch events
On branch main
Your branch is ahead of 'origin/main' by 1 commit.
  (use "git push" to publish your local commits)

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .claude/hooks/check-handoff-file.ps1
	modified:   .projectlinkstore
	modified:   CLAUDE.md
	modified:   docs/handoffs/README.md

Untracked files:
  (use "git add <file>..." to include in what will be committed)
	docs/handoffs/fw_HANDOFF_2026-10-17_03_single-switch-sampler-plan.md

no changes added to commit (use "git add" and/or "git commit -a")
ae6f13a Handoff for 3f0d409: fail-safe hardware read, double press, swallowed events
```

```
$ git diff --stat 3f0d409 -- '*.c' '*.h'
(no output — no .c/.h change, committed or working, since 3f0d409)
```

FIRST ACTION — the user's three reworded handoff-rule files, committed alone,
unedited by this session. `git diff` before the commit (raw; CRLF warnings
included):

```
$ git diff -- CLAUDE.md docs/handoffs/README.md .claude/hooks/check-handoff-file.ps1
(identical content to `git show 58e2f23`, reproduced here)
diff --git a/.claude/hooks/check-handoff-file.ps1 b/.claude/hooks/check-handoff-file.ps1
index d9b7ec8..59ffd3e 100644
--- a/.claude/hooks/check-handoff-file.ps1
+++ b/.claude/hooks/check-handoff-file.ps1
@@ -64,9 +64,9 @@ summary cannot stand in for the work it summarises.
 
 Required contents, in order:
   a. Repo, branch, HEAD before, HEAD after, full "git status" output
-  b. THE FULL TEXT of every plan/design/recommendation/analysis produced,
-     verbatim and inline -- never a summary, never a pointer
-  c. The complete "git diff" for every source change, verbatim
+  b. Every plan/design/recommendation/analysis produced, written out as a
+     complete design document -- never a one-line description or a pointer
+  c. The complete "git diff" for every source change, as command output
   d. Raw output of every command run, including failures
      (secrets replaced inline with [REDACTED: what it was], never omitted)
   e. Every claim split VERIFIED vs ASSERTED, evidence attached to each
diff --git a/CLAUDE.md b/CLAUDE.md
index 2b7983d..51e82fc 100644
--- a/CLAUDE.md
+++ b/CLAUDE.md
@@ -119,10 +119,10 @@ session on the same date does not satisfy it, and unlike the log hook this one
 The two have different readers and different jobs. `SESSION_LOG.md` is a
 summary for the next Claude Code session, and it keeps that job unchanged.
 The handoff file is for a **reviewer outside this repository**, who sees only
-this file — no conversation, no transcript, no repo browsing. A summary cannot
-serve as evidence of the work it summarises; a reviewer who is handed a
-description of a plan instead of the plan cannot review it. See
-`docs/handoffs/README.md` for the incident that established this.
+this file and the files uploaded with it, and cannot browse the repo. A
+one-line description of a plan cannot be reviewed; the plan itself has to be
+written out in the file. See `docs/handoffs/README.md` for the incident that
+established this.
 
 ### Filename
 
@@ -148,14 +148,16 @@ docs/handoffs/fw_HANDOFF_<YYYY-MM-DD>_<NN>_<slug>.md
 
 - **a. Header.** Repo name, branch, HEAD before the session, HEAD after, and
   the full output of `git status` at the end.
-- **b. THE FULL TEXT of every plan, design, recommendation, or analysis the
-  session produced.** Verbatim, complete, inline in this file. Never a
-  summary. Never a pointer to another document — not even one in this repo.
-  If a plan existed only in the conversation, write it into this file in
-  full. **This is the single most important requirement and the specific
-  failure this file exists to prevent.**
-- **c. The complete `git diff` for every source change**, verbatim — not a
-  description of it. A docs-only diff over 400 lines may be truncated with an
+- **b. Every plan, design, recommendation, or analysis the session produced,
+  written out in this file as a complete design document.** Include what a
+  reviewer needs to approve or reject it without asking: the design itself,
+  pseudo-code where it helps, the files and functions affected, trade-offs,
+  and the evidence. Do not replace it with a one-line description, and do not
+  point to another document instead — not even one in this repo. **This is
+  the single most important requirement and the specific failure this file
+  exists to prevent.**
+- **c. The complete `git diff` for every source change**, pasted as command
+  output — not a description of it. A docs-only diff over 400 lines may be truncated with an
   explicit note saying so; **source diffs are never truncated.** One standing
   exception: the `docs/SESSION_LOG.md` diff never needs reproducing, because
   the log ships alongside the handoff in the same upload — note its omission
@@ -166,7 +168,7 @@ docs/handoffs/fw_HANDOFF_<YYYY-MM-DD>_<NN>_<slug>.md
   `[REDACTED: what it was]` — never silently drop the line or the command.
   A reader must be able to see that something was removed and what kind of
   thing it was. This carve-out covers command output only; it does not
-  license summarising under (b), which governs plans.
+  permit shortening a plan under (b).
 - **e. Every claim split VERIFIED vs ASSERTED, with the evidence attached to
   each VERIFIED claim** — `file:line` for a code fact, raw output for a run
   result. This repo's existing VERIFIED/ASSERTED discipline (see the section
@@ -177,8 +179,8 @@ docs/handoffs/fw_HANDOFF_<YYYY-MM-DD>_<NN>_<slug>.md
 - **g. `NOT DONE`** — anything asked for and not done, with the reason.
 
 Sections (b) and (e) are where this file earns its keep. Length is not a
-reason to summarise: if the choice is between a long file and an incomplete
-one, write the long file.
+reason to cut a plan short: if the choice is between a long file and an
+incomplete one, write the long file.
 
 ### Committing
 
diff --git a/docs/handoffs/README.md b/docs/handoffs/README.md
index d54b268..7f42e9a 100644
--- a/docs/handoffs/README.md
+++ b/docs/handoffs/README.md
@@ -1,13 +1,12 @@
 # docs/handoffs/ — reviewer-facing session handoff files
 
 One file per working session, holding the **artifacts** that session produced:
-plans in full, diffs in full, raw command output, and every claim split
-VERIFIED vs ASSERTED with its evidence attached.
+complete plans and design documents, complete diffs, raw command output, and
+every claim split VERIFIED vs ASSERTED with its evidence attached.
 
-These files are uploaded to a reviewer **outside this repository**, who has no
-access to the conversation, the transcript, or anything else that is not in
-the file. Whatever is not written here does not exist as far as review is
-concerned.
+These files are uploaded to a reviewer **outside this repository**, who sees
+only the uploaded files and cannot browse the repo. Whatever is not written
+here does not exist as far as review is concerned.
 
 ## Why this exists separately from SESSION_LOG.md
 
@@ -31,8 +30,8 @@ repair of that specific gap.
 |---|---|---|
 | Audience | the next Claude Code session | an external reviewer |
 | Form | one appended entry, summary | one file per session, artifacts |
-| Plans | referred to | reproduced in full, inline |
-| Diffs | described | pasted verbatim |
+| Plans | referred to | written out as a complete design document |
+| Diffs | described | pasted as command output |
 | Commands | conclusions | raw output, including failures |
 | Evidence | claims classified | classification **plus** the evidence |
 | Growth | one file, append-only forever | one new file per session |
@@ -60,16 +59,16 @@ docs/handoffs/fw_HANDOFF_<YYYY-MM-DD>_<NN>_<slug>.md
 
 The authoritative specification is the **"Session handoff file (required)"**
 section of `CLAUDE.md`. In short, in this order: (a) repo/branch/HEAD
-before/HEAD after/full `git status`; (b) the full text of every plan, design,
-recommendation or analysis produced, verbatim and inline; (c) the complete
+before/HEAD after/full `git status`; (b) every plan, design, recommendation
+or analysis produced, written out as a complete design document; (c) the complete
 `git diff` for every source change; (d) raw output of every command run;
 (e) VERIFIED vs ASSERTED with evidence attached; (f) QUESTIONS FOR REVIEW;
 (g) NOT DONE.
 
-The rule that matters most is (b). Never summarise a plan here, and never
-point at another document for it — inline the whole thing, even if it already
-exists elsewhere, even if it is long. If a plan existed only in the
-conversation, write it into the handoff file in full.
+The rule that matters most is (b). Never reduce a plan to a one-line
+description here, and never point at another document for it — write the
+whole design into this file, even if it already exists elsewhere, even if it
+is long.
 
 "Self-contained" means against documents the reviewer cannot see. Sibling
 handoff files uploaded in the same batch are visible, so one handoff need not
```

```
$ git add CLAUDE.md docs/handoffs/README.md .claude/hooks/check-handoff-file.ps1 && git commit -q -m "Handoff rules: reword plan/diff requirements (no change in what is required)..." && git log --oneline -2 && git status --short
warning: in the working copy of '.claude/hooks/check-handoff-file.ps1', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'CLAUDE.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/handoffs/README.md', LF will be replaced by CRLF the next time Git touches it
58e2f23 Handoff rules: reword plan/diff requirements (no change in what is required)
1046a14 .gitignore: ignore Studio-generated .projectchecksums and project PDF
 M .projectlinkstore
?? docs/handoffs/fw_HANDOFF_2026-10-17_03_single-switch-sampler-plan.md
```

(Sections b–g follow and are written as they are completed.)

Session interrupted before section (b). The plan step was dropped; the reviewer wrote the design, implemented in the next session's handoff.
