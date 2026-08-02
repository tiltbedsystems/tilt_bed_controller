# Stop hook: refuse to end a session that never wrote a handoff entry to
# docs/SESSION_LOG.md.  See "Session handoff (required)" in CLAUDE.md.
#
# Wired up in .claude/settings.json.  Windows PowerShell 5.1 (powershell.exe),
# which ships with Windows -- deliberately not pwsh, which may not be present.
#
# Why the transcript and not git: a handoff entry that was written AND
# committed leaves a clean working tree, so `git status` cannot tell "appended
# and committed" apart from "never touched".  The Stop hook is handed a
# transcript_path on stdin, so ask the transcript whether a write actually
# happened this session.
#
# Exit codes: 0 = let the session end, 2 = block and feed stderr back.

$ErrorActionPreference = 'Stop'

$LogName = 'SESSION_LOG.md'

# Tools that can write the file.  Read/Grep on SESSION_LOG.md must NOT count --
# reading the log before appending is the normal flow.
$WriteTools = @('Write', 'Edit', 'MultiEdit', 'NotebookEdit')
$ShellTools = @('Bash', 'PowerShell')

# A shell command only counts if it actually appends/writes.  Without this,
# `git add docs/SESSION_LOG.md` or `git log docs/SESSION_LOG.md` would satisfy
# the check without a single line being written.
$ShellWritePattern = '>>|Add-Content|Out-File|Set-Content|\btee\b'

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

$transcript = $payload.transcript_path
if ([string]::IsNullOrWhiteSpace($transcript) -or -not (Test-Path -LiteralPath $transcript)) {
  # No transcript to inspect -> no evidence either way.  Fail open.
  exit 0
}

$logWritten = $false

foreach ($line in [System.IO.File]::ReadLines($transcript)) {
  # Cheap pre-filter: only a handful of lines mention the log at all.
  if ($line -notmatch [regex]::Escape($LogName)) { continue }

  try { $entry = $line | ConvertFrom-Json } catch { continue }

  foreach ($block in @($entry.message.content)) {
    if ($block.type -ne 'tool_use') { continue }

    if ($WriteTools -contains $block.name) {
      # Match on the target path, not the line: an Edit to CLAUDE.md that
      # merely mentions SESSION_LOG.md must not count as writing the log.
      $target = "$($block.input.file_path)$($block.input.notebook_path)"
      if ($target -match [regex]::Escape($LogName)) { $logWritten = $true; break }
    }
    elseif ($ShellTools -contains $block.name) {
      $cmd = "$($block.input.command)"
      if ($cmd -match [regex]::Escape($LogName) -and $cmd -match $ShellWritePattern) {
        $logWritten = $true; break
      }
    }
  }

  if ($logWritten) { break }
}

if ($logWritten) { exit 0 }

$msg = @"
BLOCKED: docs/SESSION_LOG.md was not written this session.

Per "Session handoff (required)" in CLAUDE.md, APPEND a handoff entry to
docs/SESSION_LOG.md before ending the session -- newest entry at the bottom,
never overwriting an earlier one. Log even if no commits were made.

The entry must state:
  1. Date
  2. Commits (short SHA + one line each)
  3. VERIFIED vs ASSERTED-but-untested -- VERIFIED means a run was actually
     executed or a measurement actually taken; anything needing the bench that
     the user has not reported back on is ASSERTED
  4. What is left open
  5. What the next session must NOT assume

Append the entry, then stop again.
"@

[Console]::Error.WriteLine($msg)
exit 2
