"""JobPilotAI ATS source adapters.

Fetches public ATS boards (Greenhouse / Lever / Ashby), normalizes them into
the platform job contract, and publishes them through the internal
``POST /internal/jobs/source-sync`` observation API. Adapters never touch the
database directly (ADR-0008, ExecPlan 0012).
"""

from __future__ import annotations

__version__ = "0.1.0"

__all__ = ["__version__"]
