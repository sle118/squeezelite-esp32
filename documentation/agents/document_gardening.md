# Document Gardening Playbook

Use document gardening to keep agent-facing and implementation docs current,
lint-clean, and actionable during active development.

## What It Includes

- fix markdown lint/style issues
- refresh stale links and pointers
- sync status boards and handoff logs
- archive completed short-term goals
- remove drift between policy docs and current workflow

## What It Does Not Include

- changing product behavior
- changing source code unless explicitly requested
- rewriting long-form architecture without a scoped request

## CI Flow Integration

Use the lane split from `documentation/agents/ci_lane_contract.md`.

1. Upstream GitHub lane:
   - run markdown/doc lint checks
   - validate docs can be rendered and linked
   - fail fast on malformed docs
2. Local LXD lane:
   - run agent-driven gardening tasks
   - update short-term coordination docs
   - produce handoff notes for next session

## Standard Gardening Task

1. Read:
   - `AGENTS.md`
   - `documentation/agents/README.md`
   - `documentation/short-term/coordination/workstream_board.md`
2. Lint target docs.
3. Apply minimal, scoped fixes.
4. Re-run lint.
5. Update handoff/status docs if task context changed.
6. Report:
   - files touched
   - lint evidence
   - residual risks

## Baseline Commands

```bash
npx -y markdownlint-cli2 "documentation/**/*.md" AGENTS.md
```

Optional targeted run:

```bash
npx -y markdownlint-cli2 \
  AGENTS.md \
  documentation/agents/**/*.md \
  documentation/short-term/**/*.md
```

## Prompt Template

Use this when assigning gardening in CI/LXD:

```text
Run a document gardening pass for the current goal.
Scope: AGENTS.md, documentation/agents/, documentation/short-term/.
Requirements:
1) lint markdown and fix only in-scope issues,
2) keep behavior/policy intent unchanged,
3) update workstream board + handoff log if status changed,
4) report lint before/after and residual issues.
```

## Related Active Goal

- `documentation/short-term/active/GOAL-003-agent-doc-lint-ci.md`
  tracks short-term implementation for agent-driven doc linting in CI/CD.
