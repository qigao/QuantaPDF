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
WSTAR_TEXT_ID="pdfium.bug-1574-wstar-text-v1"
WSTAR_PATH_ID="yworks.clippath-cliprule-v1"

def verify(admission, text, blank, path, clip, wstar_text, wstar_path):
    assert admission["schema_version"]==1 and admission["corpus_id"]=="external-source-pdf"
    assert admission["fixture_kind"]=="external"
    assert admission["result"]=="integrity_verified_only"
    docs=admission["documents"]
    assert admission["document_count"]==len(docs)
    assert len(docs)==6
    indexed={d["id"]:d for d in docs}
    assert len(indexed)==len(docs) and set(indexed)=={TEXT_ID,BLANK_ID,PATH_ID,CLIP_ID,WSTAR_TEXT_ID,WSTAR_PATH_ID}
    assert all(d["integrity"]=="verified" and d["native_conformance"]=="not_run" for d in docs)
    assert admission["native_conformance_passed_count"]==0
    assert indexed[TEXT_ID]["expected_native_outcome"]=="unsupported"
    assert indexed[BLANK_ID]["expected_native_outcome"]=="supported-blank-page"
    assert indexed[PATH_ID]["expected_native_outcome"]=="supported-path-clip"
    assert indexed[CLIP_ID]["expected_native_outcome"]=="supported-clipped-stroke"
    assert indexed[WSTAR_TEXT_ID]["expected_native_outcome"]=="unsupported"
    assert indexed[WSTAR_PATH_ID]["expected_native_outcome"]=="supported-path-clip"
    original=indexed[WSTAR_PATH_ID]
    assert original["sha256"]=="9d30a67c8194b6822f28ad9ed4848a57c7745e95d11b79267ac5141001a91783"
    assert original["size_bytes"]==3747
    assert original["license_spdx_declared"]=="MIT"
    assert original["license_review_url"]=="https://github.com/qigao/QuantaPDF/issues/265"
    assert original["page_index"]==0
    for report,id_ in [(text,TEXT_ID),(blank,BLANK_ID),(path,PATH_ID),(clip,CLIP_ID),(wstar_text,WSTAR_TEXT_ID)]:
        assert report["schema_version"]==1
        assert report["corpus_id"]=="external-source-pdf"
        assert report["fixture_kind"]=="external"
        assert report["document_id"]==id_
        assert report["reference_oracle"]=="original-pdfium"
        assert report["generated_fixture_count"]==0 and report["result"]=="passed"
        assert report["native_expected_outcome"]==indexed[id_]["expected_native_outcome"]
        assert report["native_observed_outcome"]==report["native_expected_outcome"]
        # No currently admitted external source contains W*: metadata alone
        # must never be allowed to inflate the even-odd Clip positive count.
        assert type(report.get("native_wstar_clip_supported_count",0)) is int
        assert report.get("native_wstar_clip_supported_count",0)==0
        if id_!=CLIP_ID:
            assert report.get("native_w_clip_supported_count",0)==0
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
    assert type(clip["native_w_clip_supported_count"]) is int
    assert clip["native_w_clip_supported_count"]==1
    assert type(clip["native_wstar_clip_supported_count"]) is int
    assert clip["native_wstar_clip_supported_count"]==0
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
    assert wstar_text["pdfium_result"]=="rendered_nonblank"
    assert wstar_text["native_ir"]=="empty"
    assert wstar_text["page_width"]==200 and wstar_text["page_height"]==300
    assert type(wstar_text["reference_ink_pixels"]) is int and wstar_text["reference_ink_pixels"]>=8
    assert wstar_text["status_conformance_passed_count"]==1
    assert wstar_text["native_supported_passed_count"]==0
    for key in ("native_path_clip_supported_count","native_w_clip_supported_count",
                "native_wstar_clip_supported_count"):
        assert type(wstar_text[key]) is int and wstar_text[key]==0

    # A manifest declaration or candidate-only pixel report cannot count.
    # The sixth ORIGINAL must have an exact independent PDFium/C11 conformance
    # result after owned Source/Graph release and deterministic repeat.
    assert wstar_path["schema_version"]==1
    assert wstar_path["corpus_id"]=="external-source-pdf"
    assert wstar_path["fixture_kind"]=="external"
    assert wstar_path["document_id"]==WSTAR_PATH_ID
    assert wstar_path["reference_oracle"]=="original-pdfium"
    assert wstar_path["native_expected_outcome"]=="supported-path-clip"
    assert wstar_path["native_observed_outcome"]=="supported-path-clip"
    assert wstar_path["result"]=="passed"
    assert wstar_path["original_source_pdfium_oracle"]=="passed"
    assert wstar_path["detached_c11_native_raster"]=="passed"
    assert type(wstar_path["source_and_graph_released_before_raster"]) is bool
    assert wstar_path["source_and_graph_released_before_raster"] is True
    assert type(wstar_path["repeat_rgba_identical"]) is bool
    assert wstar_path["repeat_rgba_identical"] is True
    for key,expected in {
        "page_width":600, "page_height":600,
        "native_w_nonzero_clip_path_leaves":2,
        "native_wstar_evenodd_clip_path_leaves":2,
        "native_clip_intersection_nodes":3,
        "fixed_interior_goldens":6,
        "native_path_clip_supported_count":1,
        "native_clipped_path_supported_count":1,
        "native_w_clip_supported_count":1,
        "native_wstar_clip_supported_count":1,
        "generated_fixture_count":0, "gross_error_pixels":0
    }.items():
        assert type(wstar_path[key]) is int and wstar_path[key]==expected
    assert type(wstar_path["mean_rgb_error"]) in (float,int)
    assert 0.0 <= wstar_path["mean_rgb_error"] <= 1.0
    for key in ("reference_red_pixels","native_red_pixels","overlap_red_pixels"):
        assert type(wstar_path[key]) is int and wstar_path[key] >= 25
    assert 2*wstar_path["overlap_red_pixels"] >= wstar_path["reference_red_pixels"]
    return {
      "schema_version":1,"corpus_id":"external-source-pdf",
      "result":"passed","admitted_documents":6,
      "external_text_rejection_passed":2,
      "native_blank_page_supported":1,
      "native_path_clip_supported":3,
      "native_clipped_path_supported":2,
      "native_w_clip_supported_count":2,
      "native_wstar_clip_supported_count":1,
      "native_text_supported":0,
      "generated_source_pdf_fixtures_included":0,
      "evidence_document_ids":[TEXT_ID,BLANK_ID,PATH_ID,CLIP_ID,WSTAR_TEXT_ID,WSTAR_PATH_ID]
    }

def main():
    p=argparse.ArgumentParser()
    for key in ("admission","text","blank","path","clip","wstar-text","wstar-native","output"):
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
                (args.admission,args.text,args.blank,args.path,args.clip,args.wstar_text,args.wstar_native)]
        report=verify(*inputs)
        with staged.open("w",encoding="utf-8",newline="\n") as stream:
            json.dump(report,stream,sort_keys=True,indent=2)
            stream.write("\n")
        staged.replace(args.output)
    except (AssertionError,KeyError,TypeError,ValueError,OSError) as exc:
        p.error("external corpus evidence mismatch: "+str(exc))
    print("[external-corpus] reconciled=6 text_rejections=2 blank_native=1 path_native=3 clipped_native=2 w_clip=2 wstar_clip=1")

if __name__=="__main__":
    sys.exit(main())
