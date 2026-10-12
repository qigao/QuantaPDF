"""Synthetic and immutable original-source scanner tests; NOT pixel conformance."""
import json
import base64
import hashlib
import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "qualification"))
from screen_external_wstar import (
    screen_pdf_bytes, screen_pdf_file, main, _asciihex_bounded
)

REAL_SOURCES = Path(__file__).resolve().parent / "external_source_corpus" / "documents"
NONZERO = REAL_SOURCES / "pdfium.clip-path-v1.pdf"
WSTAR_TEXT = REAL_SOURCES / "pdfium.bug-1574-wstar-text-v1.pdf"


def pdf(content, compress=False, filter_token=b"/FlateDecode", ascii85=False,
        ascii85_pad=False, asciihex=False):
    body = zlib.compress(content) if compress else content
    if ascii85 and asciihex:
        raise ValueError("choose exactly one ASCII armoring")
    if ascii85:
        body = base64.a85encode(body, adobe=True)[2:]
    if asciihex:
        body = body.hex().encode("ascii") + b">"
    if ascii85_pad:
        if not ascii85:
            raise ValueError("ASCII85 padding requires ASCII85")
        body += b" \r\n"
    filt = b"/Filter " + filter_token + b" " if compress or ascii85 or asciihex else b""
    return (b"%PDF-1.7\n1 0 obj << /Length " + str(len(body)).encode("ascii") +
            b" " + filt + b">>\nstream\n" + body + b"\nendstream\n"
            b"endobj\ntrailer << /Size 2 >>\n%%EOF\n")


def pdf_indirect_length(content, compress=False, referenced_value=None,
                        extra_object=b""):
    body = zlib.compress(content) if compress else content
    original = pdf(content, compress=compress)
    original = original.replace(
        b"/Length " + str(len(body)).encode("ascii"),
        b"/Length 2 0 R", 1)
    value = len(body) if referenced_value is None else referenced_value
    extra = (b"2 0 obj\n" + str(value).encode("ascii") +
             b"\nendobj\n" + extra_object)
    return original.replace(b"trailer", extra + b"trailer", 1)


