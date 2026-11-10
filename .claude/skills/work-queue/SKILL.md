---
name: work-queue
description: Work through the GitHub issues labelled agent-ready without the owner present - one issue at a time, each verified by the gate and a live check, with a report at the end. Use when the owner says to work the queue, run overnight, or run unattended.
---

# Work the queue

The rules are in `docs/UNATTENDED.md`; read it first. This is the order of work.

## Start

1. `tools\agent\run.ps1 start -Note "<what this run is for>"`. If it says a game was already running, the
   owner may be using it: take only issues that need no game.
2. `tools\check-env.ps1` and `tools\gate.ps1` on `main`. If the gate is red on `main`, stop the run and
   report: nothing may be built on a red base.
3. `gh issue list --label agent-ready --state open`. Order: `priority:high` first, then lowest number.
   Arguments to this skill (issue numbers) replace that list.
4. Create `night/<yyyy-mm-dd>` from `main` and the report file `docs/runs/<yyyy-mm-dd>.md` on it.

## Per issue

1. Read the issue and every comment. If it has no **Monitor check**, do not guess one: label it
   `agent-blocked`, comment what is missing, take the next.
2. Branch `<area>/<short-name>` from `main`. Write the check first where possible (unit test, or the pipe
   command and expected value), see it fail, then make it pass.
3. `tools\gate.ps1` must be green.
4. Run the issue's monitor check live and keep the raw output. Restore every live setting you changed.
5. Start the `verifier` agent with the issue number and the branch. Fix what it rejects; a second
   rejection of the same point counts as a failed attempt.
6. Commit on the branch. Comment on the issue: what changed, the gate result, the monitor check with its
   output, what the headset must show. Add its row to `docs/HEADSET-TESTS.md`. Label `ready-for-check`,
   remove `agent-ready`.
7. Merge the branch into `night/<date>` and run the gate there. If it turns red, undo that merge and note
   the conflict in the report.

Three failed attempts at one issue (gate red, monitor check wrong, verifier rejects): stop working on it.
Leave the branch, label `agent-blocked`, comment what was tried and what you would try next, take the next
issue. The game crashing twice on one issue counts the same.

## Learn as you go

- A dead end goes into the matching note in `re/notes/` or `docs/`, in one or two lines, when it happens.
- A tool that misled you goes into "Tool pitfalls" in `CLAUDE.md`.
- Every address or offset goes into `re/symbols/` with how it was verified.
- If the same kind of failure happens twice, fix the tool or add a check to `tools\gate.ps1` (as its own
  issue branch) instead of working around it a third time.

## End

1. Finish `docs/runs/<date>.md`: issues done, issues blocked and why, the 5-10 headset checks in the order
   to do them, anything the owner must decide. Commit it on `night/<date>`.
2. Deploy nothing to the game folder that is not on `night/<date>`; remove test settings; clear
   `fakehead` and `fakehand`.
3. `tools\agent\run.ps1 stop`.
4. Final message: the report's summary in plain language.

Never during a run: push, merge into `main`, close issues, save live settings, delete branches, touch a
game the owner started. The hooks refuse most of these; the rule holds for the rest too.
