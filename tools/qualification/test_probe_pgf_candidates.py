"""Test the quarantined #262 PDF content scanner; no external corpus needed."""
from __future__ import annotations

import io
import struct
import unittest
import zlib

from pypdf import PdfWriter
from pypdf.generic import (
    ArrayObject, DecodedStreamObject, DictionaryObject, NameObject, NumberObject,
)
from probe_pgf_candidates import scan_pdf


def ordinary_pdf(contents=None, forms=None, images=False, annotations=False) -> bytes:
    writer = PdfWriter()
    page = writer.add_blank_page(width=100, height=100)
    streams = []
    for raw in contents or []:
        obj = DecodedStreamObject()
        obj.set_data(raw)
        streams.append(writer._add_object(obj))
    if streams:
        page[NameObject("/Contents")] = (
            streams[0] if len(streams) == 1 else ArrayObject(streams)
        )
    xobjs = DictionaryObject()
    for name, raw in forms or []:
        form = DecodedStreamObject()
        form.set_data(raw)
        form[NameObject("/Type")] = NameObject("/XObject")
        form[NameObject("/Subtype")] = NameObject("/Form")
        form[NameObject("/BBox")] = ArrayObject([
            NumberObject(0), NumberObject(0), NumberObject(100), NumberObject(100)
        ])
        xobjs[NameObject(name)] = writer._add_object(form)
    if images:
        img = DecodedStreamObject()
        img.set_data(b"\x00")
        img[NameObject("/Type")] = NameObject("/XObject")
        img[NameObject("/Subtype")] = NameObject("/Image")
        xobjs[NameObject("/Im")] = writer._add_object(img)
    if xobjs:
        page[NameObject("/Resources")] = DictionaryObject({
            NameObject("/XObject"): xobjs,
        })
    if annotations:
        annot = DictionaryObject({
            NameObject("/Type"): NameObject("/Annot"),
            NameObject("/Subtype"): NameObject("/Text"),
            NameObject("/Rect"): ArrayObject([
                NumberObject(0), NumberObject(0), NumberObject(10), NumberObject(10)
            ]),
        })
        page[NameObject("/Annots")] = ArrayObject([writer._add_object(annot)])
    output = io.BytesIO()
    writer.write(output)
    return output.getvalue()



def whitespace_prefixed_pdf(pdf: bytes, prefix: bytes = b"\n\n") -> bytes:
    """Add a PDF-whitespace header prefix and adjust original xref offsets."""
    data = bytearray(pdf)
    xref_at = data.rfind(b"\nxref\n") + 1
    trailer_at = data.find(b"trailer", xref_at)
    if xref_at <= 0 or trailer_at < 0:
        raise ValueError("expected classic xref table")
    lines = bytes(data[xref_at:trailer_at]).splitlines(keepends=True)
    for index in range(2, len(lines)):
        line = lines[index]
        if len(line) >= 18 and line[17:18] == b"n":
            lines[index] = f"{int(line[:10]) + len(prefix):010d}".encode() + line[10:]
    data[xref_at:trailer_at] = b"".join(lines)
    startxref_at = data.rfind(b"startxref")
    number_start = data.find(b"\n", startxref_at) + 1
    number_end = data.find(b"\n", number_start)
    data[number_start:number_end] = str(
        int(data[number_start:number_end]) + len(prefix)
    ).encode()
    return prefix + bytes(data)


def nonzero_indexed_xref_pdf(pdf: bytes) -> bytes:
    """Move a valid classic xref table from subsection 0..N to 1..N-1."""
    data = bytearray(pdf)
    xref_at = data.find(b"xref\n")
    if xref_at < 0:
        raise ValueError("expected classic xref table")
    header_start = xref_at + len(b"xref\n")
    header_end = data.find(b"\n", header_start) + 1
    start, count = map(int, bytes(data[header_start:header_end]).split())
    if start != 0 or count < 2:
        raise ValueError("unexpected xref subsection")
    data[header_start:header_end] = f"1 {count - 1}\n".encode()
    free_entry_start = header_end
    free_entry_end = data.find(b"\n", free_entry_start) + 1
    del data[free_entry_start:free_entry_end]
    return bytes(data)


def pdf15_object_stream() -> bytes:
    """A real PDF 1.5 xref stream with page object compressed into /ObjStm."""
    pdf = bytearray(b"%PDF-1.5\n%\xe2\xe3\xcf\xd3\n")
    offsets = {}

    def obj(n: int, raw: bytes) -> None:
        offsets[n] = len(pdf)
        pdf.extend(f"{n} 0 obj\n".encode())
        pdf.extend(raw)
        pdf.extend(b"\nendobj\n")

    obj(1, b"<< /Type /Catalog /Pages 2 0 R >>")
    obj(2, b"<< /Type /Pages /Kids [5 0 R] /Count 1 >>")
    page = (
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] "
        b"/Contents 6 0 R >>"
    )
    header = b"5 0 "
    packed = zlib.compress(header + page)
    obj(3, b"<< /Type /ObjStm /N 1 /First " + str(len(header)).encode() +
        b" /Length " + str(len(packed)).encode() +
        b" /Filter /FlateDecode >>\nstream\n" + packed + b"\nendstream")
    decoded = b"0 0 10 10 re 2 2 4 4 re W* n\n"
    packed = zlib.compress(decoded)
    obj(6, b"<< /Length " + str(len(packed)).encode() +
        b" /Filter /FlateDecode >>\nstream\n" + packed + b"\nendstream")
    offsets[7] = len(pdf)
    xref_entries = []
    for n in range(8):
        if n in (0, 4):
            fields = (0, 0, 65535 if n == 0 else 0)
        elif n == 5:
            fields = (2, 3, 0)
        else:
            fields = (1, offsets[n], 0)
        xref_entries.append(struct.pack(">BIH", *fields))
    compressed_xref = zlib.compress(b"".join(xref_entries))
    obj(7, b"<< /Type /XRef /Size 8 /Root 1 0 R /W [1 4 2] "
        b"/Index [0 8] /Length " + str(len(compressed_xref)).encode() +
        b" /Filter /FlateDecode >>\nstream\n" +
        compressed_xref + b"\nendstream")
    pdf.extend(f"startxref\n{offsets[7]}\n%%EOF\n".encode())
    return bytes(pdf)


