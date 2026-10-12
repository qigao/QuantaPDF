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

def verify(admission, text, blank):
    assert admission["schema_version"]==1 and admission["corpus_id"]=="external-source-pdf"
    assert admission["fixture_kind"]=="external"
    assert admission["result"]=="integrity_verified_only"
    docs=admission["documents"]
    assert admission["document_count"]==len(docs)
    assert len(docs)==2
    indexed={d["id"]:d for d in docs}
    assert len(indexed)==len(docs) and set(indexed)=={TEXT_ID,BLANK_ID}
    assert all(d["integrity"]=="verified" and d["native_conformance"]=="not_run" for d in docs)
    assert admission["native_conformance_passed_count"]==0
    assert indexed[TEXT_ID]["expected_native_outcome"]=="unsupported"
    assert indexed[BLANK_ID]["expected_native_outcome"]=="supported-blank-page"
    for report,id_ in [(text,TEXT_ID),(blank,BLANK_ID)]:
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
    return {
      "schema_version":1,"corpus_id":"external-source-pdf",
      "result":"passed","admitted_documents":2,
      "external_text_rejection_passed":1,
      "native_blank_page_supported":1,
      "native_path_clip_supported":0,
      "native_text_supported":0,
      "generated_source_pdf_fixtures_included":0,
      "evidence_document_ids":[TEXT_ID,BLANK_ID]
    }

def main():
    p=argparse.ArgumentParser()
    for key in ("admission","text","blank","output"):
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
                (args.admission,args.text,args.blank)]
        report=verify(*inputs)
        with staged.open("w",encoding="utf-8",newline="\n") as stream:
            json.dump(report,stream,sort_keys=True,indent=2)
            stream.write("\n")
        staged.replace(args.output)
    except (AssertionError,KeyError,TypeError,ValueError,OSError) as exc:
        p.error("external corpus evidence mismatch: "+str(exc))
    print("[external-corpus] reconciled=2 blank_native=1 path_clip_native=0")

if __name__=="__main__":
    sys.exit(main())
