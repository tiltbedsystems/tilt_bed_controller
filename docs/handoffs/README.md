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
`check-handoff-file.ps1` for this directory. The handoff check requires that
**this** session wrote a file (a file left by an earlier session on the same
date does not count) and **fails closed**: if it cannot confirm, it blocks.

## Filename

```
docs/handoffs/fw_HANDOFF_<YYYY-MM-DD>_<NN>_<slug>.md
```

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

"Self-contained" means against documents the reviewer cannot see. Sibling
handoff files uploaded in the same batch are visible, so one handoff need not
reproduce another. Likewise `docs/SESSION_LOG.md`, which ships alongside — its
diff never needs reproducing here.

Secrets in raw command output are replaced inline with
`[REDACTED: what it was]`, never silently dropped.

## Committing

A handoff describes the commits before it and is committed **separately and
last**, so a work commit's diff never carries the document describing it and
the handoff can quote real SHAs. There is no retention policy; nothing is
pruned.
