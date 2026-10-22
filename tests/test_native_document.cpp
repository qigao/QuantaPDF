#include <quantapdf/quantapdf.h>

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static void check(bool ok, const char *expression, int line)
{
    if (!ok) {
        std::fprintf(stderr, "%s:%d: %s\n", __FILE__, line, expression);
        std::exit(EXIT_FAILURE);
    }
}
#define CHECK(x) check((x), #x, __LINE__)
using Obj = QPDFObjectHandle;
void quantapdf_test_native_images();
void quantapdf_test_native_text();

static void near(float actual, float expected)
{
    CHECK(std::isfinite(actual) && std::abs(actual - expected) < 0.001f);
}

static Obj link(QPDF& pdf, Obj destination)
{
    Obj result = pdf.makeIndirectObject(Obj::parse(
        "<< /Type /Annot /Subtype /Link /Rect [20 40 60 80] >>"));
    result.replaceKey("/Dest", destination);
    return result;
}

struct Fixture {
    QPDF pdf;
    Obj source;
    Obj target;
    Obj annots;
    Obj destination;
    Fixture(int rotation = 0)
    {
        pdf.emptyPDF();
        source = pdf.makeIndirectObject(Obj::parse(
            "<< /Type /Page /UserUnit 2 /Resources << >> >>"));
        target = pdf.makeIndirectObject(Obj::parse(
            "<< /Type /Page /MediaBox [-40 0 200 360] "
            "/CropBox [-20 30 180 330] /Rotate 270 /UserUnit 3 /Resources << >> >>"));
        pdf.addPage(source, false);
        pdf.addPage(target, false);
        Obj parent = pdf.getRoot().getKey("/Pages");
        parent.replaceKey("/MediaBox", Obj::parse("[0 0 240 160]"));
        parent.replaceKey("/CropBox", Obj::parse("[10 20 210 120]"));
        parent.replaceKey("/Rotate", Obj::newInteger(rotation));
        annots = Obj::newArray();
        source.replaceKey("/Annots", annots);
        destination = Obj::newArray({target, Obj::newName("/XYZ"),
            Obj::newInteger(30), Obj::newInteger(250), Obj::newNull()});
    }

    void write(bool encrypted = false)
    {
        QPDFWriter writer(pdf, NATIVE_DOCUMENT_OUTPUT_PDF);
        if (encrypted)
            writer.setR6EncryptionParameters("reader", "owner", true, true,
                true, true, true, true, qpdf_r3p_full, true);
        writer.write();
    }
};

static quantapdf_link_info info(quantapdf_link_page *links, size_t index)
{
    quantapdf_link_info result = {};
    result.struct_size = sizeof(result);
    CHECK(quantapdf_link_get_info(links, index, &result) == QUANTAPDF_OK);
    return result;
}

