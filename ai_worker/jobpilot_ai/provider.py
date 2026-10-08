"""Provider-neutral OpenAI-compatible client (ExecPlan 0013 section 6.4).

One wire protocol, no vendor SDK:

- ``POST {base_url}/chat/completions``
- ``POST {base_url}/embeddings``

DeepSeek, Qwen, OpenAI, vLLM and Ollama are all reachable by changing
``base_url`` + ``model``.

The key is read from the environment variable named by ``key_env`` only. It is
never a CLI flag, never part of the config file, never logged, never echoed in
an error message and never written into a task result. Provider error bodies are
treated as untrusted data: only a short, truncated excerpt is used for a
diagnostic message, and no provider text ever reaches a result field.

Timeouts and bounded retries (exponential backoff plus jitter, honouring a
clamped ``Retry-After``) live here, not in the provider SDK.
"""

from __future__ import annotations

import json
import math
import os
import random
import time
from typing import Any, Callable, Mapping, Sequence

from .client import (
    DEFAULT_USER_AGENT,
    RetryPolicy,
    Transport,
    TransportError,
    UrllibTransport,
    request_with_retries,
)
from .config import ProviderSettings
from .merge import (
    REASON_PROVIDER_HTTP_ERROR,
    REASON_PROVIDER_INVALID_JSON,
    REASON_PROVIDER_NOT_CONFIGURED,
    REASON_PROVIDER_RATE_LIMITED,
    REASON_PROVIDER_REQUEST_REJECTED,
    REASON_PROVIDER_SCHEMA_INVALID,
    REASON_PROVIDER_SERVER_ERROR,
    REASON_PROVIDER_TIMEOUT,
    REASON_PROVIDER_UNAVAILABLE,
    REASON_PROVIDER_UNAUTHORIZED,
)

CHAT_PATH = "/chat/completions"
EMBEDDINGS_PATH = "/embeddings"

MAX_PROVIDER_MESSAGE_CHARS = 200


class ProviderError(Exception):
    """A provider call that could not produce usable output."""

    def __init__(
        self,
        message: str,
        *,
        reason: str = REASON_PROVIDER_HTTP_ERROR,
        retryable: bool = True,
    ) -> None:
        super().__init__(message)
        self.reason = reason
        self.retryable = retryable


class ProviderNotConfigured(ProviderError):
    """The key or the endpoint is absent: degrade to the rule baseline."""

    def __init__(
        self,
        message: str,
        *,
        reason: str = REASON_PROVIDER_NOT_CONFIGURED,
        retryable: bool = False,
    ) -> None:
        super().__init__(message, reason=reason, retryable=retryable)


def parse_json_object(text: str) -> dict[str, Any]:
    """Parse model content into a JSON object, tolerating a code fence."""

    candidate = _strip_code_fence(text)

    try:
        document = json.loads(candidate)
    except (json.JSONDecodeError, TypeError) as error:
        raise ProviderError(
            "provider chat content is not valid JSON",
            reason=REASON_PROVIDER_INVALID_JSON,
            retryable=True,
        ) from error

    if not isinstance(document, Mapping):
        raise ProviderError(
            "provider chat content must be a JSON object",
            reason=REASON_PROVIDER_SCHEMA_INVALID,
            retryable=True,
        )

    return dict(document)


def _strip_code_fence(text: str) -> str:
    candidate = text.strip()

    if not candidate.startswith("```"):
        return candidate

    lines = candidate.splitlines()

    if lines:
        lines = lines[1:]

    if lines and lines[-1].strip().startswith("```"):
        lines = lines[:-1]

    return "\n".join(lines).strip()


