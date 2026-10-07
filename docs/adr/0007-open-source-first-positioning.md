# ADR-0007: Open-source-first positioning

## Status

Accepted

## Context

JobPilotAI can be evaluated as either:

- a commercial SaaS product needing billing, multi-tenancy, RBAC, gateway,
  platform-funded AI, crawler operations, notifications, and a complete
  frontend; or
- an open-source engineering showcase and self-hosted personal tool focused on
  C++ backend depth, AI engineering boundaries, testing, and a complete local
  workflow.

The current repository already contains a complete C++ network/HTTP core,
User/Profile/Skill, Resume, Job Read, Application, and Job Ingestion slices.

## Decision

Adopt:

```text
Open-source first
+ Self-hosted
+ Extensible toward SaaS
```

Do not build SaaS infrastructure before the open-source MVP is complete.

## Consequences

### Positive

- The project stays runnable and demonstrable at every stage.
- Personal data remains local by default.
- C++ backend depth remains the primary engineering signal.
- AI providers can be optional and user-selected.

### Negative

- Commercial operations and multi-tenant concerns remain unaddressed.
- A minimal UI is still needed for a convincing demo.
- Some source/provider integrations remain operator-configured.

## Rejected Alternatives

- **SaaS-first**: too much infrastructure and cost before a complete local
  workflow exists.
- **Unlimited future-proofing**: MQ, gateway, billing, RBAC, and multi-tenancy
  are deferred until a concrete requirement appears.
- **AutoApply in MVP**: platform compliance, account-safety, quality, and
  human-review risks are too high.