static void test_geometry_and_links(int rotation, bool encrypted)
{
    Fixture fixture(rotation);
    Obj uri_link = link(fixture.pdf, Obj::newNull());
    uri_link.replaceKey("/A", Obj::parse("<< /S /URI /URI (https://example.com/a%20b) >>"));
    fixture.annots.appendItem(uri_link);
    fixture.annots.appendItem(link(fixture.pdf, fixture.destination));
    Obj action_link = link(fixture.pdf, Obj::newNull());
    Obj action = Obj::parse("<< /S /GoTo >>");
    action.replaceKey("/D", fixture.destination);
    action_link.replaceKey("/A", action);
    fixture.annots.appendItem(action_link);
    Obj legacy = Obj::newDictionary();
    Obj wrapped = Obj::newDictionary();
    wrapped.replaceKey("/D", fixture.destination);
    legacy.replaceKey("/Legacy", wrapped);
    fixture.pdf.getRoot().replaceKey("/Dests", legacy);
    fixture.annots.appendItem(link(fixture.pdf, Obj::newName("/Legacy")));
    Obj leaf = fixture.pdf.makeIndirectObject(Obj::newDictionary());
    leaf.replaceKey("/Names", Obj::newArray({Obj::newString("named"), fixture.destination}));
    Obj tree = Obj::newDictionary();
    tree.replaceKey("/Kids", Obj::newArray({leaf}));
    Obj names = Obj::newDictionary();
    names.replaceKey("/Dests", tree);
    fixture.pdf.getRoot().replaceKey("/Names", names);
    fixture.annots.appendItem(link(fixture.pdf, Obj::newString("named")));
    fixture.annots.appendItem(link(fixture.pdf, Obj::newArray({fixture.target,
        Obj::newName("/Fit")})));
    fixture.annots.appendItem(link(fixture.pdf, Obj::newArray({fixture.target,
        Obj::newName("/XYZ"), Obj::newNull(), Obj::newNull(), Obj::newNull()})));
    fixture.annots.appendItem(link(fixture.pdf, Obj::newString("Legacy")));
    Obj degenerate = link(fixture.pdf, fixture.destination);
    degenerate.replaceKey("/Rect", Obj::parse("[20 40 20 40]"));
    fixture.annots.appendItem(degenerate);
    fixture.annots.appendItem(Obj::parse("<< /Subtype /Text >>"));
    fixture.write(encrypted);

    quantapdf_document *document = nullptr;
    if (encrypted) {
        CHECK(quantapdf_open(NATIVE_DOCUMENT_OUTPUT_PDF, "wrong", &document) ==
            QUANTAPDF_ERROR_PASSWORD);
        CHECK(document == nullptr);
    }
    CHECK(quantapdf_open(NATIVE_DOCUMENT_OUTPUT_PDF,
        encrypted ? "reader" : nullptr, &document) == QUANTAPDF_OK);
    int count = 0;
    CHECK(quantapdf_page_count(document, &count) == QUANTAPDF_OK && count == 2);
    quantapdf_page *page = nullptr;
    CHECK(quantapdf_load_page(document, 2, &page) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(page == nullptr);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    quantapdf_rect bounds = {};
    CHECK(quantapdf_page_bounds(page, &bounds) == QUANTAPDF_OK);
    near(bounds.x0, 0); near(bounds.y0, 0);
    near(bounds.x1, rotation % 180 == 0 ? 400.0f : 200.0f);
    near(bounds.y1, rotation % 180 == 0 ? 200.0f : 400.0f);
    quantapdf_link_page *links = nullptr;
    CHECK(quantapdf_extract_links(page, &links) == QUANTAPDF_OK);
    quantapdf_drop_page(page);
    CHECK(quantapdf_load_page(document, 1, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_page_bounds(page, &bounds) == QUANTAPDF_OK);
    near(bounds.x1, 900); near(bounds.y1, 600);
    quantapdf_link_page *empty = nullptr;
    CHECK(quantapdf_extract_links(page, &empty) == QUANTAPDF_OK);
    size_t size = 99;
    CHECK(quantapdf_link_count(empty, &size) == QUANTAPDF_OK && size == 0);
    quantapdf_drop_link_page(empty);
    quantapdf_drop_page(page);
    quantapdf_close(document);
    /* Every snapshot observation below runs after both source owners die. */
    CHECK(quantapdf_link_count(links, &size) == QUANTAPDF_OK && size == 9);
    auto external = info(links, 0);
    CHECK(external.kind == QUANTAPDF_LINK_URI && external.target_page == -1);
    static const float expected[4][4] = {
        {20, 80, 100, 160}, {40, 20, 120, 100},
        {300, 40, 380, 120}, {80, 300, 160, 380}};
    auto const *rect = expected[rotation / 90];
    near(external.hotspot.x0, rect[0]); near(external.hotspot.y0, rect[1]);
    near(external.hotspot.x1, rect[2]); near(external.hotspot.y1, rect[3]);
    const char *uri = nullptr;
    CHECK(quantapdf_link_uri(links, 0, &uri, &size) == QUANTAPDF_OK);
    CHECK(std::string(uri, size) == "https://example.com/a%20b");
    for (size_t i = 1; i < 5; ++i) {
        auto target = info(links, i);
        CHECK(target.kind == QUANTAPDF_LINK_INTERNAL && target.target_page == 1);
        near(target.target.x, 240); near(target.target.y, 450);
    }
    auto fit = info(links, 5);
    near(fit.target.x, 0); near(fit.target.y, 0);
    auto null_xyz = info(links, 6);
    near(null_xyz.target.x, 0); near(null_xyz.target.y, 600);
    auto legacy_string = info(links, 7);
    near(legacy_string.target.x, 240); near(legacy_string.target.y, 450);
    auto empty_rect = info(links, 8);
    near(empty_rect.hotspot.x0, empty_rect.hotspot.x1);
    near(empty_rect.hotspot.y0, empty_rect.hotspot.y1);
    quantapdf_drop_link_page(links);
}

static void expect_failure(Fixture& fixture, quantapdf_status expected)
{
    fixture.write();
    quantapdf_document *document = nullptr;
    quantapdf_page *page = nullptr;
    CHECK(quantapdf_open(NATIVE_DOCUMENT_OUTPUT_PDF, nullptr, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    quantapdf_link_page *links = reinterpret_cast<quantapdf_link_page *>(1);
    CHECK(quantapdf_extract_links(page, &links) == expected);
    CHECK(links == nullptr);
    quantapdf_drop_page(page);
    quantapdf_close(document);
}

static void test_atomic_rejections()
{
    {
        Fixture fixture;
        fixture.annots.appendItem(link(fixture.pdf, fixture.destination));
        Obj unsupported = link(fixture.pdf, Obj::newNull());
        unsupported.replaceKey("/A", Obj::parse("<< /S /JavaScript /JS (x) >>"));
        fixture.annots.appendItem(unsupported);
        expect_failure(fixture, QUANTAPDF_ERROR_UNSUPPORTED);
    }
    {
        Fixture fixture;
        fixture.destination.setArrayItem(2, Obj::newString("bad"));
        fixture.annots.appendItem(link(fixture.pdf, fixture.destination));
        expect_failure(fixture, QUANTAPDF_ERROR_FORMAT);
    }
    {
        Fixture fixture;
        Obj tree = fixture.pdf.makeIndirectObject(Obj::newDictionary());
        tree.replaceKey("/Kids", Obj::newArray({tree}));
        Obj names = Obj::newDictionary();
        names.replaceKey("/Dests", tree);
        fixture.pdf.getRoot().replaceKey("/Names", names);
        fixture.annots.appendItem(link(fixture.pdf, Obj::newString("missing")));
        expect_failure(fixture, QUANTAPDF_ERROR_FORMAT);
    }
    {
        Fixture fixture;
        fixture.source.replaceKey("/Annots", Obj::newInteger(42));
        expect_failure(fixture, QUANTAPDF_ERROR_FORMAT);
    }
    {
        Fixture fixture;
        Obj uri_link = link(fixture.pdf, Obj::newNull());
        Obj action = Obj::parse("<< /S /URI >>");
        action.replaceKey("/URI", Obj::newString(std::string(1048577u, 'a')));
        uri_link.replaceKey("/A", action);
        fixture.annots.appendItem(uri_link);
        expect_failure(fixture, QUANTAPDF_ERROR_UNSUPPORTED);
    }
    {
        Fixture fixture;
        Obj repeated = link(fixture.pdf, fixture.destination);
        for (size_t i = 0; i < 16385u; ++i)
            fixture.annots.appendItem(repeated);
        expect_failure(fixture, QUANTAPDF_ERROR_UNSUPPORTED);
    }
}

static void test_structural_operations()
{
    Fixture fixture;
    Obj metadata = Obj::newDictionary();
    metadata.replaceKey("/Title", Obj::newUnicodeString("Native document"));
    fixture.pdf.getTrailer().replaceKey("/Info", fixture.pdf.makeIndirectObject(metadata));
    fixture.write();
    quantapdf_document *document = nullptr;
    CHECK(quantapdf_open(NATIVE_DOCUMENT_OUTPUT_PDF, nullptr, &document) == QUANTAPDF_OK);
    char *title = nullptr;
    size_t size = 0;
    CHECK(quantapdf_document_metadata(document, QUANTAPDF_METADATA_TITLE,
        &title, &size) == QUANTAPDF_OK);
    CHECK(std::string(title, size) == "Native document");
    quantapdf_free(title);
    quantapdf_pdf_edit *edit = nullptr;
    CHECK(quantapdf_pdf_edit_begin(document, &edit) == QUANTAPDF_OK);
    quantapdf_drop_pdf_edit(edit);
    quantapdf_output *output = nullptr;
    CHECK(quantapdf_rewrite_lossless(document, &output) == QUANTAPDF_OK);
    quantapdf_close(document);
    const unsigned char *data = nullptr;
    CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);
    CHECK(size > 5 && std::memcmp(data, "%PDF-", 5) == 0);
    quantapdf_drop_output(output);
}

int main()
{
    for (int rotation : {0, 90, 180, 270})
        test_geometry_and_links(rotation, false);
    test_geometry_and_links(90, true);
    test_atomic_rejections();
    test_structural_operations();
    quantapdf_test_native_images();
    quantapdf_test_native_text();
    std::remove(NATIVE_DOCUMENT_OUTPUT_PDF);
    return EXIT_SUCCESS;
}