class ExternalWStarScreening(unittest.TestCase):
    def test_real_original_w_is_not_wstar(self):
        # Exact unchanged independently published PDFium source admitted in
        # #260. The W clipping positive MUST NOT raise the W* counter.
        src = NONZERO.read_bytes()
        self.assertEqual(len(src), 514)
        self.assertEqual(hashlib.sha256(src).hexdigest(),
                         "6674dbd092ef8ae55b6ab1cda6a8b966019fbf22f7556291253853303072288b")
        info = screen_pdf_file(NONZERO)
        self.assertEqual(info["status"], "no_wstar_observed")
        self.assertEqual(info["operator_counts"]["w"], 1)
        self.assertEqual(info["operator_counts"]["wstar"], 0)
        self.assertGreaterEqual(info["operator_counts"]["path_commands"], 4)
        self.assertEqual(info["native_wstar_clip_supported_count"], 0)
        self.assertEqual(info["native_pixel_conformance"], "not_run")

    def test_real_original_wstar_with_late_text_is_not_positive(self):
        # The original PDFium source from #264 contains W* n + f*
        # followed by later font/text operators. Report rejection evidence,
        # NOT a successful native W* pixel rendering.
        src = WSTAR_TEXT.read_bytes()
        self.assertEqual(len(src), 724)
        self.assertEqual(hashlib.sha256(src).hexdigest(),
                         "900d0ff1911736c372e0c7ead2c68be338479b985c676572db1cc6e35483b890")
        info = screen_pdf_file(WSTAR_TEXT)
        self.assertEqual(info["status"], "wstar_with_unsupported_operations")
        self.assertEqual(info["operator_counts"]["wstar"], 1)
        self.assertEqual(info["operator_counts"]["w"], 0)
        self.assertGreaterEqual(info["operator_counts"]["paint"], 1)
        self.assertIn("text", info["unsupported_feature_classes"])
        self.assertGreater(info["unsupported_feature_counts"]["text"], 0)
        self.assertEqual(info["native_wstar_clip_supported_count"], 0)
        self.assertEqual(info["native_pixel_conformance"], "not_run")

    def test_real_source_multi_document_screening_keeps_bytes(self):
        # The two pinned originals are not modified or re-rendered by the
        # scanner; it does not claim page resolution or native parity.
        sources = (NONZERO, WSTAR_TEXT)
        before = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                  for path in sources}
        with tempfile.TemporaryDirectory() as directory:
            report_path = Path(directory) / "screen-originals.json"
            self.assertEqual(main(["--pdf", str(WSTAR_TEXT), "--pdf",
                                   str(NONZERO), "--report", str(report_path)]), 0)
            report = json.loads(report_path.read_text())
            self.assertEqual(report["result"], "screening_only")
            self.assertEqual(report["native_wstar_clip_supported_count"], 0)
            docs = {doc["filename"]: doc for doc in report["documents"]}
            self.assertEqual(set(docs), set(before))
            for path in sources:
                self.assertEqual(docs[path.name]["sha256"], before[path.name])
                self.assertEqual(docs[path.name]["page_content_attribution"],
                                 "not_verified")
                self.assertEqual(docs[path.name]["license_review"], "required")
                self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),
                                 before[path.name])

    def test_path_clip_candidate_is_screening_only(self):
        doc = screen_pdf_bytes(pdf(b"q 1 1 3 3 re W* n 1 0 0 rg 0 0 5 5 re f Q"))
        self.assertEqual(doc["status"], "candidate_requires_full_page_preflight")
        self.assertEqual(doc["operator_counts"]["wstar"], 1)
        self.assertEqual(doc["native_wstar_clip_supported_count"], 0)
        self.assertEqual(doc["native_pixel_conformance"], "not_run")

    def test_nested_decodeparms_does_not_hide_flate_filter(self):
        # /Filter belongs to the outer stream dictionary even when the
        # closest << is a nested /DecodeParms dictionary.
        content = b"q 0 0 10 10 re W* n 0 0 12 12 re f Q"
        source = pdf(content, compress=True).replace(
            b" >>\nstream",
            b" /DecodeParms << /Predictor 1 >> >>\nstream",
            1,
        )
        result = screen_pdf_bytes(source)
        self.assertEqual(result["status"], "candidate_requires_full_page_preflight")
        self.assertEqual(result["operator_counts"]["wstar"], 1)
        self.assertEqual(result["decoded_stream_count"], 1)

    def test_embedded_endstream_word_does_not_truncate_real_content(self):
        original = pdf(
            b"q 0 0 10 10 re W* n (endstream) 0 0 12 12 re f Q"
        )
        result = screen_pdf_bytes(original)
        self.assertEqual(result["status"], "candidate_requires_full_page_preflight")
        self.assertEqual(result["operator_counts"]["wstar"], 1)
        self.assertEqual(result["operator_counts"]["paint"], 1)

    def test_indirect_stream_length_is_inconclusive(self):
        original = pdf(b"0 0 10 10 re W* n 0 0 12 12 re f")
        source = __import__("re").sub(
            rb"/Length \d+", b"/Length 5 0 R", original, count=1
        )
        result = screen_pdf_bytes(source)
        self.assertEqual(result["status"], "inconclusive")
        self.assertIn("indirect_or_ambiguous_length", result["stream_issues"])

    def test_unique_indirect_stream_length_discovers_wstar(self):
        content = b"q 0 0 10 10 re W* n 0 0 12 12 re f Q"
        original = pdf_indirect_length(content, compress=True)
        before = hashlib.sha256(original).hexdigest()
        report = screen_pdf_bytes(original)
        self.assertEqual(report["status"], "candidate_requires_full_page_preflight")
        self.assertEqual(report["operator_counts"]["wstar"], 1)
        self.assertEqual(report["decoded_stream_count"], 1)
        self.assertEqual(report["native_wstar_clip_supported_count"], 0)
        self.assertEqual(report["native_pixel_conformance"], "not_run")
        self.assertEqual(hashlib.sha256(original).hexdigest(), before)

    def test_duplicate_indirect_length_objects_are_inconclusive(self):
        original = pdf_indirect_length(
            b"0 0 10 10 re W* n 0 0 12 12 re f",
            extra_object=b"2 0 obj\n40\nendobj\n")
        report = screen_pdf_bytes(original)
        self.assertEqual(report["status"], "inconclusive")
        self.assertIn("indirect_or_ambiguous_length", report["stream_issues"])

    def test_complex_indirect_length_object_is_inconclusive(self):
        original = pdf_indirect_length(b"0 0 10 10 re W* n 0 0 12 12 re f")
        original = original.replace(
            b"2 0 obj\n", b"2 0 obj\n<< /Value 20 >>\n", 1)
        report = screen_pdf_bytes(original)
        self.assertEqual(report["status"], "inconclusive")
        self.assertIn("indirect_or_ambiguous_length", report["stream_issues"])

    def test_wrong_indirect_length_boundary_is_inconclusive(self):
        content = b"0 0 10 10 re W* n 0 0 12 12 re f"
        original = pdf_indirect_length(content, referenced_value=len(content)-1)
        report = screen_pdf_bytes(original)
        self.assertEqual(report["status"], "inconclusive")
        self.assertIn("stream_length_or_boundary_mismatch", report["stream_issues"])

    def test_missing_indirect_length_object_is_inconclusive(self):
        original = pdf_indirect_length(b"0 0 10 10 re W* n 0 0 12 12 re f")
        original = original.replace(b"2 0 obj\n", b"3 0 obj\n", 1)
        report = screen_pdf_bytes(original)
        self.assertEqual(report["status"], "inconclusive")
        self.assertIn("indirect_or_ambiguous_length", report["stream_issues"])

    def test_stream_length_boundary_mismatch_is_inconclusive(self):
        original = pdf(b"0 0 10 10 re W* n 0 0 12 12 re f")
        source = __import__("re").sub(
            rb"/Length \d+", b"/Length 1", original, count=1
        )
        result = screen_pdf_bytes(source)
        self.assertEqual(result["status"], "inconclusive")
        self.assertIn("stream_length_or_boundary_mismatch", result["stream_issues"])

    def test_png_predictor_is_inconclusive_not_no_wstar(self):
        original = pdf(b"q 0 0 3 3 re W* n 0 0 4 4 re f Q",
                       compress=True)
        original = original.replace(
            b" >>\nstream",
            b" /DecodeParms << /Predictor 12 /Columns 4 >> >>\nstream",
            1,
        )
        report = screen_pdf_bytes(original)
        self.assertEqual(report["status"], "inconclusive")
        self.assertIn("unsupported_stream_filter", report["stream_issues"])

    def test_indirect_decodeparms_is_inconclusive(self):
        original = pdf(b"q 0 0 3 3 re W* n 0 0 4 4 re f Q",
                       compress=True)
        original = original.replace(
            b" >>\nstream", b" /DecodeParms 9 0 R >>\nstream", 1,
        )
        report = screen_pdf_bytes(original)
        self.assertEqual(report["status"], "inconclusive")
        self.assertIn("unsupported_stream_filter", report["stream_issues"])

    def test_absent_stream_is_inconclusive(self):
        report = screen_pdf_bytes(b"%PDF-1.7\n%%EOF\n")
        self.assertEqual(report["status"], "inconclusive")
        self.assertIn("no_streams_found", report["stream_issues"])

    def test_declared_ascii85_flate_with_unencoded_payload_fails_closed(self):
        # Flate bytes are NOT ASCII85 just because the dictionary declares
        # a supported filter chain. Never reinterpret the payload as Flate
        # or convert a malformed original into a candidate success.
        report = screen_pdf_bytes(pdf(
            b"0 0 10 10 re W* n 0 0 12 12 re f",
            compress=True,
            filter_token=b"[/ASCII85Decode /FlateDecode]",
        ))
        self.assertEqual(report["status"], "inconclusive")
        self.assertIn("ascii85_decode_failed_or_over_budget", report["stream_issues"])
        self.assertEqual(report["decoded_stream_count"], 0)
        self.assertEqual(report["native_wstar_clip_supported_count"], 0)

    def test_ascii85_without_valid_flate_payload_fails_closed(self):
        source = pdf(
            b"q 0 0 10 10 re W* n 0 0 12 12 re f Q",
            compress=False, ascii85=True,
            filter_token=b"[/ASCII85Decode /FlateDecode]",
        )
        report = screen_pdf_bytes(source)
        self.assertEqual(report["status"], "inconclusive")
        self.assertIn("ascii85_decode_failed_or_over_budget", report["stream_issues"])
        self.assertEqual(report["operator_counts"]["wstar"], 0)
        self.assertEqual(report["native_pixel_conformance"], "not_run")

    def test_ascii85_flate_array_discovers_wstar(self):
        source = pdf(b"q 0 0 10 10 re W* n 1 0 0 rg 0 0 12 12 re f Q",
                     compress=True,
                     filter_token=b"[/ASCII85Decode /FlateDecode]",
                     ascii85=True)
        before = hashlib.sha256(source).hexdigest()
        report = screen_pdf_bytes(source)
        self.assertEqual(report["status"], "candidate_requires_full_page_preflight")
        self.assertEqual(report["decoded_stream_count"], 1)
        self.assertEqual(report["operator_counts"]["wstar"], 1)
        self.assertEqual(report["native_wstar_clip_supported_count"], 0)
        self.assertEqual(report["native_pixel_conformance"], "not_run")
        self.assertEqual(hashlib.sha256(source).hexdigest(), before)

    def test_ascii85_single_filter_discovers_wstar(self):
        source = pdf(b"q 0 0 10 10 re W* n 0 0 12 12 re f Q",
                     filter_token=b"/ASCII85Decode", ascii85=True)
        report = screen_pdf_bytes(source)
        self.assertEqual(report["status"], "candidate_requires_full_page_preflight")
        self.assertEqual(report["operator_counts"]["wstar"], 1)

    def test_ascii85_terminal_whitespace_is_ignored(self):
        source = pdf(b"q 0 0 10 10 re W* n 0 0 12 12 re f Q",
                     filter_token=b"/ASCII85Decode", ascii85=True,
                     ascii85_pad=True)
        report = screen_pdf_bytes(source)
        self.assertEqual(report["status"], "candidate_requires_full_page_preflight")
        self.assertEqual(report["operator_counts"]["wstar"], 1)

    def test_ascii85_flate_reversed_filters_stay_inconclusive(self):
        source = pdf(b"q 0 0 10 10 re W* n 0 0 12 12 re f Q",
                     compress=True,
                     filter_token=b"[/FlateDecode /ASCII85Decode]",
                     ascii85=True)
        report = screen_pdf_bytes(source)
        self.assertEqual(report["status"], "inconclusive")
        self.assertIn("unsupported_stream_filter", report["stream_issues"])

    def test_ascii85_flate_decodeparms_are_inconclusive(self):
        source = pdf(b"q 0 0 10 10 re W* n 0 0 12 12 re f Q",
                     compress=True,
                     filter_token=b"[/ASCII85Decode /FlateDecode]",
                     ascii85=True).replace(
                         b" >>\nstream",
                         b" /DecodeParms [null << /Predictor 1 >>] >>\nstream", 1)
        report = screen_pdf_bytes(source)
        self.assertEqual(report["status"], "inconclusive")
        self.assertIn("unsupported_stream_filter", report["stream_issues"])

    def test_asciihex_single_original_stream_is_screening_only(self):
        original = pdf(
            b"q 0 0 10 10 re W* n 1 0 0 rg 0 0 11 11 re f Q",
            filter_token=b"/ASCIIHexDecode", asciihex=True)
        digest = hashlib.sha256(original).hexdigest()
        report = screen_pdf_bytes(original)
        self.assertEqual(report["status"], "candidate_requires_full_page_preflight")
        self.assertEqual(report["decoded_stream_count"], 1)
        self.assertEqual(report["operator_counts"]["wstar"], 1)
        self.assertEqual(report["native_wstar_clip_supported_count"], 0)
        self.assertEqual(report["native_pixel_conformance"], "not_run")
        self.assertEqual(hashlib.sha256(original).hexdigest(), digest)

    def test_asciihex_flate_chain_decodes_in_declared_order(self):
        original = pdf(
            b"q 0 0 10 10 re W* n 0 0 11 11 re f Q",
            compress=True, asciihex=True,
            filter_token=b"[/ASCIIHexDecode /FlateDecode]")
        report = screen_pdf_bytes(original)
        self.assertEqual(report["status"], "candidate_requires_full_page_preflight")
        self.assertEqual(report["operator_counts"]["wstar"], 1)
        self.assertEqual(report["decoded_stream_count"], 1)
        self.assertEqual(report["native_wstar_clip_supported_count"], 0)

    def test_asciihex_mixed_case_whitespace_and_odd_nibble(self):
        self.assertEqual(_asciihex_bounded(b"57 2a\r\n 6e > \t"), b"W*n")
        self.assertEqual(_asciihex_bounded(b"F>"), b"\xf0")
        self.assertEqual(_asciihex_bounded(b"aB C>"), b"\xab\xc0")

    def test_asciihex_invalid_character_fails_closed(self):
        original = pdf(
            b"q 0 0 10 10 re W* n 0 0 11 11 re f",
            filter_token=b"/ASCIIHexDecode", asciihex=True)
        mutated = bytearray(original)
        start = mutated.index(b"stream\n") + len(b"stream\n")
        mutated[start] = ord("Z")
        report = screen_pdf_bytes(bytes(mutated))
        self.assertEqual(report["status"], "inconclusive")
        self.assertIn("asciihex_decode_failed_or_over_budget", report["stream_issues"])
        self.assertEqual(report["native_wstar_clip_supported_count"], 0)

    def test_asciihex_missing_terminator_fails_closed(self):
        original = pdf(
            b"q 0 0 10 10 re W* n 0 0 11 11 re f",
            filter_token=b"/ASCIIHexDecode", asciihex=True)
        mutated = original.replace(b">\nendstream", b"!\nendstream", 1)
        self.assertNotEqual(mutated, original)
        report = screen_pdf_bytes(mutated)
        self.assertEqual(report["status"], "inconclusive")
        self.assertIn("asciihex_decode_failed_or_over_budget", report["stream_issues"])

    def test_asciihex_flate_invalid_inner_payload_fails_closed(self):
        original = pdf(
            b"q 0 0 10 10 re W* n 0 0 11 11 re f",
            asciihex=True,
            filter_token=b"[/ASCIIHexDecode /FlateDecode]")
        report = screen_pdf_bytes(original)
        self.assertEqual(report["status"], "inconclusive")
        self.assertIn("asciihex_decode_failed_or_over_budget", report["stream_issues"])
        self.assertEqual(report["operator_counts"]["wstar"], 0)

    def test_asciihex_reversed_filter_order_stays_inconclusive(self):
        original = pdf(
            b"q 0 0 10 10 re W* n 0 0 11 11 re f",
            compress=True, asciihex=True,
            filter_token=b"[/FlateDecode /ASCIIHexDecode]")
        report = screen_pdf_bytes(original)
        self.assertEqual(report["status"], "inconclusive")
        self.assertIn("unsupported_stream_filter", report["stream_issues"])

    def test_asciihex_decodeparms_array_stays_inconclusive(self):
        original = pdf(
            b"q 0 0 10 10 re W* n 0 0 11 11 re f",
            compress=True, asciihex=True,
            filter_token=b"[/ASCIIHexDecode /FlateDecode]")
        amended = original.replace(
            b" >>\nstream",
            b" /DecodeParms [null << /Predictor 1 >>] >>\nstream", 1)
        self.assertNotEqual(amended, original)
        report = screen_pdf_bytes(amended)
        self.assertEqual(report["status"], "inconclusive")
        self.assertIn("unsupported_stream_filter", report["stream_issues"])

    def test_single_filter_array_accepts_whitespace(self):
        original = pdf(b"q 0 0 10 10 re W* n 0 0 12 12 re f Q",
                       compress=True,
                       filter_token=b"[ /FlateDecode\n ]")
        report = screen_pdf_bytes(original)
        self.assertEqual(report["status"], "candidate_requires_full_page_preflight")
        self.assertEqual(report["operator_counts"]["wstar"], 1)

    def test_missing_endstream_is_inconclusive(self):
        original = pdf(b"0 0 10 10 re W* n 0 0 12 12 re f")
        result = screen_pdf_bytes(original.replace(b"endstream", b"endstram"))
        self.assertEqual(result["status"], "inconclusive")
        self.assertIn("stream_length_or_boundary_mismatch", result["stream_issues"])

    def test_fstar_fill_is_not_evenodd_clip(self):
        doc = screen_pdf_bytes(pdf(b"q 0 0 10 10 re f* Q"))
        self.assertEqual(doc["status"], "no_wstar_observed")
        self.assertEqual(doc["operator_counts"]["wstar"], 0)

    def test_late_text_rejected_for_positive_screening(self):
        doc = screen_pdf_bytes(pdf(b"q 0 0 5 5 re W* n 0 0 5 5 re f BT ET Q"))
        self.assertEqual(doc["status"], "wstar_with_unsupported_operations")
        self.assertIn("text", doc["unsupported_feature_classes"])

    def test_xobject_rejected_for_positive_screening(self):
        doc = screen_pdf_bytes(pdf(b"q 0 0 5 5 re W* n 0 0 5 5 re f /Im7 Do Q"))
        self.assertEqual(doc["status"], "wstar_with_unsupported_operations")
        self.assertIn("xobject", doc["unsupported_feature_classes"])

    def test_flate_is_screened_without_modifying_original(self):
        original = pdf(b"q 1 1 3 3 re W* n 1 0 0 rg 0 0 5 5 re f Q", compress=True)
        doc = screen_pdf_bytes(original)
        self.assertEqual(doc["status"], "candidate_requires_full_page_preflight")
        self.assertEqual(doc["decoded_stream_count"], 1)

    def test_strings_comments_names_do_not_count_as_operator(self):
        doc = screen_pdf_bytes(pdf(b"% W* n\n(W* in string (W*)) /W* <572a> 0 0 2 2 re f"))
        self.assertEqual(doc["operator_counts"]["wstar"], 0)

    def test_unknown_filter_is_inconclusive(self):
        doc = screen_pdf_bytes(pdf(b"q 0 0 5 5 re W* n f Q", True, b"/LZWDecode"))
        self.assertEqual(doc["status"], "inconclusive")
        self.assertIn("unsupported_stream_filter", doc["stream_issues"])

    def test_incomplete_flate_is_inconclusive(self):
        doc = screen_pdf_bytes(pdf(b"not zlib", True).replace(b"x\x9c", b"x\x00", 1))
        self.assertEqual(doc["status"], "inconclusive")

    def test_bounded_decode_exceedance_is_inconclusive(self):
        doc = screen_pdf_bytes(pdf(b"A" * (1024 * 1024 + 2), compress=True))
        self.assertEqual(doc["status"], "inconclusive")

    def test_missing_paint_does_not_qualify(self):
        doc = screen_pdf_bytes(pdf(b"0 0 5 5 re W* n"))
        self.assertEqual(doc["status"], "wstar_without_observed_path_paint")

    def test_file_hash_and_atomic_report(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory) / "source.pdf"
            report = Path(directory) / "screen.json"
            p.write_bytes(pdf(b"0 0 5 5 re W* n 0 0 5 5 re f"))
            self.assertEqual(main(["--pdf", str(p), "--report", str(report)]), 0)
            result = json.loads(report.read_text())
            self.assertEqual(result["result"], "screening_only")
            self.assertEqual(result["documents"][0]["filename"], "source.pdf")
            self.assertEqual(len(result["documents"][0]["sha256"]), 64)
            self.assertEqual(result["native_wstar_clip_supported_count"], 0)

    def test_report_collision_preserves_original(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.pdf"
            original = pdf(b"0 0 5 5 re W* n 0 0 5 5 re f")
            source.write_bytes(original)
            with contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    main(["--pdf", str(source), "--report", str(source)])
            self.assertEqual(source.read_bytes(), original)

    def test_invalid_source_preserves_preexisting_report(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "invalid.pdf"
            source.write_bytes(b"not a PDF")
            report = Path(directory) / "result.json"
            report.write_text('{"existing":"do-not-delete"}')
            with contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    main(["--pdf", str(source), "--report", str(report)])
            self.assertEqual(report.read_text(), '{"existing":"do-not-delete"}')
            self.assertFalse(Path(str(report) + ".tmp").exists())

    def test_report_hardlink_to_original_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            original = Path(directory) / "original.pdf"
            original.write_bytes(NONZERO.read_bytes())
            report = Path(directory) / "alias.json"
            try:
                import os
                os.link(original, report)
            except (OSError, NotImplementedError):
                self.skipTest("hardlinks unavailable")
            before = hashlib.sha256(original.read_bytes()).hexdigest()
            with contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    main(["--pdf", str(original), "--report", str(report)])
            self.assertEqual(hashlib.sha256(original.read_bytes()).hexdigest(), before)
            self.assertEqual(report.read_bytes(), original.read_bytes())

    def test_pdf_dir_screens_unchanged_nested_pinned_originals(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "corpus"
            (root / "a").mkdir(parents=True)
            (root / "b").mkdir()
            w = root / "a" / NONZERO.name
            star = root / "b" / WSTAR_TEXT.name
            w.write_bytes(NONZERO.read_bytes())
            star.write_bytes(WSTAR_TEXT.read_bytes())
            (root / "notes.txt").write_text("unrelated file")
            before = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                      for p in (w, star)}
            output = Path(directory) / "screen.json"
            self.assertEqual(main(["--pdf-dir", str(root),
                                   "--report", str(output)]), 0)
            report = json.loads(output.read_text())
            documents = {doc["filename"]: doc for doc in report["documents"]}
            self.assertEqual(set(documents), set(before))
            for p in (w, star):
                self.assertEqual(documents[p.name]["sha256"], before[p.name])
                self.assertEqual(hashlib.sha256(p.read_bytes()).hexdigest(),
                                 before[p.name])
                self.assertEqual(documents[p.name]["native_pixel_conformance"],
                                 "not_run")
            self.assertEqual(report["native_wstar_clip_supported_count"], 0)

    def test_pdf_dir_rejects_duplicate_basenames(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "corpus"
            (root / "a").mkdir(parents=True)
            (root / "b").mkdir()
            (root / "a" / "duplicate.pdf").write_bytes(NONZERO.read_bytes())
            (root / "b" / "duplicate.pdf").write_bytes(WSTAR_TEXT.read_bytes())
            report = Path(directory) / "result.json"
            with contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    main(["--pdf-dir", str(root), "--report", str(report)])
            self.assertFalse(report.exists())

    def test_pdf_dir_rejects_symlink_to_original(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "corpus"
            root.mkdir()
            target = root / "genuine.pdf"
            target.write_bytes(NONZERO.read_bytes())
            alias = root / "alias.pdf"
            try:
                alias.symlink_to(target)
            except (OSError, NotImplementedError):
                self.skipTest("symlinks unavailable")
            report = Path(directory) / "result.json"
            with contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    main(["--pdf-dir", str(root), "--report", str(report)])
            self.assertFalse(report.exists())
            self.assertEqual(target.read_bytes(), NONZERO.read_bytes())

    def test_pdf_dir_rejects_more_than_128_candidates(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "corpus"
            root.mkdir()
            for index in range(129):
                (root / f"candidate-{index:03d}.pdf").write_bytes(b"%PDF")
            report = Path(directory) / "result.json"
            with contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    main(["--pdf-dir", str(root), "--report", str(report)])
            self.assertFalse(report.exists())

    def test_pdf_dir_rejects_empty_root(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "corpus"
            root.mkdir()
            report = Path(directory) / "result.json"
            with contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    main(["--pdf-dir", str(root), "--report", str(report)])
            self.assertFalse(report.exists())

    def test_symlink_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "real.pdf"
            alias = Path(directory) / "link.pdf"
            target.write_bytes(pdf(b"0 0 4 4 re W* n 0 0 4 4 re f"))
            try:
                alias.symlink_to(target)
            except (OSError, NotImplementedError):
                self.skipTest("symlinks unavailable")
            with self.assertRaises(ValueError):
                screen_pdf_file(alias)


if __name__ == "__main__":
    unittest.main()
