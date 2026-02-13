# Remote Transport Lock Record

This file is the single source of truth for the remote-role prompting
transport mechanism (`arch` -> `runner` / `infra`).

Once `status: locked`, follow-on work must implement the locked
mechanism and must not re-litigate transport per session.

## Status

status: evaluating
locked_at_utc: (unset)

## Current Mechanism

primary_transport: ssh
fallback_transport: ssh

## Candidate Mechanisms

- codex_app_server
- codex_mcp_server

## Decision Criteria (v1)

- supports parallel, multi-step threads without relying on implicit chat
  context
- supports stable correlation (`thread_ref`) across sessions
- preserves repo boundary contract (role repos remain authoritative)
- does not require secrets to be written into repositories
- has a clear break-glass path (SSH) during rollout

## Notes / Next Actions

- Populate evaluation notes and prototype plan under GOAL-001/WS12.
- When ready to lock:
  - set `status: locked`
  - set `primary_transport: <chosen>`
  - record `locked_at_utc`
  - record rollback/fallback expectations

