"""robots.txt fetching, parsing and per-path verdicts (ExecPlan 0014 section 5.2).

Policy implemented here (and enforced by :mod:`jobpilot_sources.fetch`):

* **Fail closed.** A robots.txt that cannot be retrieved (transport error, 5xx),
  cannot be decoded, or cannot be parsed into rules denies every path of that
  host. ``404``/``410`` mean "no rules published" and therefore allow (RFC 9309
  section 2.3.1.3), which is the only permissive non-2xx outcome.
* **Per path, not per host.** The caller asks about the exact list/detail path it
  is going to read, and a ``Disallow`` verdict aborts that source for the whole
  run (no retry, no downgrade to a different path).
* **RFC 9309 matching.** The most specific (longest) matching rule wins; on an
  equal-length tie (equivalent match) ``Allow`` wins, per RFC 9309 section 2.2.2.
  ``*`` and ``$`` wildcards are supported.
* **Group selection.** The most specific user-agent group that matches our token
  wins, falling back to ``*``.
* ``Crawl-delay`` from the selected group (or ``*``) is exposed as the
  inter-request floor.

Nothing here performs I/O by itself except :class:`RobotsCache`, which asks an
injected ``fetcher`` callable for the raw bytes so tests stay offline.
"""

from __future__ import annotations

import time
import urllib.parse
from dataclasses import dataclass, field
from typing import Callable, Iterable

# The honest identifying product token used for group selection. It matches the
# User-Agent prefix in :mod:`jobpilot_sources.client`.
DEFAULT_AGENT_TOKEN = "jobpilot-source-adapters"

ROBOTS_PATH = "/robots.txt"
DEFAULT_CACHE_TTL_SECONDS = 3600.0

ALLOWED = "allowed"
DISALLOWED = "disallowed"


class RobotsUnavailable(Exception):
    """robots.txt could not be retrieved, decoded or parsed (fail closed)."""


@dataclass(frozen=True)
class RobotsRule:
    allow: bool
    pattern: str


@dataclass(frozen=True)
class RobotsRules:
    """Parsed rules for one host from the perspective of our user agent."""

    rules: tuple[RobotsRule, ...] = ()
    crawl_delay: float | None = None
    #: True when an explicit ``*`` or matching group was present.
    explicit_group: bool = False

    def verdict(self, path: str) -> str:
        """Return ``ALLOWED``/``DISALLOWED`` for an absolute path.

        The most specific (longest) matching rule wins. When an ``Allow`` and a
        ``Disallow`` rule are *equally specific* (equivalent match), ``Allow``
        wins, per RFC 9309 section 2.2.2.

        This single equivalent-match rule is the only permissive path here.
        Every fail-closed path is unchanged: an unreachable, non-2xx,
        undecodable or unparsable robots.txt denies the host (see
        :class:`RobotsCache`), and a more specific ``Disallow`` still beats a
        shorter ``Allow``.
        """

        target = _normalize_path(path)
        best: RobotsRule | None = None

        for rule in self.rules:
            if not _pattern_matches(rule.pattern, target):
                continue

            if best is None or len(rule.pattern) > len(best.pattern):
                best = rule
            elif len(rule.pattern) == len(best.pattern) and rule.allow:
                # Equivalent match: the less restrictive rule wins (RFC 9309).
                best = rule

        if best is None:
            return ALLOWED

        return ALLOWED if best.allow else DISALLOWED

    def is_allowed(self, path: str) -> bool:
        return self.verdict(path) == ALLOWED


def robots_url(url: str) -> str:
    """Return the robots.txt URL for the origin of ``url``."""

    parts = urllib.parse.urlsplit(url)
    return urllib.parse.urlunsplit(
        (parts.scheme, parts.netloc, ROBOTS_PATH, "", "")
    )


