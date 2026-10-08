"""Merge/validation: strict payloads, evidence binding, rule authorities."""

from __future__ import annotations

import copy
import unittest

from jobpilot_ai.merge import (
    MAX_EXPLANATION_CHARS,
    MAX_SUGGESTIONS,
    AiPayload,
    EvidenceIndex,
    MergeError,
    build_evidence_index,
    degraded_result,
    merge_result,
    validate_ai_metadata,
    validate_payload,
    validate_rule_result,
    verify_rule_authority,
)

from .support import input_bundle

BUNDLE = input_bundle()
RULE = BUNDLE["rule_result"]
SUPPORTED_EXPLANATION = "The rule baseline matched C++ and Redis."
SUPPORTED_SUGGESTION = "Add a Kubernetes project to your resume."
FABRICATED_EXPLANATION = "You led the Netflix streaming team for 12 years."
FABRICATED_SUGGESTION = "Ask your former Google manager for a referral."


class EvidenceIndexTest(unittest.TestCase):
    def test_structured_and_free_text_terms_are_evidence(self) -> None:
        index = build_evidence_index(BUNDLE)

        for term in ("c++", "redis", "kubernetes", "acme", "shanghai",
                     "billing", "warehouse"):
            with self.subTest(term=term):
                self.assertIn(term, index.terms)

    def test_anchors_are_reported_case_insensitively(self) -> None:
        index = build_evidence_index(BUNDLE)
        anchors = index.anchors(SUPPORTED_EXPLANATION)

        self.assertIn("c++", anchors)
        self.assertIn("redis", anchors)
        self.assertNotIn("the", anchors)
        self.assertNotIn("and", anchors)
        self.assertTrue(index.supports(SUPPORTED_EXPLANATION))

    def test_a_claim_referencing_nothing_is_unsupported(self) -> None:
        index = build_evidence_index(BUNDLE)

        self.assertEqual(index.anchors(FABRICATED_EXPLANATION), ())
        self.assertFalse(index.supports(FABRICATED_EXPLANATION))

    def test_numeric_only_tokens_are_not_evidence(self) -> None:
        index = build_evidence_index(BUNDLE)

        self.assertFalse(index.supports("20"))

    def test_stopwords_are_not_evidence(self) -> None:
        index = EvidenceIndex(frozenset({"the", "and"}))

        self.assertFalse(index.supports("the and"))

    def test_empty_bundle_has_no_evidence(self) -> None:
        self.assertFalse(build_evidence_index({}).supports("anything at all"))


class ValidateRuleResultTest(unittest.TestCase):
    def test_valid_baseline_is_copied(self) -> None:
        validated = validate_rule_result(RULE)

        self.assertIsNot(validated, RULE)
        validated["score"] = 1

        self.assertEqual(RULE["score"], 82)

    def test_missing_fields_are_rejected(self) -> None:
        for key in ("score", "score_version", "source", "breakdown",
                    "matched_skills", "missing_skills"):
            with self.subTest(key=key):
                broken = copy.deepcopy(RULE)
                del broken[key]

                with self.assertRaises(MergeError):
                    validate_rule_result(broken)

    def test_non_object_is_rejected(self) -> None:
        for value in (None, [], "rule"):
            with self.subTest(value=value):
                with self.assertRaises(MergeError):
                    validate_rule_result(value)

    def test_score_must_be_a_number_in_range(self) -> None:
        for value in ("82", True, None, -1, 101):
            with self.subTest(value=value):
                broken = copy.deepcopy(RULE)
                broken["score"] = value

                with self.assertRaises(MergeError):
                    validate_rule_result(broken)

    def test_breakdown_must_be_an_object(self) -> None:
        broken = copy.deepcopy(RULE)
        broken["breakdown"] = [1.0]

        with self.assertRaises(MergeError):
            validate_rule_result(broken)

    def test_skill_lists_must_be_strings(self) -> None:
        broken = copy.deepcopy(RULE)
        broken["matched_skills"] = [1]

        with self.assertRaises(MergeError):
            validate_rule_result(broken)

    def test_score_version_and_source_must_be_non_empty(self) -> None:
        for key in ("score_version", "source"):
            with self.subTest(key=key):
                broken = copy.deepcopy(RULE)
                broken[key] = "  "

                with self.assertRaises(MergeError):
                    validate_rule_result(broken)


