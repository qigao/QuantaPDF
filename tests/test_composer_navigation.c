#include <quantapdf/quantapdf.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            fprintf(stderr, "CHECK failed at %s:%d: %s\\n",                  \
                    __FILE__, __LINE__, #expr);                                \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static int close_float(float a, float b)
{
    float d = a - b;
    if (d < 0.0f)
        d = -d;
    return d < 0.01f;
}

static int add_page(quantapdf_composer *composer, size_t expected)
{
    quantapdf_composer_page_options page = {0};
    size_t page_index = SIZE_MAX;

    page.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page.width_points = 200.0f;
    page.height_points = 200.0f;
    page.background_argb = UINT32_C(0xffffffff);
    return quantapdf_composer_add_page(composer, &page, &page_index) ==
            QUANTAPDF_OK &&
        page_index == expected;
}

static int check_rect(
    const quantapdf_rect *rect,
    float x0,
    float y0,
    float x1,
    float y1)
{
    return close_float(rect->x0, x0) &&
        close_float(rect->y0, y0) &&
        close_float(rect->x1, x1) &&
        close_float(rect->y1, y1);
}

static int expect_outline_title(
    const quantapdf_outline *outline,
    size_t index,
    const char *expected)
{
    const char *title = NULL;
    size_t size = 0u;

    if (quantapdf_outline_title(outline, index, &title, &size) !=
        QUANTAPDF_OK)
        return 0;
    return title != NULL &&
        size == strlen(expected) &&
        memcmp(title, expected, size) == 0 &&
        title[size] == '\0';
}

static int expect_outline_info(
    const quantapdf_outline *outline,
    size_t index,
    size_t parent,
    size_t first_child,
    size_t next_sibling,
    int target_page,
    float x,
    float y,
    int is_open)
{
    quantapdf_outline_info info = {0};

    info.struct_size = sizeof(info);
    if (quantapdf_outline_get_info(outline, index, &info) != QUANTAPDF_OK)
        return 0;
    return info.parent_index == parent &&
        info.first_child_index == first_child &&
        info.next_sibling_index == next_sibling &&
        info.destination_kind == QUANTAPDF_OUTLINE_DESTINATION_INTERNAL &&
        info.target_page == target_page &&
        close_float(info.target.x, x) &&
        close_float(info.target.y, y) &&
        info.is_open == is_open;
}