class ProbeTests(unittest.TestCase):
    def test_comment_and_text_literal_are_not_operators(self):
        content = b"% W* comment\nBT /F1 10 Tf (W*) Tj ET\n0 0 5 5 re f\n"
        result = scan_pdf(ordinary_pdf([content]))
        self.assertEqual(result["scan_status"], "confirmed_no_wstar")
        self.assertEqual(result["effective_wstar_count"], 0)
        self.assertIn("text", result["pages"][0]["native_raster_risks"])

    def test_split_contents_preserves_path(self):
        result = scan_pdf(ordinary_pdf([
            b"0 0 50 50 re\n", b"4 4 10 10 re W* n\n",
        ]))
        self.assertEqual(result["scan_status"], "wstar_found")
        self.assertEqual(result["page_content_wstar_count"], 1)
        self.assertTrue(result["pages"][0]["clip_evidence"][0][
            "potential_evenodd_difference"
        ])

    def test_form_xobject_is_traversed(self):
        result = scan_pdf(ordinary_pdf(
            [b"/Fm1 Do"], forms=[(
                "/Fm1", b"0 0 30 30 re 4 4 8 8 re W* n"
            )],
        ))
        self.assertEqual(result["scan_status"], "wstar_found")
        self.assertEqual(result["page_content_wstar_count"], 0)
        self.assertEqual(result["form_wstar_count"], 1)
        self.assertIn("form_xobject", result["pages"][0]["native_raster_risks"])

    def test_unscanned_annotation_appearance_is_inconclusive(self):
        result = scan_pdf(ordinary_pdf([b"0 0 5 5 re f"], annotations=True))
        self.assertEqual(result["scan_status"], "inconclusive")
        self.assertIsNone(result["effective_wstar_count"])
        self.assertIn("annotations_not_scanned", result["reason"])

    def test_image_does_not_invalidate_operator_count(self):
        result = scan_pdf(ordinary_pdf(
            [b"/Im Do 0 0 20 20 re W* n"], images=True,
        ))
        self.assertEqual(result["scan_status"], "wstar_found")
        self.assertIn("image_xobject", result["pages"][0]["native_raster_risks"])

    def test_unknown_filter_cannot_be_claimed_zero(self):
        original = ordinary_pdf([b"W* n"])
        corrupted = original.replace(
            b"/Length 4", b"/Length 4 /Filter /BrokenDecoder",
        )
        self.assertNotEqual(original, corrupted)
        result = scan_pdf(corrupted)
        self.assertEqual(result["scan_status"], "inconclusive")
        self.assertIsNone(result["effective_wstar_count"])

    def test_truncated_pdf_cannot_be_claimed_zero(self):
        result = scan_pdf(b"%PDF-1.5\n1 0 obj << /Contents 7 0 R")
        self.assertEqual(result["scan_status"], "inconclusive")
        self.assertIsNone(result["effective_wstar_count"])

    def test_pdf_header_after_bounded_whitespace_is_scanned_unchanged(self):
        original = ordinary_pdf([b"0 0 10 10 re W* n"])
        source = whitespace_prefixed_pdf(original)
        result = scan_pdf(source)
        self.assertEqual(source[:2], b"\n\n")

        self.assertEqual(result["pdf_header_offset_bytes"], 2)
        self.assertEqual(result["scan_status"], "wstar_found", result["reason"])
        self.assertEqual(result["effective_wstar_count"], 1)

    def test_nonwhitespace_before_pdf_header_is_inconclusive(self):
        result = scan_pdf(b"not-a-pdf " + ordinary_pdf([b"W* n"]))
        self.assertEqual(result["scan_status"], "inconclusive")
        self.assertEqual(result["reason"], "invalid_pdf_header_prefix")
        self.assertIsNone(result["effective_wstar_count"])


    def test_nonzero_indexed_xref_requires_independent_validation(self):
        original = ordinary_pdf([b"0 0 10 10 re W* n"])
        repaired_xref = nonzero_indexed_xref_pdf(original)
        strict_result = scan_pdf(repaired_xref)
        self.assertEqual(strict_result["scan_status"], "inconclusive")
        self.assertIsNone(strict_result["effective_wstar_count"])
        validated_result = scan_pdf(
            repaired_xref, allow_nonzero_xref_warning=True
        )
        self.assertEqual(
            validated_result["scan_status"], "wstar_found",
            validated_result["reason"],
        )
        self.assertEqual(validated_result["effective_wstar_count"], 1)

    def test_pdf15_page_object_in_objstm(self):
        result = scan_pdf(pdf15_object_stream())
        self.assertEqual(result["scan_status"], "wstar_found", result["reason"])
        self.assertEqual(result["page_content_wstar_count"], 1)
        self.assertEqual(result["effective_wstar_count"], 1)


if __name__ == "__main__":
    unittest.main()
