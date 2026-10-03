# JobPilotAI ExecPlans

ExecPlans track Level 3 and Level 4 work that affects multiple modules,
architectural boundaries, concurrency, compatibility, or rollback safety.

## Layout

```text
docs/exec-plans/
├── active/
├── completed/
└── README.md
```

## Required Sections

- Background and current state
- Goal and non-goals
- Constraints and invariants
- Research evidence
- Selected design
- Rejected alternatives
- Implementation phases
- Verification and rollback
- Current progress
- Known issues and follow-up work

Do not create an ExecPlan for documentation fixes or small local changes.