static int test_argument_and_capacity_contract(void)
{
    quantapdf_composer_options composer_options = {0};
    quantapdf_composer *composer = NULL;
    quantapdf_composer_outline_options outline_options = {0};
    quantapdf_composer_outline_id outline_id = 99u;
    quantapdf_rect hotspot = {10.0f, 10.0f, 40.0f, 30.0f};
    quantapdf_point target = {20.0f, 30.0f};
    char bad_utf8[] = {(char)0xc0, '\0'};

    composer_options.struct_size = QUANTAPDF_COMPOSER_OPTIONS_V2_SIZE;
    composer_options.max_navigation_items = 1u;
    CHECK(quantapdf_composer_create(&composer_options, &composer) ==
          QUANTAPDF_OK);
    CHECK(add_page(composer, 0u));
    CHECK(add_page(composer, 1u));

    CHECK(quantapdf_composer_add_uri_link(
              NULL, 0u, &hotspot, "https://example.com") ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_composer_add_uri_link(
              composer, 2u, &hotspot, "https://example.com") ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_composer_add_uri_link(
              composer, 0u, NULL, "https://example.com") ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_composer_add_uri_link(
              composer, 0u, &hotspot, "") == QUANTAPDF_ERROR_FORMAT);
    CHECK(quantapdf_composer_add_uri_link(
              composer, 0u, &hotspot, bad_utf8) == QUANTAPDF_ERROR_FORMAT);

    CHECK(quantapdf_composer_add_page_link(
              composer, 0u, &hotspot, 2u, target) ==
          QUANTAPDF_ERROR_ARGUMENT);
    target.x = NAN;
    CHECK(quantapdf_composer_add_page_link(
              composer, 0u, &hotspot, 1u, target) ==
          QUANTAPDF_ERROR_ARGUMENT);
    target.x = 20.0f;

    CHECK(quantapdf_composer_add_uri_link(
              composer, 0u, &hotspot, "https://example.com") ==
          QUANTAPDF_OK);
    CHECK(quantapdf_composer_add_page_link(
              composer, 0u, &hotspot, 1u, target) ==
          QUANTAPDF_ERROR_UNSUPPORTED);

    outline_options.struct_size =
        QUANTAPDF_COMPOSER_OUTLINE_OPTIONS_V1_SIZE;
    outline_options.target_page_index = 1u;
    outline_options.target = target;
    outline_options.is_open = 1;
    CHECK(quantapdf_composer_add_outline(
              composer, "Outline", &outline_options, &outline_id) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(outline_id == 0u);
    quantapdf_drop_composer(composer);

    composer = NULL;
    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer, 0u));
    outline_options.parent_id = 1u;
    outline_options.target_page_index = 0u;
    outline_id = 99u;
    CHECK(quantapdf_composer_add_outline(
              composer, "Bad parent", &outline_options, &outline_id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(outline_id == 0u);
    outline_options.parent_id = 0u;
    outline_options.struct_size =
        QUANTAPDF_COMPOSER_OUTLINE_OPTIONS_V1_MIN_SIZE - 1u;
    CHECK(quantapdf_composer_add_outline(
              composer, "Small options", &outline_options, &outline_id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(outline_id == 0u);
    outline_options.struct_size =
        QUANTAPDF_COMPOSER_OUTLINE_OPTIONS_V1_SIZE;
    outline_options.is_open = 2;
    CHECK(quantapdf_composer_add_outline(
              composer, "Bad open", &outline_options, &outline_id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(outline_id == 0u);
    outline_options.is_open = 0;
    CHECK(quantapdf_composer_add_outline(
              composer, bad_utf8, &outline_options, &outline_id) ==
          QUANTAPDF_ERROR_FORMAT);
    CHECK(outline_id == 0u);

    quantapdf_drop_composer(composer);
    return 0;
}

static int test_round_trip_and_determinism(void)
{
    static const char uri[] = "https://example.com/composer-navigation";
    static const char chapter1[] = "Chapter 1 Caf\xC3\xA9";
    quantapdf_composer *composer = NULL;
    quantapdf_output *first = NULL;
    quantapdf_output *second = NULL;
    quantapdf_document *document = NULL;
    quantapdf_page *page = NULL;
    quantapdf_link_page *links = NULL;
    quantapdf_outline *outline = NULL;
    quantapdf_composer_outline_options outline_options = {0};
    quantapdf_composer_outline_id chapter1_id = 0u;
    quantapdf_composer_outline_id section1_id = 0u;
    quantapdf_composer_outline_id chapter2_id = 0u;
    quantapdf_composer_outline_id section2_id = 0u;
    quantapdf_rect uri_hotspot = {20.0f, 40.0f, 100.0f, 60.0f};
    quantapdf_rect page_hotspot = {20.0f, 80.0f, 100.0f, 100.0f};
    quantapdf_point page_target = {30.0f, 50.0f};
    const unsigned char *first_data = NULL;
    const unsigned char *second_data = NULL;
    const char *actual_uri = NULL;
    size_t first_size = 0u;
    size_t second_size = 0u;
    size_t uri_size = 0u;
    size_t count = 0u;
    quantapdf_link_info info = {0};

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer, 0u));
    CHECK(add_page(composer, 1u));
    CHECK(add_page(composer, 2u));

    CHECK(quantapdf_composer_add_uri_link(
              composer, 0u, &uri_hotspot, uri) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_add_page_link(
              composer, 0u, &page_hotspot, 1u, page_target) ==
          QUANTAPDF_OK);

    outline_options.struct_size =
        QUANTAPDF_COMPOSER_OUTLINE_OPTIONS_V1_SIZE;
    outline_options.parent_id = 0u;
    outline_options.target_page_index = 0u;
    outline_options.target = (quantapdf_point){30.0f, 50.0f};
    outline_options.is_open = 1;
    CHECK(quantapdf_composer_add_outline(
              composer, chapter1, &outline_options, &chapter1_id) ==
          QUANTAPDF_OK);
    CHECK(chapter1_id == 1u);

    outline_options.parent_id = chapter1_id;
    outline_options.target_page_index = 1u;
    outline_options.target = (quantapdf_point){10.0f, 20.0f};
    outline_options.is_open = 0;
    CHECK(quantapdf_composer_add_outline(
              composer, "Section 1.1", &outline_options, &section1_id) ==
          QUANTAPDF_OK);
    CHECK(section1_id == 2u);

    outline_options.parent_id = 0u;
    outline_options.target_page_index = 2u;
    outline_options.target = (quantapdf_point){40.0f, 40.0f};
    outline_options.is_open = 0;
    CHECK(quantapdf_composer_add_outline(
              composer, "Chapter 2", &outline_options, &chapter2_id) ==
          QUANTAPDF_OK);
    CHECK(chapter2_id == 3u);

    outline_options.parent_id = chapter2_id;
    outline_options.target = (quantapdf_point){50.0f, 60.0f};
    CHECK(quantapdf_composer_add_outline(
              composer, "Section 2.1", &outline_options, &section2_id) ==
          QUANTAPDF_OK);
    CHECK(section2_id == 4u);

    CHECK(quantapdf_composer_finish(composer, &first) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(composer, &second) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(first, &first_data, &first_size) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_data(second, &second_data, &second_size) ==
          QUANTAPDF_OK);
    CHECK(first_size == second_size);
    CHECK(memcmp(first_data, second_data, first_size) == 0);

    CHECK(quantapdf_output_save_file(first, COMPOSER_NAVIGATION_OUTPUT_PDF) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_open(COMPOSER_NAVIGATION_OUTPUT_PDF, NULL, &document) ==
          QUANTAPDF_OK);

    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_links(page, &links) == QUANTAPDF_OK);
    quantapdf_drop_page(page);
    page = NULL;
    CHECK(quantapdf_link_count(links, &count) == QUANTAPDF_OK);
    CHECK(count == 2u);

    info.struct_size = sizeof(info);
    CHECK(quantapdf_link_get_info(links, 0u, &info) == QUANTAPDF_OK);
    CHECK(info.kind == QUANTAPDF_LINK_URI);
    CHECK(check_rect(&info.hotspot, 20.0f, 40.0f, 100.0f, 60.0f));
    CHECK(quantapdf_link_uri(
              links, 0u, &actual_uri, &uri_size) == QUANTAPDF_OK);
    CHECK(uri_size == strlen(uri));
    CHECK(memcmp(actual_uri, uri, uri_size) == 0);

    info.struct_size = sizeof(info);
    CHECK(quantapdf_link_get_info(links, 1u, &info) == QUANTAPDF_OK);
    CHECK(info.kind == QUANTAPDF_LINK_INTERNAL);
    CHECK(check_rect(&info.hotspot, 20.0f, 80.0f, 100.0f, 100.0f));
    CHECK(info.target_page == 1);
    CHECK(close_float(info.target.x, 30.0f));
    CHECK(close_float(info.target.y, 50.0f));
    quantapdf_drop_link_page(links);
    links = NULL;

    CHECK(quantapdf_document_outline(document, &outline) == QUANTAPDF_OK);
    quantapdf_close(document);
    document = NULL;
    CHECK(quantapdf_outline_count(outline, &count) == QUANTAPDF_OK);
    CHECK(count == 4u);

    CHECK(expect_outline_info(
        outline, 0u, SIZE_MAX, 1u, 2u, 0, 30.0f, 50.0f, 1));
    CHECK(expect_outline_info(
        outline, 1u, 0u, SIZE_MAX, SIZE_MAX, 1, 10.0f, 20.0f, 0));
    CHECK(expect_outline_info(
        outline, 2u, SIZE_MAX, 3u, SIZE_MAX, 2, 40.0f, 40.0f, 0));
    CHECK(expect_outline_info(
        outline, 3u, 2u, SIZE_MAX, SIZE_MAX, 2, 50.0f, 60.0f, 0));

    CHECK(expect_outline_title(outline, 0u, chapter1));
    CHECK(expect_outline_title(outline, 1u, "Section 1.1"));
    CHECK(expect_outline_title(outline, 2u, "Chapter 2"));
    CHECK(expect_outline_title(outline, 3u, "Section 2.1"));

    quantapdf_drop_outline(outline);
    quantapdf_drop_output(second);
    quantapdf_drop_output(first);
    quantapdf_drop_composer(composer);
    return 0;
}

int main(void)
{
    CHECK(test_argument_and_capacity_contract() == 0);
    CHECK(test_round_trip_and_determinism() == 0);
    return 0;
}
