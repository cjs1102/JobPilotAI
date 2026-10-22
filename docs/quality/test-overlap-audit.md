# Test Overlap Audit — summary

The full 2026-10-11 audit was removed from the public tree to keep the
repository reader-focused. Its machine-readable data remains in
[`test-merge-map.json`](test-merge-map.json) and is enforced by
[`tools/checks/test_coverage_map.py`](../../tools/checks/test_coverage_map.py)
in CI.

The audit classified every candidate pair by assertion target and concluded that
no test case is removable:

- 312 CTest cases before and after the audit; 0 removed.
- 66 candidate pairs evaluated; 47 recorded only; 20-pair cap respected.
- No survivor is missing, no removed case is still live, and the live CTest list
  still matches the map.

The complete historical report and its negative controls remain in Git history.

<!-- audit-counts: {"before":312,"after":312,"removed":0,"survivors":0,"candidates_evaluated":66,"candidate_pairs_unordered":50,"record_only":47,"cap":20} -->
