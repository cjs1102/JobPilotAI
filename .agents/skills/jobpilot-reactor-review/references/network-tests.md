# Network Test and Verification Checklist

## Tests

- Require focused tests for the changed invariant.
- Prefer deterministic event-loop tests over sleep-based timing assumptions.
- Cover failure and shutdown paths, not only the happy path.

## Verification

- Run the focused network tests and the relevant full test subset.
- Use ASan and UBSan for lifetime or bounds changes.
- Use TSan for cross-thread changes when the build configuration supports it.
- Do not claim thread safety or leak freedom without a corresponding check or
  an explicit limitation.

## Required Evidence

- Findings first, ordered by severity and grounded in file and line references.
- Invariants checked and their observed status.
- Tests run and results.
- Verification gaps and residual risk.
