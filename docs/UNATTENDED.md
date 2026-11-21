# Unattended runs

How the agent works through a list of issues while the owner is away, and how the owner checks the
result afterwards. Rules that matter are enforced by scripts and hooks, not by this text.

## The idea

| Part | Where it lives | Who owns it |
| --- | --- | --- |
| Queue | GitHub issues labelled `agent-ready` | Owner writes or approves each one |
| Loop | The skill `/work-queue` (`.claude/skills/work-queue/SKILL.md`) | Agent |
| Proof | `tools\gate.ps1`, the issue's monitor check, the `verifier` agent | Agent, checked by scripts |
| Limits | Hooks in `.claude/settings.json`, `tools/githooks/pre-commit` | Scripts |
| Result | Branch `night/<date>`, `docs/runs/<date>.md`, labels `ready-for-check` / `agent-blocked` | Owner reviews |
| Memory | `re/notes/`, `re/symbols/`, `CLAUDE.md` "Tool pitfalls", issue comments | Agent writes, everyone reads |

State lives in issues and files, never in the conversation: a session that is restarted or loses its
context continues from the labels, the branches and the run report.

## Before leaving (owner)

1. Label the issues to work on `agent-ready`. Each needs a **Monitor check**: a command and what it must
   show, without the headset. The issue templates ask for it. An issue without one is skipped.
2. Close Battlefield 6 and any EasyAntiCheat game. Close BF4 too, or leave it and accept that the run
   will not touch the game.
3. Start a session in the repository in **auto** permission mode and type `/work-queue` (optionally with
   issue numbers). Keep the PC awake (Claude desktop: keep-awake setting; or Windows power settings).

## What the agent does

One issue at a time, each on its own branch from `dev` (the working branch; `main` holds releases only,
`CLAUDE.md` "Branches"):

1. Check first: a unit test or the monitor check, seen failing.
2. Change, then `tools\gate.ps1` green.
3. Monitor check live, with the raw output kept.
4. The `verifier` agent, which has not seen the work, reads the issue, the diff and the evidence.
5. Commit, evidence as an issue comment, a row in `docs/HEADSET-TESTS.md`, label `ready-for-check`.
6. Merge into `night/<date>`, gate again there.

Three failed attempts on one issue: the branch is left as it is, the issue gets `agent-blocked` and a
comment with what was tried and what to try next. Then the next issue.

## The gate

`tools\gate.ps1` (about one minute when little changed):

- no dumps, game files, screenshots or profile data in the tree;
- both presets (`bf4-debug`, `bf3-debug`) build without warnings;
- all unit tests pass for both;
- the Python tools compile.

The result is stamped with a hash of the sources. `tools\gate.ps1 -Status` says whether the last
result still matches them. A commit or a document change does not make it stale.

## What is enforced, and how

| Rule | Enforced by | When |
| --- | --- | --- |
| No game or RE tool beside Battlefield 6 / EasyAntiCheat | `tools/agent/pre-command.ps1` | Always |
| Commit hooks are never skipped | `pre-command.ps1` | Always |
| No dumps, game files, screenshots, profile data in a commit | `tools/githooks/pre-commit` | Always |
| No push, no PR merge, no closing issues, no destructive git | `pre-command.ps1` | During a run |
| No commit, cherry-pick or rebase on `main`; no merge into it but `--ff-only dev` (a release) | `pre-command.ps1` | Always |
| No commit, merge or rebase on `dev` | `pre-command.ps1` | During a run |
| Live settings are not saved | `pre-command.ps1` | During a run |
| A turn cannot end with the gate red or stale | `tools/agent/stop-check.ps1` | During a run |

"During a run" means between `tools\agent\run.ps1 start` and `stop` (the mark is
`.claude\run-active.json`, not committed). Outside a run the hooks stay out of the way.

Honest limits of the enforcement:

- The stop hook objects once per stop. An agent that cannot get the gate green is then allowed to end
  its turn, so that a hopeless task is parked as `agent-blocked` instead of looping all night.
- The hooks match command text. They stop mistakes, not a determined bypass.
- The permission list in `.claude/settings.json` covers the usual commands. The first long run will
  show which prompts are still missing; add them there.
- New hooks and permissions load when a session starts. A session that was open before they were
  written does not have them.

## After returning (owner)

1. Read `docs/runs/<date>.md` on `night/<date>`: done, blocked, and the headset checks in order.
2. Build `night/<date>` (Release) and deploy it; do the 5-10 headset checks. Name results by ID
   ("GRN-4 works, HAND-15 still low").
3. Say which branches to merge into `dev`. Blocked issues: answer the question in the comment, or
   re-label `agent-ready` with a better monitor check.

## Writing issues that work unattended

- **One outcome per issue.** "Grenade sits at the left hip" not "improve grenades".
- **A check the monitor can show.** Unit test, pipe status line, a peeked value, a screenshot with a
  named thing in a named place. What only the headset can judge goes under "Headset check".
- **Reverse engineering: one question, and the evidence that answers it.** An address with its bytes,
  a value that changes as predicted, or a documented "not possible because". Use the RE template.
- **Say what must not change.** The agent treats everything else as allowed.
- **Order dependent work.** If B needs A, say so in B; the loop takes the lower number first.

## Limits of the method

- How something looks or feels in the headset cannot be checked by the agent. Such work ends at
  `ready-for-check`, never at "done".
- The game is the fragile part: the EA app shows prompts, the game crashes, and the owner's own mouse
  and keyboard reach it. Two crashes on one issue park it. A game that was already running when the
  run started is never touched.
- Input tools refuse to send keys unless the game is the foreground window, so a run that needs input
  takes the foreground. Do not use the PC for other work during such a run.
- The usage limit of the Claude plan can pause a long run. The state is in the issues, so a new
  session with `/work-queue` continues it.
