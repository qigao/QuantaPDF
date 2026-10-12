"""Independent capability attribution; declarations alone never count as tests."""
import unittest
from pathlib import Path
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"/"qualification"))
from reconcile_external_corpus import verify, TEXT_ID, BLANK_ID, PATH_ID


class ExternalEvidenceReconciliation(unittest.TestCase):
    def setUp(self):
        self.admission={"schema_version":1,"corpus_id":"external-source-pdf",
            "fixture_kind":"external","result":"integrity_verified_only",
            "document_count":3,"native_conformance_passed_count":0,
            "documents":[
                {"id":TEXT_ID,"integrity":"verified","native_conformance":"not_run",
                 "expected_native_outcome":"unsupported"},
                {"id":BLANK_ID,"integrity":"verified","native_conformance":"not_run",
                 "expected_native_outcome":"supported-blank-page"},
                {"id":PATH_ID,"integrity":"verified","native_conformance":"not_run",
                 "expected_native_outcome":"supported-path-clip"}]}
        self.text={"schema_version":1,"corpus_id":"external-source-pdf",
            "fixture_kind":"external","document_id":TEXT_ID,
            "reference_oracle":"original-pdfium","generated_fixture_count":0,
            "result":"passed","native_expected_outcome":"unsupported",
            "native_observed_outcome":"unsupported","pdfium_result":"rendered_nonblank",
            "native_ir":"empty","status_conformance_passed_count":1,
            "native_supported_passed_count":0}
        self.blank={"schema_version":1,"corpus_id":"external-source-pdf",
            "fixture_kind":"external","document_id":BLANK_ID,
            "reference_oracle":"original-pdfium","generated_fixture_count":0,
            "result":"passed","native_expected_outcome":"supported-blank-page",
            "native_observed_outcome":"supported-blank-page",
            "pdfium_result":"rendered_blank","page_width":612,"page_height":792,
            "mean_rgb_error":0,"mismatched_channels":0,
            "native_blank_page_supported_count":1,
            "native_path_clip_supported_count":0,"native_text_supported_count":0}
        self.path={"schema_version":1,"corpus_id":"external-source-pdf",
            "fixture_kind":"external","document_id":PATH_ID,
            "reference_oracle":"original-pdfium","generated_fixture_count":0,
            "result":"passed","native_expected_outcome":"supported-path-clip",
            "native_observed_outcome":"supported-path-clip",
            "pdfium_result":"rendered_opaque_black","page_width":612,
            "page_height":792,"native_painted_path_count":1,
            "native_clipped_path_supported_count":0,
            "native_path_clip_supported_count":1,
            "native_blank_page_supported_count":0,
            "native_text_supported_count":0,"mean_rgb_error":0.0,
            "mismatched_channels":0,"gross_error_pixels":0}

    def accept(self):
        return verify(self.admission,self.text,self.blank,self.path)

    def test_valid_evidence_remains_capability_scoped(self):
        report=self.accept()
        self.assertEqual(report["native_path_clip_supported"],1)
        self.assertEqual(report["native_clipped_path_supported"],0)
        self.assertEqual(report["native_blank_page_supported"],1)
        self.assertEqual(report["admitted_documents"],3)

    def test_missing_qualified_document_rejects(self):
        self.admission["documents"].pop()
        self.admission["document_count"]=2
        with self.assertRaises(AssertionError): self.accept()

    def test_unreviewed_document_rejects(self):
        self.admission["documents"][0]["integrity"]="not_verified"
        with self.assertRaises(AssertionError): self.accept()

    def test_forged_path_clip_pass_rejects(self):
        self.blank["native_path_clip_supported_count"]=1
        with self.assertRaises(AssertionError): self.accept()

    def test_forged_blank_pixel_result_rejects(self):
        self.blank["mismatched_channels"]=1
        with self.assertRaises(AssertionError): self.accept()

    def test_forged_text_support_rejects(self):
        self.text["native_supported_passed_count"]=1
        with self.assertRaises(AssertionError): self.accept()

    def test_swapped_semantic_report_rejects(self):
        self.blank["document_id"]=TEXT_ID
        with self.assertRaises(AssertionError): self.accept()

    def test_missing_semantic_result_rejects(self):
        self.text["result"]="not_run"
        with self.assertRaises(AssertionError): self.accept()

    def test_generated_counts_cannot_leak(self):
        self.blank["generated_fixture_count"]=36
        with self.assertRaises(AssertionError): self.accept()

    def test_manifest_declaration_is_not_execution(self):
        self.admission["native_conformance_passed_count"]=3
        with self.assertRaises(AssertionError): self.accept()

    def test_forged_opaque_pixel_error_rejects(self):
        self.path["mean_rgb_error"]=200
        with self.assertRaises(AssertionError): self.accept()

    def test_missing_opaque_painted_path_rejects(self):
        self.path["native_painted_path_count"]=0
        with self.assertRaises(AssertionError): self.accept()

    def test_forged_clip_support_rejects(self):
        self.path["native_clipped_path_supported_count"]=1
        with self.assertRaises(AssertionError): self.accept()

    def test_swapped_source_path_id_rejects(self):
        self.path["document_id"]=BLANK_ID
        with self.assertRaises(AssertionError): self.accept()

    def test_unqualified_opaque_source_rejects(self):
        self.path["result"]="not_run"
        with self.assertRaises(AssertionError): self.accept()


if __name__=="__main__":
    unittest.main()
