"""Offline unit tests for QuantaPDF's installed-SDK/source-data boundary."""
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "qualification"))
from sdk_test_data_boundary import (  # noqa: E402
    SDKDataBoundaryError, SOURCE_SHA256, check_install_manifest
)


class SDKTestDataBoundaryTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.installed = self.root / "prefix" / "share" / "quantapdf"
        self.installed.mkdir(parents=True)
        self.notice = self.installed / "THIRD_PARTY.md"
        self.notice.write_text(
            "Dominique Wuertz — TeXample CC BY-SA 4.0; "
            "https://creativecommons.org/licenses/by-sa/4.0/",
            encoding="utf-8",
        )
        self.license = self.installed / "LICENSE"
        self.license.write_text("QuantaPDF software license", encoding="utf-8")
        self.manifest = self.root / "install_manifest.txt"
        self.write_manifest(self.notice, self.license)

    def write_manifest(self, *paths):
        self.manifest.write_text(
            "".join(str(path) + "\n" for path in paths),
            encoding="utf-8",
        )

    def rejects(self, message):
        with self.assertRaisesRegex(SDKDataBoundaryError, message):
            check_install_manifest(self.manifest)

    def test_only_legal_notices_install(self):
        result = check_install_manifest(self.manifest)
        self.assertEqual(result["result"], "test_only_pdf_excluded")
        self.assertEqual(result["installed_entries"], 2)
        self.assertFalse(result["source_pdf_installed"])

    def test_source_filename_leak_rejected(self):
        source = self.installed / "texample.intersecting-rings-v1.pdf"
        self.write_manifest(self.notice, self.license, source)
        self.rejects("test-only licensed original")

    def test_source_corpus_path_leak_rejected(self):
        source = self.installed / "tests" / "external_source_corpus" / "other.pdf"
        self.write_manifest(self.notice, self.license, source)
        self.rejects("test-only licensed original")

    def test_renamed_real_original_pdf_is_rejected(self):
        original = Path(__file__).parent / "external_source_corpus" / "documents" / (
            "texample.intersecting-rings-v1.pdf"
        )
        data = original.read_bytes()
        self.assertEqual(hashlib.sha256(data).hexdigest(), SOURCE_SHA256)
        renamed = self.root / "prefix" / "share" / "quantapdf" / "renamed.pdf"
        renamed.write_bytes(data)
        self.write_manifest(self.notice, self.license, renamed)
        self.rejects("renamed original")

    def test_renamed_original_with_non_pdf_extension_rejected(self):
        original = Path(__file__).parent / "external_source_corpus" / "documents" / (
            "texample.intersecting-rings-v1.pdf"
        )
        renamed = self.installed / "source-data.bin"
        renamed.write_bytes(original.read_bytes())
        self.write_manifest(self.notice, self.license, renamed)
        self.rejects("renamed original")

    def test_non_pdf_file_with_same_length_does_not_false_positive(self):
        blob = self.installed / "ordinary.dat"
        blob.write_bytes(b"x" * 1396)
        self.write_manifest(self.notice, self.license, blob)
        check_install_manifest(self.manifest)

    def test_an_unrelated_pdf_may_be_installed(self):
        unrelated = self.installed / "other.pdf"
        unrelated.write_bytes(b"%PDF-1.4\n%%EOF\n")
        self.write_manifest(self.notice, self.license, unrelated)
        result = check_install_manifest(self.manifest)
        self.assertEqual(result["pdf_entries_checked"], 1)

    def test_missing_third_party_notice_rejected(self):
        self.write_manifest(self.license)
        self.rejects("lacks installed THIRD_PARTY")

    def test_missing_author_attribution_rejected(self):
        self.notice.write_text("CC BY-SA 4.0", encoding="utf-8")
        self.rejects("lacks attribution")

    def test_missing_project_license_rejected(self):
        self.write_manifest(self.notice)
        self.rejects("lacks installed THIRD_PARTY")

    def test_relative_installed_path_rejected(self):
        self.manifest.write_text("share/quantapdf/LICENSE\n", encoding="utf-8")
        self.rejects("absolute")

    def test_symlinked_manifest_rejected(self):
        alias = self.root / "alias.txt"
        try:
            alias.symlink_to(self.manifest)
        except (OSError, NotImplementedError):
            self.skipTest("symlink unavailable")
        with self.assertRaisesRegex(SDKDataBoundaryError, "symlinked"):
            check_install_manifest(alias)

    def test_duplicate_path_rejected(self):
        self.write_manifest(self.notice, self.license, self.notice)
        self.rejects("duplicate")

    def test_missing_manifest_rejected(self):
        self.manifest.unlink()
        self.rejects("missing")


if __name__ == "__main__":
    unittest.main()
