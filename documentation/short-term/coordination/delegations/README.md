# Delegation Packets

Packets in this folder are prompts meant to be fed to `codex exec -` on
remote role machines (typically via SSH transport).

Naming conventions:
- `GOAL-001_WS8_runner_packet.md`: primary WS8 implementation packet
- `GOAL-001_WS8_runner_pushfix_packet.md`: shim for a past filename
  collision; points at the real WS8 push blocker packets
- `GOAL-001_WS8_infra_blockers_packet.md`: infra-scope packet for WS8
  blockers (runner VM DNS/routing, USB passthrough)
- `GOAL-001_WS8_runner_handshake_push_packet.md`: runner packet to
  handshake, verify credentials gate, and push WS8 commits with evidence
- `GOAL-001_WS8_runner_inventory_refresh_packet.md`: runner packet to
  refresh inventory evidence after USB passthrough and report device count
- `GOAL-001_WS8_runner_closeout_packet.md`: runner packet to finish WS8
  (inventory + slot mapping validation + lock selftest), commit, and push
- `GOAL-001_WS8_runner_closeout_continue_packet.md`: runner packet to
  continue closeout if an earlier closeout run was interrupted
- `GOAL-001_WS8_blocker_*.md`: WS8 blocker-clearing packets (do not
  represent separate workstreams)