class ValidatePayloadTest(unittest.TestCase):
    def test_valid_payload_is_parsed(self) -> None:
        payload = validate_payload(
            {
                "explanation": "  matched C++  ",
                "suggestions": ["Add Kubernetes.", "Add Kubernetes."],
            }
        )

        self.assertIsInstance(payload, AiPayload)
        self.assertEqual(payload.explanation, "matched C++")
        self.assertEqual(payload.suggestions, ("Add Kubernetes.",))

    def test_blank_explanation_is_treated_as_absent(self) -> None:
        self.assertIsNone(validate_payload({"explanation": "   "}).explanation)

    def test_payload_must_be_an_object(self) -> None:
        for value in (None, [], "text", 3):
            with self.subTest(value=value):
                with self.assertRaises(MergeError):
                    validate_payload(value)

    def test_deterministic_fields_are_rejected_by_name(self) -> None:
        for key in ("score", "breakdown", "matched_skills", "missing_skills",
                    "score_version", "source", "ai"):
            with self.subTest(key=key):
                with self.assertRaises(MergeError) as caught:
                    validate_payload({"explanation": "hi", key: 1})

                self.assertIn(key, str(caught.exception))
                self.assertIn("deterministic", str(caught.exception))

    def test_unknown_fields_are_rejected(self) -> None:
        with self.assertRaises(MergeError) as caught:
            validate_payload({"explanation": "hi", "confidence": 0.9})

        self.assertIn("confidence", str(caught.exception))

    def test_explanation_must_be_a_string(self) -> None:
        with self.assertRaises(MergeError):
            validate_payload({"explanation": 3})

    def test_explanation_length_is_bounded(self) -> None:
        with self.assertRaises(MergeError):
            validate_payload({"explanation": "x" * (MAX_EXPLANATION_CHARS + 1)})

    def test_suggestions_must_be_an_array_of_strings(self) -> None:
        for value in ("text", {"text": "x"}, [1], [""], [None]):
            with self.subTest(value=value):
                with self.assertRaises(MergeError):
                    validate_payload({"suggestions": value})

    def test_suggestions_are_bounded(self) -> None:
        with self.assertRaises(MergeError):
            validate_payload(
                {"suggestions": ["s"] * (MAX_SUGGESTIONS + 1)}
            )

    def test_suggestion_length_is_bounded(self) -> None:
        with self.assertRaises(MergeError):
            validate_payload({"suggestions": ["y" * 501]})

    def test_missing_keys_are_allowed(self) -> None:
        payload = validate_payload({})

        self.assertIsNone(payload.explanation)
        self.assertEqual(payload.suggestions, ())


class MergeResultTest(unittest.TestCase):
    def test_supported_claims_are_added(self) -> None:
        result = merge_result(
            BUNDLE,
            RULE,
            {
                "explanation": SUPPORTED_EXPLANATION,
                "suggestions": [SUPPORTED_SUGGESTION],
            },
        )

        self.assertEqual(result["explanation"], SUPPORTED_EXPLANATION)
        self.assertEqual(result["suggestions"], [SUPPORTED_SUGGESTION])
        self.assertEqual(result["ai"], {"degraded": False})

    def test_unreferenced_claims_are_dropped(self) -> None:
        result = merge_result(
            BUNDLE,
            RULE,
            {
                "explanation": FABRICATED_EXPLANATION,
                "suggestions": [FABRICATED_SUGGESTION, SUPPORTED_SUGGESTION],
            },
        )

        self.assertNotIn("explanation", result)
        self.assertEqual(result["suggestions"], [SUPPORTED_SUGGESTION])

    def test_all_claims_unreferenced_leaves_only_the_baseline(self) -> None:
        result = merge_result(
            BUNDLE,
            RULE,
            {
                "explanation": FABRICATED_EXPLANATION,
                "suggestions": [FABRICATED_SUGGESTION],
            },
        )

        self.assertNotIn("explanation", result)
        self.assertNotIn("suggestions", result)

    def test_deterministic_fields_are_never_overwritten(self) -> None:
        result = merge_result(
            BUNDLE,
            RULE,
            {"explanation": SUPPORTED_EXPLANATION},
        )

        for key in ("score_version", "score", "source", "breakdown",
                    "matched_skills", "missing_skills"):
            with self.subTest(key=key):
                self.assertEqual(result[key], RULE[key])

    def test_the_caller_baseline_is_not_mutated(self) -> None:
        rule = copy.deepcopy(RULE)
        before = copy.deepcopy(rule)

        merge_result(BUNDLE, rule, {"explanation": SUPPORTED_EXPLANATION})

        self.assertEqual(rule, before)

    def test_protected_key_in_the_payload_is_rejected(self) -> None:
        with self.assertRaises(MergeError):
            merge_result(BUNDLE, RULE, {"score": 99})

    def test_ai_metadata_is_validated(self) -> None:
        result = merge_result(
            BUNDLE,
            RULE,
            {"explanation": SUPPORTED_EXPLANATION},
            ai_metadata={"degraded": True, "reason": "embeddings_unavailable"},
        )

        self.assertEqual(
            result["ai"],
            {"degraded": True, "reason": "embeddings_unavailable"},
        )

    def test_explicit_evidence_index_is_used(self) -> None:
        result = merge_result(
            BUNDLE,
            RULE,
            {"explanation": FABRICATED_EXPLANATION},
            evidence=EvidenceIndex(frozenset({"netflix"})),
        )

        self.assertEqual(result["explanation"], FABRICATED_EXPLANATION)

    def test_unusable_baseline_is_rejected(self) -> None:
        with self.assertRaises(MergeError):
            merge_result(BUNDLE, {"score": 1}, {"explanation": "x"})


