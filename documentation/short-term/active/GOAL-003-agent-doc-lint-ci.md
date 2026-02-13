# GOAL-003: Agent-Driven Documentation Linting in CI/CD

Status: active  
Owner: unassigned  
Last updated: 2026-02-12

## Objective

Make documentation linting reliable and low-overhead by combining:

1. upstream merge-gating markdown lint checks, and
2. local LXD agent-driven document gardening loops.

Reference docs:

- `documentation/agents/ci_lane_contract.md`
- `documentation/agents/document_gardening.md`
- `documentation/agents/start_here.md`

## Scope

In scope:

- markdown lint contract and ownership by lane
- agent run instructions and evidence format
- progressive discovery pointers for docs work

Out of scope:

- product/firmware behavior changes
- non-doc CI jobs

## Strategy

1. Keep upstream lane authoritative for merge-gating doc lint.
2. Use local lane for agent-powered doc cleanup and maintenance.
3. Keep agent context small with route-based document discovery.

## Workstreams

## WS1: Upstream Doc Lint Gate

1. Define lint target scope:
   - `AGENTS.md`
   - `documentation/**/*.md`
2. Define failure policy:
   - lint failure blocks merge when docs change.

Acceptance:

- lane contract documents required lint command and blocking policy.

## WS2: Local Agent Gardening Loop

1. Standardize gardening task contract (prompt template + commands).
2. Require before/after lint evidence in session output.
3. Require board/handoff updates when status changes.

Acceptance:

- agents can run one reusable gardening flow with consistent output.

## WS3: Progressive Context Discovery

1. Keep one `start_here` route document as first entry.
2. Route agents to one task-specific doc at a time.
3. Avoid large up-front context loads.

Acceptance:

- docs in `documentation/agents/` support stepwise discovery.

## WS4: Reporting and Handoff Format

1. Standardize lint report fields:
   - scope
   - command
   - before errors
   - after errors
   - residual risk
2. Record unresolved lint debt in short-term handoff when needed.

Acceptance:

- any agent can continue lint remediation from prior evidence.

## Exit Criteria

1. `documentation/agents` docs consistently point to progressive startup.
2. Lint ownership is explicit per CI lane.
3. Gardening sessions produce machine-usable and human-usable evidence.
