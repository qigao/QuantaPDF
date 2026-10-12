"""Synthetic byte-level scanner tests; NO external native conformance."""
import json
import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "qualification"))
from screen_external_wstar import screen_pdf_bytes, screen_pdf_file, main


def pdf(content, compress=False, filter_token=b"/FlateDecode"):
    body = zlib.compress(content) if compress else content
    filt = b"/Filter " + filter_token + b" " if compress else b""
    return (b"%PDF-1.7\n1 0 obj << /Length " + str(len(body)).encode("ascii") +
            b" " + filt + b">>\nstream\n" + body + b"\nendstream\n"
            b"endobj\ntrailer << /Size 2 >>\n%%EOF\n")


class ExternalWStarScreening(unittest.TestCase):
    def test_path_clip_candidate_is_screening_only(self):
        doc = screen_pdf_bytes(pdf(b"q 1 1 3 3 re W* n 1 0 0 rg 0 0 5 5 re f Q"))
        self.assertEqual(doc["status"], "candidate_requires_full_page_preflight")
        self.assertEqual(doc["operator_counts"]["wstar"], 1)
        self.assertEqual(doc["native_wstar_clip_supported_count"], 0)
        self.assertEqual(doc["native_pixel_conformance"], "not_run")

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