class ValidateAiMetadataTest(unittest.TestCase):
    def test_default_is_not_degraded(self) -> None:
        self.assertEqual(validate_ai_metadata(None), {"degraded": False})
        self.assertEqual(validate_ai_metadata({}), {"degraded": False})

    def test_unknown_keys_are_rejected(self) -> None:
        with self.assertRaises(MergeError):
            validate_ai_metadata({"model": "fixture-model"})

    def test_degraded_must_be_a_boolean(self) -> None:
        with self.assertRaises(MergeError):
            validate_ai_metadata({"degraded": "yes"})

    def test_degraded_requires_a_reason_code(self) -> None:
        with self.assertRaises(MergeError):
            validate_ai_metadata({"degraded": True})

    def test_free_text_reasons_are_rejected(self) -> None:
        with self.assertRaises(MergeError):
            validate_ai_metadata(
                {"degraded": True, "reason": "the model said: sk-abc"}
            )

    def test_scores_are_bounded_and_rounded(self) -> None:
        metadata = validate_ai_metadata(
            {"embedding_score": 0.87654321, "llm_score": 1}
        )

        self.assertEqual(metadata["embedding_score"], 0.8765)
        self.assertEqual(metadata["llm_score"], 1.0)

    def test_scores_out_of_range_are_rejected(self) -> None:
        for value in (-0.1, 1.1, float("nan"), True, "0.5"):
            with self.subTest(value=value):
                with self.assertRaises(MergeError):
                    validate_ai_metadata({"embedding_score": value})


class VerifyRuleAuthorityTest(unittest.TestCase):
    def test_unchanged_fields_pass(self) -> None:
        verify_rule_authority(RULE, copy.deepcopy(RULE))

    def test_changed_fields_are_caught(self) -> None:
        tampered = copy.deepcopy(RULE)
        tampered["score"] = 99

        with self.assertRaises(MergeError) as caught:
            verify_rule_authority(RULE, tampered)

        self.assertIn("score", str(caught.exception))


class DegradedResultTest(unittest.TestCase):
    def test_degraded_result_keeps_the_baseline(self) -> None:
        result = degraded_result(RULE, "provider_not_configured")

        self.assertEqual(result["score"], RULE["score"])
        self.assertEqual(result["matched_skills"], RULE["matched_skills"])
        self.assertEqual(
            result["ai"],
            {"degraded": True, "reason": "provider_not_configured"},
        )

    def test_extra_ai_metadata_is_preserved(self) -> None:
        result = degraded_result(
            RULE,
            "provider_timeout",
            ai_metadata={"embedding_score": 0.5},
        )

        self.assertEqual(result["ai"]["embedding_score"], 0.5)
        self.assertTrue(result["ai"]["degraded"])

    def test_unknown_reason_codes_are_rejected(self) -> None:
        with self.assertRaises(MergeError):
            degraded_result(RULE, "the provider exploded")

    def test_the_caller_baseline_is_not_mutated(self) -> None:
        rule = copy.deepcopy(RULE)

        degraded_result(rule, "provider_timeout")

        self.assertNotIn("ai", rule)


if __name__ == "__main__":
    unittest.main()
