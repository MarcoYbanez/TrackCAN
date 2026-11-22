---
name: jira-status-sync
description: Keep a TCAN Jira ticket's status in step with git. In Progress when work on it starts, Done when its pull request is merged. Use when starting work on a ticket, creating its branch, merging a PR, finishing a ticket, or when asked to sync or check ticket status.
---

The owner wants the Jira board to show the truth without being told. This skill is instructions for me, not an automation: it runs only when I follow it in a session. A merge the owner does on GitHub is caught the next time I run the sweep below.

## The rules

| Evidence in git | Status |
|---|---|
| The ticket's branch exists and holds at least one commit of its own (not in `main`), or I just started work on the ticket | **In Progress** |
| A pull request for the ticket is **MERGED** | **Done** |
| Neither | leave it alone |

- A ticket the owner placed in **High Priority** stays there until work starts, then moves to In Progress. Never move a ticket back to To Do or out of Done.
- An **Epic** is never closed by this skill. When all its children are Done, say so and let the owner close it.
- A branch with no commits of its own, or a ticket only mentioned in text, is not evidence.

## Matching a ticket to its branch and PR

Match **only** on the branch name (`<KEY>-...`) or the PR title (`<KEY>: ...` or `<KEY> ...`). Never on a PR body or a commit message: PR #6 named TCAN-6 to TCAN-13 in its body, and Jira linked them although they were not done.

**Key collisions are real.** This Jira project started at TCAN-1 on 2026-11-17. Older branches and PRs (for example `TCAN-6-Switch-Datalogger-node-from-FreeRTOS-to-the-Zephyr-Project`, PR #3, July 2026) come from an earlier numbering and are not these tickets. Ignore any branch or PR whose first commit or `createdAt` is **before the ticket's `created` date**. Without this check an old merged PR would mark a new ticket Done.

## Facts

- Site `trackcan-ai.atlassian.net`, cloudId `19ef0b3f-b5ed-4a22-83a9-0080148c43e7`, project `TCAN`.
- Statuses: To Do, High Priority (the owner's own column, category In Progress), In Progress, Done.
- Do not hardcode transition ids. Run `executeRead` with `listJiraIssueTransitions` for the issue (find it with `discover` if needed), pick the transition whose **target status** matches, then call `transitionJiraIssue` with its id. At the time of writing they were To Do 11, In Progress 21, Done 31, High Priority 2.
- `gh pr list --state all --json number,title,headRefName,createdAt,mergedAt` and `git log` give the evidence.

## When to run it

1. **Starting a ticket** (I create its `TCAN-n-...` branch or make its first commit): transition to In Progress.
2. **A PR for the ticket is merged** (I merged it, or `gh pr view` shows MERGED): transition to Done.
3. **Sweep** at the start of a session about a ticket, or when asked: for every non-Done, non-Epic ticket (`project = TCAN AND issuetype != Epic AND status != Done`) apply the rules above using real `gh` and `git` output, then transition what changed.

## After transitioning

Report each change in one line (ticket, old status, new status, the evidence: branch or PR number). For each ticket moved to **Done**, look in its description for unchecked scope items (`- [ ]` or `\[ \]`). A merge does not prove every item was done, so list the open ones and offer to move them into a new ticket. Do not reopen a ticket on my own.

If a transition is refused or the evidence is unclear, say so and leave the status unchanged.
