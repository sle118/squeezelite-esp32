# GOAL-001 / WS8 Delegation Packet: Runner Push Fix (shim) (runner:codex)

This file exists to resolve a filename collision/ambiguity in WS8
delegation references.

Use the purpose-specific WS8 packets instead:

- Primary WS8 implementation:
  - `GOAL-001_WS8_runner_packet.md`
- WS8 push blocker (DNS/name resolution + push):
  - `GOAL-001_WS8_blocker_runner_push_unblock_packet.md`
- WS8 push retry requiring real VM network/privileges:
  - `GOAL-001_WS8_blocker_runner_push_retry_danger_packet.md`

If you are the runner role and you received this shim:

1. Pick the correct packet above based on current blocker symptoms.
2. Execute it inside the runner VM repo:
   - `/home/runner/workspaces/codex-runner-agent`
3. In the runner handoff summary, include:
   - `thread_ref=<value if provided by arch>`
   - pushed commit SHA(s) (or remaining blocker)
   - evidence path(s)

