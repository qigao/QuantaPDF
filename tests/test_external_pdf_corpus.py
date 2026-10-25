"""Contract tests for the offline external PDF corpus admission gate.

The temporary PDF-looking bytes test hashing and provenance validation ONLY;
they are not counted as externally sourced PDFs or native render conformance.
"""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] /
                       "tools" / "qualification"))
from external_pdf_corpus import (  # noqa: E402
    CorpusAdmissionError, atomic_report, verify_corpus
)


class ExternalPDFCorpusAdmissionTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.assets = self.root / "documents"
        self.assets.mkdir()
        self.manifest = self.root / "manifest.json"
        self.data = b"%PDF-1.4\n1 0 obj\n<<>>\nendobj\ntrailer\n<<>>\n%%EOF\n"
        self.doc = {
            "id": "sample.public-domain-v1",
            "source_url": "https://example.org/samples/sample.pdf",
            "source_revision": "immutable-version-1",
            "sha256": hashlib.sha256(self.data).hexdigest(),
            "size_bytes": len(self.data),
            "license_spdx": "CC0-1.0",
            "license_url": "https://creativecommons.org/publicdomain/zero/1.0/",
            "license_review_url": "https://github.com/qigao/QuantaPDF/issues/233",
            "attribution": "Synthetic test data only; no external PDF license asserted",
            "feature_tags": ["smoke", "text"],
            "page_index": 0,
            "expected_native_outcome": "unsupported"
        }

    def write(self, documents):
        self.manifest.write_text(json.dumps({
            "schema_version": 1,
            "corpus_id": "external-source-pdf",
            "documents": documents
        }), encoding="utf-8")

    def fixture(self):
        (self.assets / (self.doc["id"] + ".pdf")).write_bytes(self.data)
        self.write([self.doc])

    def rejects(self, message):
        with self.assertRaisesRegex(CorpusAdmissionError, message):
            verify_corpus(self.manifest, self.assets)

    def test_empty_registry_does_not_claim_conformance(self):
        self.write([])
        result = verify_corpus(self.manifest, self.assets)
        self.assertEqual(result["result"], "no_external_documents")
        self.assertEqual(result["document_count"], 0)
        self.assertEqual(result["native_conformance_passed_count"], 0)

    def test_exact_pdf_hash_and_provenance_record(self):
        self.fixture()
        result = verify_corpus(self.manifest, self.assets)
        self.assertEqual(result["result"], "integrity_verified_only")
        self.assertEqual(result["document_count"], 1)
        self.assertEqual(result["documents"][0]["integrity"], "verified")
        self.assertEqual(result["documents"][0]["native_conformance"], "not_run")
        self.assertEqual(result["native_conformance_passed_count"], 0)

    def test_modified_bytes_are_rejected(self):
        self.fixture()
        path = self.assets / (self.doc["id"] + ".pdf")
        path.write_bytes(self.data.replace(b"obj", b"OBj", 1))
        self.rejects("SHA-256 mismatch")

    def test_mismatched_size_is_rejected(self):
        self.fixture()
        self.doc["size_bytes"] -= 1
        self.write([self.doc])
        self.rejects("byte length mismatch")

    def test_missing_file_is_rejected(self):
        self.write([self.doc])
        self.rejects("absent")

    def test_undeclared_pdf_is_rejected(self):
        self.write([])
        (self.assets / "unregistered.pdf").write_bytes(self.data)
        self.rejects("unregistered")

    def test_duplicate_id_is_rejected(self):
        self.fixture()
        self.write([self.doc, dict(self.doc)])
        self.rejects("duplicate external PDF ID")

    def test_manifest_duplicate_json_key_is_rejected(self):
        self.manifest.write_text(
            '{"schema_version":1,"schema_version":1,'
            '"corpus_id":"external-source-pdf","documents":[]}',
            encoding="utf-8")
        self.rejects("duplicate JSON key")

    def test_unreviewed_license_is_rejected(self):
        self.fixture()
        self.doc["license_spdx"] = "Unknown-Unreviewed"
        self.write([self.doc])
        self.rejects("unreviewed")

    def test_source_specific_cc_by_sa_is_permitted_with_attribution(self):
        self.fixture()
        self.doc["license_spdx"] = "CC-BY-SA-4.0"
        self.doc["license_url"] = "https://creativecommons.org/licenses/by-sa/4.0/"
        self.doc["license_review_url"] = "https://github.com/qigao/QuantaPDF/issues/262"
        self.doc["attribution"] = (
            "Dominique Wuertz; unchanged original; CC BY-SA 4.0; "
            "retained license notice"
        )
        # The fixture() helper wrote the original CC0 manifest. Re-serialize
        # the mutated per-document rights record before offline validation.
        self.write([self.doc])
        result = verify_corpus(self.manifest, self.assets)
        self.assertEqual(result["documents"][0]["license_spdx_declared"],
                         "CC-BY-SA-4.0")
        self.assertEqual(result["documents"][0]["license_url"],
                         "https://creativecommons.org/licenses/by-sa/4.0/")
        self.assertEqual(result["native_conformance_passed_count"], 0)

    def test_supported_wstar_clip_outcome_is_admissible(self):
        self.fixture()
        self.doc["expected_native_outcome"] = "supported-wstar-clip"
        self.write([self.doc])
        record = verify_corpus(self.manifest, self.assets)["documents"][0]
        self.assertEqual(record["expected_native_outcome"], "supported-wstar-clip")
        self.assertEqual(record["native_conformance"], "not_run")

    def test_supported_blank_is_separate_from_path_clip(self):
        self.fixture()
        self.doc["expected_native_outcome"] = "supported-blank-page"
        self.write([self.doc])
        record = verify_corpus(self.manifest, self.assets)["documents"][0]
        self.assertEqual(record["expected_native_outcome"], "supported-blank-page")
        self.assertEqual(record["native_conformance"], "not_run")

    def test_unknown_expected_capability_is_rejected(self):
        self.fixture()
        self.doc["expected_native_outcome"] = "supported-all-pdf"
        self.write([self.doc])
        self.rejects("unknown expected native outcome")

    def test_http_source_is_rejected(self):
        self.fixture()
        self.doc["source_url"] = "http://example.org/a.pdf"
        self.write([self.doc])
        self.rejects("HTTPS URL")

    def test_license_review_must_be_linked_to_project(self):
        self.fixture()
        self.doc["license_review_url"] = "https://example.org/no-review"
        self.write([self.doc])
        self.rejects("QuantaPDF issue or PR")

    def test_unexpected_fields_fail_closed(self):
        self.fixture()
        self.doc["uncontrolled_extra"] = True
        self.write([self.doc])
        self.rejects("unknown fields")

    def test_invalid_path_component_is_rejected(self):
        self.fixture()
        self.doc["id"] = "../escape"
        self.write([self.doc])
        self.rejects("invalid external document ID")

    def test_pdf_magic_and_eof_are_checked(self):
        self.fixture()
        path = self.assets / (self.doc["id"] + ".pdf")
        changed = b"NOTPDF-1" + self.data[8:]
        path.write_bytes(changed)
        self.doc["sha256"] = hashlib.sha256(changed).hexdigest()
        self.doc["size_bytes"] = len(changed)
        self.write([self.doc])
        self.rejects("minimal PDF header/trailer")

    def test_boolean_byte_count_is_rejected(self):
        self.fixture()
        self.doc["size_bytes"] = True
        self.write([self.doc])
        self.rejects("byte budget")

    def test_atomic_report_is_deterministic(self):
        self.fixture()
        report = verify_corpus(self.manifest, self.assets)
        output = self.root / "report.json"
        atomic_report(output, report)
        first = output.read_bytes()
        self.assertEqual(json.loads(first)["documents"][0]["id"], self.doc["id"])
        atomic_report(output, report)
        self.assertEqual(first, output.read_bytes())
        self.assertFalse((self.root / "report.json.tmp").exists())

    def test_symlink_file_is_rejected_when_supported(self):
        self.fixture()
        path = self.assets / (self.doc["id"] + ".pdf")
        path.unlink()
        target = self.root / "source.pdf"
        target.write_bytes(self.data)
        try:
            path.symlink_to(target)
        except (NotImplementedError, OSError):
            self.skipTest("Creating symlinks is restricted on this runner")
        self.rejects("symlink")


if __name__ == "__main__":
    unittest.main()
