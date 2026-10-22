#include "pdfium_document.h"
#include "render/render_plan.h"

#include "pdfium_runtime.h"
#include "qpdf_document.h"

#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif

#include <fpdfview.h>
#include <fpdf_edit.h>
#include <fpdf_formfill.h>

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
    quantapdf_render_plan plan = {};
    quantapdf_render_transform source_to_page = {};
    quantapdf_affine_transform source_to_device = {};
    FS_MATRIX matrix = {};
    FS_RECTF device_clip = {};
    FPDF_BITMAP pdfium_bitmap = nullptr;
    int source_stride;
    unsigned char *data = nullptr;

    if (out_bitmap == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    std::memset(out_bitmap, 0, sizeof(*out_bitmap));
    if (page == nullptr || !std::isfinite(dpi) || dpi <= 0.0f ||
        !std::isfinite(rotation_degrees) || !std::isfinite(user_unit) ||
        user_unit <= 0.0 || (alpha != 0 && alpha != 1))
        return QUANTAPDF_ERROR_ARGUMENT;

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
    status = quantapdf_render_plan_create(
        page_size.width * user_unit, page_size.height * user_unit,
        dpi, rotation_degrees, clip, alpha, &plan);
    if (status != QUANTAPDF_OK)
        return status;
    int const width = plan.width;
    int const height = plan.height;
    int const components = plan.components;
    size_t const stride = static_cast<size_t>(plan.stride);
    size_t const size = plan.size;

    if (!plan.aligned) {
        // PDFium first applies its own display matrix (CropBox, intrinsic
        // rotation and downward y). Only UserUnit remains before our plan.
        source_to_page.a = user_unit;
        source_to_page.d = user_unit;
        status = quantapdf_render_plan_map_source(&plan, &source_to_page, &source_to_device);
        if (status != QUANTAPDF_OK)
            return status;
        matrix = {source_to_device.a, source_to_device.b, source_to_device.c,
                  source_to_device.d, source_to_device.e, source_to_device.f};
        device_clip = {0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)};
    }
    pdfium_bitmap = FPDFBitmap_Create(width, height, alpha);
    if (pdfium_bitmap == nullptr)
        return QUANTAPDF_ERROR_NOMEM;
    FPDFBitmap_FillRect(pdfium_bitmap, 0, 0, width, height, plan.background_argb);
    if (plan.aligned) {
        FPDF_RenderPageBitmap(
            pdfium_bitmap, page->handle,
            plan.start_x, plan.start_y, plan.size_x, plan.size_y,
            0, FPDF_ANNOT | FPDF_LCD_TEXT);
        if (page->form_handle != nullptr) {
            FPDF_FFLDraw(
                page->form_handle, pdfium_bitmap, page->handle,
                plan.start_x, plan.start_y, plan.size_x, plan.size_y, 0,
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
