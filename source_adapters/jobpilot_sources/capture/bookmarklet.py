"""Bookmarklet packaging: load the JS, build the ``javascript:`` href, render help.

The JavaScript lives in ``bookmarklet.js`` next to this module so there is one
source of truth that is both pasteable by a human and executable by a test.
This module never touches the network and never reads a credential: the
bookmarklet must stay credential-free by construction.
"""

from __future__ import annotations

import json
import urllib.parse
from typing import Any, Mapping

BOOKMARKLET_FILENAME = "bookmarklet.js"
BOOKMARKLET_GLOBAL = "jobpilotCapture"

USAGE_STEPS = (
    "1. Copy the bookmarklet link from `python -m jobpilot_sources "
    "capture-bookmarklet`.",
    "2. Create a browser bookmark and paste the link as its URL.",
    "3. Open the job page in your own already-logged-in browser session "
    "(the bookmarklet never logs in and never bypasses a login wall).",
    "4. Click the bookmark: it reads only what the page shows and copies a "
    "JSON capture to your clipboard.",
    "5. Save the JSON to a local file, review it, then run "
    "`python -m jobpilot_sources publish-capture --file capture.json`.",
)


class BookmarkletError(Exception):
    """The packaged bookmarklet resource is missing or unreadable."""


def bookmarklet_source() -> str:
    """Return the raw JavaScript source of the bookmarklet."""

    from importlib import resources

    try:
        resource = resources.files(__package__).joinpath(
            BOOKMARKLET_FILENAME
        )
        return resource.read_text(encoding="utf-8")
    except (FileNotFoundError, ModuleNotFoundError) as error:
        raise BookmarkletError(
            f"cannot read {BOOKMARKLET_FILENAME}: {error}"
        ) from error


def bookmarklet_body(options: Mapping[str, Any] | None = None) -> str:
    """Return the JavaScript that the ``javascript:`` URL will execute."""

    source = bookmarklet_source()
    arguments = json.dumps(dict(options or {}), ensure_ascii=False)

    return (
        "(function(){"
        + source
        + f";{BOOKMARKLET_GLOBAL}.run(document, {arguments});"
        + "})()"
    )


def bookmarklet_href(options: Mapping[str, Any] | None = None) -> str:
    """Return the pasteable ``javascript:`` bookmark URL."""

    body = bookmarklet_body(options)

    return "javascript:" + urllib.parse.quote(body, safe="")


def instructions_html(
    options: Mapping[str, Any] | None = None,
) -> str:
    """A small self-contained help page (no scripts, no external resources)."""

    href = bookmarklet_href(options)
    escaped = (
        href.replace("&", "&amp;")
        .replace('"', "&quot;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
    )
    steps = "\n".join(
        f"      <li>{step}</li>" for step in USAGE_STEPS
    )

    return (
        "<!DOCTYPE html>\n"
        '<html lang="en">\n'
        "<head><meta charset=\"utf-8\">"
        "<title>JobPilotAI manual capture</title></head>\n"
        "<body>\n"
        "  <h1>JobPilotAI manual capture (0014)</h1>\n"
        "  <p>Drag this link to your bookmarks bar, then click it on a job "
        "page you are already allowed to see:</p>\n"
        f'  <p><a href="{escaped}">JobPilotAI capture</a></p>\n'
        "  <p>The bookmarklet holds no credential and makes no network "
        "request; it only copies what the page shows to your clipboard.</p>\n"
        "  <ol>\n"
        f"{steps}\n"
        "  </ol>\n"
        "</body>\n"
        "</html>\n"
    )


__all__ = [
    "BOOKMARKLET_FILENAME",
    "BOOKMARKLET_GLOBAL",
    "BookmarkletError",
    "USAGE_STEPS",
    "bookmarklet_body",
    "bookmarklet_href",
    "bookmarklet_source",
    "instructions_html",
]