def parse_robots(
    text: str,
    *,
    user_agent: str = DEFAULT_AGENT_TOKEN,
) -> RobotsRules:
    """Parse robots.txt text into the rules that apply to ``user_agent``.

    Never raises for malformed content: unparsable lines are ignored, which is
    what every mainstream crawler does. Undecodable *bytes* are rejected earlier
    by the caller (see :class:`RobotsCache`).
    """

    if not isinstance(text, str):
        raise RobotsUnavailable("robots.txt payload is not text")

    token = _agent_token(user_agent)
    groups = _split_groups(text)

    selected = _select_group(groups, token)

    if selected is None:
        return RobotsRules()

    allow: list[str] = []
    disallow: list[str] = []
    crawl_delay: float | None = None

    for line in selected:
        field_name, _, value = line.partition(":")
        field_name = field_name.strip().lower()
        value = value.strip()

        if field_name == "allow":
            allow.append(value)
        elif field_name == "disallow":
            disallow.append(value)
        elif field_name == "crawl-delay":
            parsed = _parse_crawl_delay(value)

            if parsed is not None:
                crawl_delay = parsed

    rules = tuple(
        [RobotsRule(True, pattern) for pattern in allow]
        + [RobotsRule(False, pattern) for pattern in disallow]
    )

    return RobotsRules(
        rules=rules,
        crawl_delay=crawl_delay,
        explicit_group=True,
    )


def _agent_token(user_agent: str) -> str:
    """Reduce a User-Agent header to the product token used for matching."""

    if not isinstance(user_agent, str):
        return DEFAULT_AGENT_TOKEN

    first = user_agent.split("/", 1)[0].split(" ", 1)[0].strip()
    return first.lower() or DEFAULT_AGENT_TOKEN


def _split_groups(text: str) -> list[tuple[list[str], list[str]]]:
    """Split robots.txt into (user-agents, rules) groups."""

    groups: list[tuple[list[str], list[str]]] = []
    agents: list[str] = []
    rules: list[str] = []

    for raw_line in text.splitlines():
        line = raw_line.split("#", 1)[0].strip()

        if not line:
            continue

        field_name, separator, value = line.partition(":")

        if not separator:
            continue

        field_name = field_name.strip().lower()
        value = value.strip()

        if field_name == "user-agent":
            if rules:
                groups.append((agents, rules))
                agents = []
                rules = []

            agents.append(value.lower())
        elif field_name in {"allow", "disallow", "crawl-delay"}:
            if not agents:
                # A rule before any user-agent line belongs to no group.
                continue

            rules.append(f"{field_name}:{value}")

    if agents or rules:
        groups.append((agents, rules))

    return groups


def _select_group(
    groups: Iterable[tuple[list[str], list[str]]],
    token: str,
) -> list[str] | None:
    """Pick the most specific matching group, falling back to ``*``."""

    best_specificity = -1
    best_rules: list[str] | None = None
    wildcard_rules: list[str] | None = None

    for agents, rules in groups:
        for agent in agents:
            if agent == "*":
                if wildcard_rules is None:
                    wildcard_rules = rules

                continue

            if token.startswith(agent) or agent in token:
                if len(agent) > best_specificity:
                    best_specificity = len(agent)
                    best_rules = rules

    if best_rules is not None:
        return best_rules

    return wildcard_rules


def _parse_crawl_delay(value: str) -> float | None:
    try:
        parsed = float(value)
    except (TypeError, ValueError):
        return None

    if parsed < 0 or parsed != parsed or parsed in (float("inf"),):
        return None

    return parsed


def _normalize_path(path: str) -> str:
    if not path:
        return "/"

    if not path.startswith("/"):
        path = "/" + path

    # Keep the query string (it is part of the target URI) but drop the
    # fragment, which is never sent to the server.
    return path.split("#", 1)[0]


def _pattern_matches(pattern: str, path: str) -> bool:
    """Match an Allow/Disallow pattern against a path (``*`` and ``$``)."""

    if pattern == "":
        # An empty Disallow means "nothing is disallowed"; an empty Allow is a
        # no-op. Treating it as a non-match gives both semantics for free.
        return False

    anchored_end = pattern.endswith("$")
    body = pattern[:-1] if anchored_end else pattern
    segments = body.split("*")

    position = 0

    for index, segment in enumerate(segments):
        if segment == "":
            continue

        if index == 0:
            if not path.startswith(segment):
                return False

            position = len(segment)
            continue

        found = path.find(segment, position)

        if found < 0:
            return False

        position = found + len(segment)

    if anchored_end and not body.endswith("*"):
        return path.endswith(segments[-1])

    return True


