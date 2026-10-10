"""Charset precedence tests: BOM > HTTP header > <meta> > utf-8."""

from __future__ import annotations

import unittest

from jobpilot_sources.decoding import (
    decode_body,
    decode_html_entities,
    charset_from_bom,
    charset_from_content_type,
    charset_from_meta,
    normalize_label,
    sniff_charset,
)

UTF8_CHINESE = "中文招聘".encode("utf-8")
GBK_CHINESE = "中文招聘".encode("gb18030")


class LabelTest(unittest.TestCase):
    def test_legacy_labels_normalize_to_gb18030(self) -> None:
        self.assertEqual(normalize_label("GBK"), "gb18030")
        self.assertEqual(normalize_label("gb2312"), "gb18030")
        self.assertEqual(normalize_label("x-gbk"), "gb18030")
        self.assertEqual(normalize_label("utf8"), "utf-8")

    def test_unknown_and_empty_labels(self) -> None:
        self.assertIsNone(normalize_label("not-a-charset"))
        self.assertIsNone(normalize_label(""))
        self.assertIsNone(normalize_label(None))

    def test_content_type_extraction(self) -> None:
        self.assertEqual(
            charset_from_content_type("text/html; charset=GBK"), "gb18030"
        )
        self.assertIsNone(charset_from_content_type("text/html"))


class PrecedenceTest(unittest.TestCase):
    def test_bom_beats_header_and_meta(self) -> None:
        payload = b"\xef\xbb\xbf" + UTF8_CHINESE
        body = (
            b"<html><head><meta charset=gbk></head><body>"
            + UTF8_CHINESE
            + b"</body></html>"
        )

        self.assertEqual(charset_from_bom(payload), "utf-8-sig")
        self.assertEqual(
            decode_body(
                payload + body,
                content_type="text/html; charset=gbk",
            ).count("中文招聘"),
            2,
        )

    def test_header_beats_meta(self) -> None:
        body = (
            b"<html><head><meta charset=big5></head><body>"
            + UTF8_CHINESE
            + b"</body></html>"
        )

        self.assertIn("中文招聘", decode_body(body, content_type="text/html; charset=utf-8"))

    def test_meta_is_used_without_bom_or_header(self) -> None:
        body = (
            b"<html><head><meta charset=gb2312></head><body>"
            + GBK_CHINESE
            + b"</body></html>"
        )

        self.assertIn("中文招聘", decode_body(body))

    def test_http_equiv_meta_form(self) -> None:
        body = (
            b'<meta http-equiv="Content-Type" content="text/html; charset=gbk">'
            + GBK_CHINESE
        )

        self.assertEqual(charset_from_meta(body), "gb18030")
        self.assertIn("中文招聘", decode_body(body))

    def test_utf8_default_and_sniff(self) -> None:
        self.assertEqual(sniff_charset(UTF8_CHINESE), "utf-8")
        self.assertEqual(decode_body(UTF8_CHINESE), "中文招聘")

    def test_unknown_charset_falls_back_without_raising(self) -> None:
        body = b"<meta charset=x-unknown-9>" + UTF8_CHINESE

        self.assertEqual(
            decode_body(body),
            "<meta charset=x-unknown-9>中文招聘",
        )

    def test_invalid_bytes_never_raise(self) -> None:
        # No BOM, not valid UTF-8: replacement characters, never an exception.
        result = decode_body(b"\xff\xff\xff\xff")

        self.assertIsInstance(result, str)
        self.assertTrue(result)
        self.assertEqual(decode_body(b""), "")

    def test_utf16_bom_is_honoured(self) -> None:
        payload = "<meta charset=gbk><p>中文</p>".encode("utf-16")

        self.assertEqual(
            sniff_charset(payload, content_type="text/html; charset=gbk"),
            "utf-16",
        )
        self.assertIn("中文", decode_body(payload))

    def test_non_bytes_input_is_empty(self) -> None:
        self.assertEqual(decode_body(None), "")

    def test_html_entity_decoding(self) -> None:
        self.assertEqual(
            decode_html_entities("C&amp;C &lt;dev&gt; &#20013;"),
            "C&C <dev> 中",
        )
        self.assertEqual(decode_html_entities(None), "")


if __name__ == "__main__":
    unittest.main()
