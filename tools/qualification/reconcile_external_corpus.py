#!/usr/bin/env python3
"""Fail-closed cross-check of external PDF admission and actual semantic evidence.

Do not derive native support from a manifest declaration or PDFium rendering.
Only exact, known and executed CTest semantic reports may increase a counter.
"""
import argparse
import json
from pathlib import Path
import sys

TEXT_ID="tinytoolkit.sample-1-page-v1"
BLANK_ID="tinytoolkit.sample-blank-letter-v1"
PATH_ID="pdfium.black-original-v1"
CLIP_ID="pdfium.clip-path-v1"
WSTAR_ID="pdfium.wstar-late-text-v1"
WSTAR_POSITIVE_ID="yworks.clippath-cliprule-v1"

def verify(admission, text, blank, path, clip, wstar, wstar_positive):
    assert admission["schema_version"]==1 and admission["corpus_id"]=="external-source-pdf"
    assert admission["fixture_kind"]=="external"
    assert admission["result"]=="integrity_verified_only"
    docs=admission["documents"]
    assert admission["document_count"]==len(docs)
    assert len(docs)==6
    indexed={d["id"]:d for d in docs}
    assert len(indexed)==len(docs) and set(indexed)=={TEXT_ID,BLANK_ID,PATH_ID,CLIP_ID,WSTAR_ID,WSTAR_POSITIVE_ID}
    assert all(d["integrity"]=="verified" and d["native_conformance"]=="not_run" for d in docs)
    assert admission["native_conformance_passed_count"]==0
    assert indexed[TEXT_ID]["expected_native_outcome"]=="unsupported"
    assert indexed[BLANK_ID]["expected_native_outcome"]=="supported-blank-page"
    assert indexed[PATH_ID]["expected_native_outcome"]=="supported-path-clip"
    assert indexed[CLIP_ID]["expected_native_outcome"]=="supported-clipped-stroke"
    assert indexed[WSTAR_ID]["expected_native_outcome"]=="unsupported"
    assert indexed[WSTAR_ID]["sha256"]=="900d0ff1911736c372e0c7ead2c68be338479b985c676572db1cc6e35483b890"
    assert indexed[WSTAR_ID]["size_bytes"]==724
    assert indexed[WSTAR_ID]["license_spdx_declared"]=="BSD-3-Clause"
    assert indexed[WSTAR_ID]["license_review_url"]=="https://github.com/qigao/QuantaPDF/issues/264"
    assert indexed[WSTAR_POSITIVE_ID]["expected_native_outcome"]=="supported-wstar-clip"
    assert indexed[WSTAR_POSITIVE_ID]["sha256"]=="9d30a67c8194b6822f28ad9ed4848a57c7745e95d11b79267ac5141001a91783"
    assert indexed[WSTAR_POSITIVE_ID]["size_bytes"]==3747
    assert indexed[WSTAR_POSITIVE_ID]["license_spdx_declared"]=="MIT"
    assert indexed[WSTAR_POSITIVE_ID]["license_review_url"]=="https://github.com/qigao/QuantaPDF/issues/271"
    assert indexed[WSTAR_POSITIVE_ID]["source_revision"]=="b564556f41b245893fbadd3a6c4f832455383dbe"
    assert indexed[WSTAR_POSITIVE_ID]["source_url"]=="https://github.com/yWorks/svg2pdf.js/blob/b564556f41b245893fbadd3a6c4f832455383dbe/test/clippath/clippath-cliprule.pdf"
    assert indexed[WSTAR_POSITIVE_ID]["license_url"]=="https://github.com/yWorks/svg2pdf.js/blob/b564556f41b245893fbadd3a6c4f832455383dbe/LICENSE"
    assert indexed[WSTAR_POSITIVE_ID]["page_index"]==0
    assert "clip-evenodd" in indexed[WSTAR_POSITIVE_ID]["feature_tags"]
    assert "clip-nonzero" in indexed[WSTAR_POSITIVE_ID]["feature_tags"]
    assert "YWORKS-MIT-NOTICE.txt" in indexed[WSTAR_POSITIVE_ID]["attribution"]
    for report,id_ in [(text,TEXT_ID),(blank,BLANK_ID),(path,PATH_ID),(clip,CLIP_ID),(wstar,WSTAR_ID),(wstar_positive,WSTAR_POSITIVE_ID)]:
        assert report["schema_version"]==1
        assert report["corpus_id"]=="external-source-pdf"
        assert report["fixture_kind"]=="external"
        assert report["document_id"]==id_
        assert report["reference_oracle"]=="original-pdfium"
        assert report["generated_fixture_count"]==0 and report["result"]=="passed"
        assert report["native_expected_outcome"]==indexed[id_]["expected_native_outcome"]
        assert report["native_observed_outcome"]==report["native_expected_outcome"]
    assert text["pdfium_result"]=="rendered_nonblank"
    assert text["native_ir"]=="empty" and text["status_conformance_passed_count"]==1
    assert text["native_supported_passed_count"]==0
    assert blank["pdfium_result"]=="rendered_blank"
    assert blank["page_width"]==612 and blank["page_height"]==792
    assert blank["mean_rgb_error"]==0 and blank["mismatched_channels"]==0
    assert blank["native_blank_page_supported_count"]==1
    assert blank["native_path_clip_supported_count"]==0
    assert blank["native_text_supported_count"]==0
    assert path["pdfium_result"]=="rendered_opaque_black"
    assert path["page_width"]==612 and path["page_height"]==792
    assert path["native_painted_path_count"]==1
    assert path["native_clipped_path_supported_count"]==0
    assert path["native_path_clip_supported_count"]==1
    assert path["native_blank_page_supported_count"]==0
    assert path["native_text_supported_count"]==0
    assert type(path["mean_rgb_error"]) in (int,float)
    assert 0<=path["mean_rgb_error"]<=3.0
    assert type(path["gross_error_pixels"]) is int and path["gross_error_pixels"]==0
    assert type(path["mismatched_channels"]) is int and path["mismatched_channels"]>=0
    assert clip["pdfium_result"]=="rendered_clipped_blue_stroke"
    assert clip["page_width"]==100 and clip["page_height"]==50
    assert clip["native_path_clip_supported_count"]==1
    assert clip["native_clipped_path_supported_count"]==1
    assert clip["native_stroke_1px_supported_count"]==1
    assert clip["native_blank_page_supported_count"]==0
    assert clip["native_text_supported_count"]==0
    assert type(clip["mean_rgb_error"]) in (float,int)
    assert 0.0 <= clip["mean_rgb_error"] <= 6.0
    assert type(clip["gross_error_pixels"]) is int
    assert 0 <= clip["gross_error_pixels"] <= (100*50)//12
    assert type(clip["reference_blue_pixels"]) is int and clip["reference_blue_pixels"]>=15
    assert type(clip["native_blue_pixels"]) is int and clip["native_blue_pixels"]>=15
    assert type(clip["common_blue_pixels"]) is int
    assert 2*clip["common_blue_pixels"]>=clip["reference_blue_pixels"]
    assert wstar["pdfium_result"]=="rendered_nonblank"
    assert type(wstar["pdfium_dark_pixels"]) is int and wstar["pdfium_dark_pixels"]>0
    assert wstar["native_ir"]=="empty"
    assert wstar["status_conformance_passed_count"]==1
    assert wstar["native_supported_passed_count"]==0
    assert wstar["native_wstar_clip_supported_count"]==0
    assert wstar["late_text_after_wstar_rejected"] is True
    assert wstar["source_bytes"]==724
    assert wstar["source_bytes_unchanged_after_reference_and_native"] is True
    assert wstar["pdfium_fallback_used"] is False
    assert wstar_positive["pdfium_result"]=="rendered_wstar_clip"
    assert wstar_positive["native_ir"]=="lowered"
    assert wstar_positive["native_supported_passed_count"]==1
    assert wstar_positive["native_wstar_clip_supported_count"]==1
    assert wstar_positive["external_wstar_pixel_supported_count"]==1
    assert wstar_positive["official_corpus_admitted"] is True
    assert wstar_positive["source_and_graph_released_before_raster"] is True
    assert wstar_positive["source_bytes"]==3747
    assert wstar_positive["source_sha256"]=="9d30a67c8194b6822f28ad9ed4848a57c7745e95d11b79267ac5141001a91783"
    assert wstar_positive["source_bytes_unchanged_after_reference_and_native"] is True
    assert wstar_positive["pdfium_fallback_used"] is False
    assert wstar_positive["page_width"]==600 and wstar_positive["page_height"]==600
    assert wstar_positive["native_painted_paths"]==3
    assert wstar_positive["native_w_nonzero_clip_path_leaves"]==2
    assert wstar_positive["native_wstar_evenodd_clip_path_leaves"]==2
    assert wstar_positive["native_clip_intersection_nodes"]==3
    assert wstar_positive["native_total_clip_nodes"]==7
    assert wstar_positive["fixed_interior_goldens"]==8
    assert wstar_positive["repeat_rgba_identical"] is True
    assert type(wstar_positive["mean_rgb_error"]) in (int,float)
    assert 0.0<=wstar_positive["mean_rgb_error"]<=0.05
    assert wstar_positive["gross_error_pixels"]==0
    assert wstar_positive["generated_fixture_count"]==0
    assert wstar_positive["reference_red_pixels"]>=150 and wstar_positive["native_red_pixels"]>=150
    assert 100*wstar_positive["overlap_red_pixels"]>=99*wstar_positive["reference_red_pixels"]
    assert wstar_positive["reference_blue_pixels"]>=2000 and wstar_positive["native_blue_pixels"]>=2000
    assert 100*wstar_positive["overlap_blue_pixels"]>=99*wstar_positive["reference_blue_pixels"]
    return {
      "schema_version":1,"corpus_id":"external-source-pdf",
      "result":"passed","admitted_documents":6,
      "external_text_rejection_passed":1,
      "external_wstar_late_text_rejection_passed":1,
      "native_wstar_clip_supported_count":1,
      "native_blank_page_supported":1,
      "native_path_clip_supported":2,
      "native_clipped_path_supported":1,
      "native_text_supported":0,
      "generated_source_pdf_fixtures_included":0,
      "wstar_pixel_positive_document_count":1,
      "evidence_document_ids":[TEXT_ID,BLANK_ID,PATH_ID,CLIP_ID,WSTAR_ID,WSTAR_POSITIVE_ID]
    }

def main():
    p=argparse.ArgumentParser()
    for key in ("admission","text","blank","path","clip","wstar","wstar-positive","output"):
        p.add_argument("--"+key,required=True,type=Path)
    args=p.parse_args()
    if args.output.is_symlink():
        p.error("output symlink rejected")
    args.output.unlink(missing_ok=True)
    staged=Path(str(args.output)+".tmp")
    if staged.is_symlink():
        p.error("staged output symlink rejected")
    staged.unlink(missing_ok=True)
    try:
        inputs=[json.loads(path.read_text(encoding="utf-8")) for path in
                (args.admission,args.text,args.blank,args.path,args.clip,args.wstar,args.wstar_positive)]
        report=verify(*inputs)
        with staged.open("w",encoding="utf-8",newline="\n") as stream:
            json.dump(report,stream,sort_keys=True,indent=2)
            stream.write("\n")
        staged.replace(args.output)
    except (AssertionError,KeyError,TypeError,ValueError,OSError) as exc:
        p.error("external corpus evidence mismatch: "+str(exc))
    print("[external-corpus] reconciled=6 blank_native=1 path_native=2 clipped_native=1 wstar_late_text_rejected=1 wstar_pixel_positive=1")

if __name__=="__main__":
    sys.exit(main())
