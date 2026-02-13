# CI Lane Contract

Define and preserve the split between upstream CI and local hardware CI.

## Purpose

Maintain seamless developer flow while acknowledging hardware constraints:

- GitHub CI cannot access physical hardware
- Local LXD/HUT CI can run physical integration tests

## Lane Definitions

## 1) Upstream GitHub Lane (hardware-agnostic)

Allowed:

- compile/build
- packaging
- lint/static checks
- unit tests and non-hardware integration tests

Not allowed:

- direct serial/USB assumptions
- relay/home-automation dependencies
- any test requiring physical board availability

## 2) Local LXD Hardware Lane (HIL)

Allowed:

- flash/monitor cycles
- physical integration and regression tests
- hard off/on recovery with relay control
- multi-HUT scheduling and isolation

## Seamless Handoff Rules

1. Hardware runs should map to the same commit SHA validated upstream.
2. Artifact format and naming must be lane-neutral and SHA-addressable.
3. Hardware result summaries must include:
   - commit SHA
   - HUT slot
   - pass/fail
   - recovery/power-cycle events
4. Upstream checks must not block on hardware-runner availability.

## Operational Guidance

1. Treat upstream lane as fast feedback gate.
2. Treat local HIL lane as higher-fidelity system validation.
3. Keep trigger policy explicit:
   - automatic for upstream lane
   - explicit/controlled for local HIL lane
4. Minimize drift between lanes by reusing the same
   container/toolchain baseline where feasible.

## Documentation Lint Contract

1. Upstream GitHub lane must run markdown lint checks for:

   - `AGENTS.md`
   - `documentation/**/*.md`
2. Local LXD lane should run agent-driven document gardening for:

   - lint fixes
   - pointer refresh
   - short-term board/handoff updates
3. Upstream lint failures block merge for documentation changes.
4. Local gardening output is evidence, not a replacement for upstream checks.

## Lane Vs Role Boundary

CI lanes and agent roles are different dimensions:

1. Lanes define where validation runs (`upstream` vs `local-hil`).
2. Roles define who executes/coordinates (`arch`, `infra`, `runner`).
3. Do not infer ownership from lane alone; use
   `documentation/short-term/coordination/workstream_board.md` owner
   field as source of truth.
4. Use handoff `context` to distinguish control plane vs live remote
   execution (`arch-local`, `infra-live`, `runner-live`).