@dataclass
class _CacheEntry:
    rules: RobotsRules
    fetched_at: float = field(default=0.0)


class RobotsCache:
    """Fetch robots.txt once per origin and answer per-path verdicts.

    ``fetcher`` is any callable ``(url) -> bytes``; it must bypass the robots
    check itself (the :class:`~jobpilot_sources.fetch.Fetcher` does this by
    calling with ``check_robots=False``).
    """

    def __init__(
        self,
        fetcher: Callable[[str], bytes],
        *,
        user_agent: str = DEFAULT_AGENT_TOKEN,
        ttl_seconds: float = DEFAULT_CACHE_TTL_SECONDS,
        clock: Callable[[], float] = time.monotonic,
    ) -> None:
        self._fetcher = fetcher
        self._user_agent = user_agent
        self._ttl_seconds = ttl_seconds
        self._clock = clock
        self._entries: dict[str, _CacheEntry] = {}
        #: Origins whose robots.txt could not be used (denied, fail closed).
        self.denied_origins: set[str] = set()

    def rules_for(self, url: str) -> RobotsRules:
        """Return the rules for the origin of ``url`` or raise."""

        origin = _origin(url)

        entry = self._entries.get(origin)

        if entry is not None and (
            self._clock() - entry.fetched_at
        ) < self._ttl_seconds:
            return entry.rules

        rules = self._load(url)
        self._entries[origin] = _CacheEntry(
            rules=rules,
            fetched_at=self._clock(),
        )

        return rules

    def verdict_for(self, url: str) -> str:
        """Per-path verdict for the *requested* URL (fail closed)."""

        try:
            rules = self.rules_for(url)
        except RobotsUnavailable:
            self.denied_origins.add(_origin(url))
            return DISALLOWED

        path = urllib.parse.urlsplit(url)
        target = path.path or "/"

        if path.query:
            target = f"{target}?{path.query}"

        return rules.verdict(target)

    def is_allowed(self, url: str) -> bool:
        return self.verdict_for(url) == ALLOWED

    def crawl_delay(self, url: str) -> float | None:
        try:
            return self.rules_for(url).crawl_delay
        except RobotsUnavailable:
            return None

    def _load(self, url: str) -> RobotsRules:
        target = robots_url(url)

        try:
            loaded = self._fetcher(target)
        except Exception as error:  # noqa: BLE001 - fail closed for any cause
            raise RobotsUnavailable(
                f"robots.txt unavailable for {_origin(url)}: {error}"
            ) from error

        if isinstance(loaded, tuple):
            # A (status, body) pair is accepted so the caller can keep its own
            # HTTP layer.
            status, payload = loaded
        else:
            status, payload = 200, loaded

        # 404/410 mean "no rules published" (RFC 9309 section 2.3.1.3). The
        # body of such a response is an error page, so it is never parsed and
        # its encoding is irrelevant.
        if status in (404, 410):
            return RobotsRules()

        if status < 200 or status >= 300:
            raise RobotsUnavailable(
                f"robots.txt returned HTTP {status} for {_origin(url)}"
            )

        if not isinstance(payload, (bytes, bytearray)):
            raise RobotsUnavailable("robots.txt payload is not bytes")

        try:
            text = bytes(payload).decode("utf-8")
        except UnicodeDecodeError as error:
            raise RobotsUnavailable(
                "robots.txt is not valid UTF-8"
            ) from error

        return parse_robots(text, user_agent=self._user_agent)


def _origin(url: str) -> str:
    parts = urllib.parse.urlsplit(url)
    return f"{parts.scheme}://{parts.netloc}".lower()
