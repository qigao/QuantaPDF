"""Independent capability attribution; declarations alone never count as tests."""
import unittest
from pathlib import Path
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"/"qualification"))
from reconcile_external_corpus import verify, TEXT_ID, BLANK_ID, PATH_ID, CLIP_ID, WSTAR_ID, WSTAR_POSITIVE_ID


class ExternalEvidenceReconciliation(unittest.TestCase):
    def setUp(self):
        self.admission={"schema_version":1,"corpus_id":"external-source-pdf",
            "fixture_kind":"external","result":"integrity_verified_only",
            "document_count":6,"native_conformance_passed_count":0,
            "documents":[
                {"id":TEXT_ID,"integrity":"verified","native_conformance":"not_run",
                 "expected_native_outcome":"unsupported"},
                {"id":BLANK_ID,"integrity":"verified","native_conformance":"not_run",
                 "expected_native_outcome":"supported-blank-page"},
                {"id":PATH_ID,"integrity":"verified","native_conformance":"not_run",
                 "expected_native_outcome":"supported-path-clip"},
                {"id":CLIP_ID,"integrity":"verified","native_conformance":"not_run",
                 "expected_native_outcome":"supported-clipped-stroke"},
                {"id":WSTAR_ID,"integrity":"verified","native_conformance":"not_run",
                 "expected_native_outcome":"unsupported",
                 "sha256":"900d0ff1911736c372e0c7ead2c68be338479b985c676572db1cc6e35483b890",
                 "size_bytes":724,"license_spdx_declared":"BSD-3-Clause",
                 "license_review_url":"https://github.com/qigao/QuantaPDF/issues/264"},
                {"id":WSTAR_POSITIVE_ID,"integrity":"verified","native_conformance":"not_run",
                 "expected_native_outcome":"supported-wstar-clip",
                 "sha256":"9d30a67c8194b6822f28ad9ed4848a57c7745e95d11b79267ac5141001a91783",
                 "size_bytes":3747,"license_spdx_declared":"MIT",
                 "license_url":"https://github.com/yWorks/svg2pdf.js/blob/b564556f41b245893fbadd3a6c4f832455383dbe/LICENSE",
                 "license_review_url":"https://github.com/qigao/QuantaPDF/issues/271",
                 "source_url":"https://github.com/yWorks/svg2pdf.js/blob/b564556f41b245893fbadd3a6c4f832455383dbe/test/clippath/clippath-cliprule.pdf",
                 "source_revision":"b564556f41b245893fbadd3a6c4f832455383dbe",
                 "page_index":0,"feature_tags":["clip-evenodd","clip-nonzero"],
                 "attribution":"yWorks original PDF; see YWORKS-MIT-NOTICE.txt"}]}
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

        self.clip={"schema_version":1,"corpus_id":"external-source-pdf",
            "fixture_kind":"external","document_id":CLIP_ID,
            "reference_oracle":"original-pdfium","generated_fixture_count":0,
            "result":"passed","native_expected_outcome":"supported-clipped-stroke",
            "native_observed_outcome":"supported-clipped-stroke",
            "pdfium_result":"rendered_clipped_blue_stroke",
            "page_width":100,"page_height":50,
            "native_path_clip_supported_count":1,
            "native_clipped_path_supported_count":1,
            "native_stroke_1px_supported_count":1,
            "native_blank_page_supported_count":0,
            "native_text_supported_count":0,
            "mean_rgb_error":0.0,"gross_error_pixels":0,
            "reference_blue_pixels":30,"native_blue_pixels":30,
            "common_blue_pixels":30}
        self.wstar={"schema_version":1,"corpus_id":"external-source-pdf",
            "fixture_kind":"external","document_id":WSTAR_ID,
            "reference_oracle":"original-pdfium","generated_fixture_count":0,
            "result":"passed","native_expected_outcome":"unsupported",
            "native_observed_outcome":"unsupported",
            "pdfium_result":"rendered_nonblank","pdfium_dark_pixels":30,
            "native_ir":"empty","status_conformance_passed_count":1,
            "native_supported_passed_count":0,"native_wstar_clip_supported_count":0,
            "late_text_after_wstar_rejected":True,"source_bytes":724,
            "source_bytes_unchanged_after_reference_and_native":True,
            "pdfium_fallback_used":False}
        self.wstar_positive={"schema_version":1,"corpus_id":"external-source-pdf",
            "fixture_kind":"external","document_id":WSTAR_POSITIVE_ID,
            "reference_oracle":"original-pdfium","generated_fixture_count":0,
            "result":"passed","native_expected_outcome":"supported-wstar-clip",
            "native_observed_outcome":"supported-wstar-clip",
            "pdfium_result":"rendered_wstar_clip","native_ir":"lowered",
            "original_source_pdfium_oracle":"passed","detached_c11_native_raster":"passed",
            "native_supported_passed_count":1,"native_wstar_clip_supported_count":1,
            "external_wstar_pixel_supported_count":1,"official_corpus_admitted":True,
            "source_and_graph_released_before_raster":True,"source_bytes":3747,
            "source_sha256":"9d30a67c8194b6822f28ad9ed4848a57c7745e95d11b79267ac5141001a91783",
            "source_bytes_unchanged_after_reference_and_native":True,"pdfium_fallback_used":False,
            "page_width":600,"page_height":600,"native_painted_paths":3,
            "native_w_nonzero_clip_path_leaves":2,"native_wstar_evenodd_clip_path_leaves":2,
            "native_clip_intersection_nodes":3,"native_total_clip_nodes":7,
            "fixed_interior_goldens":8,"repeat_rgba_identical":True,
            "mean_rgb_error":0.006831,"gross_error_pixels":0,
            "reference_red_pixels":540,"native_red_pixels":540,"overlap_red_pixels":540,
            "reference_blue_pixels":4770,"native_blue_pixels":4770,"overlap_blue_pixels":4770}
    def accept(self):
        return verify(self.admission,self.text,self.blank,self.path,self.clip,self.wstar,self.wstar_positive)

    def test_valid_evidence_remains_capability_scoped(self):
        report=self.accept()
        self.assertEqual(report["native_path_clip_supported"],2)
        self.assertEqual(report["native_clipped_path_supported"],1)
        self.assertEqual(report["native_blank_page_supported"],1)
        self.assertEqual(report["admitted_documents"],6)
        self.assertEqual(report["external_wstar_late_text_rejection_passed"],1)
        self.assertEqual(report["native_wstar_clip_supported_count"],1)
        self.assertEqual(report["wstar_pixel_positive_document_count"],1)

    def test_missing_qualified_document_rejects(self):
        self.admission["documents"].pop()
        self.admission["document_count"]=3
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

    def test_forged_clip_status_cannot_count(self):
        self.clip["result"]="not_run"
        with self.assertRaises(AssertionError): self.accept()

    def test_forged_clip_overlap_cannot_count(self):
        self.clip["common_blue_pixels"]=0
        with self.assertRaises(AssertionError): self.accept()

    def test_forged_clip_native_ink_cannot_count(self):
        self.clip["native_blue_pixels"]=0
        with self.assertRaises(AssertionError): self.accept()

    def test_clip_semantics_not_generalized_to_text(self):
        self.clip["native_text_supported_count"]=1
        with self.assertRaises(AssertionError): self.accept()

    def test_clip_wrong_original_document_rejects(self):
        self.clip["document_id"]=PATH_ID
        with self.assertRaises(AssertionError): self.accept()

    def test_generated_counts_cannot_leak(self):
        self.blank["generated_fixture_count"]=36
        with self.assertRaises(AssertionError): self.accept()

    def test_manifest_declaration_is_not_execution(self):
        self.admission["native_conformance_passed_count"]=3
        with self.assertRaises(AssertionError): self.accept()

    def test_wstar_positive_report_must_be_pixel_qualified(self):
        self.wstar_positive["native_wstar_clip_supported_count"]=0
        with self.assertRaises(AssertionError): self.accept()

    def test_wstar_positive_hash_is_pinned(self):
        self.wstar_positive["source_sha256"]="0"*64
        with self.assertRaises(AssertionError): self.accept()

    def test_wstar_positive_must_keep_zero_gross_errors(self):
        self.wstar_positive["gross_error_pixels"]=1
        with self.assertRaises(AssertionError): self.accept()

    def test_wstar_positive_must_keep_source_graph_detached(self):
        self.wstar_positive["source_and_graph_released_before_raster"]=False
        with self.assertRaises(AssertionError): self.accept()

    def test_wstar_late_text_is_status_only(self):
        report=self.accept()
        self.assertEqual(report["native_text_supported"],0)
        self.assertEqual(report["external_wstar_late_text_rejection_passed"],1)

    def test_forged_wstar_text_support_rejects(self):
        self.wstar["native_supported_passed_count"]=1
        with self.assertRaises(AssertionError): self.accept()

    def test_missing_late_text_rejection_rejects(self):
        self.wstar["late_text_after_wstar_rejected"]=False
        with self.assertRaises(AssertionError): self.accept()

    def test_wstar_wrong_source_rejects(self):
        self.wstar["document_id"]=CLIP_ID
        with self.assertRaises(AssertionError): self.accept()

    def test_wstar_positive_count_must_remain_zero(self):
        self.wstar["native_wstar_clip_supported_count"]=1
        with self.assertRaises(AssertionError): self.accept()

    def test_wstar_admission_hash_is_pinned(self):
        self.admission["documents"][-1]["sha256"]="0"*64
        with self.assertRaises(AssertionError): self.accept()

    def test_wstar_admission_size_is_pinned(self):
        self.admission["documents"][-1]["size_bytes"]=723
        with self.assertRaises(AssertionError): self.accept()

    def test_wstar_license_review_is_pinned(self):
        self.admission["documents"][-1]["license_review_url"]="https://github.com/qigao/QuantaPDF/issues/233"
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
