# JobPilotAI Source Adapters

Python adapters that fetch public ATS boards (Greenhouse, Lever, Ashby),
normalize them into the platform job contract, and publish **one observation
per board** through the C++ internal API. Adapters never write the database
directly ([ADR-0008](../docs/adr/0008-source-adapters-and-job-discovery.md),
ExecPlan 0012).

## Layout

```text
source_adapters/
├── pyproject.toml              # only runtime dependency: PyYAML
├── config.example.yaml
└── jobpilot_sources/
    ├── cli.py                  # python -m jobpilot_sources sync ...
    ├── config.py               # YAML loading + validation
    ├── client.py               # stdlib HTTP transport (injectable)
    ├── normalizer.py           # deterministic field normalization
    ├── publisher.py            # batching + POST /internal/jobs/source-sync
    ├── providers/              # greenhouse.py, lever.py, ashby.py, base.py
    └── tests/                  # unittest suite + recorded fixtures
```

Requires Python 3.10+ and PyYAML. No web framework and no database driver is
used.

## Usage

```bash
cd source_adapters

# fetch everything, report only (no writes, still validates the token)
python3 -m jobpilot_sources sync --config config.yaml --dry-run

# publish every enabled source
python3 -m jobpilot_sources sync --config config.yaml

# restrict the run to one provider type
python3 -m jobpilot_sources sync --config config.yaml --provider greenhouse
```

The internal token is read from the environment variable named by
`jobpilot.internal_token_env`. A missing token fails **before any network
call**, and the token is never logged or echoed.

## Configuration

Copy `config.example.yaml` to `config.yaml`. Duplicate `(type, board)` entries
are rejected, and no board auto-discovery happens (ExecPlan 0012 section 5).

```yaml
jobpilot:
  base_url: http://127.0.0.1:8080
  internal_token_env: INTERNAL_API_TOKEN

sources:
  - type: greenhouse
    board: example-company
    company: Example Company
    enabled: true
```

`company` is optional: Greenhouse boards use the provider `company_name` when
present, otherwise the configured `company`, otherwise the board token (Lever
and Ashby do not return a company name).

## Provider endpoints

| Provider | Endpoint | Completeness |
|---|---|---|
| Greenhouse | `boards-api.greenhouse.io/v1/boards/{board}/jobs?content=true` | Complete only when `meta.total` equals the returned row count |
| Lever | `api.lever.co/v0/postings/{board}?mode=json` | Complete by contract (the array is the whole board) |
| Ashby | `api.ashbyhq.com/posting-api/job-board/{board}?includeCompensation=true` | Complete by contract; `isListed: false` postings are dropped |

## Normalization rules

- All timestamps are converted to **UTC** and written as `YYYY-MM-DD HH:MM:SS`.
  A timestamp without an offset is treated as UTC.
- Nothing is fabricated: a field is omitted when the provider does not supply a
  usable value, and a row without `title` or a job URL is skipped.
- HTML descriptions are converted to plain text; Lever/Ashby
  `descriptionPlain` is used directly.
- `requirements` stays empty: no structured skills are inferred from text.
- **Salary is not published.** Lever reports absolute yearly amounts and Ashby
  a display summary, while the platform columns are monthly thousands, so
  storing them would be fabricated data. Closing this gap needs an agreed unit
  and currency mapping.

## Batch rules

The internal observation API accepts at most 200 jobs per request. A board
larger than that is split into 200-job chunks and every chunk is published with
`complete=false`, so a partial observation can never close jobs. An empty board
is only published when completeness was proven; otherwise the run reports
"nothing published".

## Tests

```bash
cd source_adapters
python3 -m unittest discover -s jobpilot_sources/tests -t .
```

The suite is fixture driven and never contacts a live ATS API. For an opt-in
live smoke (not part of CI), point a config at real boards and use `--dry-run`:

```bash
INTERNAL_API_TOKEN=dummy python3 -m jobpilot_sources sync \
  --config /tmp/live-config.yaml --dry-run
```
