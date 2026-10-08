"""JobPilotAI AI worker.

Claims ``job_match`` tasks from the C++ internal API (``/internal/ai/tasks/*``),
optionally calls an OpenAI-compatible provider, and merges strictly validated,
evidence-bound output onto the deterministic ``match_rule_v1`` baseline.

The worker never reads or writes business tables, never recomputes the score,
and degrades to the complete rule baseline when the provider is absent or
failing (ADR-0009, ExecPlan 0013 sections 6.3-6.5).
"""

from __future__ import annotations

__version__ = "0.1.0"

__all__ = ["__version__"]
