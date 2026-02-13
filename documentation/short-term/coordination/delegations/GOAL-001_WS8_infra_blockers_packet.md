# GOAL-001 / WS8 Infra Blockers Packet (infra:codex)

Goal: unblock `GOAL-001/WS8` by fixing infra-scope dependencies for the
runner VM:

- pass through at least 2 real USB serial devices so runner sees stable
  `/dev/serial/by-id/*` identities
- ensure runner VM can resolve and reach `git.lecsys.net` (DNS/routing)

This is a blocker-clearing packet for WS8, not a separate workstream.

Hard rules:
- execute only in infra scope (host/LXD/VM/networking). Do not clone or
  edit the product repo (`squeezelite-esp32`).
- prefer least-invasive, reversible fixes; capture evidence.

## Deliverables (infra repo)

1. Evidence log (commit this) capturing:
   - current runner VM network/DNS status relevant to `git.lecsys.net`
   - USB passthrough configuration and the resulting `/dev/serial/by-id`
     view inside the runner VM
2. A short infra handoff entry (`context=infra-live`) summarizing:
   - what changed
   - whether runner VM can now push to GitLab
   - whether runner VM now sees >=2 `/dev/serial/by-id/*` devices

## Tasks (infra)

1. Runner VM reachability for GitLab (evidence):
   - verify runner VM can resolve and reach `git.lecsys.net`
     (DNS + routing).
   - if name resolution is broken, fix it at the correct layer
     (VM DNS config, LXD network config, host resolver/DHCP), preferring
     reversible changes.
2. USB passthrough (evidence):
   - identify at least 2 target USB serial devices on the host
   - configure passthrough into the runner VM
   - verify inside the runner VM that `/dev/serial/by-id/*` contains
     stable identities for both devices

## Evidence naming

- `documentation/evidence/infra_ws8_blockers_<timestamp>.log`

## Completion criteria

- runner VM can resolve/reach `git.lecsys.net` (or infra reports the
  remaining hard blocker and whether `operator_required=yes`)
- runner VM sees >=2 serial devices with stable `/dev/serial/by-id/*`
  identities