def cosine_similarity(left: Sequence[float], right: Sequence[float]) -> float:
    """Similarity in [-1, 1]; raises ValueError on unusable vectors."""

    if not left or not right or len(left) != len(right):
        raise ValueError("vectors must be non-empty and equally sized")

    dot = 0.0
    left_norm = 0.0
    right_norm = 0.0

    for left_value, right_value in zip(left, right):
        first = float(left_value)
        second = float(right_value)
        dot += first * second
        left_norm += first * first
        right_norm += second * second

    if left_norm <= 0.0 or right_norm <= 0.0:
        raise ValueError("vectors must have a non-zero magnitude")

    return dot / math.sqrt(left_norm * right_norm)


class ProviderClient:
    """OpenAI-compatible chat + embeddings with bounded retries."""

    def __init__(
        self,
        settings: ProviderSettings,
        *,
        transport: Transport | None = None,
        environ: Mapping[str, str] | None = None,
        timeout: float | None = None,
        policy: RetryPolicy | None = None,
        sleep: Callable[[float], None] = time.sleep,
        jitter: Callable[[], float] = random.random,
        user_agent: str = DEFAULT_USER_AGENT,
    ) -> None:
        self._settings = settings
        self._transport = transport or UrllibTransport(user_agent=user_agent)
        self._environ: Mapping[str, str] = (
            os.environ if environ is None else environ
        )
        self._timeout = (
            settings.timeout_seconds if timeout is None else float(timeout)
        )
        self._policy = policy or RetryPolicy(
            max_retries=settings.max_retries
        )
        self._sleep = sleep
        self._jitter = jitter
        self._user_agent = user_agent

    @property
    def settings(self) -> ProviderSettings:
        return self._settings

    @property
    def embedding_configured(self) -> bool:
        return self._settings.embedding_model is not None

    @property
    def configured(self) -> bool:
        """True when a call can be attempted (a key is not needed for local)."""

        try:
            self.resolve_key()
        except ProviderNotConfigured:
            return False

        return True

    def resolve_key(self) -> str | None:
        """Read the key from the environment; never from anywhere else."""

        name = self._settings.key_env

        if name is None:
            return None

        value = self._environ.get(name)

        if not isinstance(value, str) or not value.strip():
            raise ProviderNotConfigured(
                f"provider key environment variable {name} is not set; "
                "completing with the rule baseline only"
            )

        return value.strip()

    def chat(
        self,
        messages: Sequence[Mapping[str, Any]],
        *,
        temperature: float = 0.2,
        max_tokens: int = 800,
    ) -> str:
        document = self.post_json(
            CHAT_PATH,
            {
                "model": self._settings.model,
                "messages": [dict(message) for message in messages],
                "temperature": temperature,
                "max_tokens": max_tokens,
            },
        )
        choices = document.get("choices")

        if not isinstance(choices, list) or not choices:
            raise ProviderError(
                "provider chat response has no choices",
                reason=REASON_PROVIDER_SCHEMA_INVALID,
                retryable=True,
            )

        content: Any = None
        first = choices[0]

        if isinstance(first, Mapping):
            message = first.get("message")

            if isinstance(message, Mapping):
                content = message.get("content")

        if not isinstance(content, str) or not content.strip():
            raise ProviderError(
                "provider chat response has no message content",
                reason=REASON_PROVIDER_SCHEMA_INVALID,
                retryable=True,
            )

        return content

    def chat_json(
        self,
        messages: Sequence[Mapping[str, Any]],
        *,
        temperature: float = 0.2,
        max_tokens: int = 800,
    ) -> dict[str, Any]:
        return parse_json_object(
            self.chat(
                messages,
                temperature=temperature,
                max_tokens=max_tokens,
            )
        )

    def embed(self, texts: Sequence[str]) -> list[list[float]]:
        model = self._settings.embedding_model

        if model is None:
            raise ValueError("no embedding_model is configured")

        items = [text if isinstance(text, str) else str(text) for text in texts]

        if not items:
            raise ValueError("texts must not be empty")

        document = self.post_json(
            EMBEDDINGS_PATH,
            {"model": model, "input": items},
        )
        data = document.get("data")

        if not isinstance(data, list) or len(data) != len(items):
            raise ProviderError(
                "provider embeddings response does not match the request",
                reason=REASON_PROVIDER_SCHEMA_INVALID,
                retryable=True,
            )

        vectors: list[list[float]] = []

        for index, entry in enumerate(data):
            vector = (
                entry.get("embedding")
                if isinstance(entry, Mapping)
                else None
            )

            if not isinstance(vector, list) or not vector:
                raise ProviderError(
                    "provider embeddings response has no vector at "
                    f"index {index}",
                    reason=REASON_PROVIDER_SCHEMA_INVALID,
                    retryable=True,
                )

            try:
                vectors.append([float(value) for value in vector])
            except (TypeError, ValueError) as error:
                raise ProviderError(
                    f"provider embeddings vector {index} is not numeric",
                    reason=REASON_PROVIDER_SCHEMA_INVALID,
                    retryable=True,
                ) from error

        return vectors

    def post_json(
        self,
        path: str,
        document: Mapping[str, Any],
    ) -> dict[str, Any]:
        """POST a JSON document, retrying only 429/5xx/transport failures."""

        key = self.resolve_key()
        url = self._settings.base_url.rstrip("/") + path
        headers = {
            "Accept": "application/json",
            "Content-Type": "application/json",
            "User-Agent": self._user_agent,
        }

        if key is not None:
            headers["Authorization"] = f"Bearer {key}"

        body = json.dumps(document, ensure_ascii=False).encode("utf-8")

        try:
            response = request_with_retries(
                self._transport,
                method="POST",
                url=url,
                body=body,
                headers=headers,
                timeout=self._timeout,
                policy=self._policy,
                sleep=self._sleep,
                jitter=self._jitter,
            )
        except TransportError as error:
            reason = (
                REASON_PROVIDER_TIMEOUT
                if error.reason == "timeout"
                else REASON_PROVIDER_UNAVAILABLE
            )
            raise ProviderError(
                f"provider request to {url} failed: {error}",
                reason=reason,
                retryable=True,
            ) from error

        return self._decode(url, response)

    def _decode(self, url: str, response: Any) -> dict[str, Any]:
        if response.status < 200 or response.status >= 300:
            raise self._http_error(url, response)

        try:
            document = json.loads(response.body.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise ProviderError(
                f"provider response from {url} is not valid JSON "
                f"(HTTP {response.status})",
                reason=REASON_PROVIDER_INVALID_JSON,
                retryable=True,
            ) from error

        if not isinstance(document, Mapping):
            raise ProviderError(
                f"provider response from {url} must be a JSON object",
                reason=REASON_PROVIDER_SCHEMA_INVALID,
                retryable=True,
            )

        return dict(document)

    @staticmethod
    def _http_error(url: str, response: Any) -> ProviderError:
        detail = f"HTTP {response.status}"
        message: Any = None

        try:
            document = json.loads(response.body.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            document = None

        if isinstance(document, Mapping):
            raw = document.get("error")

            if isinstance(raw, Mapping) and isinstance(raw.get("message"), str):
                message = raw["message"]
            elif isinstance(document.get("message"), str):
                message = document["message"]

        if isinstance(message, str) and message.strip():
            detail += f": {message.strip()[:MAX_PROVIDER_MESSAGE_CHARS]}"

        text = f"provider request to {url} failed with {detail}"

        if response.status == 429:
            return ProviderError(
                text,
                reason=REASON_PROVIDER_RATE_LIMITED,
                retryable=True,
            )

        if response.status >= 500:
            return ProviderError(
                text,
                reason=REASON_PROVIDER_SERVER_ERROR,
                retryable=True,
            )

        if response.status in (401, 403):
            return ProviderError(
                text,
                reason=REASON_PROVIDER_UNAUTHORIZED,
                retryable=False,
            )

        return ProviderError(
            text,
            reason=REASON_PROVIDER_REQUEST_REJECTED,
            retryable=False,
        )
