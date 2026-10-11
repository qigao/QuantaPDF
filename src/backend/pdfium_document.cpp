#include "pdfium_document.h"

#include "pdfium_runtime.h"
#include "qpdf_document.h"
#include "snapshot/text_snapshot.h"
#include "snapshot/image_snapshot.h"
#include "snapshot/link_snapshot.h"

#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif

#include <fpdfview.h>
#include <fpdf_edit.h>
#include <fpdf_doc.h>
#include <fpdf_formfill.h>
#include <fpdf_text.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <vector>

struct quantapdf_pdfium_document {
    FPDF_DOCUMENT handle;
    FPDF_FORMHANDLE form_handle;
    FPDF_FORMFILLINFO form_info;
    unsigned char *owned_data;
};

struct quantapdf_pdfium_page {
    FPDF_DOCUMENT document;
    FPDF_FORMHANDLE form_handle;
    int page_index;
    FPDF_PAGE handle;
    bool form_page_loaded;
};

namespace {

class pdfium_scope final {
public:
    explicit pdfium_scope(quantapdf_status status) noexcept
        : entered_(status == QUANTAPDF_OK)
    {
    }

    ~pdfium_scope()
    {
        if (entered_)
            quantapdf_pdfium_leave();
    }

    pdfium_scope(pdfium_scope const&) = delete;
    pdfium_scope& operator=(pdfium_scope const&) = delete;

private:
    bool entered_;
};

quantapdf_status status_from_pdfium(unsigned long error) noexcept
{
    switch (error) {
    case FPDF_ERR_FILE:
        return QUANTAPDF_ERROR_IO;
    case FPDF_ERR_FORMAT:
    case FPDF_ERR_PAGE:
        return QUANTAPDF_ERROR_FORMAT;
    case FPDF_ERR_PASSWORD:
        return QUANTAPDF_ERROR_PASSWORD;
    case FPDF_ERR_SECURITY:
        return QUANTAPDF_ERROR_UNSUPPORTED;
    case FPDF_ERR_SUCCESS:
    case FPDF_ERR_UNKNOWN:
    default:
        return QUANTAPDF_ERROR_BACKEND;
    }
}

quantapdf_status ensure_page_handle(quantapdf_pdfium_page *page) noexcept
{
    if (page->handle != nullptr)
        return QUANTAPDF_OK;
    page->handle = FPDF_LoadPage(page->document, page->page_index);
    if (page->handle == nullptr)
        return status_from_pdfium(FPDF_GetLastError());
    if (page->form_handle != nullptr) {
        FORM_OnAfterLoadPage(page->handle, page->form_handle);
        page->form_page_loaded = true;
    }
    return QUANTAPDF_OK;
}

void pdfium_form_invalidate(
    FPDF_FORMFILLINFO *, FPDF_PAGE, double, double, double, double)
{
}

bool checked_bitmap_size(int width, int height, int components,
                         size_t *out_stride, size_t *out_size) noexcept
{
    size_t const row = static_cast<size_t>(width) *
        static_cast<size_t>(components);

    if (width <= 0 || height <= 0 || components <= 0 ||
        row > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        static_cast<size_t>(height) >
            std::numeric_limits<size_t>::max() / row)
        return false;
    *out_stride = row;
    *out_size = row * static_cast<size_t>(height);
    return true;
}

size_t append_utf8(uint32_t codepoint, char output[4]) noexcept
{
    uint32_t rune = codepoint;

    if (rune > 0x10ffffu || (rune >= 0xd800u && rune <= 0xdfffu))
        rune = 0xfffdu;
    if (rune <= 0x7fu) {
        output[0] = static_cast<char>(rune);
        return 1;
    }
    if (rune <= 0x7ffu) {
        output[0] = static_cast<char>(0xc0u | (rune >> 6));
        output[1] = static_cast<char>(0x80u | (rune & 0x3fu));
        return 2;
    }
    if (rune <= 0xffffu) {
        output[0] = static_cast<char>(0xe0u | (rune >> 12));
        output[1] = static_cast<char>(0x80u | ((rune >> 6) & 0x3fu));
        output[2] = static_cast<char>(0x80u | (rune & 0x3fu));
        return 3;
    }
    output[0] = static_cast<char>(0xf0u | (rune >> 18));
    output[1] = static_cast<char>(0x80u | ((rune >> 12) & 0x3fu));
    output[2] = static_cast<char>(0x80u | ((rune >> 6) & 0x3fu));
    output[3] = static_cast<char>(0x80u | (rune & 0x3fu));
    return 4;
}

struct extracted_character {
    uint32_t codepoint;
    quantapdf_rect bounds;
    float font_size;
    float angle;
    uint32_t argb;
    bool break_before;
};

struct page_geometry {
    float left;
    float bottom;
    float right;
    float top;
    float scale;
    int rotation;
};

quantapdf_status load_page_geometry(
    FPDF_DOCUMENT document,
    int page_index,
    FPDF_PAGE page,
    page_geometry *out_geometry)
{
    FS_RECTF visible = {};
    FS_SIZEF size = {};

    if (!FPDF_GetPageBoundingBox(page, &visible))
        return QUANTAPDF_ERROR_FORMAT;
    out_geometry->left = std::min(visible.left, visible.right);
    out_geometry->bottom = std::min(visible.bottom, visible.top);
    out_geometry->right = std::max(visible.left, visible.right);
    out_geometry->top = std::max(visible.bottom, visible.top);
    if (!std::isfinite(out_geometry->left) ||
        !std::isfinite(out_geometry->bottom) ||
        !std::isfinite(out_geometry->right) ||
        !std::isfinite(out_geometry->top) ||
        out_geometry->right <= out_geometry->left ||
        out_geometry->top <= out_geometry->bottom)
        return QUANTAPDF_ERROR_FORMAT;
    out_geometry->rotation = FPDFPage_GetRotation(page);
    if (out_geometry->rotation < 0 || out_geometry->rotation > 3)
        return QUANTAPDF_ERROR_FORMAT;
    if (!FPDF_GetPageSizeByIndexF(document, page_index, &size))
        return status_from_pdfium(FPDF_GetLastError());
    float const raw_width = out_geometry->right - out_geometry->left;
    float const raw_height = out_geometry->top - out_geometry->bottom;
    out_geometry->scale =
        (out_geometry->rotation == 1 || out_geometry->rotation == 3) ?
        size.width / raw_height : size.width / raw_width;
    if (!std::isfinite(out_geometry->scale) || out_geometry->scale <= 0.0f)
        return QUANTAPDF_ERROR_FORMAT;
    return QUANTAPDF_OK;
}

quantapdf_point page_point(
    page_geometry const& geometry, float raw_x, float raw_y) noexcept
{
    quantapdf_point point = {};
    switch (geometry.rotation) {
    case 0:
        point.x = (raw_x - geometry.left) * geometry.scale;
        point.y = (geometry.top - raw_y) * geometry.scale;
        break;
    case 1:
        point.x = (raw_y - geometry.bottom) * geometry.scale;
        point.y = (raw_x - geometry.left) * geometry.scale;
        break;
    case 2:
        point.x = (geometry.right - raw_x) * geometry.scale;
        point.y = (raw_y - geometry.bottom) * geometry.scale;
        break;
    case 3:
        point.x = (geometry.top - raw_y) * geometry.scale;
        point.y = (geometry.right - raw_x) * geometry.scale;
        break;
    }
    return point;
}

quantapdf_rect page_rectangle(
    page_geometry const& geometry, FS_RECTF const& raw) noexcept
{
    quantapdf_point const points[4] = {
        page_point(geometry, raw.left, raw.bottom),
        page_point(geometry, raw.left, raw.top),
        page_point(geometry, raw.right, raw.bottom),
        page_point(geometry, raw.right, raw.top)
    };
    quantapdf_rect result = {
        points[0].x, points[0].y, points[0].x, points[0].y
    };
    for (size_t index = 1; index < 4u; ++index) {
        result.x0 = std::min(result.x0, points[index].x);
        result.y0 = std::min(result.y0, points[index].y);
        result.x1 = std::max(result.x1, points[index].x);
        result.y1 = std::max(result.y1, points[index].y);
    }
    return result;
}

void union_rect(quantapdf_rect *target, quantapdf_rect const& source) noexcept
{
    target->x0 = std::min(target->x0, source.x0);
    target->y0 = std::min(target->y0, source.y0);
    target->x1 = std::max(target->x1, source.x1);
    target->y1 = std::max(target->y1, source.y1);
}

void dispose_text_snapshot(quantapdf_text_page *text) noexcept
{
    if (text == nullptr)
        return;
    std::free(text->blocks);
    std::free(text->lines);
    std::free(text->spans);
    std::free(text->chars);
    std::free(text->strings);
    std::free(text);
}

quantapdf_status collect_characters(
    FPDF_TEXTPAGE text_page,
    page_geometry const& geometry,
    std::vector<extracted_character> *out_characters)
{
    int const count = FPDFText_CountChars(text_page);
    bool pending_break = false;

    if (count < 0)
        return QUANTAPDF_ERROR_BACKEND;
    out_characters->reserve(static_cast<size_t>(count));
    for (int index = 0; index < count; ++index) {
        uint32_t codepoint = static_cast<uint32_t>(
            FPDFText_GetUnicode(text_page, index));
        FS_RECTF box = {};
        unsigned int red = 0;
        unsigned int green = 0;
        unsigned int blue = 0;
        unsigned int alpha = 255;
        extracted_character character = {};

        if (codepoint == 0u)
            codepoint = 0xfffdu;
        if (codepoint == '\r' || codepoint == '\n') {
            pending_break = true;
            continue;
        }

        character.codepoint = codepoint;
        character.font_size = static_cast<float>(
            FPDFText_GetFontSize(text_page, index));
        character.angle = FPDFText_GetCharAngle(text_page, index);
        character.argb = 0xff000000u;
        if (FPDFText_GetFillColor(
                text_page, index, &red, &green, &blue, &alpha)) {
            character.argb = ((alpha & 0xffu) << 24) |
                ((red & 0xffu) << 16) |
                ((green & 0xffu) << 8) |
                (blue & 0xffu);
        }
        if (FPDFText_GetLooseCharBox(text_page, index, &box)) {
            character.bounds = page_rectangle(geometry, box);
        }
        character.break_before = pending_break;
        pending_break = false;

        if (FPDFText_IsGenerated(text_page, index) == 1 &&
            !out_characters->empty() && codepoint == ' ') {
            character.font_size = out_characters->back().font_size;
            character.angle = out_characters->back().angle;
            character.argb = out_characters->back().argb;
        }
        out_characters->push_back(character);
    }
    return QUANTAPDF_OK;
}

struct captured_image {
    quantapdf_quad quad;
    std::vector<unsigned char> pixels;
    int width;
    int height;
    int stride;
    int pixel_components;
    int source_components;
    int bits_per_component;
    int has_alpha;
};

FS_MATRIX multiply_matrix(FS_MATRIX const& parent, FS_MATRIX const& child) noexcept
{
    FS_MATRIX result = {};
    result.a = parent.a * child.a + parent.c * child.b;
    result.b = parent.b * child.a + parent.d * child.b;
    result.c = parent.a * child.c + parent.c * child.d;
    result.d = parent.b * child.c + parent.d * child.d;
    result.e = parent.a * child.e + parent.c * child.f + parent.e;
    result.f = parent.b * child.e + parent.d * child.f + parent.f;
    return result;
}

quantapdf_point image_point(
    FS_MATRIX const& matrix,
    float x,
    float y,
    page_geometry const& geometry) noexcept
{
    return page_point(
        geometry,
        matrix.a * x + matrix.c * y + matrix.e,
        matrix.b * x + matrix.d * y + matrix.f);
}

int image_source_components(int colorspace) noexcept
{
    switch (colorspace) {
    case FPDF_COLORSPACE_DEVICEGRAY:
    case FPDF_COLORSPACE_CALGRAY:
        return 1;
    case FPDF_COLORSPACE_DEVICECMYK:
        return 4;
    case FPDF_COLORSPACE_DEVICERGB:
    case FPDF_COLORSPACE_CALRGB:
    case FPDF_COLORSPACE_LAB:
    case FPDF_COLORSPACE_ICCBASED:
    case FPDF_COLORSPACE_INDEXED:
    default:
        return 3;
    }
}

bool read_bitmap_pixel(
    unsigned char const *row,
    int format,
    int x,
    unsigned char *red,
    unsigned char *green,
    unsigned char *blue,
    unsigned char *alpha) noexcept
{
    switch (format) {
    case FPDFBitmap_Gray:
        *red = *green = *blue = row[x];
        *alpha = 255;
        return true;
    case FPDFBitmap_BGR:
        *blue = row[static_cast<size_t>(x) * 3u];
        *green = row[static_cast<size_t>(x) * 3u + 1u];
        *red = row[static_cast<size_t>(x) * 3u + 2u];
        *alpha = 255;
        return true;
    case FPDFBitmap_BGRx:
    case FPDFBitmap_BGRA:
    case FPDFBitmap_BGRA_Premul:
        *blue = row[static_cast<size_t>(x) * 4u];
        *green = row[static_cast<size_t>(x) * 4u + 1u];
        *red = row[static_cast<size_t>(x) * 4u + 2u];
        *alpha = format == FPDFBitmap_BGRx ? 255 :
            row[static_cast<size_t>(x) * 4u + 3u];
        return true;
    default:
        return false;
    }
}

quantapdf_status decode_image(
    FPDF_DOCUMENT document,
    FPDF_PAGE page,
    FPDF_PAGEOBJECT object,
    FPDF_IMAGEOBJ_METADATA const& metadata,
    captured_image *out_image)
{
    FPDF_BITMAP bitmap = FPDFImageObj_GetRenderedBitmap(
        document, page, object);
    if (bitmap == nullptr)
        bitmap = FPDFImageObj_GetBitmap(object);
    if (bitmap == nullptr)
        return QUANTAPDF_ERROR_BACKEND;

    int const source_width = FPDFBitmap_GetWidth(bitmap);
    int const source_height = FPDFBitmap_GetHeight(bitmap);
    int const source_stride = FPDFBitmap_GetStride(bitmap);
    int const format = FPDFBitmap_GetFormat(bitmap);
    auto const *source = static_cast<unsigned char const *>(
        FPDFBitmap_GetBuffer(bitmap));
    if (source_width <= 0 || source_height <= 0 || source_stride <= 0 ||
        source == nullptr || metadata.width == 0u || metadata.height == 0u ||
        metadata.width > static_cast<unsigned int>(std::numeric_limits<int>::max()) ||
        metadata.height > static_cast<unsigned int>(std::numeric_limits<int>::max())) {
        FPDFBitmap_Destroy(bitmap);
        return QUANTAPDF_ERROR_FORMAT;
    }

    int const width = static_cast<int>(metadata.width);
    int const height = static_cast<int>(metadata.height);
    bool has_alpha = false;
    if (format == FPDFBitmap_BGRA || format == FPDFBitmap_BGRA_Premul) {
        for (int y = 0; y < source_height && !has_alpha; ++y) {
            auto const *row = source +
                static_cast<size_t>(source_stride) * static_cast<size_t>(y);
            for (int x = 0; x < source_width; ++x) {
                if (row[static_cast<size_t>(x) * 4u + 3u] != 255u) {
                    has_alpha = true;
                    break;
                }
            }
        }
    }
    int const components = has_alpha ? 4 : 3;
    size_t stride;
    size_t size;
    if (!checked_bitmap_size(width, height, components, &stride, &size)) {
        FPDFBitmap_Destroy(bitmap);
        return QUANTAPDF_ERROR_FORMAT;
    }
    out_image->pixels.resize(size);
    for (int y = 0; y < height; ++y) {
        int const source_y = static_cast<int>(
            static_cast<long long>(y) * source_height / height);
        auto const *source_row = source +
            static_cast<size_t>(source_stride) * static_cast<size_t>(source_y);
        auto *target_row = out_image->pixels.data() +
            stride * static_cast<size_t>(y);
        for (int x = 0; x < width; ++x) {
            int const source_x = static_cast<int>(
                static_cast<long long>(x) * source_width / width);
            unsigned char red;
            unsigned char green;
            unsigned char blue;
            unsigned char alpha;
            if (!read_bitmap_pixel(
                    source_row, format, source_x,
                    &red, &green, &blue, &alpha)) {
                FPDFBitmap_Destroy(bitmap);
                return QUANTAPDF_ERROR_UNSUPPORTED;
            }
            if (has_alpha && format != FPDFBitmap_BGRA_Premul) {
                red = static_cast<unsigned char>(
                    (static_cast<unsigned int>(red) * alpha + 127u) / 255u);
                green = static_cast<unsigned char>(
                    (static_cast<unsigned int>(green) * alpha + 127u) / 255u);
                blue = static_cast<unsigned char>(
                    (static_cast<unsigned int>(blue) * alpha + 127u) / 255u);
            }
            auto *target = target_row +
                static_cast<size_t>(x) * static_cast<size_t>(components);
            target[0] = red;
            target[1] = green;
            target[2] = blue;
            if (has_alpha)
                target[3] = alpha;
        }
    }
    FPDFBitmap_Destroy(bitmap);

    out_image->width = width;
    out_image->height = height;
    out_image->stride = static_cast<int>(stride);
    out_image->pixel_components = components;
    out_image->source_components = image_source_components(metadata.colorspace);
    out_image->bits_per_component = out_image->source_components > 0 ?
        static_cast<int>(metadata.bits_per_pixel) / out_image->source_components : 0;
    out_image->has_alpha = has_alpha ? 1 : 0;
    return QUANTAPDF_OK;
}

quantapdf_status collect_images_from_object(
    FPDF_DOCUMENT document,
    FPDF_PAGE page,
    FPDF_PAGEOBJECT object,
    FS_MATRIX const& parent_matrix,
    page_geometry const& geometry,
    std::vector<captured_image> *images)
{
    int const type = FPDFPageObj_GetType(object);
    FS_MATRIX object_matrix = { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
    (void)FPDFPageObj_GetMatrix(object, &object_matrix);
    FS_MATRIX const matrix = multiply_matrix(parent_matrix, object_matrix);

    if (type == FPDF_PAGEOBJ_FORM) {
        int const count = FPDFFormObj_CountObjects(object);
        if (count < 0)
            return QUANTAPDF_ERROR_BACKEND;
        for (int index = 0; index < count; ++index) {
            FPDF_PAGEOBJECT child = FPDFFormObj_GetObject(
                object, static_cast<unsigned long>(index));
            if (child == nullptr)
                return QUANTAPDF_ERROR_BACKEND;
            quantapdf_status const status = collect_images_from_object(
                document, page, child, matrix, geometry, images);
            if (status != QUANTAPDF_OK)
                return status;
        }
        return QUANTAPDF_OK;
    }
    if (type != FPDF_PAGEOBJ_IMAGE)
        return QUANTAPDF_OK;

    FPDF_IMAGEOBJ_METADATA metadata = {};
    if (!FPDFImageObj_GetImageMetadata(object, page, &metadata))
        return QUANTAPDF_ERROR_BACKEND;
    captured_image image = {};
    image.quad.ul = image_point(matrix, 0.0f, 1.0f, geometry);
    image.quad.ur = image_point(matrix, 1.0f, 1.0f, geometry);
    image.quad.ll = image_point(matrix, 0.0f, 0.0f, geometry);
    image.quad.lr = image_point(matrix, 1.0f, 0.0f, geometry);
    quantapdf_status const status = decode_image(
        document, page, object, metadata, &image);
    if (status != QUANTAPDF_OK)
        return status;
    images->push_back(std::move(image));
    return QUANTAPDF_OK;
}

struct captured_link {
    quantapdf_rect hotspot;
    quantapdf_link_kind kind;
    int target_page;
    quantapdf_point target;
    std::string uri;
};

quantapdf_status capture_link(
    FPDF_DOCUMENT document,
    FPDF_LINK link,
    page_geometry const& geometry,
    captured_link *out_link)
{
    FS_RECTF rect = {};
    if (!FPDFLink_GetAnnotRect(link, &rect))
        return QUANTAPDF_ERROR_BACKEND;
    out_link->hotspot = page_rectangle(geometry, rect);
    out_link->target_page = -1;

    FPDF_DEST destination = FPDFLink_GetDest(document, link);
    FPDF_ACTION action = FPDFLink_GetAction(link);
    if (destination == nullptr && action != nullptr &&
        FPDFAction_GetType(action) == PDFACTION_GOTO)
        destination = FPDFAction_GetDest(document, action);

    if (destination != nullptr) {
        FPDF_BOOL has_x = 0;
        FPDF_BOOL has_y = 0;
        FPDF_BOOL has_zoom = 0;
        FS_FLOAT x = 0.0f;
        FS_FLOAT y = 0.0f;
        FS_FLOAT zoom = 0.0f;
        FPDF_PAGE target_page = nullptr;

        out_link->kind = QUANTAPDF_LINK_INTERNAL;
        out_link->target_page = FPDFDest_GetDestPageIndex(
            document, destination);
        if (out_link->target_page < 0)
            return QUANTAPDF_ERROR_FORMAT;
        if (FPDFDest_GetLocationInPage(
                destination, &has_x, &has_y, &has_zoom,
                &x, &y, &zoom)) {
            page_geometry target_geometry = {};
            target_page = FPDF_LoadPage(document, out_link->target_page);
            if (target_page == nullptr)
                return status_from_pdfium(FPDF_GetLastError());
            quantapdf_status const geometry_status = load_page_geometry(
                document, out_link->target_page, target_page,
                &target_geometry);
            FPDF_ClosePage(target_page);
            if (geometry_status != QUANTAPDF_OK)
                return geometry_status;
            float raw_x = target_geometry.left;
            float raw_y = target_geometry.top;
            if (has_x)
                raw_x = x;
            if (has_y)
                raw_y = y;
            out_link->target = page_point(target_geometry, raw_x, raw_y);
        }
        return QUANTAPDF_OK;
    }

    if (action == nullptr || FPDFAction_GetType(action) != PDFACTION_URI)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    unsigned long const uri_size = FPDFAction_GetURIPath(
        document, action, nullptr, 0);
    if (uri_size == 0)
        return QUANTAPDF_ERROR_FORMAT;
    std::vector<char> uri(uri_size);
    if (FPDFAction_GetURIPath(
            document, action, uri.data(), uri_size) != uri_size)
        return QUANTAPDF_ERROR_BACKEND;
    out_link->kind = QUANTAPDF_LINK_URI;
    out_link->uri.assign(uri.data(), uri_size - 1u);
    return QUANTAPDF_OK;
}

} // namespace

extern "C" quantapdf_status quantapdf_pdfium_open_memory(
    const unsigned char *data,
    size_t size,
    const char *password_utf8,
    quantapdf_pdfium_document **out_document)
{
    quantapdf_status status;

    if (out_document == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_document = nullptr;
    if (data == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        auto document = std::make_unique<quantapdf_pdfium_document>();
        document->handle = nullptr;
        document->form_handle = nullptr;
        std::memset(&document->form_info, 0, sizeof(document->form_info));
        document->owned_data = nullptr;
        status = quantapdf_pdfium_enter();
        if (status != QUANTAPDF_OK)
            return status;
        pdfium_scope const scope(status);
        document->handle = FPDF_LoadMemDocument64(
            data, size, password_utf8);
        if (document->handle == nullptr)
            return status_from_pdfium(FPDF_GetLastError());
        if (FPDF_GetPageCount(document->handle) > 0) {
            FPDF_PAGE probe = FPDF_LoadPage(document->handle, 0);
            if (probe != nullptr) {
                FPDF_ClosePage(probe);
            } else {
                unsigned char *normalized = nullptr;
                size_t normalized_size = 0;
                if (quantapdf_qpdf_rewrite_memory(
                        data, size, &normalized, &normalized_size) ==
                    QUANTAPDF_OK) {
                    FPDF_DOCUMENT replacement = FPDF_LoadMemDocument64(
                        normalized, normalized_size, password_utf8);
                    FPDF_PAGE replacement_probe = replacement == nullptr ?
                        nullptr : FPDF_LoadPage(replacement, 0);
                    if (replacement_probe != nullptr) {
                        FPDF_ClosePage(replacement_probe);
                        FPDF_CloseDocument(document->handle);
                        document->handle = replacement;
                        document->owned_data = normalized;
                    } else {
                        if (replacement != nullptr)
                            FPDF_CloseDocument(replacement);
                        std::free(normalized);
                    }
                }
            }
        }
        document->form_info.version = 1;
        document->form_info.FFI_Invalidate = pdfium_form_invalidate;
        document->form_handle = FPDFDOC_InitFormFillEnvironment(
            document->handle, &document->form_info);
        *out_document = document.release();
        return QUANTAPDF_OK;
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_pdfium_page_count(
    quantapdf_pdfium_document *document,
    int *out_page_count)
{
    quantapdf_status status;
    int count;

    if (document == nullptr || out_page_count == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_pdfium_enter();
    if (status != QUANTAPDF_OK)
        return status;
    pdfium_scope const scope(status);
    count = FPDF_GetPageCount(document->handle);
    if (count < 0)
        return QUANTAPDF_ERROR_BACKEND;
    *out_page_count = count;
    return QUANTAPDF_OK;
}

extern "C" quantapdf_status quantapdf_pdfium_load_page(
    quantapdf_pdfium_document *document,
    int page_index,
    quantapdf_pdfium_page **out_page)
{
    quantapdf_status status;

    if (out_page == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_page = nullptr;
    if (document == nullptr || page_index < 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        auto page = std::make_unique<quantapdf_pdfium_page>();
        page->document = nullptr;
        page->form_handle = nullptr;
        page->page_index = -1;
        page->handle = nullptr;
        page->form_page_loaded = false;
        status = quantapdf_pdfium_enter();
        if (status != QUANTAPDF_OK)
            return status;
        pdfium_scope const scope(status);
        if (page_index >= FPDF_GetPageCount(document->handle))
            return QUANTAPDF_ERROR_ARGUMENT;
        page->document = document->handle;
        page->form_handle = document->form_handle;
        page->page_index = page_index;
        *out_page = page.release();
        return QUANTAPDF_OK;
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_pdfium_page_bounds(
    quantapdf_pdfium_page *page,
    quantapdf_rect *out_bounds)
{
    quantapdf_status status;
    float width;
    float height;

    if (page == nullptr || out_bounds == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_pdfium_enter();
    if (status != QUANTAPDF_OK)
        return status;
    pdfium_scope const scope(status);
    status = ensure_page_handle(page);
    if (status != QUANTAPDF_OK)
        return status;
    width = FPDF_GetPageWidthF(page->handle);
    height = FPDF_GetPageHeightF(page->handle);
    if (!std::isfinite(width) || !std::isfinite(height) ||
        width <= 0.0f || height <= 0.0f)
        return QUANTAPDF_ERROR_FORMAT;
    out_bounds->x0 = 0.0f;
    out_bounds->y0 = 0.0f;
    out_bounds->x1 = width;
    out_bounds->y1 = height;
    return QUANTAPDF_OK;
}

extern "C" quantapdf_status quantapdf_pdfium_render_page(
    quantapdf_pdfium_page *page,
    float dpi,
    float rotation_degrees,
    const quantapdf_rect *clip,
    double user_unit,
    int alpha,
    quantapdf_pdfium_bitmap *out_bitmap)
{
    quantapdf_status status;
    FS_SIZEF page_size = {};
    page_geometry geometry = {};
    FS_MATRIX matrix = {};
    FS_RECTF device_clip = {};
    FPDF_BITMAP pdfium_bitmap = nullptr;
    double const radians = static_cast<double>(rotation_degrees) *
        3.14159265358979323846 / 180.0;
    double cosine = std::cos(radians);
    double sine = std::sin(radians);
    double const scale = static_cast<double>(dpi) / 72.0;
    double source_x0;
    double source_y0;
    double source_x1;
    double source_y1;
    double min_x;
    double min_y;
    double max_x;
    double max_y;
    double public_a;
    double public_b;
    double public_c;
    double public_d;
    double public_e;
    double public_f;
    int width;
    int height;
    int components;
    int source_stride;
    size_t stride;
    size_t size;
    unsigned char *data = nullptr;

    if (out_bitmap == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    std::memset(out_bitmap, 0, sizeof(*out_bitmap));
    if (page == nullptr || !std::isfinite(dpi) || dpi <= 0.0f ||
        !std::isfinite(rotation_degrees) || !std::isfinite(user_unit) ||
        user_unit <= 0.0 || (alpha != 0 && alpha != 1))
        return QUANTAPDF_ERROR_ARGUMENT;

    if (std::abs(cosine) < 1.0e-12)
        cosine = 0.0;
    if (std::abs(sine) < 1.0e-12)
        sine = 0.0;

    status = quantapdf_pdfium_enter();
    if (status != QUANTAPDF_OK)
        return status;
    pdfium_scope const scope(status);
    status = ensure_page_handle(page);
    if (status != QUANTAPDF_OK)
        return status;
    if (!FPDF_GetPageSizeByIndexF(page->document, page->page_index, &page_size))
        return status_from_pdfium(FPDF_GetLastError());
    status = load_page_geometry(
        page->document, page->page_index, page->handle, &geometry);
    if (status != QUANTAPDF_OK)
        return status;
    geometry.scale = static_cast<float>(geometry.scale * user_unit);

    source_x0 = clip == nullptr ? 0.0 : clip->x0;
    source_y0 = clip == nullptr ? 0.0 : clip->y0;
    source_x1 = clip == nullptr ? page_size.width * user_unit : clip->x1;
    source_y1 = clip == nullptr ? page_size.height * user_unit : clip->y1;

    {
        double const source_width = (source_x1 - source_x0) * scale;
        double const source_height = (source_y1 - source_y0) * scale;
        double const xs[] = {
            0.0,
            cosine * source_width,
            -sine * source_height,
            cosine * source_width - sine * source_height
        };
        double const ys[] = {
            0.0,
            sine * source_width,
            cosine * source_height,
            sine * source_width + cosine * source_height
        };
        min_x = *std::min_element(std::begin(xs), std::end(xs));
        max_x = *std::max_element(std::begin(xs), std::end(xs));
        min_y = *std::min_element(std::begin(ys), std::end(ys));
        max_y = *std::max_element(std::begin(ys), std::end(ys));
    }

    if (!std::isfinite(min_x) || !std::isfinite(min_y) ||
        !std::isfinite(max_x) || !std::isfinite(max_y) ||
        max_x - min_x > static_cast<double>(std::numeric_limits<int>::max()) ||
        max_y - min_y > static_cast<double>(std::numeric_limits<int>::max()))
        return QUANTAPDF_ERROR_ARGUMENT;

    width = static_cast<int>(std::ceil(max_x) - std::floor(min_x));
    height = static_cast<int>(std::ceil(max_y) - std::floor(min_y));
    components = alpha != 0 ? 4 : 3;
    if (!checked_bitmap_size(width, height, components, &stride, &size))
        return QUANTAPDF_ERROR_ARGUMENT;

    pdfium_bitmap = FPDFBitmap_Create(width, height, alpha);
    if (pdfium_bitmap == nullptr)
        return QUANTAPDF_ERROR_NOMEM;
    FPDFBitmap_FillRect(
        pdfium_bitmap, 0, 0, width, height,
        alpha != 0 ? 0x00000000u : 0xffffffffu);

    switch (geometry.rotation) {
    case 0:
        public_a = geometry.scale;
        public_b = 0.0;
        public_c = 0.0;
        public_d = -geometry.scale;
        public_e = -geometry.left * geometry.scale;
        public_f = geometry.top * geometry.scale;
        break;
    case 1:
        public_a = 0.0;
        public_b = geometry.scale;
        public_c = geometry.scale;
        public_d = 0.0;
        public_e = -geometry.bottom * geometry.scale;
        public_f = -geometry.left * geometry.scale;
        break;
    case 2:
        public_a = -geometry.scale;
        public_b = 0.0;
        public_c = 0.0;
        public_d = geometry.scale;
        public_e = geometry.right * geometry.scale;
        public_f = -geometry.bottom * geometry.scale;
        break;
    case 3:
        public_a = 0.0;
        public_b = -geometry.scale;
        public_c = -geometry.scale;
        public_d = 0.0;
        public_e = geometry.top * geometry.scale;
        public_f = geometry.right * geometry.scale;
        break;
    default:
        return QUANTAPDF_ERROR_FORMAT;
    }
    matrix.a = static_cast<float>(
        scale * (cosine * public_a - sine * public_b));
    matrix.b = static_cast<float>(
        scale * (sine * public_a + cosine * public_b));
    matrix.c = static_cast<float>(
        scale * (cosine * public_c - sine * public_d));
    matrix.d = static_cast<float>(
        scale * (sine * public_c + cosine * public_d));
    matrix.e = static_cast<float>(
        scale * (cosine * (public_e - source_x0) -
                 sine * (public_f - source_y0)) -
        std::floor(min_x));
    matrix.f = static_cast<float>(
        scale * (sine * (public_e - source_x0) +
                 cosine * (public_f - source_y0)) -
        std::floor(min_y));
    device_clip.left = 0.0f;
    device_clip.top = 0.0f;
    device_clip.right = static_cast<float>(width);
    device_clip.bottom = static_cast<float>(height);
    if (cosine == 1.0 && sine == 0.0) {
        int const start_x = static_cast<int>(
            std::floor(-source_x0 * scale));
        int const start_y = static_cast<int>(
            std::floor(-source_y0 * scale));
        int const size_x = static_cast<int>(
            std::ceil(page_size.width * user_unit * scale));
        int const size_y = static_cast<int>(
            std::ceil(page_size.height * user_unit * scale));
        FPDF_RenderPageBitmap(
            pdfium_bitmap, page->handle,
            start_x, start_y, size_x, size_y,
            0, FPDF_ANNOT | FPDF_LCD_TEXT);
        if (page->form_handle != nullptr) {
            FPDF_FFLDraw(
                page->form_handle, pdfium_bitmap, page->handle,
                start_x, start_y, size_x, size_y, 0,
                FPDF_ANNOT | FPDF_LCD_TEXT);
        }
    } else {
        FPDF_RenderPageBitmapWithMatrix(
            pdfium_bitmap, page->handle, &matrix, &device_clip,
            FPDF_ANNOT | FPDF_LCD_TEXT);
    }

    try {
        data = static_cast<unsigned char *>(std::malloc(size));
        if (data == nullptr) {
            FPDFBitmap_Destroy(pdfium_bitmap);
            return QUANTAPDF_ERROR_NOMEM;
        }
        auto const *source = static_cast<unsigned char const *>(
            FPDFBitmap_GetBuffer(pdfium_bitmap));
        source_stride = FPDFBitmap_GetStride(pdfium_bitmap);
        if (source == nullptr || source_stride <= 0) {
            std::free(data);
            FPDFBitmap_Destroy(pdfium_bitmap);
            return QUANTAPDF_ERROR_BACKEND;
        }
        for (int y = 0; y < height; ++y) {
            auto const *source_row = source +
                static_cast<size_t>(source_stride) * static_cast<size_t>(y);
            auto *target_row = data + stride * static_cast<size_t>(y);
            for (int x = 0; x < width; ++x) {
                auto const *pixel = source_row + static_cast<size_t>(x) * 4u;
                auto *target = target_row +
                    static_cast<size_t>(x) * static_cast<size_t>(components);
                target[0] = pixel[2];
                target[1] = pixel[1];
                target[2] = pixel[0];
                if (alpha != 0)
                    target[3] = pixel[3];
            }
        }
    } catch (...) {
        std::free(data);
        FPDFBitmap_Destroy(pdfium_bitmap);
        return QUANTAPDF_ERROR_BACKEND;
    }

    FPDFBitmap_Destroy(pdfium_bitmap);
    out_bitmap->data = data;
    out_bitmap->size = size;
    out_bitmap->width = width;
    out_bitmap->height = height;
    out_bitmap->stride = static_cast<int>(stride);
    out_bitmap->components = components;
    return QUANTAPDF_OK;
}

extern "C" quantapdf_status quantapdf_pdfium_extract_text(
    quantapdf_pdfium_page *page,
    char **out_utf8,
    size_t *out_size)
{
    quantapdf_status status;
    FPDF_TEXTPAGE text_page = nullptr;

    if (out_utf8 != nullptr)
        *out_utf8 = nullptr;
    if (out_size != nullptr)
        *out_size = 0;
    if (page == nullptr || out_utf8 == nullptr || out_size == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        std::string text;
        status = quantapdf_pdfium_enter();
        if (status != QUANTAPDF_OK)
            return status;
        pdfium_scope const scope(status);
        status = ensure_page_handle(page);
        if (status != QUANTAPDF_OK)
            return status;
        text_page = FPDFText_LoadPage(page->handle);
        if (text_page == nullptr)
            return QUANTAPDF_ERROR_BACKEND;

        int const count = FPDFText_CountChars(text_page);
        if (count < 0) {
            FPDFText_ClosePage(text_page);
            return QUANTAPDF_ERROR_BACKEND;
        }
        text.reserve(static_cast<size_t>(count));
        for (int index = 0; index < count; ++index) {
            uint32_t codepoint = static_cast<uint32_t>(
                FPDFText_GetUnicode(text_page, index));
            char encoded[4];
            size_t encoded_size;

            if (codepoint == 0u)
                codepoint = 0xfffdu;
            encoded_size = append_utf8(codepoint, encoded);
            text.append(encoded, encoded_size);
        }
        FPDFText_ClosePage(text_page);
        text_page = nullptr;

        auto *copy = static_cast<char *>(std::malloc(text.size() + 1u));
        if (copy == nullptr)
            return QUANTAPDF_ERROR_NOMEM;
        if (!text.empty())
            std::memcpy(copy, text.data(), text.size());
        copy[text.size()] = '\0';
        *out_utf8 = copy;
        *out_size = text.size();
        return QUANTAPDF_OK;
    } catch (std::bad_alloc const&) {
        if (text_page != nullptr)
            FPDFText_ClosePage(text_page);
        return QUANTAPDF_ERROR_NOMEM;
    } catch (...) {
        if (text_page != nullptr)
            FPDFText_ClosePage(text_page);
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_pdfium_extract_structured_text(
    quantapdf_pdfium_page *page,
    quantapdf_text_page **out_text)
{
    quantapdf_status status;
    FPDF_TEXTPAGE text_page = nullptr;
    quantapdf_text_page *text = nullptr;

    if (out_text == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_text = nullptr;
    if (page == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        page_geometry geometry = {};
        std::vector<extracted_character> characters;
        size_t line_capacity = 0;
        size_t string_capacity;
        size_t line_index = 0;
        size_t span_index = 0;
        size_t string_position = 0;
        bool line_open = false;
        bool span_open = false;

        status = quantapdf_pdfium_enter();
        if (status != QUANTAPDF_OK)
            return status;
        pdfium_scope const scope(status);
        status = ensure_page_handle(page);
        if (status != QUANTAPDF_OK)
            return status;
        status = load_page_geometry(
            page->document, page->page_index, page->handle, &geometry);
        if (status != QUANTAPDF_OK)
            return status;
        text_page = FPDFText_LoadPage(page->handle);
        if (text_page == nullptr)
            return QUANTAPDF_ERROR_BACKEND;
        status = collect_characters(text_page, geometry, &characters);
        FPDFText_ClosePage(text_page);
        text_page = nullptr;
        if (status != QUANTAPDF_OK)
            return status;

        if (!characters.empty()) {
            line_capacity = 1;
            for (size_t index = 1; index < characters.size(); ++index) {
                if (characters[index].break_before)
                    ++line_capacity;
            }
        }
        if (characters.size() >
            (std::numeric_limits<size_t>::max() - 1u) / 5u)
            return QUANTAPDF_ERROR_NOMEM;
        string_capacity = characters.size() * 5u + 1u;

        text = static_cast<quantapdf_text_page *>(
            std::calloc(1u, sizeof(*text)));
        if (text == nullptr)
            return QUANTAPDF_ERROR_NOMEM;
        if (!characters.empty()) {
            text->blocks = static_cast<quantapdf_text_block_internal *>(
                std::calloc(1u, sizeof(*text->blocks)));
            text->lines = static_cast<quantapdf_text_line_internal *>(
                std::calloc(line_capacity, sizeof(*text->lines)));
            text->spans = static_cast<quantapdf_text_span_internal *>(
                std::calloc(characters.size(), sizeof(*text->spans)));
            text->chars = static_cast<quantapdf_text_char_internal *>(
                std::calloc(characters.size(), sizeof(*text->chars)));
        }
        text->strings = static_cast<char *>(std::malloc(string_capacity));
        if (text->strings == nullptr ||
            (!characters.empty() &&
             (text->blocks == nullptr || text->lines == nullptr ||
              text->spans == nullptr || text->chars == nullptr))) {
            dispose_text_snapshot(text);
            return QUANTAPDF_ERROR_NOMEM;
        }
        text->strings[0] = '\0';

        for (size_t char_index = 0; char_index < characters.size(); ++char_index) {
            extracted_character const& source = characters[char_index];
            bool const new_line = !line_open || source.break_before;
            bool new_span;

            if (new_line) {
                if (span_open)
                    text->strings[string_position++] = '\0';
                if (line_open) {
                    auto *previous_line = &text->lines[line_index - 1u];
                    previous_line->span_count = span_index -
                        previous_line->first_span;
                }
                auto *line = &text->lines[line_index++];
                line->bounds = source.bounds;
                line->direction_x = std::cos(source.angle);
                line->direction_y = -std::sin(source.angle);
                line->writing_mode = 0;
                line->first_span = span_index;
                line_open = true;
                span_open = false;
            } else {
                union_rect(&text->lines[line_index - 1u].bounds, source.bounds);
            }

            new_span = !span_open;
            if (span_open) {
                auto const& previous = text->spans[span_index - 1u];
                new_span = std::abs(previous.font_size - source.font_size) > 0.001f ||
                    previous.argb != source.argb;
            }
            if (new_span) {
                if (span_open)
                    text->strings[string_position++] = '\0';
                auto *span = &text->spans[span_index++];
                span->bounds = source.bounds;
                span->font_size = source.font_size;
                span->argb = source.argb;
                span->bidi_level = 0;
                span->flags = 0;
                span->first_char = char_index;
                span->text_offset = string_position;
                span_open = true;
            } else {
                union_rect(&text->spans[span_index - 1u].bounds, source.bounds);
            }

            {
                auto *target = &text->chars[char_index];
                char encoded[4];
                size_t const encoded_size = append_utf8(
                    source.codepoint, encoded);
                auto *span = &text->spans[span_index - 1u];

                target->codepoint = source.codepoint;
                target->bidi = 0;
                target->flags = 0;
                target->quad.ul = { source.bounds.x0, source.bounds.y0 };
                target->quad.ur = { source.bounds.x1, source.bounds.y0 };
                target->quad.ll = { source.bounds.x0, source.bounds.y1 };
                target->quad.lr = { source.bounds.x1, source.bounds.y1 };
                target->span_index = span_index - 1u;
                std::memcpy(
                    text->strings + string_position, encoded, encoded_size);
                string_position += encoded_size;
                span->text_size += encoded_size;
                ++span->char_count;
            }
        }

        if (span_open)
            text->strings[string_position++] = '\0';
        if (line_open) {
            auto *last_line = &text->lines[line_index - 1u];
            last_line->span_count = span_index - last_line->first_span;
            text->blocks[0].bounds = text->lines[0].bounds;
            for (size_t index = 1; index < line_index; ++index)
                union_rect(&text->blocks[0].bounds, text->lines[index].bounds);
            text->blocks[0].first_line = 0;
            text->blocks[0].line_count = line_index;
            text->block_count = 1;
        }
        text->line_count = line_index;
        text->span_count = span_index;
        text->char_count = characters.size();
        text->string_size = string_position;
        *out_text = text;
        return QUANTAPDF_OK;
    } catch (std::bad_alloc const&) {
        if (text_page != nullptr)
            FPDFText_ClosePage(text_page);
        dispose_text_snapshot(text);
        return QUANTAPDF_ERROR_NOMEM;
    } catch (...) {
        if (text_page != nullptr)
            FPDFText_ClosePage(text_page);
        dispose_text_snapshot(text);
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_pdfium_extract_images(
    quantapdf_pdfium_page *page,
    quantapdf_image_page **out_images)
{
    quantapdf_status status;
    quantapdf_image_page *snapshot = nullptr;

    if (out_images == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_images = nullptr;
    if (page == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        page_geometry geometry = {};
        FS_MATRIX const identity = {
            1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f
        };
        std::vector<captured_image> images;

        status = quantapdf_pdfium_enter();
        if (status != QUANTAPDF_OK)
            return status;
        pdfium_scope const scope(status);
        status = ensure_page_handle(page);
        if (status != QUANTAPDF_OK)
            return status;
        status = load_page_geometry(
            page->document, page->page_index, page->handle, &geometry);
        if (status != QUANTAPDF_OK)
            return status;

        int const count = FPDFPage_CountObjects(page->handle);
        if (count < 0)
            return QUANTAPDF_ERROR_BACKEND;
        for (int index = 0; index < count; ++index) {
            FPDF_PAGEOBJECT object = FPDFPage_GetObject(page->handle, index);
            if (object == nullptr)
                return QUANTAPDF_ERROR_BACKEND;
            status = collect_images_from_object(
                page->document, page->handle, object, identity,
                geometry, &images);
            if (status != QUANTAPDF_OK)
                return status;
        }

        snapshot = static_cast<quantapdf_image_page *>(
            std::calloc(1u, sizeof(*snapshot)));
        if (snapshot == nullptr)
            return QUANTAPDF_ERROR_NOMEM;
        if (!images.empty()) {
            snapshot->items = static_cast<quantapdf_image_occurrence_internal *>(
                std::calloc(images.size(), sizeof(*snapshot->items)));
            if (snapshot->items == nullptr) {
                std::free(snapshot);
                return QUANTAPDF_ERROR_NOMEM;
            }
        }
        snapshot->count = images.size();
        for (size_t index = 0; index < images.size(); ++index) {
            captured_image const& source = images[index];
            auto *target = &snapshot->items[index];
            target->pixels = static_cast<unsigned char *>(
                std::malloc(source.pixels.size()));
            if (target->pixels == nullptr) {
                for (size_t cleanup = 0; cleanup < index; ++cleanup)
                    std::free(snapshot->items[cleanup].pixels);
                std::free(snapshot->items);
                std::free(snapshot);
                return QUANTAPDF_ERROR_NOMEM;
            }
            std::memcpy(
                target->pixels, source.pixels.data(), source.pixels.size());
            target->quad = source.quad;
            target->pixel_size = source.pixels.size();
            target->pixel_width = source.width;
            target->pixel_height = source.height;
            target->pixel_stride = source.stride;
            target->pixel_components = source.pixel_components;
            target->components = source.source_components;
            target->bits_per_component = source.bits_per_component;
            target->has_alpha = source.has_alpha;
        }
        *out_images = snapshot;
        return QUANTAPDF_OK;
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_pdfium_extract_links(
    quantapdf_pdfium_page *page,
    quantapdf_link_page **out_links)
{
    quantapdf_status status;

    if (out_links == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_links = nullptr;
    if (page == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        page_geometry geometry = {};
        std::vector<captured_link> links;
        int position = 0;
        FPDF_LINK link = nullptr;

        status = quantapdf_pdfium_enter();
        if (status != QUANTAPDF_OK)
            return status;
        pdfium_scope const scope(status);
        status = ensure_page_handle(page);
        if (status != QUANTAPDF_OK)
            return status;
        status = load_page_geometry(
            page->document, page->page_index, page->handle, &geometry);
        if (status != QUANTAPDF_OK)
            return status;

        while (FPDFLink_Enumerate(page->handle, &position, &link)) {
            captured_link captured = {};
            status = capture_link(
                page->document, link, geometry, &captured);
            if (status != QUANTAPDF_OK)
                return status;
            links.push_back(std::move(captured));
        }

        auto *snapshot = static_cast<quantapdf_link_page *>(
            std::calloc(1u, sizeof(quantapdf_link_page)));
        if (snapshot == nullptr)
            return QUANTAPDF_ERROR_NOMEM;
        if (!links.empty()) {
            snapshot->items = static_cast<quantapdf_link_internal *>(
                std::calloc(links.size(), sizeof(*snapshot->items)));
            if (snapshot->items == nullptr) {
                std::free(snapshot);
                return QUANTAPDF_ERROR_NOMEM;
            }
        }
        snapshot->count = links.size();
        for (size_t index = 0; index < links.size(); ++index) {
            captured_link const& source = links[index];
            auto *target = &snapshot->items[index];
            target->hotspot = source.hotspot;
            target->kind = source.kind;
            target->target_page = source.target_page;
            target->target = source.target;
            if (!source.uri.empty()) {
                target->uri = static_cast<char *>(
                    std::malloc(source.uri.size() + 1u));
                if (target->uri == nullptr) {
                    for (size_t cleanup = 0; cleanup < index; ++cleanup)
                        std::free(snapshot->items[cleanup].uri);
                    std::free(snapshot->items);
                    std::free(snapshot);
                    return QUANTAPDF_ERROR_NOMEM;
                }
                std::memcpy(
                    target->uri, source.uri.data(), source.uri.size());
                target->uri[source.uri.size()] = '\0';
                target->uri_size = source.uri.size();
            }
        }
        *out_links = snapshot;
        return QUANTAPDF_OK;
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" void quantapdf_pdfium_drop_page(quantapdf_pdfium_page *page)
{
    quantapdf_status status;

    if (page == nullptr)
        return;
    status = quantapdf_pdfium_enter();
    if (status != QUANTAPDF_OK)
        return;
    pdfium_scope const scope(status);
    if (page->handle != nullptr && page->form_page_loaded)
        FORM_OnBeforeClosePage(page->handle, page->form_handle);
    if (page->handle != nullptr)
        FPDF_ClosePage(page->handle);
    delete page;
}

extern "C" void quantapdf_pdfium_close(
    quantapdf_pdfium_document *document)
{
    quantapdf_status status;

    if (document == nullptr)
        return;
    status = quantapdf_pdfium_enter();
    if (status != QUANTAPDF_OK)
        return;
    pdfium_scope const scope(status);
    FPDFDOC_ExitFormFillEnvironment(document->form_handle);
    FPDF_CloseDocument(document->handle);
    std::free(document->owned_data);
    delete document;
}
