# Stop hook: refuse to end a session that produced no reviewer-facing handoff
# file.  See "Session handoff file (required)" in CLAUDE.md.
#
# Companion to check-session-log.ps1, which enforces the OTHER artifact.  Both
# are registered on Stop in .claude/settings.json and both must pass, so the
# effective rule is: docs/SESSION_LOG.md was written AND this session wrote a
# handoff file for today.  They are deliberately separate scripts - the two
# artifacts have different jobs and different failure messages.
#
# Wired up in .claude/settings.json.  Windows PowerShell 5.1 (powershell.exe),
# which ships with Windows -- deliberately not pwsh, which may not be present.
#
# TWO CHECKS, BOTH REQUIRED:
#   1. This session actually WROTE a handoff file for today (transcript scan).
#   2. That file exists on disk now.
#
# Check 1 is the point.  An earlier version tested existence only, which meant
# a second session on the same date was satisfied by the first session's file
# and could end having written nothing - a version of the exact failure this
# artifact exists to prevent.  Tightened on review.
#
# FAILS CLOSED, deliberately, and unlike check-session-log.ps1.  If the input
# is unparseable or the transcript cannot be read, this hook BLOCKS rather than
# waving the session through.  The other hook fails open because a false block
# there is merely annoying; here a false pass loses the evidence a reviewer
# needs, which is the failure mode this whole mechanism was built for.  The
# stop_hook_active guard below is what keeps fail-closed from becoming an
# unbreakable loop.
#
# Exit codes: 0 = let the session end, 2 = block and feed stderr back.

$ErrorActionPreference = 'Stop'

$HandoffDir = 'docs/handoffs'

# Tools that can write the file.  A Read of an existing handoff must NOT count.
$WriteTools = @('Write', 'Edit', 'MultiEdit', 'NotebookEdit')
$ShellTools = @('Bash', 'PowerShell')

# A shell command only counts if it actually writes.  Without this,
# `git add docs/handoffs/...` would satisfy the check with nothing written.
$ShellWritePattern = '>>|>|Add-Content|Out-File|Set-Content|\btee\b'

$today = Get-Date -Format 'yyyy-MM-dd'
$namePattern = "fw_HANDOFF_$([regex]::Escape($today))_\d{2}_[^\\/]*\.md$"

function Deny([string]$why) {
  $msg = @"
BLOCKED: $why

Per "Session handoff file (required)" in CLAUDE.md, this session must leave a
reviewer-facing handoff file alongside the docs/SESSION_LOG.md entry. The log
is a summary for the next session; the handoff file is the evidence, and a
summary cannot stand in for the work it summarises.

  Filename: $HandoffDir/fw_HANDOFF_${today}_<NN>_<slug>.md
            <NN> = two-digit sequence within today, starting at 01
            <slug> = short kebab-case description
            The fw_ prefix is MANDATORY (the app repo uses app_, and both
            upload to the same place -- a bare name silently overwrites).

  A file left by an EARLIER session today does not satisfy this. This session
  must write its own.

Required contents, in order:
  a. Repo, branch, HEAD before, HEAD after, full "git status" output
  b. Every plan/design/recommendation/analysis produced, written out as a
     complete design document -- never a one-line description or a pointer
  c. The complete "git diff" for every source change, as command output
  d. Raw output of every command run, including failures
     (secrets replaced inline with [REDACTED: what it was], never omitted)
  e. Every claim split VERIFIED vs ASSERTED, evidence attached to each
     VERIFIED claim (file:line, or raw output)
  f. QUESTIONS FOR REVIEW
  g. NOT DONE

Write the file, then stop again.
"@
  [Console]::Error.WriteLine($msg)
  exit 2
}

$raw = [Console]::In.ReadToEnd()
try {
  $payload = $raw | ConvertFrom-Json
} catch {
  Deny "the Stop hook payload could not be parsed, so this session's handoff file could not be confirmed (failing closed)."
}

# Loop guard: this hook already blocked once and Claude is stopping again.
# Never block twice -- that is an unbreakable stop loop. This is the one and
# only path that exits 0 without evidence.
if ($payload.stop_hook_active) { exit 0 }

$transcript = $payload.transcript_path
if ([string]::IsNullOrWhiteSpace($transcript) -or -not (Test-Path -LiteralPath $transcript)) {
  Deny "the session transcript could not be read, so this session's handoff file could not be confirmed (failing closed)."
}

# --- Check 1: did THIS session write a handoff file for today? ---
$written = $false

foreach ($line in [System.IO.File]::ReadLines($transcript)) {
  # Cheap pre-filter: only a handful of lines mention a handoff file at all.
  if ($line -notmatch 'fw_HANDOFF_') { continue }

  try { $entry = $line | ConvertFrom-Json } catch { continue }

  foreach ($block in @($entry.message.content)) {
    if ($block.type -ne 'tool_use') { continue }

    if ($WriteTools -contains $block.name) {
      # Match the target path, not the line: a handoff file that merely
      # mentions another handoff filename must not count as writing it.
      $target = "$($block.input.file_path)$($block.input.notebook_path)"
      if ($target -match $namePattern) { $written = $true; break }
    }
    elseif ($ShellTools -contains $block.name) {
      $cmd = "$($block.input.command)"
      if ($cmd -match "fw_HANDOFF_$([regex]::Escape($today))_\d{2}_" -and $cmd -match $ShellWritePattern) {
        $written = $true; break
      }
    }
  }

  if ($written) { break }
}

if (-not $written) {
  Deny "this session did not write a handoff file for today ($today) in $HandoffDir/."
}

# --- Check 2: is it actually on disk? ---
$found = @()
if (Test-Path -LiteralPath $HandoffDir) {
  $found = @(Get-ChildItem -LiteralPath $HandoffDir -File -ErrorAction SilentlyContinue |
             Where-Object { $_.Name -match $namePattern })
}

if ($found.Count -eq 0) {
  Deny "a handoff file for today was written during this session but is not present in $HandoffDir/ now (deleted, renamed, or written elsewhere)."
}

exit 0
