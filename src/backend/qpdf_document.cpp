#include "validation/validation.h"
#include "graph/identity.h"
#include "qpdf_document_internal.h"
#include "qpdf_edit.h"
#include "edit_fault.h"
#include "qpdf_graph.h"
#include "qpdf_text.h"
#include "graph/graph_query.h"
#include "jpeg_encoder.h"
#include "secure_random.h"
#include "snapshot/annotation_snapshot.h"
#include "snapshot/form_snapshot.h"
#include "snapshot/outline_snapshot.h"
#include "snapshot/link_snapshot.h"

#include <qpdf/Constants.h>
#include <qpdf/Pipeline.hh>
#include <qpdf/Pl_DCT.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFAnnotationObjectHelper.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>
#include <qpdf/QUtil.hh>
#include <qpdf/RandomDataProvider.hh>

#include <array>
#include <climits>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <new>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

struct quantapdf_qpdf_edit_annotation_entry {
    QPDFObjectHandle object = QPDFObjectHandle::newNull();
    int page_index = -1;
    bool live = false;
};

struct quantapdf_qpdf_edit_form_entry {
    QPDFObjectHandle object = QPDFObjectHandle::newNull();
};

struct quantapdf_qpdf_edit {
    bool rollback_failed = false;
    std::shared_ptr<QPDF> pdf;
    std::vector<unsigned char> source_bytes;
    uint64_t annotation_scope = 0;
    uint64_t form_scope = 0;
    std::vector<quantapdf_qpdf_edit_annotation_entry> annotations;
    std::vector<quantapdf_qpdf_edit_form_entry> forms;
};

static std::shared_ptr<QPDF> quantapdf_qpdf_fresh_document(
    quantapdf_qpdf_document const& document,
    char const *description)
{
    auto pdf = QPDF::create();
    pdf->processMemoryFile(
        description,
        reinterpret_cast<char const *>(document.source_data),
        document.source_size,
        document.password);
    return pdf;
}

static quantapdf_status quantapdf_qpdf_write_memory(
    QPDF& pdf,
    unsigned char **out_data,
    size_t *out_size)
{
    QPDFWriter writer(pdf);
    writer.setOutputMemory();
    writer.setDeterministicID(true);
    writer.setObjectStreamMode(qpdf_o_disable);
    writer.setStreamDataMode(qpdf_s_preserve);
    writer.setPreserveUnreferencedObjects(false);
    writer.write();
    std::unique_ptr<Buffer> buffer(writer.getBuffer());
    if (buffer == nullptr || buffer->getSize() == 0 ||
        buffer->getBuffer() == nullptr)
        return QUANTAPDF_ERROR_BACKEND;
    auto *copy = static_cast<unsigned char *>(
        std::malloc(buffer->getSize()));
    if (copy == nullptr)
        return QUANTAPDF_ERROR_NOMEM;
    std::memcpy(copy, buffer->getBuffer(), buffer->getSize());
    *out_data = copy;
    *out_size = buffer->getSize();
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_status_from_qpdf(
    QPDFExc const& error) noexcept
{
    switch (error.getErrorCode()) {
    case qpdf_e_password:
        return QUANTAPDF_ERROR_PASSWORD;
    case qpdf_e_unsupported:
        return QUANTAPDF_ERROR_UNSUPPORTED;
    case qpdf_e_system:
        return QUANTAPDF_ERROR_IO;
    case qpdf_e_damaged_pdf:
    case qpdf_e_pages:
    case qpdf_e_object:
    case qpdf_e_json:
    case qpdf_e_linearization:
        return QUANTAPDF_ERROR_FORMAT;
    case qpdf_e_success:
    case qpdf_e_internal:
    default:
        return QUANTAPDF_ERROR_BACKEND;
    }
}

QPDFObjectHandle quantapdf_qpdf_inherited_value(
    QPDFObjectHandle current,
    char const *key)
{
    while (!current.isNull()) {
        QPDFObjectHandle value = current.getKey(key);
        if (!value.isNull())
            return value;
        current = current.getKey("/Parent");
    }
    return QPDFObjectHandle::newNull();
}

static quantapdf_annotation_type quantapdf_qpdf_annotation_type(
    std::string const& name) noexcept
{
    if (name == "/Text") return QUANTAPDF_ANNOTATION_TEXT;
    if (name == "/FreeText") return QUANTAPDF_ANNOTATION_FREE_TEXT;
    if (name == "/Line") return QUANTAPDF_ANNOTATION_LINE;
    if (name == "/Square") return QUANTAPDF_ANNOTATION_SQUARE;
    if (name == "/Circle") return QUANTAPDF_ANNOTATION_CIRCLE;
    if (name == "/Polygon") return QUANTAPDF_ANNOTATION_POLYGON;
    if (name == "/PolyLine") return QUANTAPDF_ANNOTATION_POLY_LINE;
    if (name == "/Highlight") return QUANTAPDF_ANNOTATION_HIGHLIGHT;
    if (name == "/Underline") return QUANTAPDF_ANNOTATION_UNDERLINE;
    if (name == "/Squiggly") return QUANTAPDF_ANNOTATION_SQUIGGLY;
    if (name == "/StrikeOut") return QUANTAPDF_ANNOTATION_STRIKE_OUT;
    if (name == "/Redact") return QUANTAPDF_ANNOTATION_REDACT;
    if (name == "/Stamp") return QUANTAPDF_ANNOTATION_STAMP;
    if (name == "/Caret") return QUANTAPDF_ANNOTATION_CARET;
    if (name == "/Ink") return QUANTAPDF_ANNOTATION_INK;
    if (name == "/FileAttachment") return QUANTAPDF_ANNOTATION_FILE_ATTACHMENT;
    if (name == "/Sound") return QUANTAPDF_ANNOTATION_SOUND;
    if (name == "/Movie") return QUANTAPDF_ANNOTATION_MOVIE;
    if (name == "/RichMedia") return QUANTAPDF_ANNOTATION_RICH_MEDIA;
    if (name == "/Screen") return QUANTAPDF_ANNOTATION_SCREEN;
    if (name == "/PrinterMark") return QUANTAPDF_ANNOTATION_PRINTER_MARK;
    if (name == "/TrapNet") return QUANTAPDF_ANNOTATION_TRAP_NET;
    if (name == "/Watermark") return QUANTAPDF_ANNOTATION_WATERMARK;
    if (name == "/3D") return QUANTAPDF_ANNOTATION_3D;
    if (name == "/Projection") return QUANTAPDF_ANNOTATION_PROJECTION;
    return QUANTAPDF_ANNOTATION_UNKNOWN;
}

struct quantapdf_qpdf_captured_annotation {
    quantapdf_annotation_type type;
    quantapdf_rect bounds;
    uint32_t flags;
    std::string contents;
    bool has_contents;
};

struct quantapdf_qpdf_captured_outline_node {
    size_t parent_index = SIZE_MAX;
    size_t first_child_index = SIZE_MAX;
    size_t next_sibling_index = SIZE_MAX;
    quantapdf_outline_destination_kind destination_kind =
        QUANTAPDF_OUTLINE_DESTINATION_NONE;
    int target_page = -1;
    quantapdf_point target = {};
    std::string title;
    std::string uri;
    bool has_title = false;
    bool is_open = false;
};

static bool quantapdf_qpdf_same_object(
    QPDFObjectHandle const& left,
    QPDFObjectHandle const& right)
{
    return left.isSameObjectAs(right);
}

static quantapdf_status quantapdf_qpdf_decode_outline_destination(
    QPDF& pdf,
    QPDFObjectHandle const& item,
    quantapdf_qpdf_captured_outline_node *node)
{
    QPDFObjectHandle destination = item.getKey("/Dest");
    QPDFObjectHandle action = item.getKey("/A");

    if (destination.isNull() && !action.isNull()) {
        if (!action.isDictionary())
            return QUANTAPDF_ERROR_FORMAT;
        QPDFObjectHandle action_type = action.getKey("/S");
        if (!action_type.isName())
            return QUANTAPDF_ERROR_FORMAT;
        if (action_type.getName() == "/URI") {
            QPDFObjectHandle uri = action.getKey("/URI");
            if (!uri.isString())
                return QUANTAPDF_ERROR_FORMAT;
            node->destination_kind = QUANTAPDF_OUTLINE_DESTINATION_URI;
            node->uri = uri.getUTF8Value();
            return QUANTAPDF_OK;
        }
        if (action_type.getName() != "/GoTo")
            return QUANTAPDF_ERROR_UNSUPPORTED;
        destination = action.getKey("/D");
    }
    if (destination.isNull())
        return QUANTAPDF_OK;
    if (!destination.isArray() || destination.getArrayNItems() < 1)
        return QUANTAPDF_ERROR_FORMAT;

    QPDFObjectHandle destination_page = destination.getArrayItem(0);
    auto const& pages = pdf.getAllPages();
    int page_index = -1;
    for (size_t index = 0; index < pages.size(); ++index) {
        if (quantapdf_qpdf_same_object(destination_page, pages[index])) {
            page_index = static_cast<int>(index);
            break;
        }
    }
    if (page_index < 0)
        return QUANTAPDF_ERROR_FORMAT;

    QPDFObjectHandle crop = quantapdf_qpdf_inherited_value(
        pages[static_cast<size_t>(page_index)], "/CropBox");
    if (crop.isNull()) {
        crop = quantapdf_qpdf_inherited_value(
            pages[static_cast<size_t>(page_index)], "/MediaBox");
    }
    if (!crop.isArray() || crop.getArrayNItems() != 4 ||
        !crop.getArrayItem(0).isNumber() ||
        !crop.getArrayItem(3).isNumber())
        return QUANTAPDF_ERROR_FORMAT;
    double const crop_left = crop.getArrayItem(0).getNumericValue();
    double const crop_top = crop.getArrayItem(3).getNumericValue();
    double user_unit = 1.0;
    QPDFObjectHandle user_unit_object =
        pages[static_cast<size_t>(page_index)].getKey("/UserUnit");
    if (!user_unit_object.isNull()) {
        if (!user_unit_object.isNumber())
            return QUANTAPDF_ERROR_FORMAT;
        user_unit = user_unit_object.getNumericValue();
    }

    double x = crop_left;
    double y = crop_top;
    if (destination.getArrayNItems() >= 4 &&
        destination.getArrayItem(1).isName() &&
        destination.getArrayItem(1).getName() == "/XYZ") {
        QPDFObjectHandle x_object = destination.getArrayItem(2);
        QPDFObjectHandle y_object = destination.getArrayItem(3);
        if (!x_object.isNull()) {
            if (!x_object.isNumber())
                return QUANTAPDF_ERROR_FORMAT;
            x = x_object.getNumericValue();
        }
        if (!y_object.isNull()) {
            if (!y_object.isNumber())
                return QUANTAPDF_ERROR_FORMAT;
            y = y_object.getNumericValue();
        }
    }
    node->destination_kind = QUANTAPDF_OUTLINE_DESTINATION_INTERNAL;
    node->target_page = page_index;
    node->target.x = static_cast<float>((x - crop_left) * user_unit);
    node->target.y = static_cast<float>((crop_top - y) * user_unit);
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_qpdf_walk_outline_siblings(
    QPDF& pdf,
    QPDFObjectHandle first,
    QPDFObjectHandle last,
    QPDFObjectHandle const& parent_object,
    size_t parent_index,
    size_t depth,
    std::vector<quantapdf_qpdf_captured_outline_node> *nodes,
    std::set<QPDFObjGen> *seen)
{
    if (first.isNull() != last.isNull())
        return QUANTAPDF_ERROR_FORMAT;
    if (first.isNull())
        return QUANTAPDF_OK;
    if (depth > 256u)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    QPDFObjectHandle previous = QPDFObjectHandle::newNull();
    size_t previous_index = SIZE_MAX;
    QPDFObjectHandle current = first;
    while (!current.isNull()) {
        if (!current.isDictionary() || !current.isIndirect())
            return QUANTAPDF_ERROR_FORMAT;
        if (!seen->insert(current.getObjGen()).second)
            return QUANTAPDF_ERROR_FORMAT;
        if (!quantapdf_qpdf_same_object(
                current.getKey("/Parent"), parent_object))
            return QUANTAPDF_ERROR_FORMAT;

        QPDFObjectHandle current_previous = current.getKey("/Prev");
        if (previous.isNull()) {
            if (!current_previous.isNull())
                return QUANTAPDF_ERROR_FORMAT;
        } else if (!quantapdf_qpdf_same_object(current_previous, previous)) {
            return QUANTAPDF_ERROR_FORMAT;
        }

        size_t const current_index = nodes->size();
        if (previous_index != SIZE_MAX)
            (*nodes)[previous_index].next_sibling_index = current_index;
        nodes->emplace_back();
        auto *node = &nodes->back();
        node->parent_index = parent_index;

        QPDFObjectHandle title = current.getKey("/Title");
        if (!title.isNull()) {
            if (!title.isString())
                return QUANTAPDF_ERROR_FORMAT;
            node->title = title.getUTF8Value();
            node->has_title = true;
        }
        QPDFObjectHandle count = current.getKey("/Count");
        if (!count.isNull()) {
            if (!count.isInteger())
                return QUANTAPDF_ERROR_FORMAT;
            node->is_open = count.getIntValue() > 0;
        }
        quantapdf_status status = quantapdf_qpdf_decode_outline_destination(
            pdf, current, node);
        if (status != QUANTAPDF_OK)
            return status;

        QPDFObjectHandle child_first = current.getKey("/First");
        QPDFObjectHandle child_last = current.getKey("/Last");
        if (!child_first.isNull())
            node->first_child_index = nodes->size();
        status = quantapdf_qpdf_walk_outline_siblings(
            pdf, child_first, child_last, current, current_index,
            depth + 1u, nodes, seen);
        if (status != QUANTAPDF_OK)
            return status;

        QPDFObjectHandle next = current.getKey("/Next");
        if (next.isNull()) {
            if (!quantapdf_qpdf_same_object(current, last))
                return QUANTAPDF_ERROR_FORMAT;
            return QUANTAPDF_OK;
        }
        previous = current;
        previous_index = current_index;
        current = next;
    }
    return QUANTAPDF_ERROR_FORMAT;
}

static quantapdf_status quantapdf_qpdf_read_rectangle(
    QPDFObjectHandle const& object,
    double values[4], bool allow_empty = false)
{
    if (!object.isArray() || object.getArrayNItems() != 4)
        return QUANTAPDF_ERROR_FORMAT;
    double input[4];
    for (int index = 0; index < 4; ++index) {
        QPDFObjectHandle value = object.getArrayItem(index);
        if (!value.isNumber())
            return QUANTAPDF_ERROR_FORMAT;
        input[index] = value.getNumericValue();
        if (!std::isfinite(input[index]))
            return QUANTAPDF_ERROR_FORMAT;
    }
    values[0] = input[0] < input[2] ? input[0] : input[2];
    values[1] = input[1] < input[3] ? input[1] : input[3];
    values[2] = input[0] < input[2] ? input[2] : input[0];
    values[3] = input[1] < input[3] ? input[3] : input[1];
    if (!allow_empty && (values[2] <= values[0] || values[3] <= values[1]))
        return QUANTAPDF_ERROR_FORMAT;
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_qpdf_find_page(
    QPDFObjectHandle node,
    int target_index,
    int *current_index,
    size_t depth,
    std::set<QPDFObjGen> *seen,
    QPDFObjectHandle *out_page,
    QPDFObjectHandle const *target_object = nullptr,
    size_t *remaining = nullptr)
{
    if (remaining != nullptr) {
        if (*remaining == 0)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        --*remaining;
    }
    if (depth > 256u || !node.isDictionary() || !node.isIndirect())
        return QUANTAPDF_ERROR_FORMAT;
    if (!seen->insert(node.getObjGen()).second)
        return QUANTAPDF_ERROR_FORMAT;

    QPDFObjectHandle type = node.getKey("/Type");
    if (type.isName() && type.getName() == "/Page") {
        if (target_object != nullptr ? node.isSameObjectAs(*target_object) :
            *current_index == target_index) {
            *out_page = node;
            return QUANTAPDF_OK;
        }
        if (*current_index == INT_MAX)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        ++*current_index;
        return QUANTAPDF_ERROR_ARGUMENT;
    }
    if (!type.isName() || type.getName() != "/Pages")
        return QUANTAPDF_ERROR_FORMAT;
    QPDFObjectHandle kids = node.getKey("/Kids");
    if (!kids.isArray())
        return QUANTAPDF_ERROR_FORMAT;
    int const count = kids.getArrayNItems();
    for (int index = 0; index < count; ++index) {
        quantapdf_status status = quantapdf_qpdf_find_page(
            kids.getArrayItem(index), target_index, current_index,
            depth + 1u, seen, out_page, target_object, remaining);
        if (status == QUANTAPDF_OK)
            return status;
        if (status != QUANTAPDF_ERROR_ARGUMENT)
            return status;
    }
    return QUANTAPDF_ERROR_ARGUMENT;
}

quantapdf_status quantapdf_qpdf_load_page_geometry(
    QPDF& pdf,
    int page_index,
    quantapdf_qpdf_page_geometry *out_geometry)
{
    if (page_index < 0)
        return QUANTAPDF_ERROR_ARGUMENT;
    int current_index = 0;
    std::set<QPDFObjGen> page_tree_seen;
    QPDFObjectHandle page = QPDFObjectHandle::newNull();
    quantapdf_status status = quantapdf_qpdf_find_page(
        pdf.getRoot().getKey("/Pages"), page_index, &current_index, 1u,
        &page_tree_seen, &page);
    if (status != QUANTAPDF_OK)
        return status;

    QPDFObjectHandle media = QPDFObjectHandle::newNull();
    QPDFObjectHandle crop = QPDFObjectHandle::newNull();
    QPDFObjectHandle rotate = QPDFObjectHandle::newNull();
    QPDFObjectHandle current = page;
    std::set<QPDFObjGen> seen;
    size_t depth = 0;
    while (!current.isNull()) {
        if (++depth > 257u)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        if (!current.isDictionary() || !current.isIndirect())
            return QUANTAPDF_ERROR_FORMAT;
        if (!seen.insert(current.getObjGen()).second)
            return QUANTAPDF_ERROR_FORMAT;
        if (media.isNull())
            media = current.getKey("/MediaBox");
        if (crop.isNull())
            crop = current.getKey("/CropBox");
        if (rotate.isNull())
            rotate = current.getKey("/Rotate");
        current = current.getKey("/Parent");
    }
    if (media.isNull())
        return QUANTAPDF_ERROR_FORMAT;
    bool const has_explicit_crop = !crop.isNull();
    if (crop.isNull())
        crop = media;

    double media_values[4];
    double crop_values[4];
    status = quantapdf_qpdf_read_rectangle(
        media, media_values);
    if (status != QUANTAPDF_OK)
        return status;
    status = quantapdf_qpdf_read_rectangle(crop, crop_values);
    if (status != QUANTAPDF_OK)
        return status;

    double const left = media_values[0] > crop_values[0] ?
        media_values[0] : crop_values[0];
    double const bottom = media_values[1] > crop_values[1] ?
        media_values[1] : crop_values[1];
    double const right = media_values[2] < crop_values[2] ?
        media_values[2] : crop_values[2];
    double const top = media_values[3] < crop_values[3] ?
        media_values[3] : crop_values[3];
    if (right <= left || top <= bottom)
        return QUANTAPDF_ERROR_FORMAT;

    int rotation = 0;
    if (!rotate.isNull()) {
        if (!rotate.isInteger())
            return QUANTAPDF_ERROR_FORMAT;
        long long const raw_rotation = rotate.getIntValue();
        if (raw_rotation % 90 != 0)
            return QUANTAPDF_ERROR_FORMAT;
        rotation = static_cast<int>(raw_rotation % 360);
        if (rotation < 0)
            rotation += 360;
    }
    double user_unit = 1.0;
    QPDFObjectHandle user_unit_object = page.getKey("/UserUnit");
    if (!user_unit_object.isNull()) {
        if (!user_unit_object.isNumber())
            return QUANTAPDF_ERROR_FORMAT;
        user_unit = user_unit_object.getNumericValue();
        if (!std::isfinite(user_unit) || user_unit <= 0.0 ||
            user_unit > 75000.0)
            return QUANTAPDF_ERROR_FORMAT;
    }

    out_geometry->page = page;
    std::copy(std::begin(media_values), std::end(media_values),
              std::begin(out_geometry->media));
    std::copy(std::begin(crop_values), std::end(crop_values),
              std::begin(out_geometry->crop));
    out_geometry->left = left;
    out_geometry->bottom = bottom;
    out_geometry->right = right;
    out_geometry->top = top;
    out_geometry->user_unit = user_unit;
    out_geometry->has_explicit_crop = has_explicit_crop;
    out_geometry->rotation = rotation;
    if (rotation == 90 || rotation == 270) {
        out_geometry->public_width = (top - bottom) * user_unit;
        out_geometry->public_height = (right - left) * user_unit;
    } else {
        out_geometry->public_width = (right - left) * user_unit;
        out_geometry->public_height = (top - bottom) * user_unit;
    }
    return QUANTAPDF_OK;
}

void quantapdf_qpdf_pdf_point_to_public(
    quantapdf_qpdf_page_geometry const& geometry,
    double raw_x,
    double raw_y,
    double *out_x,
    double *out_y)
{
    switch (geometry.rotation) {
    case 0:
        *out_x = (raw_x - geometry.left) * geometry.user_unit;
        *out_y = (geometry.top - raw_y) * geometry.user_unit;
        break;
    case 90:
        *out_x = (raw_y - geometry.bottom) * geometry.user_unit;
        *out_y = (raw_x - geometry.left) * geometry.user_unit;
        break;
    case 180:
        *out_x = (geometry.right - raw_x) * geometry.user_unit;
        *out_y = (raw_y - geometry.bottom) * geometry.user_unit;
        break;
    case 270:
        *out_x = (geometry.top - raw_y) * geometry.user_unit;
        *out_y = (geometry.right - raw_x) * geometry.user_unit;
        break;
    }
}

static quantapdf_rect quantapdf_qpdf_pdf_rectangle_to_public(
    quantapdf_qpdf_page_geometry const& geometry,
    double const raw[4])
{
    /* Geometry admission guarantees quarter turns. Initialize output
     * coordinates explicitly so strict Linux GCC does not infer a possibly
     * uninitialized point through the separate void helper. */
    double x[4] = {};
    double y[4] = {};
    quantapdf_qpdf_pdf_point_to_public(
        geometry, raw[0], raw[1], &x[0], &y[0]);
    quantapdf_qpdf_pdf_point_to_public(
        geometry, raw[0], raw[3], &x[1], &y[1]);
    quantapdf_qpdf_pdf_point_to_public(
        geometry, raw[2], raw[1], &x[2], &y[2]);
    quantapdf_qpdf_pdf_point_to_public(
        geometry, raw[2], raw[3], &x[3], &y[3]);
    quantapdf_rect result = {};
    result.x0 = result.x1 = static_cast<float>(x[0]);
    result.y0 = result.y1 = static_cast<float>(y[0]);
    for (size_t index = 1; index < 4u; ++index) {
        result.x0 = std::min(result.x0, static_cast<float>(x[index]));
        result.y0 = std::min(result.y0, static_cast<float>(y[index]));
        result.x1 = std::max(result.x1, static_cast<float>(x[index]));
        result.y1 = std::max(result.y1, static_cast<float>(y[index]));
    }
    return result;
}

static bool quantapdf_qpdf_has_signed_field(
    QPDFObjectHandle field,
    size_t depth,
    std::set<QPDFObjGen> *seen)
{
    if (depth > 256u || !field.isDictionary())
        return true;
    if (field.isIndirect() && !seen->insert(field.getObjGen()).second)
        return true;
    QPDFObjectHandle type = field.getKey("/FT");
    if (type.isName() && type.getName() == "/Sig" &&
        !field.getKey("/V").isNull())
        return true;
    QPDFObjectHandle kids = field.getKey("/Kids");
    if (kids.isNull())
        return false;
    if (!kids.isArray())
        return true;
    int const count = kids.getArrayNItems();
    for (int index = 0; index < count; ++index) {
        if (quantapdf_qpdf_has_signed_field(
                kids.getArrayItem(index), depth + 1u, seen))
            return true;
    }
    return false;
}

static bool quantapdf_qpdf_rewrite_forbidden(QPDF& pdf)
{
    if (pdf.isEncrypted())
        return true;
    QPDFObjectHandle acroform = pdf.getRoot().getKey("/AcroForm");
    if (acroform.isNull())
        return false;
    if (!acroform.isDictionary())
        return true;
    QPDFObjectHandle fields = acroform.getKey("/Fields");
    if (fields.isNull())
        return false;
    if (!fields.isArray())
        return true;
    std::set<QPDFObjGen> seen;
    int const count = fields.getArrayNItems();
    for (int index = 0; index < count; ++index) {
        if (quantapdf_qpdf_has_signed_field(
                fields.getArrayItem(index), 1u, &seen))
            return true;
    }
    return false;
}

static quantapdf_status quantapdf_qpdf_lossless_field_preflight(
    QPDFObjectHandle field,
    QPDFObjectHandle expected_parent,
    std::string const& inherited_type,
    QPDFObjectHandle inherited_value,
    size_t depth,
    std::set<QPDFObjGen> *seen)
{
    if (depth > 256u || !field.isDictionary())
        return QUANTAPDF_ERROR_FORMAT;
    if (field.isIndirect() && !seen->insert(field.getObjGen()).second)
        return QUANTAPDF_ERROR_FORMAT;

    QPDFObjectHandle actual_parent = field.getKey("/Parent");
    if ((expected_parent.isNull() && !actual_parent.isNull()) ||
        (!expected_parent.isNull() &&
         !quantapdf_qpdf_same_object(expected_parent, actual_parent)))
        return QUANTAPDF_ERROR_FORMAT;

    std::string field_type = inherited_type;
    QPDFObjectHandle type = field.getKey("/FT");
    if (!type.isNull()) {
        if (!type.isName())
            return QUANTAPDF_ERROR_FORMAT;
        field_type = type.getName();
    }
    QPDFObjectHandle value = field.getKey("/V");
    if (value.isNull())
        value = inherited_value;
    if (field_type == "/Sig" && !value.isNull()) {
        if (!value.isDictionary())
            return QUANTAPDF_ERROR_FORMAT;
        QPDFObjectHandle signature_type = value.getKey("/Type");
        if (!signature_type.isNull() &&
            (!signature_type.isName() ||
             signature_type.getName() != "/Sig"))
            return QUANTAPDF_ERROR_FORMAT;
        return QUANTAPDF_ERROR_UNSUPPORTED;
    }

    QPDFObjectHandle kids = field.getKey("/Kids");
    if (kids.isNull())
        return QUANTAPDF_OK;
    if (!kids.isArray())
        return QUANTAPDF_ERROR_FORMAT;
    int const count = kids.getArrayNItems();
    for (int index = 0; index < count; ++index) {
        quantapdf_status const status =
            quantapdf_qpdf_lossless_field_preflight(
                kids.getArrayItem(index), field, field_type, value,
                depth + 1u, seen);
        if (status != QUANTAPDF_OK)
            return status;
    }
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_qpdf_lossless_preflight(QPDF& pdf)
{
    if (pdf.isEncrypted())
        return QUANTAPDF_ERROR_UNSUPPORTED;

    QPDFObjectHandle root = pdf.getRoot();
    if (!root.isDictionary())
        return QUANTAPDF_ERROR_FORMAT;
    QPDFObjectHandle permissions = root.getKey("/Perms");
    if (!permissions.isNull()) {
        if (!permissions.isDictionary())
            return QUANTAPDF_ERROR_FORMAT;
        for (char const *key : {"/DocMDP", "/UR", "/UR3"}) {
            QPDFObjectHandle signature = permissions.getKey(key);
            if (signature.isNull())
                continue;
            if (!signature.isDictionary())
                return QUANTAPDF_ERROR_FORMAT;
            QPDFObjectHandle type = signature.getKey("/Type");
            if (!type.isNull() &&
                (!type.isName() || type.getName() != "/Sig"))
                return QUANTAPDF_ERROR_FORMAT;
            return QUANTAPDF_ERROR_UNSUPPORTED;
        }
    }

    QPDFObjectHandle acroform = root.getKey("/AcroForm");
    if (acroform.isNull())
        return QUANTAPDF_OK;
    if (!acroform.isDictionary())
        return QUANTAPDF_ERROR_FORMAT;
    QPDFObjectHandle fields = acroform.getKey("/Fields");
    if (fields.isNull())
        return QUANTAPDF_OK;
    if (!fields.isArray())
        return QUANTAPDF_ERROR_FORMAT;

    std::set<QPDFObjGen> seen;
    int const count = fields.getArrayNItems();
    for (int index = 0; index < count; ++index) {
        quantapdf_status const status =
            quantapdf_qpdf_lossless_field_preflight(
                fields.getArrayItem(index), QPDFObjectHandle::newNull(),
                std::string(), QPDFObjectHandle::newNull(), 1u, &seen);
        if (status != QUANTAPDF_OK)
            return status;
    }
    return QUANTAPDF_OK;
}

struct quantapdf_qpdf_work_budget {
    size_t limit = 0;
    size_t used = 0;

    bool take() noexcept
    {
        if (used >= limit)
            return false;
        ++used;
        return true;
    }
};

struct quantapdf_qpdf_audit_field_item {
    QPDFObjectHandle field = QPDFObjectHandle::newNull();
    QPDFObjectHandle expected_parent = QPDFObjectHandle::newNull();
    std::string inherited_type;
    QPDFObjectHandle inherited_value = QPDFObjectHandle::newNull();
};

static quantapdf_status quantapdf_qpdf_init_work_budget(
    size_t source_size,
    quantapdf_qpdf_work_budget *budget)
{
    quantapdf_status const status =
        quantapdf_qpdf_compute_work_budget_limit(
            source_size, &budget->limit);
    if (status != QUANTAPDF_OK)
        return status;
    budget->used = 0;
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_qpdf_classify_action(
    QPDFObjectHandle action,
    uint32_t *finding)
{
    *finding = 0;
    if (!action.isDictionary())
        return QUANTAPDF_ERROR_FORMAT;
    QPDFObjectHandle type = action.getKey("/S");
    if (!type.isName())
        return QUANTAPDF_ERROR_FORMAT;
    std::string const name = type.getName();
    *finding = quantapdf_validation_action_finding(name.data(), name.size());
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_qpdf_work_push(
    QPDFObjectHandle object,
    std::vector<QPDFObjectHandle> *work,
    std::set<QPDFObjGen> *seen,
    quantapdf_qpdf_work_budget *budget)
{
    if (!budget->take())
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (object.isIndirect() && !seen->insert(object.getObjGen()).second)
        return QUANTAPDF_OK;
    work->push_back(object);
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_qpdf_audit_push_action(
    QPDFObjectHandle action,
    std::vector<QPDFObjectHandle> *work,
    std::set<QPDFObjGen> *seen,
    quantapdf_qpdf_work_budget *budget)
{
    return quantapdf_qpdf_work_push(action, work, seen, budget);
}

static quantapdf_status quantapdf_qpdf_audit_actions(
    std::vector<QPDFObjectHandle> *work,
    std::set<QPDFObjGen> *seen,
    quantapdf_qpdf_work_budget *budget,
    uint32_t *findings)
{
    while (!work->empty()) {
        QPDFObjectHandle action = work->back();
        work->pop_back();
        if (!action.isDictionary())
            return QUANTAPDF_ERROR_FORMAT;
        uint32_t finding = 0;
        quantapdf_status const classification =
            quantapdf_qpdf_classify_action(action, &finding);
        if (classification != QUANTAPDF_OK)
            return classification;
        *findings |= finding;

        if (!action.hasKey("/Next"))
            continue;
        QPDFObjectHandle next = action.getKey("/Next");
        if (next.isDictionary()) {
            quantapdf_status const status = quantapdf_qpdf_audit_push_action(
                next, work, seen, budget);
            if (status != QUANTAPDF_OK)
                return status;
        } else if (next.isArray()) {
            int const count = next.getArrayNItems();
            for (int index = 0; index < count; ++index) {
                QPDFObjectHandle item = next.getArrayItem(index);
                if (!item.isDictionary())
                    return QUANTAPDF_ERROR_FORMAT;
                quantapdf_status const status =
                    quantapdf_qpdf_audit_push_action(
                        item, work, seen, budget);
                if (status != QUANTAPDF_OK)
                    return status;
            }
        } else {
            return QUANTAPDF_ERROR_FORMAT;
        }
    }
    return QUANTAPDF_OK;
}

static bool quantapdf_qpdf_name_key_less(
    std::string const& left,
    std::string const& right)
{
    return std::lexicographical_compare(
        left.begin(), left.end(), right.begin(), right.end(),
        [](char a, char b) {
            return static_cast<unsigned char>(a) <
                static_cast<unsigned char>(b);
        });
}

static quantapdf_status quantapdf_qpdf_audit_javascript_name_tree(
    QPDFObjectHandle tree,
    std::vector<QPDFObjectHandle> *action_work,
    std::set<QPDFObjGen> *action_seen,
    quantapdf_qpdf_work_budget *budget)
{
    struct frame {
        QPDFObjectHandle node = QPDFObjectHandle::newNull();
        bool initialized = false;
        int next_child = 0;
        int child_count = 0;
        bool has_range = false;
        std::string first;
        std::string last;
        bool has_limits = false;
        std::string limit_first;
        std::string limit_last;
    };
    std::vector<frame> node_work;
    std::set<QPDFObjGen> node_seen;
    if (!budget->take())
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (tree.isIndirect())
        node_seen.insert(tree.getObjGen());
    node_work.emplace_back();
    node_work.back().node = tree;

    while (!node_work.empty()) {
        frame& current = node_work.back();
        if (!current.initialized) {
            QPDFObjectHandle node = current.node;
            if (!node.isDictionary())
                return QUANTAPDF_ERROR_FORMAT;
            bool const has_kids = node.hasKey("/Kids");
            bool const has_names = node.hasKey("/Names");
            if (has_kids && has_names)
                return QUANTAPDF_ERROR_FORMAT;
            if (node.hasKey("/Limits")) {
                QPDFObjectHandle limits = node.getKey("/Limits");
                if (!limits.isArray() || limits.getArrayNItems() != 2)
                    return QUANTAPDF_ERROR_FORMAT;
                for (int index = 0; index < 2; ++index) {
                    if (!budget->take())
                        return QUANTAPDF_ERROR_UNSUPPORTED;
                    if (!limits.getArrayItem(index).isString())
                        return QUANTAPDF_ERROR_FORMAT;
                }
                current.has_limits = true;
                current.limit_first =
                    limits.getArrayItem(0).getStringValue();
                current.limit_last =
                    limits.getArrayItem(1).getStringValue();
            }
            if (has_kids) {
                QPDFObjectHandle kids = node.getKey("/Kids");
                if (!kids.isArray())
                    return QUANTAPDF_ERROR_FORMAT;
                current.child_count = kids.getArrayNItems();
            } else if (has_names) {
                QPDFObjectHandle names = node.getKey("/Names");
                if (!names.isArray())
                    return QUANTAPDF_ERROR_FORMAT;
                int const count = names.getArrayNItems();
                if ((count & 1) != 0)
                    return QUANTAPDF_ERROR_FORMAT;
                for (int index = 0; index < count; index += 2) {
                    if (!budget->take())
                        return QUANTAPDF_ERROR_UNSUPPORTED;
                    QPDFObjectHandle key = names.getArrayItem(index);
                    if (!key.isString())
                        return QUANTAPDF_ERROR_FORMAT;
                    std::string const value = key.getStringValue();
                    if (current.has_range &&
                        !quantapdf_qpdf_name_key_less(current.last, value))
                        return QUANTAPDF_ERROR_FORMAT;
                    if (!current.has_range) {
                        current.first = value;
                        current.has_range = true;
                    }
                    current.last = value;
                    QPDFObjectHandle action = names.getArrayItem(index + 1);
                    if (!action.isDictionary())
                        return QUANTAPDF_ERROR_FORMAT;
                    quantapdf_status const status =
                        quantapdf_qpdf_audit_push_action(
                            action, action_work, action_seen, budget);
                    if (status != QUANTAPDF_OK)
                        return status;
                }
            }
            current.initialized = true;
        }

        if (current.next_child < current.child_count) {
            QPDFObjectHandle kids = current.node.getKey("/Kids");
            QPDFObjectHandle child =
                kids.getArrayItem(current.next_child++);
            if (!budget->take())
                return QUANTAPDF_ERROR_UNSUPPORTED;
            if (!child.isDictionary())
                return QUANTAPDF_ERROR_FORMAT;
            if (child.isIndirect() &&
                !node_seen.insert(child.getObjGen()).second)
                return QUANTAPDF_ERROR_FORMAT;
            node_work.emplace_back();
            node_work.back().node = child;
            continue;
        }

        if (current.has_limits &&
            (!current.has_range || current.limit_first != current.first ||
             current.limit_last != current.last))
            return QUANTAPDF_ERROR_FORMAT;

        bool const has_range = current.has_range;
        std::string const first = current.first;
        std::string const last = current.last;
        node_work.pop_back();
        if (!node_work.empty() && has_range) {
            frame& parent = node_work.back();
            if (parent.has_range &&
                !quantapdf_qpdf_name_key_less(parent.last, first))
                return QUANTAPDF_ERROR_FORMAT;
            if (!parent.has_range) {
                parent.first = first;
                parent.has_range = true;
            }
            parent.last = last;
        }
    }
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_qpdf_security_signature_dictionary(
    QPDFObjectHandle object,
    bool *out_is_signature)
{
    *out_is_signature = false;
    if (!object.isDictionary())
        return QUANTAPDF_OK;

    QPDFObjectHandle type = object.getKey("/Type");
    bool const has_type = object.hasKey("/Type");
    bool const tagged = has_type && type.isName() &&
        (type.getName() == "/Sig" || type.getName() == "/DocTimeStamp");
    bool const signature_shaped = object.hasKey("/ByteRange");
    if (!tagged && !signature_shaped)
        return QUANTAPDF_OK;
    *out_is_signature = true;
    if ((has_type && !tagged) || !object.hasKey("/ByteRange") ||
        !object.hasKey("/Contents"))
        return QUANTAPDF_ERROR_FORMAT;

    QPDFObjectHandle byte_range = object.getKey("/ByteRange");
    QPDFObjectHandle contents = object.getKey("/Contents");
    if (!byte_range.isArray() || byte_range.getArrayNItems() < 4 ||
        (byte_range.getArrayNItems() % 2) != 0 || !contents.isString())
        return QUANTAPDF_ERROR_FORMAT;
    for (int index = 0; index < byte_range.getArrayNItems(); ++index) {
        QPDFObjectHandle value = byte_range.getArrayItem(index);
        if (!value.isInteger() || value.getIntValue() < 0)
            return QUANTAPDF_ERROR_FORMAT;
    }
    return QUANTAPDF_ERROR_UNSUPPORTED;
}

static quantapdf_status quantapdf_qpdf_audit_signature_fields(
    QPDFObjectHandle acroform,
    quantapdf_qpdf_work_budget *budget,
    uint32_t *findings)
{
    if (!acroform.hasKey("/Fields"))
        return QUANTAPDF_OK;
    QPDFObjectHandle fields = acroform.getKey("/Fields");
    if (!fields.isArray())
        return QUANTAPDF_ERROR_FORMAT;

    std::vector<quantapdf_qpdf_audit_field_item> work;
    std::set<QPDFObjGen> seen;
    int const field_count = fields.getArrayNItems();
    for (int index = 0; index < field_count; ++index) {
        if (!budget->take())
            return QUANTAPDF_ERROR_UNSUPPORTED;
        quantapdf_qpdf_audit_field_item item;
        item.field = fields.getArrayItem(index);
        work.push_back(std::move(item));
    }

    while (!work.empty()) {
        quantapdf_qpdf_audit_field_item item = std::move(work.back());
        work.pop_back();
        QPDFObjectHandle field = item.field;
        if (!field.isDictionary())
            return QUANTAPDF_ERROR_FORMAT;
        if (field.isIndirect() && !seen.insert(field.getObjGen()).second)
            return QUANTAPDF_ERROR_FORMAT;

        QPDFObjectHandle actual_parent = field.getKey("/Parent");
        if ((item.expected_parent.isNull() && !actual_parent.isNull()) ||
            (!item.expected_parent.isNull() &&
             !quantapdf_qpdf_same_object(
                 item.expected_parent, actual_parent)))
            return QUANTAPDF_ERROR_FORMAT;

        std::string field_type = item.inherited_type;
        QPDFObjectHandle type = field.getKey("/FT");
        if (!type.isNull()) {
            if (!type.isName())
                return QUANTAPDF_ERROR_FORMAT;
            field_type = type.getName();
        }
        QPDFObjectHandle value = field.getKey("/V");
        if (value.isNull())
            value = item.inherited_value;
        if (field_type == "/Sig" && !value.isNull()) {
            if (!value.isDictionary())
                return QUANTAPDF_ERROR_FORMAT;
            bool is_signature = false;
            quantapdf_status const status =
                quantapdf_qpdf_security_signature_dictionary(
                    value, &is_signature);
            if (status != QUANTAPDF_ERROR_UNSUPPORTED || !is_signature)
                return QUANTAPDF_ERROR_FORMAT;
            QPDFObjectHandle signature_type = value.getKey("/Type");
            if (value.hasKey("/Type") &&
                (!signature_type.isName() ||
                 (signature_type.getName() != "/Sig" &&
                  signature_type.getName() != "/DocTimeStamp")))
                return QUANTAPDF_ERROR_FORMAT;
            *findings |= QUANTAPDF_AUDIT_SIGNATURE;
        }

        QPDFObjectHandle kids = field.getKey("/Kids");
        if (kids.isNull())
            continue;
        if (!kids.isArray())
            return QUANTAPDF_ERROR_FORMAT;
        int const kid_count = kids.getArrayNItems();
        for (int index = 0; index < kid_count; ++index) {
            if (!budget->take())
                return QUANTAPDF_ERROR_UNSUPPORTED;
            quantapdf_qpdf_audit_field_item child;
            child.field = kids.getArrayItem(index);
            child.expected_parent = field;
            child.inherited_type = field_type;
            child.inherited_value = value;
            work.push_back(std::move(child));
        }
    }
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_qpdf_audit_catalog_signatures(
    QPDFObjectHandle root,
    quantapdf_qpdf_work_budget *budget,
    uint32_t *findings)
{
    if (!root.hasKey("/Perms"))
        return QUANTAPDF_OK;
    QPDFObjectHandle permissions = root.getKey("/Perms");
    if (!permissions.isDictionary())
        return QUANTAPDF_ERROR_FORMAT;
    for (char const *key : {"/DocMDP", "/UR", "/UR3"}) {
        if (!permissions.hasKey(key))
            continue;
        if (!budget->take())
            return QUANTAPDF_ERROR_UNSUPPORTED;
        QPDFObjectHandle signature = permissions.getKey(key);
        if (!signature.isDictionary())
            return QUANTAPDF_ERROR_FORMAT;
        bool is_signature = false;
        quantapdf_status const status =
            quantapdf_qpdf_security_signature_dictionary(
                signature, &is_signature);
        if (status != QUANTAPDF_ERROR_UNSUPPORTED || !is_signature)
            return QUANTAPDF_ERROR_FORMAT;
        QPDFObjectHandle type = signature.getKey("/Type");
        if (signature.hasKey("/Type") &&
            (!type.isName() || type.getName() != "/Sig"))
            return QUANTAPDF_ERROR_FORMAT;
        *findings |= QUANTAPDF_AUDIT_SIGNATURE;
    }
    return QUANTAPDF_OK;
}

static bool quantapdf_qpdf_audit_rich_media(std::string const& subtype)
{
    return quantapdf_validation_rich_media(subtype.data(), subtype.size()) != 0;
}

static quantapdf_status quantapdf_qpdf_audit_graph(
    QPDFObjectHandle root,
    quantapdf_qpdf_work_budget *budget,
    uint32_t *findings)
{
    std::vector<QPDFObjectHandle> graph_work;
    std::vector<QPDFObjectHandle> action_work;
    std::set<QPDFObjGen> graph_seen;
    std::set<QPDFObjGen> action_seen;
    quantapdf_status status = quantapdf_qpdf_work_push(
        root, &graph_work, &graph_seen, budget);
    if (status != QUANTAPDF_OK)
        return status;

    if (root.hasKey("/OpenAction")) {
        QPDFObjectHandle open_action = root.getKey("/OpenAction");
        if (!open_action.isArray() && !open_action.isName() &&
            !open_action.isString()) {
            status = quantapdf_qpdf_audit_push_action(
                open_action, &action_work, &action_seen, budget);
            if (status != QUANTAPDF_OK)
                return status;
        }
    }

    if (root.hasKey("/Names")) {
        QPDFObjectHandle names = root.getKey("/Names");
        if (names.hasKey("/JavaScript")) {
            status = quantapdf_qpdf_audit_javascript_name_tree(
                names.getKey("/JavaScript"), &action_work,
                &action_seen, budget);
            if (status != QUANTAPDF_OK)
                return status;
        }
    }

    while (!graph_work.empty()) {
        QPDFObjectHandle object = graph_work.back();
        graph_work.pop_back();
        if (object.isStream()) {
            QPDFObjectHandle dictionary = object.getDict();
            QPDFObjectHandle type = dictionary.getKey("/Type");
            if (type.isName() && type.getName() == "/EmbeddedFile")
                *findings |= QUANTAPDF_AUDIT_EMBEDDED_FILE;
            object = dictionary;
        }
        if (object.isArray()) {
            int const count = object.getArrayNItems();
            for (int index = 0; index < count; ++index) {
                status = quantapdf_qpdf_work_push(
                    object.getArrayItem(index), &graph_work,
                    &graph_seen, budget);
                if (status != QUANTAPDF_OK)
                    return status;
            }
            continue;
        }
        if (!object.isDictionary())
            continue;

        if (object.hasKey("/AF") || object.hasKey("/EF"))
            *findings |= QUANTAPDF_AUDIT_EMBEDDED_FILE;
        if (object.hasKey("/A")) {
            status = quantapdf_qpdf_audit_push_action(
                object.getKey("/A"), &action_work, &action_seen, budget);
            if (status != QUANTAPDF_OK)
                return status;
        }
        if (object.hasKey("/AA")) {
            QPDFObjectHandle additional = object.getKey("/AA");
            if (!additional.isDictionary())
                return QUANTAPDF_ERROR_FORMAT;
            for (std::string const& key : additional.getKeys()) {
                status = quantapdf_qpdf_audit_push_action(
                    additional.getKey(key), &action_work,
                    &action_seen, budget);
                if (status != QUANTAPDF_OK)
                    return status;
            }
        }
        if (object.hasKey("/Annots")) {
            QPDFObjectHandle annots = object.getKey("/Annots");
            if (!annots.isArray())
                return QUANTAPDF_ERROR_FORMAT;
            int const count = annots.getArrayNItems();
            for (int index = 0; index < count; ++index) {
                if (!budget->take())
                    return QUANTAPDF_ERROR_UNSUPPORTED;
                QPDFObjectHandle annotation = annots.getArrayItem(index);
                if (!annotation.isDictionary())
                    return QUANTAPDF_ERROR_FORMAT;
                QPDFObjectHandle subtype = annotation.getKey("/Subtype");
                if (!subtype.isName())
                    continue;
                std::string const name = subtype.getName();
                if (name == "/FileAttachment")
                    *findings |= QUANTAPDF_AUDIT_EMBEDDED_FILE;
                if (quantapdf_qpdf_audit_rich_media(name))
                    *findings |= QUANTAPDF_AUDIT_RICH_MEDIA;
            }
        }
        for (std::string const& key : object.getKeys()) {
            status = quantapdf_qpdf_work_push(
                object.getKey(key), &graph_work, &graph_seen, budget);
            if (status != QUANTAPDF_OK)
                return status;
        }
    }
    return quantapdf_qpdf_audit_actions(
        &action_work, &action_seen, budget, findings);
}

static quantapdf_status quantapdf_qpdf_audit_document(
    QPDF& pdf,
    size_t source_size,
    uint32_t *findings)
{
    quantapdf_qpdf_work_budget budget;
    quantapdf_status status =
        quantapdf_qpdf_init_work_budget(source_size, &budget);
    if (status != QUANTAPDF_OK)
        return status;

    QPDFObjectHandle root = pdf.getRoot();
    if (!root.isDictionary())
        return QUANTAPDF_ERROR_FORMAT;
    if (pdf.isEncrypted())
        *findings |= QUANTAPDF_AUDIT_ENCRYPTION;

    status = quantapdf_qpdf_audit_catalog_signatures(
        root, &budget, findings);
    if (status != QUANTAPDF_OK)
        return status;

    if (root.hasKey("/Names")) {
        QPDFObjectHandle names = root.getKey("/Names");
        if (!names.isDictionary())
            return QUANTAPDF_ERROR_FORMAT;
        if (names.hasKey("/JavaScript"))
            *findings |= QUANTAPDF_AUDIT_JAVASCRIPT_ACTION;
        if (names.hasKey("/EmbeddedFiles"))
            *findings |= QUANTAPDF_AUDIT_EMBEDDED_FILE;
    }

    if (root.hasKey("/AcroForm")) {
        QPDFObjectHandle acroform = root.getKey("/AcroForm");
        if (!acroform.isDictionary())
            return QUANTAPDF_ERROR_FORMAT;
        if (acroform.hasKey("/XFA"))
            *findings |= QUANTAPDF_AUDIT_XFA;
        status = quantapdf_qpdf_audit_signature_fields(
            acroform, &budget, findings);
        if (status != QUANTAPDF_OK)
            return status;
    }
    return quantapdf_qpdf_audit_graph(root, &budget, findings);
}

static quantapdf_status quantapdf_qpdf_sanitize_queue_action(
    QPDFObjectHandle action,
    uint32_t flags,
    std::vector<QPDFObjectHandle> *work,
    std::set<QPDFObjGen> *seen,
    quantapdf_qpdf_work_budget *budget,
    bool *selected)
{
    if (!budget->take())
        return QUANTAPDF_ERROR_UNSUPPORTED;
    uint32_t finding = 0;
    quantapdf_status const status =
        quantapdf_qpdf_classify_action(action, &finding);
    if (status != QUANTAPDF_OK)
        return status;
    *selected = (finding & flags) != 0;
    if (*selected)
        return QUANTAPDF_OK;
    if (action.isIndirect() && !seen->insert(action.getObjGen()).second)
        return QUANTAPDF_OK;
    work->push_back(action);
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_qpdf_sanitize_actions(
    uint32_t flags,
    std::vector<QPDFObjectHandle> *work,
    std::set<QPDFObjGen> *seen,
    std::set<QPDFObjGen> *emptied_by_selected_removal,
    quantapdf_qpdf_work_budget *budget)
{
    while (!work->empty()) {
        QPDFObjectHandle action = work->back();
        work->pop_back();
        if (!action.hasKey("/Next"))
            continue;

        QPDFObjectHandle next = action.getKey("/Next");
        if (next.isDictionary()) {
            bool selected = false;
            quantapdf_status const status =
                quantapdf_qpdf_sanitize_queue_action(
                    next, flags, work, seen, budget, &selected);
            if (status != QUANTAPDF_OK)
                return status;
            if (selected)
                action.removeKey("/Next");
            continue;
        }
        if (!next.isArray())
            return QUANTAPDF_ERROR_FORMAT;

        std::vector<int> selected_indices;
        int const count = next.getArrayNItems();
        selected_indices.reserve(static_cast<size_t>(count));
        for (int index = 0; index < count; ++index) {
            bool selected = false;
            quantapdf_status const status =
                quantapdf_qpdf_sanitize_queue_action(
                    next.getArrayItem(index), flags, work, seen,
                    budget, &selected);
            if (status != QUANTAPDF_OK)
                return status;
            if (selected)
                selected_indices.push_back(index);
        }
        for (auto index = selected_indices.rbegin();
             index != selected_indices.rend(); ++index) {
            next.eraseItem(*index);
        }
        bool const emptied =
            !selected_indices.empty() && next.getArrayNItems() == 0;
        if (emptied && next.isIndirect())
            emptied_by_selected_removal->insert(next.getObjGen());
        bool const shared_emptied =
            next.isIndirect() && next.getArrayNItems() == 0 &&
            emptied_by_selected_removal->find(next.getObjGen()) !=
                emptied_by_selected_removal->end();
        if (emptied || shared_emptied)
            action.removeKey("/Next");
    }
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_qpdf_sanitize_javascript_name_tree(
    QPDFObjectHandle tree,
    uint32_t flags,
    std::vector<QPDFObjectHandle> *action_work,
    std::set<QPDFObjGen> *action_seen,
    quantapdf_qpdf_work_budget *budget)
{
    struct frame {
        QPDFObjectHandle node = QPDFObjectHandle::newNull();
        bool initialized = false;
        int next_child = 0;
        int child_count = 0;
        bool has_range = false;
        std::string first;
        std::string last;
        bool changed = false;
    };
    std::vector<frame> node_work;
    std::set<QPDFObjGen> node_seen;
    if (!budget->take())
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (tree.isIndirect())
        node_seen.insert(tree.getObjGen());
    node_work.emplace_back();
    node_work.back().node = tree;

    while (!node_work.empty()) {
        frame& current = node_work.back();
        if (!current.initialized) {
            if (current.node.hasKey("/Limits")) {
                for (int index = 0; index < 2; ++index) {
                    if (!budget->take())
                        return QUANTAPDF_ERROR_UNSUPPORTED;
                }
            }
            if (current.node.hasKey("/Kids")) {
                current.child_count =
                    current.node.getKey("/Kids").getArrayNItems();
            } else if (current.node.hasKey("/Names")) {
                QPDFObjectHandle names = current.node.getKey("/Names");
                std::vector<int> selected_pairs;
                int const count = names.getArrayNItems();
                selected_pairs.reserve(static_cast<size_t>(count / 2));
                for (int index = 0; index < count; index += 2) {
                    if (!budget->take())
                        return QUANTAPDF_ERROR_UNSUPPORTED;
                    bool selected = false;
                    quantapdf_status const status =
                        quantapdf_qpdf_sanitize_queue_action(
                            names.getArrayItem(index + 1), flags,
                            action_work, action_seen, budget, &selected);
                    if (status != QUANTAPDF_OK)
                        return status;
                    if (selected)
                        selected_pairs.push_back(index);
                }
                for (auto index = selected_pairs.rbegin();
                     index != selected_pairs.rend(); ++index) {
                    names.eraseItem(*index + 1);
                    names.eraseItem(*index);
                }
                current.changed = !selected_pairs.empty();
                int const retained_count = names.getArrayNItems();
                if (retained_count != 0) {
                    current.has_range = true;
                    current.first =
                        names.getArrayItem(0).getStringValue();
                    current.last = names.getArrayItem(
                        retained_count - 2).getStringValue();
                }
            }
            current.initialized = true;
        }

        if (current.next_child < current.child_count) {
            QPDFObjectHandle child = current.node.getKey("/Kids").getArrayItem(
                current.next_child++);
            if (!budget->take())
                return QUANTAPDF_ERROR_UNSUPPORTED;
            if (child.isIndirect() &&
                !node_seen.insert(child.getObjGen()).second)
                continue;
            node_work.emplace_back();
            node_work.back().node = child;
            continue;
        }

        if (current.changed && current.node.hasKey("/Limits")) {
            if (!current.has_range) {
                current.node.removeKey("/Limits");
            } else {
                QPDFObjectHandle limits = current.node.getKey("/Limits");
                if (limits.getArrayItem(0).getStringValue() != current.first ||
                    limits.getArrayItem(1).getStringValue() != current.last) {
                    QPDFObjectHandle replacement = QPDFObjectHandle::newArray();
                    replacement.appendItem(
                        QPDFObjectHandle::newString(current.first));
                    replacement.appendItem(
                        QPDFObjectHandle::newString(current.last));
                    current.node.replaceKey("/Limits", replacement);
                }
            }
        }

        bool const has_range = current.has_range;
        std::string const first = current.first;
        std::string const last = current.last;
        bool const changed = current.changed;
        node_work.pop_back();
        if (!node_work.empty()) {
            frame& parent = node_work.back();
            parent.changed = parent.changed || changed;
            if (has_range) {
                if (!parent.has_range) {
                    parent.first = first;
                    parent.has_range = true;
                }
                parent.last = last;
            }
        }
    }
    return QUANTAPDF_OK;
}

enum class quantapdf_qpdf_alias_role : unsigned {
    root,
    custom,
    catalog_names,
    acroform,
    additional_actions,
    action,
    javascript_action,
    next_array,
    annots_array,
    annotation,
    associated_files,
    associated_file,
    embedded_files,
    javascript_node,
    javascript_kids,
    javascript_names,
    page_tree,
    page_kids,
    page
};

struct quantapdf_qpdf_alias_item {
    QPDFObjectHandle object = QPDFObjectHandle::newNull();
    quantapdf_qpdf_alias_role role = quantapdf_qpdf_alias_role::custom;
    QPDFObjectHandle anchor = QPDFObjectHandle::newNull();
};

static quantapdf_status quantapdf_qpdf_alias_push(
    QPDFObjectHandle object,
    quantapdf_qpdf_alias_role role,
    QPDFObjectHandle anchor,
    std::vector<quantapdf_qpdf_alias_item> *work,
    std::set<std::pair<QPDFObjGen, unsigned>> *seen,
    std::map<QPDFObjGen, uint32_t> *roles,
    quantapdf_qpdf_work_budget *budget)
{
    if (!budget->take())
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (object.isIndirect()) {
        QPDFObjGen const id = object.getObjGen();
        anchor = object;
        (*roles)[id] |= UINT32_C(1) << static_cast<unsigned>(role);
        if (!seen->insert({id, static_cast<unsigned>(role)}).second)
            return QUANTAPDF_OK;
    }
    work->push_back({object, role, anchor});
    return QUANTAPDF_OK;
}

static void quantapdf_qpdf_alias_mark_target(
    QPDFObjectHandle object,
    QPDFObjectHandle anchor,
    std::set<QPDFObjGen> *targets)
{
    if (object.isIndirect())
        targets->insert(object.getObjGen());
    else if (anchor.isIndirect())
        targets->insert(anchor.getObjGen());
}

static quantapdf_status quantapdf_qpdf_alias_action_selected(
    QPDFObjectHandle action,
    uint32_t flags,
    bool *selected)
{
    uint32_t finding = 0;
    quantapdf_status const status =
        quantapdf_qpdf_classify_action(action, &finding);
    if (status != QUANTAPDF_OK)
        return status;
    *selected = (finding & flags) != 0;
    return QUANTAPDF_OK;
}

static bool quantapdf_qpdf_alias_rich_annotation_selected(
    QPDFObjectHandle annotation,
    uint32_t flags)
{
    QPDFObjectHandle subtype = annotation.getKey("/Subtype");
    if (!subtype.isName())
        return false;
    std::string const name = subtype.getName();
    return ((flags & QUANTAPDF_SANITIZE_EMBEDDED_FILES) != 0 &&
            name == "/FileAttachment") ||
        ((flags & QUANTAPDF_SANITIZE_RICH_MEDIA) != 0 &&
         quantapdf_qpdf_audit_rich_media(name));
}

static quantapdf_status quantapdf_qpdf_preflight_aliases(
    QPDF& pdf,
    size_t source_size,
    uint32_t flags)
{
    quantapdf_qpdf_work_budget budget;
    quantapdf_status status =
        quantapdf_qpdf_init_work_budget(source_size, &budget);
    if (status != QUANTAPDF_OK)
        return status;

    std::vector<quantapdf_qpdf_alias_item> work;
    std::set<std::pair<QPDFObjGen, unsigned>> seen;
    std::map<QPDFObjGen, uint32_t> roles;
    std::set<QPDFObjGen> targets;
    QPDFObjectHandle root = pdf.getRoot();
    status = quantapdf_qpdf_alias_push(
        root, quantapdf_qpdf_alias_role::root,
        QPDFObjectHandle::newNull(), &work, &seen,
        &roles, &budget);
    if (status != QUANTAPDF_OK)
        return status;

    while (!work.empty()) {
        quantapdf_qpdf_alias_item item = std::move(work.back());
        work.pop_back();
        QPDFObjectHandle identity = item.object;
        QPDFObjectHandle object = item.object;
        if (object.isStream())
            object = object.getDict();

        if (object.isArray()) {
            int const count = object.getArrayNItems();
            for (int index = 0; index < count; ++index) {
                QPDFObjectHandle child = object.getArrayItem(index);
                quantapdf_qpdf_alias_role child_role =
                    quantapdf_qpdf_alias_role::custom;
                switch (item.role) {
                case quantapdf_qpdf_alias_role::next_array:
                    child_role = quantapdf_qpdf_alias_role::action;
                    break;
                case quantapdf_qpdf_alias_role::annots_array:
                    child_role = quantapdf_qpdf_alias_role::annotation;
                    break;
                case quantapdf_qpdf_alias_role::associated_files:
                    child_role = quantapdf_qpdf_alias_role::associated_file;
                    break;
                case quantapdf_qpdf_alias_role::javascript_kids:
                    child_role = quantapdf_qpdf_alias_role::javascript_node;
                    break;
                case quantapdf_qpdf_alias_role::javascript_names:
                    child_role = (index & 1) == 0
                        ? quantapdf_qpdf_alias_role::custom
                        : quantapdf_qpdf_alias_role::javascript_action;
                    break;
                case quantapdf_qpdf_alias_role::page_kids: {
                    QPDFObjectHandle type = child.getKey("/Type");
                    child_role = type.isName() && type.getName() == "/Pages"
                        ? quantapdf_qpdf_alias_role::page_tree
                        : quantapdf_qpdf_alias_role::page;
                    break;
                }
                default:
                    break;
                }
                status = quantapdf_qpdf_alias_push(
                    child, child_role, item.anchor, &work,
                    &seen, &roles, &budget);
                if (status != QUANTAPDF_OK)
                    return status;
            }
            if (item.role == quantapdf_qpdf_alias_role::next_array) {
                for (int index = 0; index < count; ++index) {
                    bool selected = false;
                    status = quantapdf_qpdf_alias_action_selected(
                        object.getArrayItem(index), flags, &selected);
                    if (status != QUANTAPDF_OK)
                        return status;
                    if (selected) {
                        quantapdf_qpdf_alias_mark_target(
                            identity, item.anchor, &targets);
                        break;
                    }
                }
            } else if (item.role ==
                       quantapdf_qpdf_alias_role::annots_array) {
                for (int index = 0; index < count; ++index) {
                    if (quantapdf_qpdf_alias_rich_annotation_selected(
                            object.getArrayItem(index), flags)) {
                        quantapdf_qpdf_alias_mark_target(
                            identity, item.anchor, &targets);
                        break;
                    }
                }
            } else if (item.role ==
                           quantapdf_qpdf_alias_role::javascript_names &&
                       (flags & QUANTAPDF_SANITIZE_JAVASCRIPT_ACTIONS) == 0) {
                for (int index = 1; index < count; index += 2) {
                    bool selected = false;
                    status = quantapdf_qpdf_alias_action_selected(
                        object.getArrayItem(index), flags, &selected);
                    if (status != QUANTAPDF_OK)
                        return status;
                    if (selected) {
                        quantapdf_qpdf_alias_mark_target(
                            identity, item.anchor, &targets);
                        break;
                    }
                }
            }
            continue;
        }
        if (!object.isDictionary())
            continue;

        bool const removed_javascript_subtree =
            (flags & QUANTAPDF_SANITIZE_JAVASCRIPT_ACTIONS) != 0 &&
            (item.role == quantapdf_qpdf_alias_role::javascript_node ||
             item.role == quantapdf_qpdf_alias_role::javascript_action);
        if (!removed_javascript_subtree &&
            (flags & QUANTAPDF_SANITIZE_EMBEDDED_FILES) != 0 &&
            (object.hasKey("/AF") || object.hasKey("/EF"))) {
            quantapdf_qpdf_alias_mark_target(
                identity, item.anchor, &targets);
        }

        if (item.role == quantapdf_qpdf_alias_role::root) {
            if (object.hasKey("/Names")) {
                QPDFObjectHandle names = object.getKey("/Names");
                if (((flags & QUANTAPDF_SANITIZE_JAVASCRIPT_ACTIONS) != 0 &&
                     names.hasKey("/JavaScript")) ||
                    ((flags & QUANTAPDF_SANITIZE_EMBEDDED_FILES) != 0 &&
                     names.hasKey("/EmbeddedFiles"))) {
                    quantapdf_qpdf_alias_mark_target(
                        names, item.anchor, &targets);
                }
            }
            if (object.hasKey("/AcroForm") &&
                (flags & QUANTAPDF_SANITIZE_XFA) != 0) {
                QPDFObjectHandle acroform = object.getKey("/AcroForm");
                if (acroform.hasKey("/XFA"))
                    quantapdf_qpdf_alias_mark_target(
                        acroform, item.anchor, &targets);
            }
            if (object.hasKey("/OpenAction")) {
                QPDFObjectHandle action = object.getKey("/OpenAction");
                if (!action.isArray() && !action.isName() &&
                    !action.isString()) {
                    bool selected = false;
                    status = quantapdf_qpdf_alias_action_selected(
                        action, flags, &selected);
                    if (status != QUANTAPDF_OK)
                        return status;
                    if (selected)
                        quantapdf_qpdf_alias_mark_target(
                            identity, item.anchor, &targets);
                }
            }
        }

        if (!removed_javascript_subtree && object.hasKey("/A")) {
            bool selected = false;
            status = quantapdf_qpdf_alias_action_selected(
                object.getKey("/A"), flags, &selected);
            if (status != QUANTAPDF_OK)
                return status;
            if (selected)
                quantapdf_qpdf_alias_mark_target(
                    identity, item.anchor, &targets);
        }
        if (!removed_javascript_subtree && object.hasKey("/AA")) {
            QPDFObjectHandle additional = object.getKey("/AA");
            size_t selected_count = 0;
            std::set<std::string> const keys = additional.getKeys();
            for (std::string const& key : keys) {
                bool selected = false;
                status = quantapdf_qpdf_alias_action_selected(
                    additional.getKey(key), flags, &selected);
                if (status != QUANTAPDF_OK)
                    return status;
                if (selected)
                    ++selected_count;
            }
            if (selected_count != 0)
                quantapdf_qpdf_alias_mark_target(
                    additional, item.anchor, &targets);
            if (selected_count != 0 && selected_count == keys.size())
                quantapdf_qpdf_alias_mark_target(
                    identity, item.anchor, &targets);
        }

        bool const action_context =
            item.role == quantapdf_qpdf_alias_role::action ||
            item.role == quantapdf_qpdf_alias_role::javascript_action;
        if (action_context && !removed_javascript_subtree &&
            object.hasKey("/Next")) {
            QPDFObjectHandle next = object.getKey("/Next");
            if (next.isDictionary()) {
                bool selected = false;
                status = quantapdf_qpdf_alias_action_selected(
                    next, flags, &selected);
                if (status != QUANTAPDF_OK)
                    return status;
                if (selected)
                    quantapdf_qpdf_alias_mark_target(
                        identity, item.anchor, &targets);
            } else {
                int const count = next.getArrayNItems();
                int selected_count = 0;
                for (int index = 0; index < count; ++index) {
                    bool selected = false;
                    status = quantapdf_qpdf_alias_action_selected(
                        next.getArrayItem(index), flags, &selected);
                    if (status != QUANTAPDF_OK)
                        return status;
                    if (selected)
                        ++selected_count;
                }
                if (selected_count == count && count != 0)
                    quantapdf_qpdf_alias_mark_target(
                        identity, item.anchor, &targets);
            }
        }

        for (std::string const& key : object.getKeys()) {
            QPDFObjectHandle child = object.getKey(key);
            quantapdf_qpdf_alias_role child_role =
                quantapdf_qpdf_alias_role::custom;
            if (item.role == quantapdf_qpdf_alias_role::root &&
                key == "/Names") {
                child_role = quantapdf_qpdf_alias_role::catalog_names;
            } else if (item.role == quantapdf_qpdf_alias_role::root &&
                       key == "/Pages") {
                child_role = quantapdf_qpdf_alias_role::page_tree;
            } else if (item.role == quantapdf_qpdf_alias_role::root &&
                       key == "/AcroForm") {
                child_role = quantapdf_qpdf_alias_role::acroform;
            } else if (item.role == quantapdf_qpdf_alias_role::root &&
                       key == "/OpenAction" && !child.isArray() &&
                       !child.isName() && !child.isString()) {
                child_role = quantapdf_qpdf_alias_role::action;
            } else if (item.role == quantapdf_qpdf_alias_role::catalog_names &&
                       key == "/JavaScript") {
                child_role = quantapdf_qpdf_alias_role::javascript_node;
            } else if (item.role == quantapdf_qpdf_alias_role::javascript_node &&
                       key == "/Kids") {
                child_role = quantapdf_qpdf_alias_role::javascript_kids;
            } else if (item.role == quantapdf_qpdf_alias_role::javascript_node &&
                       key == "/Names") {
                child_role = quantapdf_qpdf_alias_role::javascript_names;
            } else if (item.role == quantapdf_qpdf_alias_role::page_tree &&
                       key == "/Kids") {
                child_role = quantapdf_qpdf_alias_role::page_kids;
            } else if ((item.role == quantapdf_qpdf_alias_role::page ||
                        item.role == quantapdf_qpdf_alias_role::page_tree) &&
                       key == "/Parent") {
                child_role = quantapdf_qpdf_alias_role::page_tree;
            } else if (item.role == quantapdf_qpdf_alias_role::annotation &&
                       key == "/P" && child.isDictionary() &&
                       child.getKey("/Type").isName() &&
                       child.getKey("/Type").getName() == "/Page") {
                child_role = quantapdf_qpdf_alias_role::page;
            } else if (item.role == quantapdf_qpdf_alias_role::annotation &&
                       key == "/IRT" && child.isDictionary() &&
                       child.getKey("/Subtype").isName()) {
                child_role = quantapdf_qpdf_alias_role::annotation;
            } else if (item.role == quantapdf_qpdf_alias_role::annotation &&
                       key == "/Popup" && child.isDictionary() &&
                       child.getKey("/Subtype").isName() &&
                       child.getKey("/Subtype").getName() == "/Popup") {
                child_role = quantapdf_qpdf_alias_role::annotation;
            } else if (item.role == quantapdf_qpdf_alias_role::annotation &&
                       key == "/Parent" && object.getKey("/Subtype").isName() &&
                       object.getKey("/Subtype").getName() == "/Popup" &&
                       child.isDictionary() &&
                       child.getKey("/Subtype").isName()) {
                child_role = quantapdf_qpdf_alias_role::annotation;
            } else if (item.role ==
                           quantapdf_qpdf_alias_role::additional_actions) {
                child_role = quantapdf_qpdf_alias_role::action;
            } else if (action_context && key == "/Next") {
                if (child.isArray()) {
                    child_role = quantapdf_qpdf_alias_role::next_array;
                } else {
                    child_role = item.role ==
                            quantapdf_qpdf_alias_role::javascript_action
                        ? quantapdf_qpdf_alias_role::javascript_action
                        : quantapdf_qpdf_alias_role::action;
                }
            } else if (key == "/A") {
                child_role = quantapdf_qpdf_alias_role::action;
            } else if (key == "/AA") {
                child_role = quantapdf_qpdf_alias_role::additional_actions;
            } else if (key == "/Annots") {
                child_role = quantapdf_qpdf_alias_role::annots_array;
            } else if (key == "/AF") {
                child_role = quantapdf_qpdf_alias_role::associated_files;
            } else if (key == "/EF") {
                child_role = quantapdf_qpdf_alias_role::embedded_files;
            }
            status = quantapdf_qpdf_alias_push(
                child, child_role, item.anchor, &work, &seen,
                &roles, &budget);
            if (status != QUANTAPDF_OK)
                return status;
        }
    }

    for (QPDFObjGen const& target : targets) {
        uint32_t const role_mask = roles[target];
        if ((role_mask & (role_mask - 1u)) != 0)
            return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_qpdf_sanitize_graph(
    QPDF& pdf,
    size_t source_size,
    uint32_t flags)
{
    quantapdf_qpdf_work_budget budget;
    quantapdf_status status =
        quantapdf_qpdf_init_work_budget(source_size, &budget);
    if (status != QUANTAPDF_OK)
        return status;

    QPDFObjectHandle root = pdf.getRoot();
    std::vector<QPDFObjectHandle> graph_work;
    std::vector<QPDFObjectHandle> action_work;
    std::set<QPDFObjGen> graph_seen;
    std::set<QPDFObjGen> action_seen;
    std::set<QPDFObjGen> emptied_by_selected_removal;

    if (root.hasKey("/Names")) {
        if (!budget.take())
            return QUANTAPDF_ERROR_UNSUPPORTED;
        QPDFObjectHandle names = root.getKey("/Names");
        bool selected_removed = false;
        if ((flags & QUANTAPDF_SANITIZE_JAVASCRIPT_ACTIONS) != 0 &&
            names.hasKey("/JavaScript")) {
            names.removeKey("/JavaScript");
            selected_removed = true;
        }
        if ((flags & QUANTAPDF_SANITIZE_EMBEDDED_FILES) != 0 &&
            names.hasKey("/EmbeddedFiles")) {
            names.removeKey("/EmbeddedFiles");
            selected_removed = true;
        }
        if (selected_removed && names.getKeys().empty())
            root.removeKey("/Names");
        else if ((flags & QUANTAPDF_SANITIZE_JAVASCRIPT_ACTIONS) == 0 &&
                 names.hasKey("/JavaScript")) {
            status = quantapdf_qpdf_sanitize_javascript_name_tree(
                names.getKey("/JavaScript"), flags, &action_work,
                &action_seen, &budget);
            if (status != QUANTAPDF_OK)
                return status;
        }
    }

    if (root.hasKey("/AcroForm") &&
        (flags & QUANTAPDF_SANITIZE_XFA) != 0) {
        if (!budget.take())
            return QUANTAPDF_ERROR_UNSUPPORTED;
        root.getKey("/AcroForm").removeKey("/XFA");
    }

    if (root.hasKey("/OpenAction")) {
        QPDFObjectHandle open_action = root.getKey("/OpenAction");
        if (!open_action.isArray() && !open_action.isName() &&
            !open_action.isString()) {
            bool selected = false;
            status = quantapdf_qpdf_sanitize_queue_action(
                open_action, flags, &action_work, &action_seen,
                &budget, &selected);
            if (status != QUANTAPDF_OK)
                return status;
            if (selected)
                root.removeKey("/OpenAction");
        }
    }

    status = quantapdf_qpdf_work_push(
        root, &graph_work, &graph_seen, &budget);
    if (status != QUANTAPDF_OK)
        return status;
    while (!graph_work.empty()) {
        QPDFObjectHandle object = graph_work.back();
        graph_work.pop_back();
        if (object.isStream())
            object = object.getDict();
        if (object.isArray()) {
            int const count = object.getArrayNItems();
            for (int index = 0; index < count; ++index) {
                status = quantapdf_qpdf_work_push(
                    object.getArrayItem(index), &graph_work,
                    &graph_seen, &budget);
                if (status != QUANTAPDF_OK)
                    return status;
            }
            continue;
        }
        if (!object.isDictionary())
            continue;

        if ((flags & QUANTAPDF_SANITIZE_EMBEDDED_FILES) != 0) {
            object.removeKey("/AF");
            object.removeKey("/EF");
        }

        if (object.hasKey("/A")) {
            bool selected = false;
            status = quantapdf_qpdf_sanitize_queue_action(
                object.getKey("/A"), flags, &action_work,
                &action_seen, &budget, &selected);
            if (status != QUANTAPDF_OK)
                return status;
            if (selected)
                object.removeKey("/A");
        }

        if (object.hasKey("/AA")) {
            QPDFObjectHandle additional = object.getKey("/AA");
            std::vector<std::string> selected_keys;
            for (std::string const& key : additional.getKeys()) {
                bool selected = false;
                status = quantapdf_qpdf_sanitize_queue_action(
                    additional.getKey(key), flags, &action_work,
                    &action_seen, &budget, &selected);
                if (status != QUANTAPDF_OK)
                    return status;
                if (selected)
                    selected_keys.push_back(key);
            }
            for (std::string const& key : selected_keys)
                additional.removeKey(key);
            bool const emptied =
                !selected_keys.empty() && additional.getKeys().empty();
            if (emptied && additional.isIndirect()) {
                emptied_by_selected_removal.insert(additional.getObjGen());
            }
            bool const shared_emptied =
                additional.isIndirect() && additional.getKeys().empty() &&
                emptied_by_selected_removal.find(additional.getObjGen()) !=
                    emptied_by_selected_removal.end();
            if (emptied || shared_emptied)
                object.removeKey("/AA");
        }

        if (object.hasKey("/Annots")) {
            QPDFObjectHandle annots = object.getKey("/Annots");
            std::vector<int> selected_indices;
            int const count = annots.getArrayNItems();
            selected_indices.reserve(static_cast<size_t>(count));
            for (int index = 0; index < count; ++index) {
                if (!budget.take())
                    return QUANTAPDF_ERROR_UNSUPPORTED;
                QPDFObjectHandle annotation = annots.getArrayItem(index);
                QPDFObjectHandle subtype = annotation.getKey("/Subtype");
                if (!subtype.isName())
                    continue;
                std::string const name = subtype.getName();
                if (((flags & QUANTAPDF_SANITIZE_EMBEDDED_FILES) != 0 &&
                     name == "/FileAttachment") ||
                    ((flags & QUANTAPDF_SANITIZE_RICH_MEDIA) != 0 &&
                     quantapdf_qpdf_audit_rich_media(name))) {
                    selected_indices.push_back(index);
                }
            }
            for (auto index = selected_indices.rbegin();
                 index != selected_indices.rend(); ++index) {
                annots.eraseItem(*index);
            }
        }

        for (std::string const& key : object.getKeys()) {
            status = quantapdf_qpdf_work_push(
                object.getKey(key), &graph_work, &graph_seen, &budget);
            if (status != QUANTAPDF_OK)
                return status;
        }
    }

    return quantapdf_qpdf_sanitize_actions(
        flags, &action_work, &action_seen,
        &emptied_by_selected_removal, &budget);
}

static quantapdf_status quantapdf_qpdf_public_crop_to_pdf(
    quantapdf_qpdf_page_geometry const& geometry,
    quantapdf_rect const& requested,
    double output[4])
{
    if (!std::isfinite(requested.x0) || !std::isfinite(requested.y0) ||
        !std::isfinite(requested.x1) || !std::isfinite(requested.y1) ||
        requested.x1 <= requested.x0 || requested.y1 <= requested.y0)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (requested.x0 < 0.0f || requested.y0 < 0.0f ||
        requested.x1 > geometry.public_width ||
        requested.y1 > geometry.public_height)
        return QUANTAPDF_ERROR_ARGUMENT;

    double const x0 = requested.x0 / geometry.user_unit;
    double const y0 = requested.y0 / geometry.user_unit;
    double const x1 = requested.x1 / geometry.user_unit;
    double const y1 = requested.y1 / geometry.user_unit;
    switch (geometry.rotation) {
    case 0:
        output[0] = geometry.left + x0;
        output[1] = geometry.top - y1;
        output[2] = geometry.left + x1;
        output[3] = geometry.top - y0;
        break;
    case 90:
        output[0] = geometry.left + y0;
        output[1] = geometry.bottom + x0;
        output[2] = geometry.left + y1;
        output[3] = geometry.bottom + x1;
        break;
    case 180:
        output[0] = geometry.right - x1;
        output[1] = geometry.bottom + y0;
        output[2] = geometry.right - x0;
        output[3] = geometry.bottom + y1;
        break;
    case 270:
        output[0] = geometry.right - y1;
        output[1] = geometry.top - x1;
        output[2] = geometry.right - y0;
        output[3] = geometry.top - x0;
        break;
    default:
        return QUANTAPDF_ERROR_FORMAT;
    }
    return QUANTAPDF_OK;
}

struct quantapdf_qpdf_form_group {
    QPDFObjectHandle head;
    std::string full_name;
    bool name_present = false;
    bool has_named_child = false;
    bool has_widget = false;
    size_t public_index = SIZE_MAX;
};

struct quantapdf_qpdf_form_node {
    QPDFObjectHandle object;
    size_t group_index;
    bool widget;
};

struct quantapdf_qpdf_form_capture {
    std::vector<quantapdf_pdf_form_field_internal> fields;
    std::vector<quantapdf_pdf_form_value_internal> values;
    std::vector<quantapdf_pdf_form_option_internal> options;
    std::vector<std::string> button_states;
    std::vector<quantapdf_pdf_form_widget_internal> widgets;
    std::string strings;
};

static void quantapdf_qpdf_dispose_form_model(
    quantapdf_pdf_form_model *model) noexcept
{
    if (model == nullptr)
        return;
    for (size_t index = 0; index < model->option_count; ++index)
        std::free(model->options[index].button_state);
    std::free(model->fields);
    std::free(model->values);
    std::free(model->options);
    std::free(model->widgets);
    std::free(model->strings);
    std::free(model);
}

static quantapdf_status quantapdf_qpdf_form_append_string(
    quantapdf_qpdf_form_capture *capture,
    std::string const& value,
    quantapdf_pdf_form_string *out_string)
{
    if (capture->strings.size() > SIZE_MAX - value.size() - 1u)
        return QUANTAPDF_ERROR_NOMEM;
    out_string->offset = capture->strings.size();
    out_string->size = value.size();
    out_string->present = 1;
    capture->strings.append(value);
    capture->strings.push_back('\0');
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_qpdf_form_read_uint32(
    QPDFObjectHandle const& object,
    char const *key,
    uint32_t default_value,
    uint32_t *out_value)
{
    QPDFObjectHandle value = quantapdf_qpdf_inherited_value(object, key);
    if (value.isNull()) {
        *out_value = default_value;
        return QUANTAPDF_OK;
    }
    if (!value.isInteger())
        return QUANTAPDF_ERROR_FORMAT;
    long long const raw = value.getIntValue();
    if (raw < 0 || static_cast<unsigned long long>(raw) > UINT32_MAX)
        return QUANTAPDF_ERROR_FORMAT;
    *out_value = static_cast<uint32_t>(raw);
    return QUANTAPDF_OK;
}

static quantapdf_form_field_type quantapdf_qpdf_form_field_type(
    QPDFObjectHandle const& object,
    uint32_t flags,
    quantapdf_status *out_status)
{
    QPDFObjectHandle type = quantapdf_qpdf_inherited_value(object, "/FT");
    *out_status = QUANTAPDF_OK;
    if (type.isNull())
        return QUANTAPDF_FORM_FIELD_UNKNOWN;
    if (!type.isName()) {
        *out_status = QUANTAPDF_ERROR_FORMAT;
        return QUANTAPDF_FORM_FIELD_UNKNOWN;
    }
    std::string const name = type.getName();
    if (name == "/Tx")
        return QUANTAPDF_FORM_FIELD_TEXT;
    if (name == "/Sig")
        return QUANTAPDF_FORM_FIELD_SIGNATURE;
    if (name == "/Btn") {
        if ((flags & (1u << 16)) != 0u)
            return QUANTAPDF_FORM_FIELD_PUSH_BUTTON;
        if ((flags & (1u << 15)) != 0u)
            return QUANTAPDF_FORM_FIELD_RADIO_BUTTON;
        return QUANTAPDF_FORM_FIELD_CHECKBOX;
    }
    if (name == "/Ch")
        return (flags & (1u << 17)) != 0u ?
            QUANTAPDF_FORM_FIELD_COMBO_BOX :
            QUANTAPDF_FORM_FIELD_LIST_BOX;
    return QUANTAPDF_FORM_FIELD_UNKNOWN;
}

static quantapdf_status quantapdf_qpdf_form_effective_string(
    QPDFObjectHandle const& object,
    char const *key,
    quantapdf_qpdf_form_capture *capture,
    quantapdf_pdf_form_string *out_string)
{
    QPDFObjectHandle value = quantapdf_qpdf_inherited_value(object, key);
    if (value.isNull())
        return QUANTAPDF_OK;
    if (!value.isString())
        return QUANTAPDF_ERROR_FORMAT;
    return quantapdf_qpdf_form_append_string(
        capture, value.getUTF8Value(), out_string);
}

static quantapdf_status quantapdf_qpdf_form_append_option_value(
    quantapdf_qpdf_form_capture *capture,
    quantapdf_pdf_form_field_internal *field,
    size_t option_index)
{
    quantapdf_pdf_form_value_internal value = {};
    value.kind = QUANTAPDF_FORM_VALUE_OPTION;
    value.option_index = option_index;
    if (field->value_count == 0u)
        field->first_value = capture->values.size();
    capture->values.push_back(value);
    ++field->value_count;
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_qpdf_form_append_utf8_value(
    quantapdf_qpdf_form_capture *capture,
    quantapdf_pdf_form_field_internal *field,
    std::string const& text)
{
    quantapdf_pdf_form_value_internal value = {};
    value.kind = QUANTAPDF_FORM_VALUE_UTF8;
    value.option_index = SIZE_MAX;
    quantapdf_status status = quantapdf_qpdf_form_append_string(
        capture, text, &value.utf8);
    if (status != QUANTAPDF_OK)
        return status;
    if (field->value_count == 0u)
        field->first_value = capture->values.size();
    capture->values.push_back(value);
    ++field->value_count;
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_qpdf_form_capture_choice_options(
    QPDFObjectHandle const& object,
    quantapdf_qpdf_form_capture *capture,
    quantapdf_pdf_form_field_internal *field)
{
    QPDFObjectHandle options = quantapdf_qpdf_inherited_value(object, "/Opt");
    field->first_option = capture->options.size();
    if (options.isNull())
        return QUANTAPDF_OK;
    if (!options.isArray())
        return QUANTAPDF_ERROR_FORMAT;
    int const count = options.getArrayNItems();
    for (int index = 0; index < count; ++index) {
        QPDFObjectHandle entry = options.getArrayItem(index);
        QPDFObjectHandle export_value = entry;
        QPDFObjectHandle display_value = entry;
        if (entry.isArray()) {
            if (entry.getArrayNItems() != 2)
                return QUANTAPDF_ERROR_FORMAT;
            export_value = entry.getArrayItem(0);
            display_value = entry.getArrayItem(1);
        }
        if (!export_value.isString() || !display_value.isString())
            return QUANTAPDF_ERROR_FORMAT;
        quantapdf_pdf_form_option_internal option = {};
        option.kind = QUANTAPDF_FORM_OPTION_CHOICE;
        quantapdf_status status = quantapdf_qpdf_form_append_string(
            capture, export_value.getUTF8Value(), &option.export_text);
        if (status != QUANTAPDF_OK)
            return status;
        status = quantapdf_qpdf_form_append_string(
            capture, display_value.getUTF8Value(), &option.display_text);
        if (status != QUANTAPDF_OK)
            return status;
        capture->options.push_back(option);
        capture->button_states.emplace_back();
        ++field->option_count;
    }
    return QUANTAPDF_OK;
}

static std::string quantapdf_qpdf_form_option_export(
    quantapdf_qpdf_form_capture const& capture,
    quantapdf_pdf_form_field_internal const& field,
    size_t index)
{
    quantapdf_pdf_form_option_internal const& option =
        capture.options[field.first_option + index];
    return capture.strings.substr(option.export_text.offset,
                                  option.export_text.size);
}

static quantapdf_status quantapdf_qpdf_form_capture_choice_value(
    QPDFObjectHandle const& object,
    quantapdf_qpdf_form_capture *capture,
    quantapdf_pdf_form_field_internal *field)
{
    QPDFObjectHandle value = quantapdf_qpdf_inherited_value(object, "/V");
    QPDFObjectHandle indices = quantapdf_qpdf_inherited_value(object, "/I");
    bool const editable = (field->flags & (1u << 18)) != 0u;
    field->is_multiselect = (field->flags & (1u << 21)) != 0u;
    if (value.isNull()) {
        if (!indices.isNull())
            return QUANTAPDF_ERROR_FORMAT;
        field->value_presence = QUANTAPDF_FORM_VALUE_MISSING;
        return QUANTAPDF_OK;
    }
    std::vector<size_t> selected;
    if (!indices.isNull()) {
        if (!indices.isArray())
            return QUANTAPDF_ERROR_FORMAT;
        int const count = indices.getArrayNItems();
        for (int index = 0; index < count; ++index) {
            QPDFObjectHandle item = indices.getArrayItem(index);
            if (!item.isInteger())
                return QUANTAPDF_ERROR_FORMAT;
            long long const raw = item.getIntValue();
            if (raw < 0 || static_cast<size_t>(raw) >= field->option_count ||
                std::find(selected.begin(), selected.end(),
                          static_cast<size_t>(raw)) != selected.end())
                return QUANTAPDF_ERROR_FORMAT;
            selected.push_back(static_cast<size_t>(raw));
        }
    }
    std::vector<std::string> values;
    if (value.isString()) {
        values.push_back(value.getUTF8Value());
    } else if (value.isArray()) {
        int const count = value.getArrayNItems();
        for (int index = 0; index < count; ++index) {
            QPDFObjectHandle item = value.getArrayItem(index);
            if (!item.isString())
                return QUANTAPDF_ERROR_FORMAT;
            values.push_back(item.getUTF8Value());
        }
    } else {
        return QUANTAPDF_ERROR_FORMAT;
    }
    if (!field->is_multiselect && values.size() > 1u)
        return QUANTAPDF_ERROR_FORMAT;
    if (!selected.empty() && selected.size() != values.size())
        return QUANTAPDF_ERROR_FORMAT;
    field->value_presence = QUANTAPDF_FORM_VALUE_PRESENT;
    for (size_t index = 0; index < values.size(); ++index) {
        size_t option_index = SIZE_MAX;
        if (!selected.empty()) {
            option_index = selected[index];
            if (quantapdf_qpdf_form_option_export(
                    *capture, *field, option_index) != values[index])
                return QUANTAPDF_ERROR_FORMAT;
        } else {
            for (size_t candidate = 0; candidate < field->option_count;
                 ++candidate) {
                if (quantapdf_qpdf_form_option_export(
                        *capture, *field, candidate) == values[index]) {
                    option_index = candidate;
                    break;
                }
            }
        }
        quantapdf_status status;
        if (option_index != SIZE_MAX) {
            status = quantapdf_qpdf_form_append_option_value(
                capture, field, option_index);
        } else if (editable) {
            status = quantapdf_qpdf_form_append_utf8_value(
                capture, field, values[index]);
        } else {
            return QUANTAPDF_ERROR_FORMAT;
        }
        if (status != QUANTAPDF_OK)
            return status;
    }
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_qpdf_form_capture_scalar_value(
    QPDFObjectHandle const& object,
    quantapdf_qpdf_form_capture *capture,
    quantapdf_pdf_form_field_internal *field)
{
    QPDFObjectHandle value = quantapdf_qpdf_inherited_value(object, "/V");
    switch (field->type) {
    case QUANTAPDF_FORM_FIELD_TEXT:
        if (value.isNull()) {
            field->value_presence = QUANTAPDF_FORM_VALUE_MISSING;
            return QUANTAPDF_OK;
        }
        if (!value.isString())
            return QUANTAPDF_ERROR_FORMAT;
        field->value_presence = QUANTAPDF_FORM_VALUE_PRESENT;
        return quantapdf_qpdf_form_append_utf8_value(
            capture, field, value.getUTF8Value());
    case QUANTAPDF_FORM_FIELD_SIGNATURE:
        field->value_presence = QUANTAPDF_FORM_VALUE_NOT_APPLICABLE;
        if (value.isNull())
            return QUANTAPDF_OK;
        if (!value.isDictionary())
            return QUANTAPDF_ERROR_FORMAT;
        if (!value.getKey("/Type").isNull() &&
            (!value.getKey("/Type").isName() ||
             value.getKey("/Type").getName() != "/Sig"))
            return QUANTAPDF_ERROR_FORMAT;
        field->is_signed = 1;
        return QUANTAPDF_OK;
    case QUANTAPDF_FORM_FIELD_PUSH_BUTTON:
    case QUANTAPDF_FORM_FIELD_UNKNOWN:
        field->value_presence = QUANTAPDF_FORM_VALUE_NOT_APPLICABLE;
        return QUANTAPDF_OK;
    default:
        return QUANTAPDF_OK;
    }
}

static quantapdf_status quantapdf_qpdf_form_publish(
    quantapdf_qpdf_form_capture const& capture,
    quantapdf_pdf_form_model **out_model)
{
    auto *model = static_cast<quantapdf_pdf_form_model *>(
        std::calloc(1u, sizeof(quantapdf_pdf_form_model)));
    if (model == nullptr)
        return QUANTAPDF_ERROR_NOMEM;
    auto copy_vector = [](auto const& source, auto **target) {
        using item_type = typename std::decay_t<decltype(source)>::value_type;
        if (source.empty())
            return true;
        *target = static_cast<item_type *>(
            std::calloc(source.size(), sizeof(item_type)));
        if (*target == nullptr)
            return false;
        std::memcpy(*target, source.data(), source.size() * sizeof(item_type));
        return true;
    };
    if (!copy_vector(capture.fields, &model->fields) ||
        !copy_vector(capture.values, &model->values) ||
        !copy_vector(capture.options, &model->options) ||
        !copy_vector(capture.widgets, &model->widgets)) {
        quantapdf_qpdf_dispose_form_model(model);
        return QUANTAPDF_ERROR_NOMEM;
    }
    model->field_count = capture.fields.size();
    model->value_count = capture.values.size();
    model->option_count = capture.options.size();
    model->widget_count = capture.widgets.size();
    if (!capture.strings.empty()) {
        model->strings = static_cast<char *>(std::malloc(capture.strings.size()));
        if (model->strings == nullptr) {
            quantapdf_qpdf_dispose_form_model(model);
            return QUANTAPDF_ERROR_NOMEM;
        }
        std::memcpy(model->strings, capture.strings.data(), capture.strings.size());
    }
    model->string_size = capture.strings.size();
    model->string_capacity = capture.strings.size();
    for (size_t index = 0; index < capture.button_states.size(); ++index) {
        std::string const& state = capture.button_states[index];
        if (state.empty())
            continue;
        auto *copy = static_cast<char *>(std::malloc(state.size() + 1u));
        if (copy == nullptr) {
            quantapdf_qpdf_dispose_form_model(model);
            return QUANTAPDF_ERROR_NOMEM;
        }
        std::memcpy(copy, state.c_str(), state.size() + 1u);
        model->options[index].button_state = copy;
    }
    *out_model = model;
    return QUANTAPDF_OK;
}

extern "C" quantapdf_status quantapdf_qpdf_open_memory(
    const unsigned char *data,
    size_t size,
    const char *password_utf8,
    quantapdf_qpdf_document **out_document)
{
    if (out_document == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_document = nullptr;

    if (data == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        auto document = std::make_unique<quantapdf_qpdf_document>();
        document->source_data = data;
        document->source_size = size;
        document->password = password_utf8 == nullptr ? "" : password_utf8;
        document->pdf = QPDF::create();
        document->pdf->processMemoryFile(
            "quantapdf-memory",
            reinterpret_cast<char const *>(data),
            size,
            password_utf8);
        *out_document = document.release();
        return QUANTAPDF_OK;
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_page_count(
    quantapdf_qpdf_document *document,
    int *out_page_count)
{
    if (out_page_count == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_page_count = 0;

    if (document == nullptr || document->pdf == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        /* getAllPages() repairs malformed page attributes in place. Counting
         * must not erase invalid /Annots or other later observation evidence. */
        int page_count = 0;
        std::set<QPDFObjGen> seen;
        QPDFObjectHandle ignored = QPDFObjectHandle::newNull();
        size_t remaining = 0;
        quantapdf_status status = quantapdf_qpdf_compute_work_budget_limit(
            document->source_size, &remaining);
        if (status != QUANTAPDF_OK)
            return status;
        status = quantapdf_qpdf_find_page(document->pdf->getRoot().getKey("/Pages"),
            INT_MAX, &page_count, 1u, &seen, &ignored, nullptr, &remaining);
        if (status == QUANTAPDF_OK)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        if (status != QUANTAPDF_ERROR_ARGUMENT)
            return status;
        *out_page_count = page_count;
        return QUANTAPDF_OK;
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" void quantapdf_qpdf_close(
    quantapdf_qpdf_document *document)
{
    delete document;
}

extern "C" quantapdf_status quantapdf_qpdf_page_user_unit(
    quantapdf_qpdf_document *document,
    int page_index,
    double *out_user_unit)
{
    if (out_user_unit == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_user_unit = 0.0;
    if (document == nullptr || document->pdf == nullptr || page_index < 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        quantapdf_qpdf_page_geometry geometry = {};
        quantapdf_status status = quantapdf_qpdf_load_page_geometry(
            *document->pdf, page_index, &geometry);
        if (status != QUANTAPDF_OK)
            return status;
        *out_user_unit = geometry.user_unit;
        return QUANTAPDF_OK;
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_page_box_bounds(
    quantapdf_qpdf_document *document,
    int page_index,
    quantapdf_page_box box,
    quantapdf_rect *out_bounds)
{
    if (document == nullptr || document->pdf == nullptr ||
        page_index < 0 || out_bounds == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (box != QUANTAPDF_PAGE_BOX_MEDIA && box != QUANTAPDF_PAGE_BOX_CROP)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        quantapdf_qpdf_page_geometry geometry = {};
        quantapdf_status status = quantapdf_qpdf_load_page_geometry(
            *document->pdf, page_index, &geometry);
        if (status != QUANTAPDF_OK)
            return status;
        double const visible[4] = {
            geometry.left, geometry.bottom, geometry.right, geometry.top
        };
        double const *selected = box == QUANTAPDF_PAGE_BOX_MEDIA ?
            geometry.media : visible;
        *out_bounds = quantapdf_qpdf_pdf_rectangle_to_public(
            geometry, selected);
        out_bounds->x0 = static_cast<float>(
            out_bounds->x0 / geometry.user_unit);
        out_bounds->y0 = static_cast<float>(
            out_bounds->y0 / geometry.user_unit);
        out_bounds->x1 = static_cast<float>(
            out_bounds->x1 / geometry.user_unit);
        out_bounds->y1 = static_cast<float>(
            out_bounds->y1 / geometry.user_unit);
        return QUANTAPDF_OK;
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

/* Link observations use the same checked page geometry as edits and page boxes.
 * They do not interpret paint, load a renderer, or retain provider objects. */
static quantapdf_status quantapdf_qpdf_named_link_destination(
    QPDF& pdf, QPDFObjectHandle name,
    quantapdf_qpdf_work_budget *budget, QPDFObjectHandle *out_destination)
{
    std::string const key = name.isName() ? name.getName().substr(1) :
        name.getStringValue();
    QPDFObjectHandle old_dests = pdf.getRoot().getKey("/Dests");
    if (!old_dests.isNull()) {
        if (!old_dests.isDictionary())
            return QUANTAPDF_ERROR_FORMAT;
        *out_destination = old_dests.getKey("/" + key);
        if (!out_destination->isNull())
            return QUANTAPDF_OK;
    }
    QPDFObjectHandle names = pdf.getRoot().getKey("/Names");
    if (!names.isDictionary())
        return QUANTAPDF_ERROR_FORMAT;
    struct item { QPDFObjectHandle node; size_t depth; };
    std::vector<item> pending = {{names.getKey("/Dests"), 0u}};
    std::set<QPDFObjGen> seen;
    while (!pending.empty()) {
        item const current = pending.back();
        pending.pop_back();
        if (!budget->take() || current.depth > 64u)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        if (!current.node.isDictionary() ||
            (current.node.isIndirect() &&
             !seen.insert(current.node.getObjGen()).second))
            return QUANTAPDF_ERROR_FORMAT;
        QPDFObjectHandle pairs = current.node.getKey("/Names");
        QPDFObjectHandle kids = current.node.getKey("/Kids");
        if (!pairs.isNull()) {
            if (!pairs.isArray() || pairs.getArrayNItems() % 2 != 0 ||
                !kids.isNull())
                return QUANTAPDF_ERROR_FORMAT;
            for (int i = 0; i < pairs.getArrayNItems(); i += 2) {
                if (!budget->take())
                    return QUANTAPDF_ERROR_UNSUPPORTED;
                QPDFObjectHandle candidate = pairs.getArrayItem(i);
                if (!candidate.isString())
                    return QUANTAPDF_ERROR_FORMAT;
                if (candidate.getStringValue() == key) {
                    *out_destination = pairs.getArrayItem(i + 1);
                    return QUANTAPDF_OK;
                }
            }
        } else {
            if (!kids.isArray())
                return QUANTAPDF_ERROR_FORMAT;
            for (int i = kids.getArrayNItems(); i > 0; --i) {
                if (!budget->take())
                    return QUANTAPDF_ERROR_UNSUPPORTED;
                pending.push_back({kids.getArrayItem(i - 1), current.depth + 1u});
            }
        }
    }
    return QUANTAPDF_ERROR_FORMAT;
}

static quantapdf_status quantapdf_qpdf_link_target(
    QPDF& pdf, QPDFObjectHandle destination,
    quantapdf_qpdf_work_budget *budget, quantapdf_link_internal *out_link)
{
    if (destination.isName() || destination.isString()) {
        quantapdf_status const status = quantapdf_qpdf_named_link_destination(
            pdf, destination, budget, &destination);
        if (status != QUANTAPDF_OK)
            return status;
    }
    if (destination.isDictionary())
        destination = destination.getKey("/D");
    if (!destination.isArray() || destination.getArrayNItems() < 2 ||
        !destination.getArrayItem(1).isName())
        return QUANTAPDF_ERROR_FORMAT;
    QPDFObjectHandle target = destination.getArrayItem(0);
    int page_index = 0;
    std::set<QPDFObjGen> seen;
    QPDFObjectHandle selected = QPDFObjectHandle::newNull();
    size_t remaining = budget->limit - budget->used;
    quantapdf_status lookup = quantapdf_qpdf_find_page(pdf.getRoot().getKey("/Pages"),
        -1, &page_index, 1u, &seen, &selected, &target, &remaining);
    budget->used = budget->limit - remaining;
    if (lookup != QUANTAPDF_OK)
        return lookup == QUANTAPDF_ERROR_ARGUMENT ? QUANTAPDF_ERROR_FORMAT : lookup;
    quantapdf_qpdf_page_geometry geometry = {};
    quantapdf_status const status = quantapdf_qpdf_load_page_geometry(
        pdf, page_index, &geometry);
    if (status != QUANTAPDF_OK)
        return status;
    /* The facade applies the destination page's own UserUnit exactly once. */
    geometry.user_unit = 1.0;
    double x = geometry.left;
    double y = geometry.top;
    std::string const mode = destination.getArrayItem(1).getName();
    int const count = destination.getArrayNItems();
    if (mode == "/XYZ") {
        if (count != 5)
            return QUANTAPDF_ERROR_FORMAT;
        for (int i = 2; i < 5; ++i) {
            QPDFObjectHandle value = destination.getArrayItem(i);
            if (!value.isNull() && (!value.isNumber() ||
                !std::isfinite(value.getNumericValue())))
                return QUANTAPDF_ERROR_FORMAT;
        }
        if (!destination.getArrayItem(2).isNull())
            x = destination.getArrayItem(2).getNumericValue();
        if (!destination.getArrayItem(3).isNull())
            y = destination.getArrayItem(3).getNumericValue();
    } else if (mode == "/Fit" || mode == "/FitB") {
        if (count != 2)
            return QUANTAPDF_ERROR_FORMAT;
    } else if (mode == "/FitH" || mode == "/FitBH" ||
               mode == "/FitV" || mode == "/FitBV" || mode == "/FitR") {
        int const expected = mode == "/FitR" ? 6 : 3;
        if (count != expected)
            return QUANTAPDF_ERROR_FORMAT;
        for (int i = 2; i < expected; ++i) {
            QPDFObjectHandle value = destination.getArrayItem(i);
            if ((!value.isNull() || mode == "/FitR") &&
                (!value.isNumber() || !std::isfinite(value.getNumericValue())))
                return QUANTAPDF_ERROR_FORMAT;
        }
        /* The public point-only observation has no fit/zoom representation,
         * matching the former reader's zero point for non-XYZ destinations. */
        out_link->target_page = page_index;
        out_link->kind = QUANTAPDF_LINK_INTERNAL;
        return QUANTAPDF_OK;
    } else {
        return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    out_link->target_page = page_index;
    out_link->kind = QUANTAPDF_LINK_INTERNAL;
    if (mode == "/XYZ") {
        double public_x = 0.0, public_y = 0.0;
        quantapdf_qpdf_pdf_point_to_public(geometry, x, y, &public_x, &public_y);
        out_link->target = {static_cast<float>(public_x), static_cast<float>(public_y)};
        if (!std::isfinite(out_link->target.x) || !std::isfinite(out_link->target.y))
            return QUANTAPDF_ERROR_FORMAT;
    }
    return QUANTAPDF_OK;
}

extern "C" quantapdf_status quantapdf_qpdf_extract_links(
    quantapdf_qpdf_document *document, int page_index,
    quantapdf_link_page **out_links)
{
    if (out_links == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_links = nullptr;
    if (document == nullptr || document->pdf == nullptr || page_index < 0)
        return QUANTAPDF_ERROR_ARGUMENT;
    try {
        quantapdf_qpdf_page_geometry geometry = {};
        quantapdf_status status = quantapdf_qpdf_load_page_geometry(
            *document->pdf, page_index, &geometry);
        if (status != QUANTAPDF_OK)
            return status;
        geometry.user_unit = 1.0;
        quantapdf_qpdf_work_budget budget;
        status = quantapdf_qpdf_init_work_budget(document->source_size, &budget);
        if (status != QUANTAPDF_OK)
            return status;
        QPDFObjectHandle annots = geometry.page.getKey("/Annots");
        if (!annots.isNull() && !annots.isArray())
            return QUANTAPDF_ERROR_FORMAT;
        auto dispose = [](quantapdf_link_page *snapshot) {
            if (snapshot == nullptr)
                return;
            for (size_t i = 0; i < snapshot->count; ++i)
                std::free(snapshot->items[i].uri);
            std::free(snapshot->items);
            std::free(snapshot);
        };
        std::unique_ptr<quantapdf_link_page, decltype(dispose)> snapshot(
            static_cast<quantapdf_link_page *>(std::calloc(1u, sizeof(quantapdf_link_page))),
            dispose);
        if (!snapshot)
            return QUANTAPDF_ERROR_NOMEM;
        std::vector<quantapdf_link_internal> links;
        std::vector<std::string> uris;
        size_t uri_bytes = 0;
        int const count = annots.isArray() ? annots.getArrayNItems() : 0;
        for (int i = 0; i < count; ++i) {
            if (!budget.take())
                return QUANTAPDF_ERROR_UNSUPPORTED;
            QPDFObjectHandle annot = annots.getArrayItem(i);
            if (!annot.isDictionary())
                return QUANTAPDF_ERROR_FORMAT;
            QPDFObjectHandle subtype = annot.getKey("/Subtype");
            if (!subtype.isName())
                return QUANTAPDF_ERROR_FORMAT;
            if (subtype.getName() != "/Link")
                continue;
            if (links.size() >= 16384u)
                return QUANTAPDF_ERROR_UNSUPPORTED;
            double rect[4];
            status = quantapdf_qpdf_read_rectangle(annot.getKey("/Rect"), rect, true);
            if (status != QUANTAPDF_OK)
                return status;
            quantapdf_link_internal link = {};
            link.target_page = -1;
            link.hotspot = quantapdf_qpdf_pdf_rectangle_to_public(geometry, rect);
            if (!std::isfinite(link.hotspot.x0) || !std::isfinite(link.hotspot.x1) ||
                !std::isfinite(link.hotspot.y0) || !std::isfinite(link.hotspot.y1))
                return QUANTAPDF_ERROR_FORMAT;
            QPDFObjectHandle destination = annot.getKey("/Dest");
            std::string uri;
            if (destination.isNull()) {
                QPDFObjectHandle action = annot.getKey("/A");
                if (!action.isDictionary() || !action.getKey("/S").isName())
                    return QUANTAPDF_ERROR_FORMAT;
                std::string const type = action.getKey("/S").getName();
                if (type == "/URI") {
                    QPDFObjectHandle value = action.getKey("/URI");
                    if (!value.isString())
                        return QUANTAPDF_ERROR_FORMAT;
                    uri = value.getStringValue();
                    if (uri.size() > 1048576u - uri_bytes)
                        return QUANTAPDF_ERROR_UNSUPPORTED;
                    if (uri.find('\0') != std::string::npos)
                        return QUANTAPDF_ERROR_FORMAT;
                    uri_bytes += uri.size();
                    link.kind = QUANTAPDF_LINK_URI;
                } else if (type == "/GoTo") {
                    destination = action.getKey("/D");
                    if (destination.isNull())
                        return QUANTAPDF_ERROR_FORMAT;
                } else {
                    return QUANTAPDF_ERROR_UNSUPPORTED;
                }
            }
            if (!destination.isNull()) {
                status = quantapdf_qpdf_link_target(*document->pdf,
                    destination, &budget, &link);
                if (status != QUANTAPDF_OK)
                    return status;
            }
            links.push_back(link);
            uris.push_back(std::move(uri));
        }
        if (!links.empty()) {
            snapshot->items = static_cast<quantapdf_link_internal *>(
                std::calloc(links.size(), sizeof(*snapshot->items)));
            if (snapshot->items == nullptr)
                return QUANTAPDF_ERROR_NOMEM;
        }
        snapshot->count = links.size();
        for (size_t i = 0; i < links.size(); ++i) {
            snapshot->items[i] = links[i];
            if (links[i].kind == QUANTAPDF_LINK_URI) {
                auto& link = snapshot->items[i];
                link.uri = static_cast<char *>(std::malloc(uris[i].size() + 1u));
                if (link.uri == nullptr)
                    return QUANTAPDF_ERROR_NOMEM;
                std::memcpy(link.uri, uris[i].c_str(), uris[i].size() + 1u);
                link.uri_size = uris[i].size();
            }
        }
        *out_links = snapshot.release();
        return QUANTAPDF_OK;
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_extract_annotations(
    quantapdf_qpdf_document *document,
    int page_index,
    quantapdf_annotation_page **out_annotations)
{
    if (out_annotations == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_annotations = nullptr;
    if (document == nullptr || document->pdf == nullptr || page_index < 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        auto const& pages = document->pdf->getAllPages();
        if (static_cast<size_t>(page_index) >= pages.size())
            return QUANTAPDF_ERROR_ARGUMENT;
        QPDFObjectHandle page = pages[static_cast<size_t>(page_index)];
        QPDFObjectHandle media = quantapdf_qpdf_inherited_value(
            page, "/MediaBox");
        QPDFObjectHandle crop = quantapdf_qpdf_inherited_value(
            page, "/CropBox");
        if (crop.isNull())
            crop = media;
        if (!crop.isArray() || crop.getArrayNItems() != 4)
            return QUANTAPDF_ERROR_FORMAT;
        QPDFObjectHandle crop_left_object = crop.getArrayItem(0);
        QPDFObjectHandle crop_top_object = crop.getArrayItem(3);
        if (!crop_left_object.isNumber() || !crop_top_object.isNumber())
            return QUANTAPDF_ERROR_FORMAT;
        double const crop_left = crop_left_object.getNumericValue();
        double const crop_top = crop_top_object.getNumericValue();

        double user_unit = 1.0;
        QPDFObjectHandle user_unit_object = page.getKey("/UserUnit");
        if (!user_unit_object.isNull()) {
            if (!user_unit_object.isNumber())
                return QUANTAPDF_ERROR_FORMAT;
            user_unit = user_unit_object.getNumericValue();
            if (!std::isfinite(user_unit) || user_unit <= 0.0 ||
                user_unit > 75000.0)
                return QUANTAPDF_ERROR_FORMAT;
        }

        std::vector<quantapdf_qpdf_captured_annotation> captured;
        QPDFObjectHandle annots = page.getKey("/Annots");
        if (annots.isArray()) {
            int const count = annots.getArrayNItems();
            for (int index = 0; index < count; ++index) {
                QPDFObjectHandle annotation = annots.getArrayItem(index);
                if (!annotation.isDictionary())
                    continue;

                QPDFObjectHandle subtype = annotation.getKey("/Subtype");
                std::string const subtype_name = subtype.isName() ?
                    subtype.getName() : std::string();
                if (subtype_name == "/Link" || subtype_name == "/Popup" ||
                    subtype_name == "/Widget")
                    continue;

                QPDFObjectHandle rect = annotation.getKey("/Rect");
                if (!rect.isArray() || rect.getArrayNItems() != 4)
                    return QUANTAPDF_ERROR_FORMAT;
                double values[4];
                for (int value_index = 0; value_index < 4; ++value_index) {
                    QPDFObjectHandle value = rect.getArrayItem(value_index);
                    if (!value.isNumber())
                        return QUANTAPDF_ERROR_FORMAT;
                    values[value_index] = value.getNumericValue();
                    if (!std::isfinite(values[value_index]))
                        return QUANTAPDF_ERROR_FORMAT;
                }
                double const left = values[0] < values[2] ? values[0] : values[2];
                double const right = values[0] < values[2] ? values[2] : values[0];
                double const bottom = values[1] < values[3] ? values[1] : values[3];
                double const top = values[1] < values[3] ? values[3] : values[1];
                if (right <= left || top <= bottom)
                    return QUANTAPDF_ERROR_FORMAT;

                quantapdf_qpdf_captured_annotation item = {};
                item.type = quantapdf_qpdf_annotation_type(subtype_name);
                item.bounds.x0 = static_cast<float>((left - crop_left) * user_unit);
                item.bounds.y0 = static_cast<float>((crop_top - top) * user_unit);
                item.bounds.x1 = static_cast<float>((right - crop_left) * user_unit);
                item.bounds.y1 = static_cast<float>((crop_top - bottom) * user_unit);

                QPDFObjectHandle flags = annotation.getKey("/F");
                if (!flags.isNull()) {
                    if (!flags.isInteger())
                        return QUANTAPDF_ERROR_FORMAT;
                    long long const flag_value = flags.getIntValue();
                    if (flag_value < 0 || flag_value > UINT32_MAX)
                        return QUANTAPDF_ERROR_FORMAT;
                    item.flags = static_cast<uint32_t>(flag_value);
                }

                QPDFObjectHandle contents = annotation.getKey("/Contents");
                if (!contents.isNull()) {
                    if (!contents.isString())
                        return QUANTAPDF_ERROR_FORMAT;
                    item.contents = contents.getUTF8Value();
                    item.has_contents = true;
                }
                captured.push_back(std::move(item));
            }
        }

        auto *snapshot = static_cast<quantapdf_annotation_page *>(
            std::calloc(1u, sizeof(quantapdf_annotation_page)));
        if (snapshot == nullptr)
            return QUANTAPDF_ERROR_NOMEM;
        if (!captured.empty()) {
            snapshot->items = static_cast<quantapdf_annotation_internal *>(
                std::calloc(
                    captured.size(), sizeof(quantapdf_annotation_internal)));
            if (snapshot->items == nullptr) {
                std::free(snapshot);
                return QUANTAPDF_ERROR_NOMEM;
            }
        }

        size_t string_size = 0;
        for (auto const& item : captured) {
            if (item.has_contents) {
                if (string_size > std::numeric_limits<size_t>::max() -
                        item.contents.size() - 1u) {
                    std::free(snapshot->items);
                    std::free(snapshot);
                    return QUANTAPDF_ERROR_NOMEM;
                }
                string_size += item.contents.size() + 1u;
            }
        }
        if (string_size != 0) {
            snapshot->strings = static_cast<char *>(std::malloc(string_size));
            if (snapshot->strings == nullptr) {
                std::free(snapshot->items);
                std::free(snapshot);
                return QUANTAPDF_ERROR_NOMEM;
            }
        }

        size_t string_position = 0;
        for (size_t index = 0; index < captured.size(); ++index) {
            auto const& source = captured[index];
            auto *target = &snapshot->items[index];
            target->type = source.type;
            target->bounds = source.bounds;
            target->flags = source.flags;
            if (source.has_contents) {
                target->contents_offset = string_position;
                target->contents_size = source.contents.size();
                target->has_contents = 1;
                std::memcpy(
                    snapshot->strings + string_position,
                    source.contents.data(),
                    source.contents.size());
                string_position += source.contents.size();
                snapshot->strings[string_position++] = '\0';
            }
        }
        snapshot->count = captured.size();
        snapshot->string_size = string_position;
        *out_annotations = snapshot;
        return QUANTAPDF_OK;
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_extract_form(
    quantapdf_qpdf_document *document,
    quantapdf_pdf_form_model **out_model)
{
    if (out_model == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_model = nullptr;
    if (document == nullptr || document->pdf == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        QPDFObjectHandle acroform = document->pdf->getRoot().getKey("/AcroForm");
        quantapdf_qpdf_form_capture capture;
        if (acroform.isNull())
            return quantapdf_qpdf_form_publish(capture, out_model);
        if (!acroform.isDictionary())
            return QUANTAPDF_ERROR_FORMAT;
        QPDFObjectHandle roots = acroform.getKey("/Fields");
        if (roots.isNull())
            return quantapdf_qpdf_form_publish(capture, out_model);
        if (!roots.isArray())
            return QUANTAPDF_ERROR_FORMAT;

        std::vector<quantapdf_qpdf_form_group> groups;
        std::vector<quantapdf_qpdf_form_node> nodes;
        std::map<QPDFObjGen, size_t> node_groups;
        std::set<std::string> names;
        quantapdf_status tree_status = QUANTAPDF_OK;
        std::function<void(QPDFObjectHandle, QPDFObjectHandle, size_t,
                           std::string const&, size_t)> visit;
        visit = [&](QPDFObjectHandle object,
                    QPDFObjectHandle parent,
                    size_t inherited_group,
                    std::string const& parent_name,
                    size_t depth) {
            if (tree_status != QUANTAPDF_OK)
                return;
            if (depth > 256u) {
                tree_status = QUANTAPDF_ERROR_UNSUPPORTED;
                return;
            }
            if (!object.isDictionary()) {
                tree_status = QUANTAPDF_ERROR_FORMAT;
                return;
            }
            if (object.isIndirect() &&
                !node_groups.emplace(object.getObjGen(), SIZE_MAX).second) {
                tree_status = QUANTAPDF_ERROR_FORMAT;
                return;
            }
            QPDFObjectHandle actual_parent = object.getKey("/Parent");
            if ((parent.isNull() && !actual_parent.isNull()) ||
                (!parent.isNull() &&
                 !quantapdf_qpdf_same_object(parent, actual_parent))) {
                tree_status = QUANTAPDF_ERROR_FORMAT;
                return;
            }
            QPDFObjectHandle partial = object.getKey("/T");
            bool const has_partial = !partial.isNull();
            std::string full_name = parent_name;
            size_t group_index = inherited_group;
            if (has_partial) {
                if (!partial.isString()) {
                    tree_status = QUANTAPDF_ERROR_FORMAT;
                    return;
                }
                std::string const segment = partial.getUTF8Value();
                if (segment.find('.') != std::string::npos) {
                    tree_status = QUANTAPDF_ERROR_FORMAT;
                    return;
                }
                full_name = parent_name.empty() ? segment :
                    parent_name + "." + segment;
                if (inherited_group != SIZE_MAX)
                    groups[inherited_group].has_named_child = true;
                group_index = groups.size();
                groups.push_back({object, full_name, true});
            } else if (group_index == SIZE_MAX) {
                group_index = groups.size();
                groups.push_back({object, {}, false});
            }
            if (object.isIndirect())
                node_groups[object.getObjGen()] = group_index;
            QPDFObjectHandle subtype = object.getKey("/Subtype");
            bool widget = false;
            if (!subtype.isNull()) {
                if (!subtype.isName()) {
                    tree_status = QUANTAPDF_ERROR_FORMAT;
                    return;
                }
                widget = subtype.getName() == "/Widget";
            }
            groups[group_index].has_widget =
                groups[group_index].has_widget || widget;
            nodes.push_back({object, group_index, widget});
            QPDFObjectHandle kids = object.getKey("/Kids");
            if (kids.isNull())
                return;
            if (!kids.isArray()) {
                tree_status = QUANTAPDF_ERROR_FORMAT;
                return;
            }
            int const count = kids.getArrayNItems();
            for (int index = 0; index < count; ++index)
                visit(kids.getArrayItem(index), object, group_index,
                      full_name, depth + 1u);
        };
        int const root_count = roots.getArrayNItems();
        for (int index = 0; index < root_count; ++index)
            visit(roots.getArrayItem(index), QPDFObjectHandle::newNull(),
                  SIZE_MAX, {}, 1u);
        if (tree_status != QUANTAPDF_OK)
            return tree_status;

        for (size_t group_index = 0; group_index < groups.size();
             ++group_index) {
            for (char const *key : {"/FT", "/Ff", "/V", "/Opt", "/I",
                                    "/TU"}) {
                bool found = false;
                std::string expected;
                for (auto const& node : nodes) {
                    if (node.group_index != group_index)
                        continue;
                    QPDFObjectHandle value = node.object.getKey(key);
                    if (value.isNull())
                        continue;
                    std::string const serialized = value.unparse();
                    if (!found) {
                        found = true;
                        expected = serialized;
                    } else if (serialized != expected) {
                        return QUANTAPDF_ERROR_FORMAT;
                    }
                }
            }
        }

        for (size_t group_index = 0; group_index < groups.size();
             ++group_index) {
            quantapdf_qpdf_form_group& group = groups[group_index];
            if (group.has_named_child) {
                if (group.has_widget)
                    return QUANTAPDF_ERROR_FORMAT;
                continue;
            }
            if (group.name_present && !names.insert(group.full_name).second)
                return QUANTAPDF_ERROR_FORMAT;
            quantapdf_pdf_form_field_internal field = {};
            field.first_value = capture.values.size();
            field.first_option = capture.options.size();
            quantapdf_status status = quantapdf_qpdf_form_read_uint32(
                group.head, "/Ff", 0u, &field.flags);
            if (status != QUANTAPDF_OK)
                return status;
            field.type = quantapdf_qpdf_form_field_type(
                group.head, field.flags, &status);
            if (status != QUANTAPDF_OK)
                return status;
            if (group.name_present) {
                status = quantapdf_qpdf_form_append_string(
                    &capture, group.full_name, &field.name);
                if (status != QUANTAPDF_OK)
                    return status;
            }
            status = quantapdf_qpdf_form_effective_string(
                group.head, "/TU", &capture, &field.label);
            if (status != QUANTAPDF_OK)
                return status;
            group.public_index = capture.fields.size();
            capture.fields.push_back(field);
        }

        std::map<QPDFObjGen, size_t> widget_button_options;
        for (size_t group_index = 0; group_index < groups.size();
             ++group_index) {
            quantapdf_qpdf_form_group const& group = groups[group_index];
            if (group.public_index == SIZE_MAX)
                continue;
            quantapdf_pdf_form_field_internal& field =
                capture.fields[group.public_index];
            if (field.type == QUANTAPDF_FORM_FIELD_COMBO_BOX ||
                field.type == QUANTAPDF_FORM_FIELD_LIST_BOX) {
                quantapdf_status status =
                    quantapdf_qpdf_form_capture_choice_options(
                        group.head, &capture, &field);
                if (status != QUANTAPDF_OK)
                    return status;
                continue;
            }
            if (field.type != QUANTAPDF_FORM_FIELD_CHECKBOX &&
                field.type != QUANTAPDF_FORM_FIELD_RADIO_BUTTON)
                continue;
            field.first_option = capture.options.size();
            for (auto const& node : nodes) {
                if (!node.widget || node.group_index != group_index)
                    continue;
                QPDFObjectHandle normal =
                    node.object.getKey("/AP").getKey("/N");
                if (!normal.isDictionary())
                    return QUANTAPDF_ERROR_FORMAT;
                std::string state;
                for (std::string const& key : normal.getKeys()) {
                    if (key == "/Off")
                        continue;
                    if (!state.empty())
                        return QUANTAPDF_ERROR_FORMAT;
                    state = key.substr(1u);
                }
                if (state.empty())
                    return QUANTAPDF_ERROR_FORMAT;
                size_t option_index = SIZE_MAX;
                for (size_t index = 0; index < field.option_count; ++index) {
                    if (capture.button_states[field.first_option + index] ==
                        state) {
                        option_index = index;
                        break;
                    }
                }
                if (option_index == SIZE_MAX) {
                    quantapdf_pdf_form_option_internal option = {};
                    option.kind = QUANTAPDF_FORM_OPTION_BUTTON_STATE;
                    capture.options.push_back(option);
                    capture.button_states.push_back(state);
                    option_index = field.option_count++;
                }
                widget_button_options[node.object.getObjGen()] = option_index;
            }
        }

        std::set<QPDFObjGen> seen_widgets;
        auto const& pages = document->pdf->getAllPages();
        for (size_t page_index = 0; page_index < pages.size(); ++page_index) {
            QPDFObjectHandle page = pages[page_index];
            QPDFObjectHandle annots = page.getKey("/Annots");
            if (!annots.isArray())
                continue;
            quantapdf_qpdf_page_geometry geometry = {};
            quantapdf_status status = quantapdf_qpdf_load_page_geometry(
                *document->pdf, static_cast<int>(page_index), &geometry);
            if (status != QUANTAPDF_OK)
                return status;
            int const count = annots.getArrayNItems();
            for (int index = 0; index < count; ++index) {
                QPDFObjectHandle widget = annots.getArrayItem(index);
                if (!widget.isDictionary())
                    continue;
                QPDFObjectHandle subtype = widget.getKey("/Subtype");
                if (!subtype.isName() || subtype.getName() != "/Widget")
                    continue;
                if (!widget.isIndirect())
                    return QUANTAPDF_ERROR_FORMAT;
                auto const group_it = node_groups.find(widget.getObjGen());
                if (group_it == node_groups.end() ||
                    !seen_widgets.insert(widget.getObjGen()).second)
                    return QUANTAPDF_ERROR_FORMAT;
                QPDFObjectHandle owner_page = widget.getKey("/P");
                if (!owner_page.isNull() &&
                    !quantapdf_qpdf_same_object(owner_page, page))
                    return QUANTAPDF_ERROR_FORMAT;
                size_t const field_index = groups[group_it->second].public_index;
                if (field_index == SIZE_MAX)
                    return QUANTAPDF_ERROR_FORMAT;
                double raw[4];
                status = quantapdf_qpdf_read_rectangle(
                    widget.getKey("/Rect"), raw);
                if (status != QUANTAPDF_OK)
                    return status;
                quantapdf_pdf_form_widget_internal captured = {};
                captured.field_index = field_index;
                captured.page_index = static_cast<int>(page_index);
                captured.bounds = quantapdf_qpdf_pdf_rectangle_to_public(
                    geometry, raw);
                captured.button_option_index = SIZE_MAX;
                QPDFObjectHandle flags = widget.getKey("/F");
                if (!flags.isNull()) {
                    if (!flags.isInteger())
                        return QUANTAPDF_ERROR_FORMAT;
                    long long const raw_flags = flags.getIntValue();
                    if (raw_flags < 0 ||
                        static_cast<unsigned long long>(raw_flags) > UINT32_MAX)
                        return QUANTAPDF_ERROR_FORMAT;
                    captured.flags = static_cast<uint32_t>(raw_flags);
                }
                auto const option = widget_button_options.find(
                    widget.getObjGen());
                if (option != widget_button_options.end())
                    captured.button_option_index = option->second;
                capture.widgets.push_back(captured);
                ++capture.fields[field_index].widget_count;
            }
        }
        for (auto const& node : nodes) {
            if (node.widget && seen_widgets.find(node.object.getObjGen()) ==
                                   seen_widgets.end())
                return QUANTAPDF_ERROR_FORMAT;
        }

        for (size_t group_index = 0; group_index < groups.size();
             ++group_index) {
            quantapdf_qpdf_form_group const& group = groups[group_index];
            if (group.public_index == SIZE_MAX)
                continue;
            quantapdf_pdf_form_field_internal& field =
                capture.fields[group.public_index];
            quantapdf_status status;
            if (field.type == QUANTAPDF_FORM_FIELD_COMBO_BOX ||
                field.type == QUANTAPDF_FORM_FIELD_LIST_BOX) {
                status = quantapdf_qpdf_form_capture_choice_value(
                    group.head, &capture, &field);
            } else if (field.type == QUANTAPDF_FORM_FIELD_CHECKBOX ||
                       field.type == QUANTAPDF_FORM_FIELD_RADIO_BUTTON) {
                QPDFObjectHandle value =
                    quantapdf_qpdf_inherited_value(group.head, "/V");
                if (value.isNull()) {
                    field.value_presence = QUANTAPDF_FORM_VALUE_MISSING;
                    status = QUANTAPDF_OK;
                } else if (!value.isName()) {
                    status = QUANTAPDF_ERROR_FORMAT;
                } else {
                    field.value_presence = QUANTAPDF_FORM_VALUE_PRESENT;
                    std::string const state = value.getName().substr(1u);
                    status = QUANTAPDF_OK;
                    if (state != "Off") {
                        size_t selected = SIZE_MAX;
                        for (size_t index = 0; index < field.option_count;
                             ++index) {
                            if (capture.button_states[
                                    field.first_option + index] == state) {
                                selected = index;
                                break;
                            }
                        }
                        if (selected == SIZE_MAX)
                            status = QUANTAPDF_ERROR_FORMAT;
                        else
                            status = quantapdf_qpdf_form_append_option_value(
                                &capture, &field, selected);
                    }
                }
            } else {
                status = quantapdf_qpdf_form_capture_scalar_value(
                    group.head, &capture, &field);
            }
            if (status != QUANTAPDF_OK)
                return status;
        }
        return quantapdf_qpdf_form_publish(capture, out_model);
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_metadata(
    quantapdf_qpdf_document *document,
    quantapdf_metadata_field field,
    char **out_utf8,
    size_t *out_size)
{
    if (out_utf8 != nullptr)
        *out_utf8 = nullptr;
    if (out_size != nullptr)
        *out_size = 0;
    if (document == nullptr || document->pdf == nullptr ||
        out_utf8 == nullptr || out_size == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;

    char const *key = quantapdf_metadata_key(field);
    if (key == nullptr) return QUANTAPDF_ERROR_ARGUMENT;

    try {
        QPDFObjectHandle info = document->pdf->getTrailer().getKey("/Info");
        if (info.isNull())
            return QUANTAPDF_OK;
        if (!info.isDictionary())
            return QUANTAPDF_ERROR_FORMAT;
        QPDFObjectHandle value = info.getKey(key);
        if (value.isNull())
            return QUANTAPDF_OK;
        if (!value.isString())
            return QUANTAPDF_ERROR_FORMAT;
        return quantapdf_qpdf_copy_text(value, out_utf8, out_size);
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_outline(
    quantapdf_qpdf_document *document,
    quantapdf_outline **out_outline)
{
    if (out_outline == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_outline = nullptr;
    if (document == nullptr || document->pdf == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        std::vector<quantapdf_qpdf_captured_outline_node> nodes;
        std::set<QPDFObjGen> seen;
        QPDFObjectHandle root = document->pdf->getRoot();
        QPDFObjectHandle outlines = root.getKey("/Outlines");
        if (!outlines.isNull()) {
            if (!outlines.isDictionary() || !outlines.isIndirect())
                return QUANTAPDF_ERROR_FORMAT;
            quantapdf_status const status =
                quantapdf_qpdf_walk_outline_siblings(
                    *document->pdf,
                    outlines.getKey("/First"),
                    outlines.getKey("/Last"),
                    outlines,
                    SIZE_MAX,
                    1u,
                    &nodes,
                    &seen);
            if (status != QUANTAPDF_OK)
                return status;
        }

        auto *snapshot = static_cast<quantapdf_outline *>(
            std::calloc(1u, sizeof(quantapdf_outline)));
        if (snapshot == nullptr)
            return QUANTAPDF_ERROR_NOMEM;
        if (!nodes.empty()) {
            snapshot->nodes = static_cast<quantapdf_outline_node_internal *>(
                std::calloc(nodes.size(), sizeof(quantapdf_outline_node_internal)));
            if (snapshot->nodes == nullptr) {
                std::free(snapshot);
                return QUANTAPDF_ERROR_NOMEM;
            }
        }

        size_t string_size = 0;
        for (auto const& node : nodes) {
            if (node.has_title) {
                if (string_size > std::numeric_limits<size_t>::max() -
                        node.title.size() - 1u) {
                    std::free(snapshot->nodes);
                    std::free(snapshot);
                    return QUANTAPDF_ERROR_NOMEM;
                }
                string_size += node.title.size() + 1u;
            }
            if (node.destination_kind == QUANTAPDF_OUTLINE_DESTINATION_URI) {
                if (string_size > std::numeric_limits<size_t>::max() -
                        node.uri.size() - 1u) {
                    std::free(snapshot->nodes);
                    std::free(snapshot);
                    return QUANTAPDF_ERROR_NOMEM;
                }
                string_size += node.uri.size() + 1u;
            }
        }
        if (string_size != 0) {
            snapshot->strings = static_cast<char *>(std::malloc(string_size));
            if (snapshot->strings == nullptr) {
                std::free(snapshot->nodes);
                std::free(snapshot);
                return QUANTAPDF_ERROR_NOMEM;
            }
        }

        size_t position = 0;
        for (size_t index = 0; index < nodes.size(); ++index) {
            auto const& source = nodes[index];
            auto *target = &snapshot->nodes[index];
            target->parent_index = source.parent_index;
            target->first_child_index = source.first_child_index;
            target->next_sibling_index = source.next_sibling_index;
            target->destination_kind = source.destination_kind;
            target->target_page = source.target_page;
            target->target = source.target;
            target->is_open = source.is_open ? 1 : 0;
            if (source.has_title) {
                target->title_offset = position;
                target->title_size = source.title.size();
                target->has_title = 1;
                std::memcpy(
                    snapshot->strings + position,
                    source.title.data(),
                    source.title.size());
                position += source.title.size();
                snapshot->strings[position++] = '\0';
            }
            if (source.destination_kind == QUANTAPDF_OUTLINE_DESTINATION_URI) {
                target->uri_offset = position;
                target->uri_size = source.uri.size();
                std::memcpy(
                    snapshot->strings + position,
                    source.uri.data(),
                    source.uri.size());
                position += source.uri.size();
                snapshot->strings[position++] = '\0';
            }
        }
        snapshot->count = nodes.size();
        snapshot->string_size = position;
        *out_outline = snapshot;
        return QUANTAPDF_OK;
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_export_pages(
    quantapdf_qpdf_document *document,
    const int *page_indices,
    size_t page_count,
    unsigned char **out_data,
    size_t *out_size)
{
    if (out_data == nullptr || out_size == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_data = nullptr;
    *out_size = 0;
    if (document == nullptr || document->pdf == nullptr ||
        page_indices == nullptr || page_count == 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        auto const& pages = document->pdf->getAllPages();
        for (size_t index = 0; index < page_count; ++index) {
            if (page_indices[index] < 0 ||
                static_cast<size_t>(page_indices[index]) >= pages.size())
                return QUANTAPDF_ERROR_ARGUMENT;
        }
        auto destination = QPDF::create();
        destination->emptyPDF();
        document->pdf->setImmediateCopyFrom(true);
        for (size_t index = 0; index < page_count; ++index) {
            destination->addPage(
                pages[static_cast<size_t>(page_indices[index])], false);
        }
        return quantapdf_qpdf_write_memory(
            *destination, out_data, out_size);
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_merge_memory(
    const unsigned char *const *input_data,
    const size_t *input_sizes,
    size_t input_count,
    unsigned char **out_data,
    size_t *out_size)
{
    if (out_data == nullptr || out_size == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_data = nullptr;
    *out_size = 0;
    if (input_data == nullptr || input_sizes == nullptr || input_count == 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        auto destination = QPDF::create();
        destination->emptyPDF();
        std::vector<std::shared_ptr<QPDF>> sources;
        sources.reserve(input_count);
        size_t total_pages = 0;
        for (size_t input_index = 0; input_index < input_count; ++input_index) {
            if (input_data[input_index] == nullptr || input_sizes[input_index] == 0)
                return QUANTAPDF_ERROR_ARGUMENT;
            auto source = QPDF::create();
            source->processMemoryFile(
                "quantapdf-merge",
                reinterpret_cast<char const *>(input_data[input_index]),
                input_sizes[input_index]);
            source->setImmediateCopyFrom(true);
            auto const& pages = source->getAllPages();
            if (pages.size() > static_cast<size_t>(INT_MAX) - total_pages)
                return QUANTAPDF_ERROR_ARGUMENT;
            total_pages += pages.size();
            for (auto const& page : pages)
                destination->addPage(page, false);
            sources.push_back(std::move(source));
        }
        return quantapdf_qpdf_write_memory(
            *destination, out_data, out_size);
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_crop_pages(
    quantapdf_qpdf_document *document,
    const quantapdf_page_crop *crops,
    size_t crop_count,
    unsigned char **out_data,
    size_t *out_size)
{
    if (out_data == nullptr || out_size == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_data = nullptr;
    *out_size = 0;
    if (document == nullptr || document->pdf == nullptr ||
        crops == nullptr || crop_count == 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        if (quantapdf_qpdf_rewrite_forbidden(*document->pdf))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        bool changed = false;
        for (size_t index = 0; index < crop_count; ++index) {
            if (crops[index].struct_size < QUANTAPDF_PAGE_CROP_V1_MIN_SIZE ||
                crops[index].struct_size > QUANTAPDF_PAGE_CROP_V1_SIZE)
                return QUANTAPDF_ERROR_ARGUMENT;
            for (size_t prior = 0; prior < index; ++prior) {
                if (crops[prior].page_index == crops[index].page_index)
                    return QUANTAPDF_ERROR_ARGUMENT;
            }
            quantapdf_qpdf_page_geometry geometry = {};
            quantapdf_status status = quantapdf_qpdf_load_page_geometry(
                *document->pdf, crops[index].page_index, &geometry);
            if (status != QUANTAPDF_OK)
                return status;
            double rectangle[4];
            status = quantapdf_qpdf_public_crop_to_pdf(
                geometry, crops[index].bounds, rectangle);
            if (status != QUANTAPDF_OK)
                return status;
            if (crops[index].bounds.x0 != 0.0f ||
                crops[index].bounds.y0 != 0.0f ||
                crops[index].bounds.x1 != geometry.public_width ||
                crops[index].bounds.y1 != geometry.public_height)
                changed = true;
        }
        if (!changed)
            return quantapdf_qpdf_write_memory(
                *document->pdf, out_data, out_size);

        unsigned char *seed = nullptr;
        size_t seed_size = 0;
        quantapdf_status status = quantapdf_qpdf_write_memory(
            *document->pdf, &seed, &seed_size);
        if (status != QUANTAPDF_OK)
            return status;
        std::unique_ptr<unsigned char, decltype(&std::free)> seed_owner(
            seed, &std::free);
        auto private_pdf = QPDF::create();
        private_pdf->processMemoryFile(
            "quantapdf-crop",
            reinterpret_cast<char const *>(seed_owner.get()),
            seed_size);

        for (size_t index = 0; index < crop_count; ++index) {
            quantapdf_qpdf_page_geometry geometry = {};
            status = quantapdf_qpdf_load_page_geometry(
                *private_pdf, crops[index].page_index, &geometry);
            if (status != QUANTAPDF_OK)
                return status;
            double rectangle[4];
            status = quantapdf_qpdf_public_crop_to_pdf(
                geometry, crops[index].bounds, rectangle);
            if (status != QUANTAPDF_OK)
                return status;
            if (crops[index].bounds.x0 == 0.0f &&
                crops[index].bounds.y0 == 0.0f &&
                crops[index].bounds.x1 == geometry.public_width &&
                crops[index].bounds.y1 == geometry.public_height)
                continue;
            std::vector<QPDFObjectHandle> values;
            values.reserve(4u);
            for (double value : rectangle)
                values.push_back(QPDFObjectHandle::newReal(value, 6, true));
            geometry.page.replaceKey(
                "/CropBox", QPDFObjectHandle::newArray(values));
        }
        return quantapdf_qpdf_write_memory(
            *private_pdf, out_data, out_size);
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_trim_pages(
    quantapdf_qpdf_document *document,
    const quantapdf_page_trim *trims,
    size_t trim_count,
    unsigned char **out_data,
    size_t *out_size)
{
    if (out_data == nullptr || out_size == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_data = nullptr;
    *out_size = 0;
    if (document == nullptr || document->pdf == nullptr ||
        trims == nullptr || trim_count == 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        auto pdf = quantapdf_qpdf_fresh_document(
            *document, "quantapdf-trim");
        if (quantapdf_qpdf_rewrite_forbidden(*pdf))
            return QUANTAPDF_ERROR_UNSUPPORTED;

        struct trim_plan {
            QPDFObjectHandle page;
            double media[4];
            bool changed;
        };
        std::vector<trim_plan> plans;
        plans.reserve(trim_count);
        for (size_t index = 0; index < trim_count; ++index) {
            if (trims[index].struct_size < QUANTAPDF_PAGE_TRIM_V1_MIN_SIZE ||
                trims[index].struct_size > QUANTAPDF_PAGE_TRIM_V1_SIZE)
                return QUANTAPDF_ERROR_ARGUMENT;
            quantapdf_rect const requested = trims[index].bounds;
            if (!std::isfinite(requested.x0) ||
                !std::isfinite(requested.y0) ||
                !std::isfinite(requested.x1) ||
                !std::isfinite(requested.y1) ||
                requested.x1 <= requested.x0 ||
                requested.y1 <= requested.y0)
                return QUANTAPDF_ERROR_ARGUMENT;
            for (size_t prior = 0; prior < index; ++prior) {
                if (trims[prior].page_index == trims[index].page_index)
                    return QUANTAPDF_ERROR_ARGUMENT;
            }

            quantapdf_qpdf_page_geometry geometry = {};
            quantapdf_status status = quantapdf_qpdf_load_page_geometry(
                *pdf, trims[index].page_index, &geometry);
            if (status != QUANTAPDF_OK)
                return status;
            quantapdf_rect const public_media =
                quantapdf_qpdf_pdf_rectangle_to_public(
                    geometry, geometry.media);
            if (requested.x0 < public_media.x0 ||
                requested.y0 < public_media.y0 ||
                requested.x1 > public_media.x1 ||
                requested.y1 > public_media.y1)
                return QUANTAPDF_ERROR_ARGUMENT;

            double const x0 = requested.x0 / geometry.user_unit;
            double const y0 = requested.y0 / geometry.user_unit;
            double const x1 = requested.x1 / geometry.user_unit;
            double const y1 = requested.y1 / geometry.user_unit;
            trim_plan plan = {geometry.page, {}, false};
            switch (geometry.rotation) {
            case 0:
                plan.media[0] = geometry.left + x0;
                plan.media[1] = geometry.top - y1;
                plan.media[2] = geometry.left + x1;
                plan.media[3] = geometry.top - y0;
                break;
            case 90:
                plan.media[0] = geometry.left + y0;
                plan.media[1] = geometry.bottom + x0;
                plan.media[2] = geometry.left + y1;
                plan.media[3] = geometry.bottom + x1;
                break;
            case 180:
                plan.media[0] = geometry.right - x1;
                plan.media[1] = geometry.bottom + y0;
                plan.media[2] = geometry.right - x0;
                plan.media[3] = geometry.bottom + y1;
                break;
            case 270:
                plan.media[0] = geometry.right - y1;
                plan.media[1] = geometry.top - x1;
                plan.media[2] = geometry.right - y0;
                plan.media[3] = geometry.top - x0;
                break;
            default:
                return QUANTAPDF_ERROR_FORMAT;
            }
            if (plan.media[0] < geometry.media[0] ||
                plan.media[1] < geometry.media[1] ||
                plan.media[2] > geometry.media[2] ||
                plan.media[3] > geometry.media[3])
                return QUANTAPDF_ERROR_ARGUMENT;
            double const visible_left = std::max(
                plan.media[0], geometry.crop[0]);
            double const visible_bottom = std::max(
                plan.media[1], geometry.crop[1]);
            double const visible_right = std::min(
                plan.media[2], geometry.crop[2]);
            double const visible_top = std::min(
                plan.media[3], geometry.crop[3]);
            if (visible_right <= visible_left || visible_top <= visible_bottom)
                return QUANTAPDF_ERROR_ARGUMENT;
            plan.changed =
                requested.x0 != public_media.x0 ||
                requested.y0 != public_media.y0 ||
                requested.x1 != public_media.x1 ||
                requested.y1 != public_media.y1;
            plans.push_back(plan);
        }

        for (trim_plan& plan : plans) {
            if (!plan.changed)
                continue;
            std::vector<QPDFObjectHandle> values;
            values.reserve(4u);
            for (double value : plan.media)
                values.push_back(QPDFObjectHandle::newReal(value, 6, true));
            plan.page.replaceKey(
                "/MediaBox", QPDFObjectHandle::newArray(values));
        }
        return quantapdf_qpdf_write_memory(*pdf, out_data, out_size);
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_poster_split_pages(
    quantapdf_qpdf_document *document,
    const quantapdf_page_poster_split *splits,
    size_t split_count,
    unsigned char **out_data,
    size_t *out_size)
{
    if (out_data == nullptr || out_size == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_data = nullptr;
    *out_size = 0;
    if (document == nullptr || document->pdf == nullptr ||
        splits == nullptr || split_count == 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        auto pdf = quantapdf_qpdf_fresh_document(
            *document, "quantapdf-poster");
        struct split_plan {
            int page_index;
            size_t columns;
            size_t rows;
            quantapdf_qpdf_page_geometry geometry;
            bool changed;
        };
        std::vector<split_plan> plans;
        plans.reserve(split_count);
        size_t added_pages = 0;
        bool any_changed = false;
        for (size_t index = 0; index < split_count; ++index) {
            if (splits[index].struct_size <
                    QUANTAPDF_PAGE_POSTER_SPLIT_V1_MIN_SIZE ||
                splits[index].struct_size >
                    QUANTAPDF_PAGE_POSTER_SPLIT_V1_SIZE ||
                splits[index].columns == 0 || splits[index].rows == 0)
                return QUANTAPDF_ERROR_ARGUMENT;
            if (splits[index].columns >
                std::numeric_limits<size_t>::max() / splits[index].rows)
                return QUANTAPDF_ERROR_ARGUMENT;
            size_t const tile_count =
                splits[index].columns * splits[index].rows;
            if (tile_count > static_cast<size_t>(INT_MAX) ||
                added_pages > static_cast<size_t>(INT_MAX) -
                    (tile_count - 1u))
                return QUANTAPDF_ERROR_ARGUMENT;
            added_pages += tile_count - 1u;
            for (size_t prior = 0; prior < index; ++prior) {
                if (splits[prior].page_index == splits[index].page_index)
                    return QUANTAPDF_ERROR_ARGUMENT;
            }
            split_plan plan = {
                splits[index].page_index,
                splits[index].columns,
                splits[index].rows,
                {},
                tile_count != 1u
            };
            quantapdf_status status = quantapdf_qpdf_load_page_geometry(
                *pdf, plan.page_index, &plan.geometry);
            if (status != QUANTAPDF_OK)
                return status;
            plans.push_back(plan);
            any_changed = any_changed || plan.changed;
        }
        if (quantapdf_qpdf_rewrite_forbidden(*pdf))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        if (!any_changed)
            return quantapdf_qpdf_write_memory(*pdf, out_data, out_size);

        std::set<QPDFObjGen> form_nodes;
        QPDFObjectHandle early_acroform = pdf->getRoot().getKey("/AcroForm");
        if (!early_acroform.isNull()) {
            if (!early_acroform.isDictionary())
                return QUANTAPDF_ERROR_FORMAT;
            QPDFObjectHandle fields = early_acroform.getKey("/Fields");
            if (!fields.isArray())
                return QUANTAPDF_ERROR_FORMAT;
            std::function<quantapdf_status(QPDFObjectHandle, size_t)>
                collect_form_nodes;
            collect_form_nodes = [&](QPDFObjectHandle node, size_t depth) {
                if (depth > 256u || !node.isDictionary() ||
                    !node.isIndirect())
                    return depth > 256u ? QUANTAPDF_ERROR_UNSUPPORTED :
                        QUANTAPDF_ERROR_FORMAT;
                if (!form_nodes.insert(node.getObjGen()).second)
                    return QUANTAPDF_ERROR_FORMAT;
                QPDFObjectHandle kids = node.getKey("/Kids");
                if (kids.isNull())
                    return QUANTAPDF_OK;
                if (!kids.isArray())
                    return QUANTAPDF_ERROR_FORMAT;
                int const count = kids.getArrayNItems();
                for (int index = 0; index < count; ++index) {
                    quantapdf_status status = collect_form_nodes(
                        kids.getArrayItem(index), depth + 1u);
                    if (status != QUANTAPDF_OK)
                        return status;
                }
                return QUANTAPDF_OK;
            };
            int const field_count = fields.getArrayNItems();
            for (int index = 0; index < field_count; ++index) {
                quantapdf_status status = collect_form_nodes(
                    fields.getArrayItem(index), 1u);
                if (status != QUANTAPDF_OK)
                    return status;
            }
        }

        size_t source_page_count = 0;
        for (;; ++source_page_count) {
            quantapdf_qpdf_page_geometry geometry = {};
            quantapdf_status status = quantapdf_qpdf_load_page_geometry(
                *pdf, static_cast<int>(source_page_count), &geometry);
            if (status == QUANTAPDF_ERROR_ARGUMENT)
                break;
            if (status != QUANTAPDF_OK)
                return status;
            QPDFObjectHandle page = geometry.page;
            if (!page.getKey("/AA").isNull())
                return QUANTAPDF_ERROR_UNSUPPORTED;
            QPDFObjectHandle annots = page.getKey("/Annots");
            if (!annots.isNull() && !annots.isArray())
                return QUANTAPDF_ERROR_FORMAT;
            if (annots.isArray()) {
                int const count = annots.getArrayNItems();
                for (int index = 0; index < count; ++index) {
                    QPDFObjectHandle annot = annots.getArrayItem(index);
                    if (!annot.isDictionary())
                        return QUANTAPDF_ERROR_FORMAT;
                    QPDFObjectHandle subtype = annot.getKey("/Subtype");
                    if (!subtype.isName())
                        return QUANTAPDF_ERROR_FORMAT;
                    if (subtype.getName() == "/Widget" &&
                        (!annot.isIndirect() ||
                         form_nodes.find(annot.getObjGen()) ==
                             form_nodes.end()))
                        return QUANTAPDF_ERROR_FORMAT;
                    if ((!annot.getKey("/AA").isNull()) ||
                        (subtype.getName() != "/Link" &&
                         !annot.getKey("/A").isNull()))
                        return QUANTAPDF_ERROR_UNSUPPORTED;
                }
            }
            if (source_page_count == static_cast<size_t>(INT_MAX))
                return QUANTAPDF_ERROR_ARGUMENT;
        }
        if (source_page_count > static_cast<size_t>(INT_MAX) - added_pages)
            return QUANTAPDF_ERROR_ARGUMENT;
        (void)pdf->getAllPages();

        QPDFObjectHandle root = pdf->getRoot();
        if (!root.getKey("/OpenAction").isNull() ||
            !root.getKey("/StructTreeRoot").isNull() ||
            !root.getKey("/MarkInfo").isNull() ||
            !root.getKey("/Perms").isNull() ||
            !root.getKey("/PageLabels").isNull())
            return QUANTAPDF_ERROR_UNSUPPORTED;
        QPDFObjectHandle acroform = root.getKey("/AcroForm");
        if (!acroform.isNull()) {
            if (!acroform.isDictionary())
                return QUANTAPDF_ERROR_FORMAT;
            if (!acroform.getKey("/XFA").isNull() ||
                !acroform.getKey("/CO").isNull())
                return QUANTAPDF_ERROR_UNSUPPORTED;
        }
        for (split_plan const& plan : plans) {
            if (!plan.changed)
                continue;
            QPDFObjectHandle page = plan.geometry.page;
            if (!page.getKey("/BleedBox").isNull() ||
                !page.getKey("/TrimBox").isNull() ||
                !page.getKey("/ArtBox").isNull() ||
                !page.getKey("/AA").isNull())
                return QUANTAPDF_ERROR_UNSUPPORTED;
        }

        std::sort(
            plans.begin(), plans.end(),
            [](split_plan const& left, split_plan const& right) {
                return left.page_index > right.page_index;
            });
        struct page_expansion {
            QPDFObjectHandle source;
            quantapdf_qpdf_page_geometry geometry;
            size_t columns;
            size_t rows;
            std::vector<QPDFObjectHandle> tiles;
        };
        std::vector<page_expansion> expansions;
        for (split_plan& plan : plans) {
            if (!plan.changed)
                continue;
            QPDFObjectHandle source = plan.geometry.page;
            size_t const tile_count = plan.columns * plan.rows;
            page_expansion expansion = {
                source, plan.geometry, plan.columns, plan.rows, {}
            };
            expansion.tiles.reserve(tile_count);
            std::vector<std::vector<QPDFObjectHandle>> tile_annots(tile_count);
            QPDFObjectHandle source_annots = source.getKey("/Annots");
            if (!source_annots.isNull()) {
                int const annot_count = source_annots.getArrayNItems();
                for (int annot_index = 0; annot_index < annot_count;
                     ++annot_index) {
                    QPDFObjectHandle annot =
                        source_annots.getArrayItem(annot_index);
                    if (!annot.isDictionary() || !annot.isIndirect())
                        return QUANTAPDF_ERROR_FORMAT;
                    if (!annot.getKey("/AA").isNull())
                        return QUANTAPDF_ERROR_UNSUPPORTED;
                    QPDFObjectHandle subtype = annot.getKey("/Subtype");
                    if (!subtype.isName())
                        return QUANTAPDF_ERROR_FORMAT;
                    if (subtype.getName() == "/Widget" &&
                        annot.getKey("/Parent").isNull() &&
                        annot.getKey("/FT").isNull())
                        return QUANTAPDF_ERROR_FORMAT;
                    double raw_rect[4];
                    quantapdf_status status = quantapdf_qpdf_read_rectangle(
                        annot.getKey("/Rect"), raw_rect);
                    if (status != QUANTAPDF_OK)
                        return status;
                    quantapdf_rect const public_rect =
                        quantapdf_qpdf_pdf_rectangle_to_public(
                            plan.geometry, raw_rect);
                    size_t owner = SIZE_MAX;
                    bool const is_link = subtype.getName() == "/Link";
                    for (size_t row = 0; row < plan.rows; ++row) {
                        double const y0 = plan.geometry.public_height *
                            row / plan.rows;
                        double const y1 = plan.geometry.public_height *
                            (row + 1u) / plan.rows;
                        for (size_t column = 0; column < plan.columns;
                             ++column) {
                            double const x0 = plan.geometry.public_width *
                                column / plan.columns;
                            double const x1 = plan.geometry.public_width *
                                (column + 1u) / plan.columns;
                            double const clip_x0 = std::max<double>(
                                public_rect.x0, x0);
                            double const clip_y0 = std::max<double>(
                                public_rect.y0, y0);
                            double const clip_x1 = std::min<double>(
                                public_rect.x1, x1);
                            double const clip_y1 = std::min<double>(
                                public_rect.y1, y1);
                            if (clip_x1 <= clip_x0 || clip_y1 <= clip_y0)
                                continue;
                            size_t const tile_index =
                                row * plan.columns + column;
                            if (is_link) {
                                quantapdf_rect clipped = {
                                    static_cast<float>(clip_x0),
                                    static_cast<float>(clip_y0),
                                    static_cast<float>(clip_x1),
                                    static_cast<float>(clip_y1)
                                };
                                double clipped_raw[4];
                                status = quantapdf_qpdf_public_crop_to_pdf(
                                    plan.geometry, clipped, clipped_raw);
                                if (status != QUANTAPDF_OK)
                                    return status;
                                QPDFObjectHandle copy =
                                    pdf->makeIndirectObject(
                                        annot.shallowCopy());
                                std::vector<QPDFObjectHandle> rect_values;
                                rect_values.reserve(4u);
                                for (double value : clipped_raw)
                                    rect_values.push_back(
                                        QPDFObjectHandle::newReal(
                                            value, 6, true));
                                copy.replaceKey(
                                    "/Rect",
                                    QPDFObjectHandle::newArray(rect_values));
                                tile_annots[tile_index].push_back(copy);
                            } else if (public_rect.x0 >= x0 &&
                                       public_rect.y0 >= y0 &&
                                       public_rect.x1 <= x1 &&
                                       public_rect.y1 <= y1) {
                                if (owner != SIZE_MAX)
                                    return QUANTAPDF_ERROR_UNSUPPORTED;
                                owner = tile_index;
                            }
                        }
                    }
                    if (!is_link) {
                        if (owner == SIZE_MAX)
                            return QUANTAPDF_ERROR_UNSUPPORTED;
                        tile_annots[owner].push_back(annot);
                    }
                }
            }
            for (size_t row = 0; row < plan.rows; ++row) {
                for (size_t column = 0; column < plan.columns; ++column) {
                    quantapdf_rect public_tile = {};
                    public_tile.x0 = static_cast<float>(
                        plan.geometry.public_width * column / plan.columns);
                    public_tile.x1 = static_cast<float>(
                        plan.geometry.public_width * (column + 1u) /
                        plan.columns);
                    public_tile.y0 = static_cast<float>(
                        plan.geometry.public_height * row / plan.rows);
                    public_tile.y1 = static_cast<float>(
                        plan.geometry.public_height * (row + 1u) / plan.rows);
                    double rectangle[4];
                    quantapdf_status status =
                        quantapdf_qpdf_public_crop_to_pdf(
                            plan.geometry, public_tile, rectangle);
                    if (status != QUANTAPDF_OK)
                        return status;
                    QPDFObjectHandle tile = pdf->makeIndirectObject(
                        source.shallowCopy());
                    std::vector<QPDFObjectHandle> values;
                    values.reserve(4u);
                    for (double value : rectangle)
                        values.push_back(
                            QPDFObjectHandle::newReal(value, 6, true));
                    QPDFObjectHandle box = QPDFObjectHandle::newArray(values);
                    tile.replaceKey("/MediaBox", box);
                    tile.replaceKey("/CropBox", box);
                    size_t const tile_index = row * plan.columns + column;
                    if (tile_annots[tile_index].empty()) {
                        tile.removeKey("/Annots");
                    } else {
                        tile.replaceKey(
                            "/Annots",
                            QPDFObjectHandle::newArray(
                                tile_annots[tile_index]));
                        for (QPDFObjectHandle& annot :
                             tile_annots[tile_index])
                            annot.replaceKey("/P", tile);
                    }
                    pdf->addPageAt(tile, true, source);
                    expansion.tiles.push_back(tile);
                }
            }
            pdf->removePage(source);
            expansions.push_back(std::move(expansion));
        }
        root.removeKey("/PageLabels");
        quantapdf_status navigation_status = QUANTAPDF_OK;
        std::set<QPDFObjGen> navigation_seen;
        std::function<void(QPDFObjectHandle)> rewrite_destinations;
        rewrite_destinations = [&](QPDFObjectHandle object) {
            if (navigation_status != QUANTAPDF_OK || object.isNull())
                return;
            if (object.isIndirect() &&
                !navigation_seen.insert(object.getObjGen()).second)
                return;
            if (object.isArray()) {
                int const count = object.getArrayNItems();
                if (count >= 2) {
                    QPDFObjectHandle target = object.getArrayItem(0);
                    for (page_expansion const& expansion : expansions) {
                        if (!quantapdf_qpdf_same_object(
                                target, expansion.source))
                            continue;
                        QPDFObjectHandle kind = object.getArrayItem(1);
                        if (!kind.isName() || kind.getName() != "/XYZ") {
                            navigation_status = QUANTAPDF_ERROR_UNSUPPORTED;
                            return;
                        }
                        if (count < 4) {
                            navigation_status = QUANTAPDF_ERROR_FORMAT;
                            return;
                        }
                        QPDFObjectHandle x = object.getArrayItem(2);
                        QPDFObjectHandle y = object.getArrayItem(3);
                        if (x.isNull() || y.isNull()) {
                            navigation_status = QUANTAPDF_ERROR_UNSUPPORTED;
                            return;
                        }
                        if (!x.isNumber() || !y.isNumber()) {
                            navigation_status = QUANTAPDF_ERROR_FORMAT;
                            return;
                        }
                        double public_x;
                        double public_y;
                        quantapdf_qpdf_pdf_point_to_public(
                            expansion.geometry,
                            x.getNumericValue(),
                            y.getNumericValue(),
                            &public_x,
                            &public_y);
                        if (public_x < 0.0 || public_y < 0.0 ||
                            public_x > expansion.geometry.public_width ||
                            public_y > expansion.geometry.public_height) {
                            navigation_status = QUANTAPDF_ERROR_UNSUPPORTED;
                            return;
                        }
                        size_t column = static_cast<size_t>(
                            public_x * expansion.columns /
                            expansion.geometry.public_width);
                        size_t row = static_cast<size_t>(
                            public_y * expansion.rows /
                            expansion.geometry.public_height);
                        if (column >= expansion.columns)
                            column = expansion.columns - 1u;
                        if (row >= expansion.rows)
                            row = expansion.rows - 1u;
                        object.setArrayItem(
                            0,
                            expansion.tiles[row * expansion.columns + column]);
                        break;
                    }
                }
                for (int index = 0; index < count; ++index)
                    rewrite_destinations(object.getArrayItem(index));
                return;
            }
            if (object.isStream()) {
                rewrite_destinations(object.getDict());
                return;
            }
            if (!object.isDictionary())
                return;
            if (!object.getKey("/AA").isNull()) {
                navigation_status = QUANTAPDF_ERROR_UNSUPPORTED;
                return;
            }
            QPDFObjectHandle action_kind = object.getKey("/S");
            if (action_kind.isName()) {
                std::string const name = action_kind.getName();
                if (name == "/Launch" || name == "/JavaScript" ||
                    name == "/GoToR" || name == "/GoToE" ||
                    name == "/Named" || name == "/SubmitForm" ||
                    name == "/ResetForm" || name == "/ImportData" ||
                    name == "/Hide" || name == "/SetOCGState" ||
                    name == "/Rendition" || name == "/Trans" ||
                    name == "/Thread" || name == "/Sound" ||
                    name == "/Movie") {
                    navigation_status = QUANTAPDF_ERROR_UNSUPPORTED;
                    return;
                }
            }
            for (std::string const& key : object.getKeys())
                rewrite_destinations(object.getKey(key));
        };
        rewrite_destinations(root);
        if (navigation_status != QUANTAPDF_OK)
            return navigation_status;
        return quantapdf_qpdf_write_memory(*pdf, out_data, out_size);
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_rewrite_memory(
    const unsigned char *data,
    size_t size,
    unsigned char **out_data,
    size_t *out_size)
{
    if (out_data == nullptr || out_size == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_data = nullptr;
    *out_size = 0;
    if (data == nullptr || size == 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        auto pdf = QPDF::create();
        pdf->setSuppressWarnings(true);
        pdf->processMemoryFile(
            "quantapdf-rewrite",
            reinterpret_cast<char const *>(data),
            size);

        QPDFWriter writer(*pdf);
        writer.setOutputMemory();
        writer.setDeterministicID(true);
        writer.setObjectStreamMode(qpdf_o_disable);
        writer.setStreamDataMode(qpdf_s_preserve);
        writer.setPreserveUnreferencedObjects(false);
        writer.write();
        std::unique_ptr<Buffer> buffer(writer.getBuffer());
        if (buffer == nullptr || buffer->getSize() == 0 ||
            buffer->getBuffer() == nullptr)
            return QUANTAPDF_ERROR_BACKEND;

        auto *copy = static_cast<unsigned char *>(
            std::malloc(buffer->getSize()));
        if (copy == nullptr)
            return QUANTAPDF_ERROR_NOMEM;
        std::memcpy(copy, buffer->getBuffer(), buffer->getSize());
        *out_data = copy;
        *out_size = buffer->getSize();
        return QUANTAPDF_OK;
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_rewrite_lossless(
    quantapdf_qpdf_document *document,
    unsigned char **out_data,
    size_t *out_size)
{
    if (out_data == nullptr || out_size == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_data = nullptr;
    *out_size = 0;
    if (document == nullptr || document->pdf == nullptr ||
        document->source_data == nullptr || document->source_size == 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        auto pdf = QPDF::create();
        pdf->setSuppressWarnings(true);
        pdf->setAttemptRecovery(false);
        pdf->processMemoryFile(
            "quantapdf-lossless-rewrite",
            reinterpret_cast<char const *>(document->source_data),
            document->source_size,
            document->password);
        (void)pdf->getAllPages();
        (void)pdf->getAllObjects();
        if (pdf->anyWarnings())
            return QUANTAPDF_ERROR_FORMAT;
        quantapdf_status const preflight =
            quantapdf_qpdf_lossless_preflight(*pdf);
        if (preflight != QUANTAPDF_OK)
            return preflight;

        quantapdf_status const status = quantapdf_qpdf_write_memory(
            *pdf, out_data, out_size);
        if (status != QUANTAPDF_OK)
            return status;
        if (pdf->anyWarnings()) {
            std::free(*out_data);
            *out_data = nullptr;
            *out_size = 0;
            return QUANTAPDF_ERROR_FORMAT;
        }
        return QUANTAPDF_OK;
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

class quantapdf_qpdf_decoded_size_mismatch final {};

class quantapdf_qpdf_counting_pipeline final : public Pipeline {
  public:
    quantapdf_qpdf_counting_pipeline(size_t expected, size_t *observed) :
        Pipeline("quantapdf decoded-byte counter", nullptr),
        expected_(expected),
        observed_(observed)
    {
    }

    void write(unsigned char const *, size_t length) override
    {
        if (length > expected_ - count_) {
            count_ = expected_;
            if (observed_ != nullptr)
                *observed_ = std::max(*observed_, expected_ + 1u);
            throw quantapdf_qpdf_decoded_size_mismatch();
        }
        count_ += length;
        if (observed_ != nullptr)
            *observed_ = std::max(*observed_, count_);
    }

    void finish() override
    {
        finished_ = true;
    }

    bool exact() const noexcept
    {
        return finished_ && count_ == expected_;
    }

  private:
    size_t expected_;
    size_t *observed_;
    size_t count_ = 0;
    bool finished_ = false;
};

struct quantapdf_qpdf_image_plan {
    QPDFObjectHandle image = QPDFObjectHandle::newNull();
    quantapdf::detail::jpeg_image_spec spec{};
};

struct quantapdf_qpdf_provider_status {
    quantapdf_status status = QUANTAPDF_OK;

    void latch(quantapdf_status value) noexcept
    {
        if (status == QUANTAPDF_OK)
            status = value;
    }
};

struct quantapdf_qpdf_provider_counts {
    size_t registrations = 0;
    size_t invocations = 0;
};

using quantapdf_qpdf_provider_count_map =
    std::map<QPDFObjGen, quantapdf_qpdf_provider_counts>;

enum quantapdf_qpdf_image_recompression_fault {
    quantapdf_qpdf_image_fault_none = 0,
    quantapdf_qpdf_image_fault_before_provider_nomem = 1,
    quantapdf_qpdf_image_fault_provider_nomem = 2,
    quantapdf_qpdf_image_fault_provider_backend = 3,
    quantapdf_qpdf_image_fault_before_publication = 4,
    quantapdf_qpdf_image_fault_work_budget = 5
};

struct quantapdf_qpdf_resource_item {
    QPDFObjectHandle owner = QPDFObjectHandle::newNull();
    QPDFObjectHandle resources = QPDFObjectHandle::newNull();
};

static quantapdf_status quantapdf_qpdf_queue_resources(
    QPDFObjectHandle owner,
    QPDFObjectHandle resources,
    std::vector<quantapdf_qpdf_resource_item> *work,
    std::set<QPDFObjGen> *seen,
    quantapdf_qpdf_work_budget *budget)
{
    if (owner.isIndirect() && !seen->insert(owner.getObjGen()).second)
        return QUANTAPDF_OK;
    if (!budget->take())
        return QUANTAPDF_ERROR_UNSUPPORTED;
    quantapdf_qpdf_resource_item item;
    item.owner = owner;
    item.resources = resources;
    work->push_back(std::move(item));
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_qpdf_queue_appearance(
    QPDFObjectHandle appearance,
    std::vector<quantapdf_qpdf_resource_item> *work,
    std::set<QPDFObjGen> *seen,
    quantapdf_qpdf_work_budget *budget)
{
    if (!appearance.isStream())
        return QUANTAPDF_ERROR_FORMAT;
    return quantapdf_qpdf_queue_resources(
        appearance,
        appearance.getDict().getKey("/Resources"),
        work,
        seen,
        budget);
}

static quantapdf_status quantapdf_qpdf_queue_page_appearances(
    QPDFObjectHandle page,
    std::vector<quantapdf_qpdf_resource_item> *work,
    std::set<QPDFObjGen> *seen,
    quantapdf_qpdf_work_budget *budget)
{
    QPDFObjectHandle annotations = page.getKey("/Annots");
    if (annotations.isNull())
        return QUANTAPDF_OK;
    if (!annotations.isArray())
        return QUANTAPDF_ERROR_FORMAT;

    int const annotation_count = annotations.getArrayNItems();
    for (int index = 0; index < annotation_count; ++index) {
        QPDFObjectHandle annotation = annotations.getArrayItem(index);
        if (!annotation.isDictionary())
            return QUANTAPDF_ERROR_FORMAT;
        QPDFObjectHandle appearances = annotation.getKey("/AP");
        if (appearances.isNull())
            continue;
        if (!appearances.isDictionary())
            return QUANTAPDF_ERROR_FORMAT;

        for (char const *key : {"/N", "/R", "/D"}) {
            QPDFObjectHandle appearance = appearances.getKey(key);
            if (appearance.isNull())
                continue;
            if (!budget->take())
                return QUANTAPDF_ERROR_UNSUPPORTED;
            if (appearance.isStream()) {
                quantapdf_status const status =
                    quantapdf_qpdf_queue_appearance(
                        appearance, work, seen, budget);
                if (status != QUANTAPDF_OK)
                    return status;
                continue;
            }
            if (!appearance.isDictionary())
                return QUANTAPDF_ERROR_FORMAT;
            for (std::string const& state : appearance.getKeys()) {
                if (!budget->take())
                    return QUANTAPDF_ERROR_UNSUPPORTED;
                QPDFObjectHandle state_appearance = appearance.getKey(state);
                if (!state_appearance.isStream())
                    return QUANTAPDF_ERROR_FORMAT;
                quantapdf_status const status =
                    quantapdf_qpdf_queue_appearance(
                        state_appearance, work, seen, budget);
                if (status != QUANTAPDF_OK)
                    return status;
            }
        }
    }
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_qpdf_discover_images(
    QPDF& pdf,
    size_t source_size,
    bool force_budget_exhaustion,
    std::vector<QPDFObjectHandle> *images)
{
    quantapdf_qpdf_work_budget budget;
    quantapdf_status status =
        quantapdf_qpdf_init_work_budget(source_size, &budget);
    if (status != QUANTAPDF_OK)
        return status;
    if (force_budget_exhaustion)
        budget.limit = 1;

    std::vector<quantapdf_qpdf_resource_item> work;
    std::set<QPDFObjGen> owner_seen;
    std::set<QPDFObjGen> image_seen;
    for (QPDFObjectHandle page : pdf.getAllPages()) {
        if (!budget.take())
            return QUANTAPDF_ERROR_UNSUPPORTED;
        QPDFObjectHandle resources =
            QPDFPageObjectHelper(page).getAttribute("/Resources", false);
        status = quantapdf_qpdf_queue_resources(
            page, resources, &work, &owner_seen, &budget);
        if (status != QUANTAPDF_OK)
            return status;
        status = quantapdf_qpdf_queue_page_appearances(
            page, &work, &owner_seen, &budget);
        if (status != QUANTAPDF_OK)
            return status;
    }

    while (!work.empty()) {
        quantapdf_qpdf_resource_item item = std::move(work.back());
        work.pop_back();
        QPDFObjectHandle resources = item.resources;
        if (resources.isNull())
            continue;
        if (!resources.isDictionary())
            return QUANTAPDF_ERROR_FORMAT;
        QPDFObjectHandle xobjects = resources.getKey("/XObject");
        if (xobjects.isNull())
            continue;
        if (!xobjects.isDictionary())
            return QUANTAPDF_ERROR_FORMAT;

        for (std::string const& key : xobjects.getKeys()) {
            if (!budget.take())
                return QUANTAPDF_ERROR_UNSUPPORTED;
            QPDFObjectHandle xobject = xobjects.getKey(key);
            if (!xobject.isStream()) {
                if (xobject.isDictionary()) {
                    QPDFObjectHandle subtype = xobject.getKey("/Subtype");
                    if (subtype.isName() &&
                        (subtype.getName() == "/Form" ||
                         subtype.getName() == "/Image"))
                        return QUANTAPDF_ERROR_FORMAT;
                }
                continue;
            }
            QPDFObjectHandle subtype = xobject.getDict().getKey("/Subtype");
            if (subtype.isNull())
                continue;
            if (!subtype.isName())
                return QUANTAPDF_ERROR_FORMAT;
            if (subtype.getName() == "/Form") {
                status = quantapdf_qpdf_queue_resources(
                    xobject,
                    xobject.getDict().getKey("/Resources"),
                    &work,
                    &owner_seen,
                    &budget);
                if (status != QUANTAPDF_OK)
                    return status;
            } else if (subtype.getName() == "/Image") {
                if (!xobject.isIndirect())
                    continue;
                if (!image_seen.insert(xobject.getObjGen()).second)
                    continue;
                if (!budget.take())
                    return QUANTAPDF_ERROR_UNSUPPORTED;
                images->push_back(xobject);
            }
        }
    }
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_qpdf_positive_dimension(
    QPDFObjectHandle value,
    size_t *out)
{
    if (!value.isInteger())
        return QUANTAPDF_ERROR_FORMAT;
    long long dimension = 0;
    if (!value.getValueAsInt(dimension) || dimension <= 0)
        return QUANTAPDF_ERROR_FORMAT;
    unsigned long long const unsigned_dimension =
        static_cast<unsigned long long>(dimension);
    if (unsigned_dimension >
            static_cast<unsigned long long>(
                std::numeric_limits<size_t>::max()) ||
        unsigned_dimension >
            static_cast<unsigned long long>(JPEG_MAX_DIMENSION) ||
        unsigned_dimension >
            static_cast<unsigned long long>(
                std::numeric_limits<JDIMENSION>::max()))
        return QUANTAPDF_ERROR_FORMAT;
    *out = static_cast<size_t>(unsigned_dimension);
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_qpdf_identity_decode(
    QPDFObjectHandle decode,
    int components,
    bool *eligible)
{
    if (decode.isNull())
        return QUANTAPDF_OK;
    if (!decode.isArray() || decode.getArrayNItems() != components * 2)
        return QUANTAPDF_ERROR_FORMAT;
    for (int index = 0; index < components * 2; ++index) {
        QPDFObjectHandle entry = decode.getArrayItem(index);
        if (!entry.isNumber())
            return QUANTAPDF_ERROR_FORMAT;
        double const value = entry.getNumericValue();
        if (!std::isfinite(value))
            return QUANTAPDF_ERROR_FORMAT;
        double const identity = (index & 1) == 0 ? 0.0 : 1.0;
        if (value != identity)
            *eligible = false;
    }
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_qpdf_classify_image(
    QPDF& pdf,
    QPDFObjectHandle image,
    int jpeg_quality,
    size_t max_decoded_bytes_per_image,
    size_t *decoded_preflight_bytes,
    bool *eligible,
    quantapdf::detail::jpeg_image_spec *spec)
{
    *eligible = false;
    QPDFObjectHandle dict = image.getDict();
    size_t width = 0;
    size_t height = 0;
    quantapdf_status status = quantapdf_qpdf_positive_dimension(
        dict.getKey("/Width"), &width);
    if (status != QUANTAPDF_OK)
        return status;
    status = quantapdf_qpdf_positive_dimension(
        dict.getKey("/Height"), &height);
    if (status != QUANTAPDF_OK)
        return status;

    QPDFObjectHandle image_mask = dict.getKey("/ImageMask");
    if (!image_mask.isNull()) {
        if (!image_mask.isBool())
            return QUANTAPDF_ERROR_FORMAT;
        if (image_mask.getBoolValue())
            return QUANTAPDF_OK;
    }
    if (!dict.getKey("/SMask").isNull() ||
        !dict.getKey("/Mask").isNull())
        return QUANTAPDF_OK;

    QPDFObjectHandle bits = dict.getKey("/BitsPerComponent");
    if (!bits.isInteger())
        return QUANTAPDF_ERROR_FORMAT;
    long long bit_count = 0;
    if (!bits.getValueAsInt(bit_count) ||
        (bit_count != 1 && bit_count != 2 && bit_count != 4 &&
         bit_count != 8 && bit_count != 16))
        return QUANTAPDF_ERROR_FORMAT;
    if (bit_count != 8)
        return QUANTAPDF_OK;

    QPDFObjectHandle color_space = dict.getKey("/ColorSpace");
    if (color_space.isArray())
        return QUANTAPDF_OK;
    if (!color_space.isName())
        return QUANTAPDF_ERROR_FORMAT;
    int components = 0;
    if (color_space.getName() == "/DeviceGray")
        components = 1;
    else if (color_space.getName() == "/DeviceRGB")
        components = 3;
    else
        return QUANTAPDF_OK;

    bool identity = true;
    status = quantapdf_qpdf_identity_decode(
        dict.getKey("/Decode"), components, &identity);
    if (status != QUANTAPDF_OK)
        return status;
    if (!identity)
        return QUANTAPDF_OK;

    if (quantapdf::detail::jpeg_sample_size_overflows(
            width, height, static_cast<size_t>(components)))
        return QUANTAPDF_ERROR_FORMAT;
    size_t const expected = width * height * static_cast<size_t>(components);
    if (expected > max_decoded_bytes_per_image)
        return QUANTAPDF_OK;

    bool filtering_attempted = false;
    if (!image.pipeStreamData(
            nullptr,
            &filtering_attempted,
            0,
            qpdf_dl_all,
            true,
            true))
        return QUANTAPDF_OK;

    quantapdf_qpdf_counting_pipeline counter(
        expected, decoded_preflight_bytes);
    filtering_attempted = false;
    bool decoded = false;
    try {
        decoded = image.pipeStreamData(
            &counter,
            &filtering_attempted,
            0,
            qpdf_dl_all,
            true,
            true);
    } catch (quantapdf_qpdf_decoded_size_mismatch const&) {
        return QUANTAPDF_ERROR_FORMAT;
    }
    if (!decoded || !counter.exact() || pdf.anyWarnings())
        return QUANTAPDF_ERROR_FORMAT;

    spec->width = width;
    spec->height = height;
    spec->components = components;
    spec->quality = jpeg_quality;
    *eligible = true;
    return QUANTAPDF_OK;
}

static void quantapdf_qpdf_register_image_provider(
    quantapdf_qpdf_image_plan plan,
    std::shared_ptr<quantapdf_qpdf_provider_status> const& provider_status,
    std::shared_ptr<quantapdf_qpdf_provider_count_map> const& provider_counts,
    std::shared_ptr<int> const& test_fault)
{
    QPDFObjGen const identity = plan.image.getObjGen();
    ++(*provider_counts)[identity].registrations;
    QPDFObjectHandle source = plan.image.copyStream();
    quantapdf::detail::jpeg_image_spec const spec = plan.spec;
    plan.image.replaceStreamData(
        std::function<bool(Pipeline*, bool, bool)>(
            [source, spec, identity, provider_status, provider_counts,
             test_fault](
                Pipeline *pipeline,
                bool suppress_warnings,
                bool) mutable -> bool {
                try {
                    ++(*provider_counts)[identity].invocations;
                    if (*test_fault ==
                        quantapdf_qpdf_image_fault_provider_nomem) {
                        *test_fault = quantapdf_qpdf_image_fault_none;
                        provider_status->latch(QUANTAPDF_ERROR_NOMEM);
                        return false;
                    }
                    if (*test_fault ==
                        quantapdf_qpdf_image_fault_provider_backend) {
                        *test_fault = quantapdf_qpdf_image_fault_none;
                        provider_status->latch(QUANTAPDF_ERROR_BACKEND);
                        return false;
                    }
                    std::unique_ptr<quantapdf::detail::jpeg_encoder> encoder;
                    quantapdf_status const status =
                        quantapdf::detail::jpeg_encoder::create(
                            spec, pipeline, &encoder);
                    if (status != QUANTAPDF_OK) {
                        provider_status->latch(status);
                        return false;
                    }
                    bool filtering_attempted = false;
                    if (!source.pipeStreamData(
                            encoder->pipeline(),
                            &filtering_attempted,
                            0,
                            qpdf_dl_all,
                            suppress_warnings,
                            true)) {
                        provider_status->latch(QUANTAPDF_ERROR_BACKEND);
                        return false;
                    }
                    return true;
                } catch (std::bad_alloc const&) {
                    provider_status->latch(QUANTAPDF_ERROR_NOMEM);
                    return false;
                } catch (...) {
                    provider_status->latch(QUANTAPDF_ERROR_BACKEND);
                    return false;
                }
            }),
        QPDFObjectHandle::newName("/DCTDecode"),
        spec.components == 3
            ? QPDFObjectHandle::parse("<< /ColorTransform 0 >>")
            : QPDFObjectHandle::newNull());
    plan.image.setFilterOnWrite(false);
}

extern "C" quantapdf_status quantapdf_qpdf_recompress_images(
    quantapdf_qpdf_document *document,
    int jpeg_quality,
    size_t max_decoded_bytes_per_image,
    int test_fault,
    quantapdf_qpdf_image_recompression_test_stats *test_stats,
    unsigned char **out_data,
    size_t *out_size)
{
    if (out_data == nullptr || out_size == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_data = nullptr;
    *out_size = 0;
    if (test_stats != nullptr)
        *test_stats = {};
    if (document == nullptr || document->pdf == nullptr ||
        jpeg_quality < 1 || jpeg_quality > 100 ||
        max_decoded_bytes_per_image == 0 ||
        test_fault < quantapdf_qpdf_image_fault_none ||
        test_fault > quantapdf_qpdf_image_fault_work_budget)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (document->source_data == nullptr || document->source_size == 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    std::shared_ptr<quantapdf_qpdf_provider_status> provider_status;
    std::shared_ptr<quantapdf_qpdf_provider_count_map> provider_counts;
    std::shared_ptr<int> pending_test_fault;
    try {
        provider_status =
            std::make_shared<quantapdf_qpdf_provider_status>();
        provider_counts =
            std::make_shared<quantapdf_qpdf_provider_count_map>();
        pending_test_fault = std::make_shared<int>(test_fault);
        auto pdf = QPDF::create();
        pdf->setSuppressWarnings(true);
        pdf->setAttemptRecovery(false);
        pdf->processMemoryFile(
            "quantapdf-image-recompression",
            reinterpret_cast<char const *>(document->source_data),
            document->source_size,
            document->password);
        (void)pdf->getAllPages();
        (void)pdf->getAllObjects();
        if (pdf->anyWarnings())
            return QUANTAPDF_ERROR_FORMAT;

        uint32_t findings = 0;
        quantapdf_status status = quantapdf_qpdf_audit_document(
            *pdf, document->source_size, &findings);
        if (status != QUANTAPDF_OK)
            return status;
        if (pdf->anyWarnings())
            return QUANTAPDF_ERROR_FORMAT;
        quantapdf_validation_report report{};
        status = quantapdf_validate_security(findings,
            QUANTAPDF_AUDIT_SIGNATURE | QUANTAPDF_AUDIT_ENCRYPTION, &report);
        if (status != QUANTAPDF_OK)
            return status;

        std::vector<QPDFObjectHandle> images;
        status = quantapdf_qpdf_discover_images(
            *pdf,
            document->source_size,
            test_fault == quantapdf_qpdf_image_fault_work_budget,
            &images);
        if (status != QUANTAPDF_OK)
            return status;

        std::vector<quantapdf_qpdf_image_plan> plans;
        plans.reserve(images.size());
        for (QPDFObjectHandle image : images) {
            quantapdf_qpdf_image_plan plan;
            bool eligible = false;
            status = quantapdf_qpdf_classify_image(
                *pdf,
                image,
                jpeg_quality,
                max_decoded_bytes_per_image,
                test_stats == nullptr
                    ? nullptr
                    : &test_stats->decoded_preflight_bytes,
                &eligible,
                &plan.spec);
            if (status != QUANTAPDF_OK)
                return status;
            if (eligible) {
                plan.image = image;
                plans.push_back(std::move(plan));
            }
        }
        if (pdf->anyWarnings())
            return QUANTAPDF_ERROR_FORMAT;

        if (test_fault ==
            quantapdf_qpdf_image_fault_before_provider_nomem)
            return QUANTAPDF_ERROR_NOMEM;

        for (quantapdf_qpdf_image_plan const& plan : plans) {
            quantapdf_qpdf_register_image_provider(
                plan,
                provider_status,
                provider_counts,
                pending_test_fault);
        }

        status = quantapdf_qpdf_write_memory(*pdf, out_data, out_size);
        if (status != QUANTAPDF_OK) {
            if (provider_status->status != QUANTAPDF_OK)
                return provider_status->status;
            return status;
        }
        if (provider_status->status != QUANTAPDF_OK) {
            std::free(*out_data);
            *out_data = nullptr;
            *out_size = 0;
            return provider_status->status;
        }
        if (test_fault ==
            quantapdf_qpdf_image_fault_before_publication) {
            std::free(*out_data);
            *out_data = nullptr;
            *out_size = 0;
            return QUANTAPDF_ERROR_BACKEND;
        }
        if (pdf->anyWarnings()) {
            std::free(*out_data);
            *out_data = nullptr;
            *out_size = 0;
            return QUANTAPDF_ERROR_FORMAT;
        }
        if (test_stats != nullptr) {
            test_stats->unique_images = provider_counts->size();
            test_stats->every_provider_once = 1;
            for (auto const& entry : *provider_counts) {
                test_stats->provider_registrations +=
                    entry.second.registrations;
                test_stats->provider_invocations += entry.second.invocations;
                if (entry.second.registrations != 1 ||
                    entry.second.invocations != 1)
                    test_stats->every_provider_once = 0;
            }
        }
        return QUANTAPDF_OK;
    } catch (QPDFExc const& error) {
        if (provider_status != nullptr &&
            provider_status->status != QUANTAPDF_OK)
            return provider_status->status;
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        if (provider_status != nullptr &&
            provider_status->status != QUANTAPDF_OK)
            return provider_status->status;
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        if (provider_status != nullptr &&
            provider_status->status != QUANTAPDF_OK)
            return provider_status->status;
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        if (provider_status != nullptr &&
            provider_status->status != QUANTAPDF_OK)
            return provider_status->status;
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_document_audit(
    quantapdf_qpdf_document *document,
    uint32_t *out_findings)
{
    if (out_findings == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_findings = 0;
    if (document == nullptr || document->pdf == nullptr ||
        document->source_data == nullptr || document->source_size == 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        auto pdf = QPDF::create();
        pdf->setSuppressWarnings(true);
        pdf->setAttemptRecovery(false);
        pdf->processMemoryFile(
            "quantapdf-document-audit",
            reinterpret_cast<char const *>(document->source_data),
            document->source_size,
            document->password);
        if (pdf->anyWarnings())
            return QUANTAPDF_ERROR_FORMAT;
        uint32_t findings = 0;
        quantapdf_status const status = quantapdf_qpdf_audit_document(
            *pdf, document->source_size, &findings);
        if (status != QUANTAPDF_OK)
            return status;
        if (pdf->anyWarnings())
            return QUANTAPDF_ERROR_FORMAT;
        *out_findings = findings;
        return QUANTAPDF_OK;
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_sanitize(
    quantapdf_qpdf_document *document,
    uint32_t flags,
    unsigned char **out_data,
    size_t *out_size)
{
    if (out_data == nullptr || out_size == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_data = nullptr;
    *out_size = 0;
    if (document == nullptr || document->pdf == nullptr ||
        document->source_data == nullptr || document->source_size == 0 ||
        flags == 0 || (flags & ~QUANTAPDF_SANITIZE_ALL) != 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        auto pdf = QPDF::create();
        pdf->setSuppressWarnings(true);
        pdf->setAttemptRecovery(false);
        pdf->processMemoryFile(
            "quantapdf-sanitize",
            reinterpret_cast<char const *>(document->source_data),
            document->source_size,
            document->password);
        if (pdf->anyWarnings())
            return QUANTAPDF_ERROR_FORMAT;

        uint32_t findings = 0;
        quantapdf_status status = quantapdf_qpdf_audit_document(
            *pdf, document->source_size, &findings);
        if (status != QUANTAPDF_OK)
            return status;
        if (pdf->anyWarnings())
            return QUANTAPDF_ERROR_FORMAT;
        quantapdf_validation_report report{};
        status = quantapdf_validate_security(findings,
            QUANTAPDF_AUDIT_SIGNATURE | QUANTAPDF_AUDIT_ENCRYPTION, &report);
        if (status != QUANTAPDF_OK)
            return status;

        status = quantapdf_qpdf_preflight_aliases(
            *pdf, document->source_size, flags);
        if (status != QUANTAPDF_OK)
            return status;

        status = quantapdf_qpdf_sanitize_graph(
            *pdf, document->source_size, flags);
        if (status != QUANTAPDF_OK)
            return status;
        if (pdf->anyWarnings())
            return QUANTAPDF_ERROR_FORMAT;

        findings = 0;
        status = quantapdf_qpdf_audit_document(
            *pdf, document->source_size, &findings);
        if (status != QUANTAPDF_OK)
            return status;
        if (pdf->anyWarnings())
            return QUANTAPDF_ERROR_FORMAT;
        status = quantapdf_validate_security(findings, flags, &report);
        if (status != QUANTAPDF_OK)
            return status;

        status = quantapdf_qpdf_write_memory(*pdf, out_data, out_size);
        if (status != QUANTAPDF_OK)
            return status;
        if (pdf->anyWarnings()) {
            std::free(*out_data);
            *out_data = nullptr;
            *out_size = 0;
            return QUANTAPDF_ERROR_FORMAT;
        }
        return QUANTAPDF_OK;
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

struct quantapdf_qpdf_flatten_item {
    QPDFObjectHandle annotation = QPDFObjectHandle::newNull();
    QPDFObjectHandle appearance = QPDFObjectHandle::newNull();
};

struct quantapdf_qpdf_flatten_page {
    QPDFObjectHandle page = QPDFObjectHandle::newNull();
    std::vector<QPDFObjectHandle> survivors;
    std::vector<quantapdf_qpdf_flatten_item> selected;
    int rotate = 0;
};

struct quantapdf_qpdf_flatten_field_change {
    QPDFObjectHandle field = QPDFObjectHandle::newNull();
    std::vector<QPDFObjectHandle> children;
};

struct quantapdf_qpdf_flatten_form_plan {
    QPDFObjectHandle acroform = QPDFObjectHandle::newNull();
    std::vector<QPDFObjectHandle> roots;
    std::vector<quantapdf_qpdf_flatten_field_change> changes;
    std::set<QPDFObjGen> all_fields;
    std::set<QPDFObjGen> removed_fields;
    std::set<QPDFObjGen> found_widgets;
    std::vector<QPDFObjectHandle> calculation_order;
    bool has_calculation_order = false;
};

static bool quantapdf_qpdf_flatten_supported_annotation(
    std::string const& subtype) noexcept
{
    static std::set<std::string> const supported = {
        "/Text", "/FreeText", "/Line", "/Square", "/Circle",
        "/Polygon", "/PolyLine", "/Highlight", "/Underline",
        "/Squiggly", "/StrikeOut", "/Stamp", "/Caret", "/Ink"};
    return supported.count(subtype) != 0u;
}

static quantapdf_status quantapdf_qpdf_flatten_number_array(
    QPDFObjectHandle const& object,
    int count,
    std::vector<double> *values)
{
    if (!object.isArray() || object.getArrayNItems() != count)
        return QUANTAPDF_ERROR_FORMAT;
    values->clear();
    values->reserve(static_cast<size_t>(count));
    for (int index = 0; index < count; ++index) {
        QPDFObjectHandle value = object.getArrayItem(index);
        if (!value.isNumber())
            return QUANTAPDF_ERROR_FORMAT;
        double const number = value.getNumericValue();
        if (!std::isfinite(number))
            return QUANTAPDF_ERROR_FORMAT;
        values->push_back(number);
    }
    return QUANTAPDF_OK;
}

class quantapdf_qpdf_flatten_content_callbacks final :
    public QPDFObjectHandle::ParserCallbacks
{
  public:
    void handleObject(QPDFObjectHandle object) override
    {
        if (!object.isOperator()) {
            if (object.isInlineImage()) {
                if (inline_image_stage_ != 2)
                    valid_ = false;
                inline_image_stage_ = 0;
                just_saw_inline_image_ = true;
                return;
            }
            if (operands_.size() >= 1024u)
                valid_ = false;
            else
                operands_.push_back(object);
            return;
        }
        std::string const op = object.getOperatorValue();
        static std::set<std::string> const known_operators = {
            "b", "b*", "B", "B*", "BDC", "BI", "BMC", "BT", "BX",
            "c", "cm", "CS", "cs", "d", "d0", "d1", "Do", "DP",
            "EI", "EMC", "ET", "EX", "f", "f*", "F", "G", "g",
            "gs", "h", "i", "ID", "j", "J", "K", "k", "l", "m",
            "M", "MP", "n", "q", "Q", "re", "RG", "rg", "ri",
            "s", "S", "SC", "SCN", "sc", "scn", "sh", "T*", "Tc",
            "Td", "TD", "Tf", "Tj", "TJ", "TL", "Tm", "Tr", "Ts",
            "Tw", "Tz", "v", "w", "W", "W*", "y", "'", "\""};
        if (known_operators.count(op) == 0u) {
            if (compatibility_depth_ == 0)
                valid_ = false;
            operands_.clear();
            return;
        }
        if (!valid_context(op) || !valid_operands(op))
            valid_ = false;
        operands_.clear();

        bool const inline_image_terminator = op == "EI";
        if (!inline_image_terminator)
            just_saw_inline_image_ = false;

        if (op == "BI")
            inline_image_stage_ = 1;
        else if (op == "ID")
            inline_image_stage_ = 2;
        else if (op == "EI")
            inline_image_stage_ = 0;
        if (inline_image_terminator)
            just_saw_inline_image_ = false;

        if (op == "q") {
            push_scope('q');
            ++graphics_depth_;
        } else if (op == "Q") {
            pop_scope('q');
            if (graphics_depth_ == 0)
                valid_ = false;
            else
                --graphics_depth_;
        } else if (op == "BT") {
            push_scope('t');
            if (text_depth_ != 0)
                valid_ = false;
            ++text_depth_;
        } else if (op == "ET") {
            pop_scope('t');
            if (text_depth_ == 0)
                valid_ = false;
            else
                --text_depth_;
        } else if (op == "BMC" || op == "BDC") {
            push_scope('m');
            ++marked_content_depth_;
        } else if (op == "EMC") {
            pop_scope('m');
            if (marked_content_depth_ == 0)
                valid_ = false;
            else
                --marked_content_depth_;
        } else if (op == "BX") {
            push_scope('x');
            ++compatibility_depth_;
        } else if (op == "EX") {
            pop_scope('x');
            if (compatibility_depth_ == 0)
                valid_ = false;
            else
                --compatibility_depth_;
        }

        if (op == "m" || op == "re") {
            path_open_ = true;
        } else if (op == "b" || op == "b*" || op == "B" ||
                   op == "B*" || op == "f" || op == "f*" ||
                   op == "F" || op == "n" || op == "s" || op == "S") {
            path_open_ = false;
        }
    }

    void handleEOF() override
    {
        if (graphics_depth_ != 0 || text_depth_ != 0 ||
            marked_content_depth_ != 0 || compatibility_depth_ != 0 ||
            inline_image_stage_ != 0 || path_open_ || !operands_.empty() ||
            !scope_stack_.empty())
            valid_ = false;
    }

    bool valid() const noexcept
    {
        return valid_;
    }

  private:
    void push_scope(char kind)
    {
        if (scope_stack_.size() >= 1024u)
            valid_ = false;
        else
            scope_stack_.push_back(kind);
    }

    void pop_scope(char kind)
    {
        if (scope_stack_.empty() || scope_stack_.back() != kind) {
            valid_ = false;
        } else {
            scope_stack_.pop_back();
        }
    }

    bool count(size_t expected) const noexcept
    {
        return operands_.size() == expected;
    }

    static bool finite_number(QPDFObjectHandle const& object)
    {
        return object.isNumber() &&
            std::isfinite(object.getNumericValue());
    }

    bool all_numbers(size_t expected) const
    {
        if (!count(expected))
            return false;
        for (auto const& operand : operands_) {
            if (!finite_number(operand))
                return false;
        }
        return true;
    }

    bool valid_context(std::string const& op) const
    {
        if (inline_image_stage_ == 1 && op != "ID")
            return false;
        if (inline_image_stage_ == 2 && op != "EI")
            return false;
        if (inline_image_stage_ == 0 && op == "ID")
            return false;
        if (op == "EI" &&
            (inline_image_stage_ != 0 || !just_saw_inline_image_))
            return false;
        if (inline_image_stage_ != 0 && op == "BI")
            return false;

        static std::set<std::string> const text_operators = {
            "'", "\"", "T*", "Tc", "Td", "TD", "Tf", "Tj", "TJ",
            "TL", "Tm", "Tr", "Ts", "Tw", "Tz"};
        if (text_operators.count(op) != 0u && text_depth_ == 0)
            return false;
        static std::set<std::string> const forbidden_in_text = {
            "b", "b*", "B", "B*", "BI", "c", "cm", "d0", "d1",
            "Do", "f", "f*", "F", "h", "l", "m", "n", "q", "Q",
            "re", "s", "S", "sh", "v", "W", "W*", "y"};
        if (text_depth_ != 0 && forbidden_in_text.count(op) != 0u)
            return false;
        if (op == "BT" && (text_depth_ != 0 || path_open_))
            return false;
        if (op == "ET" && text_depth_ == 0)
            return false;

        if ((op == "l" || op == "c" || op == "v" || op == "y" ||
             op == "h" || op == "W" || op == "W*") && !path_open_)
            return false;
        return true;
    }

    bool valid_operands(std::string const& op) const
    {
        static std::set<std::string> const zero = {
            "b", "b*", "B", "B*", "BI", "BT", "BX", "EMC",
            "ET", "EX", "f", "f*", "F", "h", "n", "q", "Q", "s",
            "S", "T*", "W", "W*"};
        if (zero.count(op) != 0u)
            return count(0u);

        static std::set<std::string> const one_name = {
            "BMC", "CS", "cs", "Do", "gs", "MP", "ri", "sh"};
        if (one_name.count(op) != 0u)
            return count(1u) && operands_[0].isName();

        static std::set<std::string> const one_number = {
            "G", "g", "i", "M", "Tc", "TL", "Ts", "Tw", "Tz", "w"};
        if (one_number.count(op) != 0u)
            return all_numbers(1u);

        if (op == "J" || op == "j" || op == "Tr")
            return count(1u) && operands_[0].isInteger();
        if (op == "Tj" || op == "'")
            return count(1u) && operands_[0].isString();
        if (op == "TJ") {
            if (!count(1u) || !operands_[0].isArray())
                return false;
            int const items = operands_[0].getArrayNItems();
            for (int index = 0; index < items; ++index) {
                QPDFObjectHandle item = operands_[0].getArrayItem(index);
                if (!item.isString() && !finite_number(item))
                    return false;
            }
            return true;
        }

        static std::set<std::string> const two_numbers = {
            "d0", "l", "m", "Td", "TD"};
        if (two_numbers.count(op) != 0u)
            return all_numbers(2u);
        static std::set<std::string> const three_numbers = {"RG", "rg"};
        if (three_numbers.count(op) != 0u)
            return all_numbers(3u);
        static std::set<std::string> const four_numbers = {
            "K", "k", "re", "v", "y"};
        if (four_numbers.count(op) != 0u)
            return all_numbers(4u);
        static std::set<std::string> const six_numbers = {
            "c", "cm", "d1", "Tm"};
        if (six_numbers.count(op) != 0u)
            return all_numbers(6u);

        if (op == "d") {
            if (!count(2u) || !operands_[0].isArray() ||
                !finite_number(operands_[1]))
                return false;
            int const items = operands_[0].getArrayNItems();
            for (int index = 0; index < items; ++index) {
                if (!finite_number(operands_[0].getArrayItem(index)))
                    return false;
            }
            return true;
        }
        if (op == "Tf")
            return count(2u) && operands_[0].isName() &&
                finite_number(operands_[1]);
        if (op == "\"")
            return count(3u) && finite_number(operands_[0]) &&
                finite_number(operands_[1]) && operands_[2].isString();
        if (op == "DP" || op == "BDC")
            return count(2u) && operands_[0].isName() &&
                (operands_[1].isName() || operands_[1].isDictionary());
        if (op == "SC" || op == "sc") {
            if (operands_.empty())
                return false;
            for (auto const& operand : operands_) {
                if (!finite_number(operand))
                    return false;
            }
            return true;
        }
        if (op == "SCN" || op == "scn") {
            if (operands_.empty())
                return false;
            size_t numbers = operands_.size();
            if (operands_.back().isName())
                --numbers;
            for (size_t index = 0; index < numbers; ++index) {
                if (!finite_number(operands_[index]))
                    return false;
            }
            return numbers == operands_.size() ||
                numbers + 1u == operands_.size();
        }
        if (op == "ID") {
            if (inline_image_stage_ != 1 || operands_.size() % 2u != 0u)
                return false;
            for (size_t index = 0; index < operands_.size(); index += 2u) {
                if (!operands_[index].isName())
                    return false;
            }
            return true;
        }
        if (op == "EI")
            return inline_image_stage_ == 0 && just_saw_inline_image_ &&
                count(0u);
        return false;
    }

    std::vector<QPDFObjectHandle> operands_;
    std::vector<char> scope_stack_;
    int graphics_depth_ = 0;
    int text_depth_ = 0;
    int marked_content_depth_ = 0;
    int compatibility_depth_ = 0;
    int inline_image_stage_ = 0;
    bool just_saw_inline_image_ = false;
    bool path_open_ = false;
    bool valid_ = true;
};

static quantapdf_status quantapdf_qpdf_flatten_parse_contents(
    QPDF& pdf,
    QPDFObjectHandle object,
    bool page_contents)
{
    quantapdf_qpdf_flatten_content_callbacks callbacks;
    if (page_contents)
        object.parsePageContents(&callbacks);
    else
        object.parseAsContents(&callbacks);
    if (pdf.anyWarnings() || !callbacks.valid())
        return QUANTAPDF_ERROR_FORMAT;
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_qpdf_flatten_appearance(
    QPDF& pdf,
    QPDFObjectHandle const& annotation,
    QPDFObjectHandle *out_appearance)
{
    QPDFObjectHandle flags = annotation.getKey("/F");
    unsigned long long flag_bits = 0;
    if (!flags.isNull()) {
        if (!flags.isInteger() || !flags.getValueAsUInt(flag_bits) ||
            flag_bits > UINT32_MAX)
            return QUANTAPDF_ERROR_FORMAT;
    }
    unsigned int const unsupported_flags =
        an_invisible | an_hidden | an_no_zoom | an_no_rotate |
        an_no_view | an_toggle_no_view;
    if ((static_cast<unsigned int>(flag_bits) & unsupported_flags) != 0u)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (!annotation.getKey("/OC").isNull())
        return QUANTAPDF_ERROR_UNSUPPORTED;
    QPDFObjectHandle opacity = annotation.getKey("/CA");
    if (!opacity.isNull()) {
        if (!opacity.isNumber())
            return QUANTAPDF_ERROR_FORMAT;
        double const value = opacity.getNumericValue();
        if (!std::isfinite(value) || value < 0.0 || value > 1.0)
            return QUANTAPDF_ERROR_FORMAT;
        if (value != 1.0)
            return QUANTAPDF_ERROR_UNSUPPORTED;
    }

    std::vector<double> rect;
    quantapdf_status status = quantapdf_qpdf_flatten_number_array(
        annotation.getKey("/Rect"), 4, &rect);
    if (status != QUANTAPDF_OK)
        return status;
    if (rect[0] == rect[2] || rect[1] == rect[3])
        return QUANTAPDF_ERROR_UNSUPPORTED;

    QPDFObjectHandle appearance_dictionary = annotation.getKey("/AP");
    if (appearance_dictionary.isNull())
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (!appearance_dictionary.isDictionary())
        return QUANTAPDF_ERROR_FORMAT;
    QPDFObjectHandle normal = appearance_dictionary.getKey("/N");
    if (normal.isNull())
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (normal.isDictionary()) {
        QPDFObjectHandle state = annotation.getKey("/AS");
        if (state.isNull())
            return QUANTAPDF_ERROR_UNSUPPORTED;
        if (!state.isName())
            return QUANTAPDF_ERROR_FORMAT;
        normal = normal.getKey(state.getName());
        if (normal.isNull() || !normal.isStream())
            return QUANTAPDF_ERROR_FORMAT;
    } else if (!normal.isStream()) {
        return QUANTAPDF_ERROR_FORMAT;
    }
    if (!normal.isIndirect())
        return QUANTAPDF_ERROR_FORMAT;

    QPDFObjectHandle dictionary = normal.getDict();
    QPDFObjectHandle subtype = dictionary.getKey("/Subtype");
    if (!subtype.isName() || subtype.getName() != "/Form")
        return QUANTAPDF_ERROR_FORMAT;
    std::vector<double> box;
    status = quantapdf_qpdf_flatten_number_array(
        dictionary.getKey("/BBox"), 4, &box);
    if (status != QUANTAPDF_OK)
        return status;
    if (box[0] >= box[2] || box[1] >= box[3])
        return QUANTAPDF_ERROR_UNSUPPORTED;
    QPDFObjectHandle matrix = dictionary.getKey("/Matrix");
    if (!matrix.isNull()) {
        std::vector<double> values;
        status = quantapdf_qpdf_flatten_number_array(matrix, 6, &values);
        if (status != QUANTAPDF_OK)
            return status;
        double const determinant = values[0] * values[3] -
            values[1] * values[2];
        if (determinant == 0.0)
            return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    QPDFObjectHandle resources = dictionary.getKey("/Resources");
    if (!resources.isNull() && !resources.isDictionary())
        return QUANTAPDF_ERROR_FORMAT;
    if (!dictionary.getKey("/OC").isNull())
        return QUANTAPDF_ERROR_UNSUPPORTED;

    status = quantapdf_qpdf_flatten_parse_contents(pdf, normal, false);
    if (status != QUANTAPDF_OK)
        return status;

    *out_appearance = normal;
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_qpdf_flatten_neutral_link(
    QPDFObjectHandle const& annotation)
{
    QPDFObjectHandle appearance = annotation.getKey("/AP");
    if (!appearance.isNull()) {
        if (!appearance.isDictionary())
            return QUANTAPDF_ERROR_FORMAT;
        if (!appearance.getKey("/N").isNull())
            return QUANTAPDF_ERROR_UNSUPPORTED;
    }

    QPDFObjectHandle border_style = annotation.getKey("/BS");
    QPDFObjectHandle width;
    if (!border_style.isNull()) {
        if (!border_style.isDictionary())
            return QUANTAPDF_ERROR_FORMAT;
        width = border_style.getKey("/W");
        if (width.isNull())
            return QUANTAPDF_ERROR_UNSUPPORTED;
    } else {
        QPDFObjectHandle border = annotation.getKey("/Border");
        if (border.isNull())
            return QUANTAPDF_ERROR_UNSUPPORTED;
        if (!border.isArray() || border.getArrayNItems() < 3 ||
            border.getArrayNItems() > 4)
            return QUANTAPDF_ERROR_FORMAT;
        for (int index = 0; index < 3; ++index) {
            QPDFObjectHandle value = border.getArrayItem(index);
            if (!value.isNumber() || !std::isfinite(value.getNumericValue()))
                return QUANTAPDF_ERROR_FORMAT;
        }
        if (border.getArrayNItems() == 4) {
            QPDFObjectHandle dash = border.getArrayItem(3);
            if (!dash.isArray())
                return QUANTAPDF_ERROR_FORMAT;
            int const dash_count = dash.getArrayNItems();
            for (int index = 0; index < dash_count; ++index) {
                QPDFObjectHandle value = dash.getArrayItem(index);
                if (!value.isNumber() ||
                    !std::isfinite(value.getNumericValue()))
                    return QUANTAPDF_ERROR_FORMAT;
            }
        }
        width = border.getArrayItem(2);
    }
    if (!width.isNumber() || !std::isfinite(width.getNumericValue()))
        return QUANTAPDF_ERROR_FORMAT;
    return width.getNumericValue() == 0.0 ?
        QUANTAPDF_OK : QUANTAPDF_ERROR_UNSUPPORTED;
}

static bool quantapdf_qpdf_flatten_points_to_selected(
    QPDFObjectHandle const& object,
    std::set<QPDFObjGen> const& selected)
{
    return object.isIndirect() && selected.count(object.getObjGen()) != 0u;
}

static quantapdf_status quantapdf_qpdf_flatten_field(
    QPDFObjectHandle field,
    QPDFObjectHandle expected_parent,
    std::set<QPDFObjGen> const& selected_widgets,
    quantapdf_qpdf_flatten_form_plan *plan,
    bool *out_remove,
    size_t depth)
{
    if (depth > 256u || !field.isDictionary() || !field.isIndirect())
        return QUANTAPDF_ERROR_FORMAT;
    QPDFObjGen const identity = field.getObjGen();
    if (!plan->all_fields.insert(identity).second)
        return QUANTAPDF_ERROR_FORMAT;
    QPDFObjectHandle parent = field.getKey("/Parent");
    if ((expected_parent.isNull() && !parent.isNull()) ||
        (!expected_parent.isNull() &&
         !quantapdf_qpdf_same_object(parent, expected_parent)))
        return QUANTAPDF_ERROR_FORMAT;

    QPDFObjectHandle subtype = field.getKey("/Subtype");
    if (!subtype.isNull() && !subtype.isName())
        return QUANTAPDF_ERROR_FORMAT;
    bool const merged_widget =
        subtype.isName() && subtype.getName() == "/Widget";
    if (merged_widget) {
        if (selected_widgets.count(identity) == 0u)
            return QUANTAPDF_ERROR_FORMAT;
        if (!plan->found_widgets.insert(identity).second)
            return QUANTAPDF_ERROR_FORMAT;
    }

    QPDFObjectHandle kids = field.getKey("/Kids");
    if (!kids.isNull() && !kids.isArray())
        return QUANTAPDF_ERROR_FORMAT;
    bool changed = false;
    std::vector<QPDFObjectHandle> survivors;
    if (kids.isArray()) {
        int const count = kids.getArrayNItems();
        survivors.reserve(static_cast<size_t>(count));
        for (int index = 0; index < count; ++index) {
            QPDFObjectHandle child = kids.getArrayItem(index);
            bool remove_child = false;
            quantapdf_status const status = quantapdf_qpdf_flatten_field(
                child, field, selected_widgets, plan, &remove_child,
                depth + 1u);
            if (status != QUANTAPDF_OK)
                return status;
            if (remove_child) {
                changed = true;
            } else {
                survivors.push_back(child);
            }
        }
    }

    bool const remove = merged_widget ||
        (changed && survivors.empty());
    if (remove)
        plan->removed_fields.insert(identity);
    if (changed)
        plan->changes.push_back({field, survivors});
    *out_remove = remove;
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_qpdf_flatten_plan_form(
    QPDF& pdf,
    std::set<QPDFObjGen> const& selected_widgets,
    quantapdf_qpdf_flatten_form_plan *plan)
{
    QPDFObjectHandle root = pdf.getRoot();
    plan->acroform = root.getKey("/AcroForm");
    if (plan->acroform.isNull())
        return selected_widgets.empty() ?
            QUANTAPDF_OK : QUANTAPDF_ERROR_FORMAT;
    if (!plan->acroform.isDictionary())
        return QUANTAPDF_ERROR_FORMAT;
    if (!selected_widgets.empty() &&
        !plan->acroform.getKey("/XFA").isNull())
        return QUANTAPDF_ERROR_UNSUPPORTED;
    QPDFObjectHandle need_appearances =
        plan->acroform.getKey("/NeedAppearances");
    if (!need_appearances.isNull()) {
        if (!need_appearances.isBool())
            return QUANTAPDF_ERROR_FORMAT;
        if (!selected_widgets.empty() && need_appearances.getBoolValue())
            return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    QPDFObjectHandle fields = plan->acroform.getKey("/Fields");
    if (fields.isNull())
        return selected_widgets.empty() ?
            QUANTAPDF_OK : QUANTAPDF_ERROR_FORMAT;
    if (!fields.isArray())
        return QUANTAPDF_ERROR_FORMAT;
    int const count = fields.getArrayNItems();
    plan->roots.reserve(static_cast<size_t>(count));
    for (int index = 0; index < count; ++index) {
        QPDFObjectHandle field = fields.getArrayItem(index);
        bool remove = false;
        quantapdf_status const status = quantapdf_qpdf_flatten_field(
            field, QPDFObjectHandle::newNull(), selected_widgets,
            plan, &remove, 1u);
        if (status != QUANTAPDF_OK)
            return status;
        if (!remove)
            plan->roots.push_back(field);
    }
    if (plan->found_widgets != selected_widgets)
        return QUANTAPDF_ERROR_FORMAT;

    QPDFObjectHandle order = plan->acroform.getKey("/CO");
    if (!order.isNull()) {
        if (!order.isArray())
            return QUANTAPDF_ERROR_FORMAT;
        plan->has_calculation_order = true;
        int const order_count = order.getArrayNItems();
        for (int index = 0; index < order_count; ++index) {
            QPDFObjectHandle field = order.getArrayItem(index);
            if (!field.isIndirect() ||
                plan->all_fields.count(field.getObjGen()) == 0u)
                return QUANTAPDF_ERROR_FORMAT;
            if (plan->removed_fields.count(field.getObjGen()) == 0u)
                plan->calculation_order.push_back(field);
        }
    }
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_qpdf_flatten_validate_page_contents(
    QPDF& pdf,
    QPDFObjectHandle const& page)
{
    QPDFObjectHandle contents = page.getKey("/Contents");
    if (!contents.isNull()) {
        if (!contents.isStream() && !contents.isArray())
            return QUANTAPDF_ERROR_FORMAT;
        if (contents.isArray()) {
            int const count = contents.getArrayNItems();
            for (int index = 0; index < count; ++index) {
                if (!contents.getArrayItem(index).isStream())
                    return QUANTAPDF_ERROR_FORMAT;
            }
        }
    }
    return quantapdf_qpdf_flatten_parse_contents(pdf, page, true);
}

static quantapdf_status quantapdf_qpdf_flatten_validate_raw_page_tree(
    QPDFObjectHandle node,
    std::set<QPDFObjGen> *seen,
    size_t depth)
{
    if (depth > 256u || !node.isDictionary() || !node.isIndirect() ||
        !seen->insert(node.getObjGen()).second)
        return QUANTAPDF_ERROR_FORMAT;
    QPDFObjectHandle type = node.getKey("/Type");
    if (!type.isName())
        return QUANTAPDF_ERROR_FORMAT;
    QPDFObjectHandle resources = node.getKey("/Resources");
    if (!resources.isNull() && !resources.isDictionary())
        return QUANTAPDF_ERROR_FORMAT;
    if (type.getName() == "/Page") {
        QPDFObjectHandle annotations = node.getKey("/Annots");
        return annotations.isNull() || annotations.isArray() ?
            QUANTAPDF_OK : QUANTAPDF_ERROR_FORMAT;
    }
    if (type.getName() != "/Pages")
        return QUANTAPDF_ERROR_FORMAT;
    QPDFObjectHandle kids = node.getKey("/Kids");
    if (!kids.isArray())
        return QUANTAPDF_ERROR_FORMAT;
    int const count = kids.getArrayNItems();
    for (int index = 0; index < count; ++index) {
        quantapdf_status const status =
            quantapdf_qpdf_flatten_validate_raw_page_tree(
                kids.getArrayItem(index), seen, depth + 1u);
        if (status != QUANTAPDF_OK)
            return status;
    }
    return QUANTAPDF_OK;
}

static bool quantapdf_qpdf_flatten_has_signed_field(
    QPDFObjectHandle field,
    std::string inherited_type,
    QPDFObjectHandle inherited_value,
    std::set<QPDFObjGen> *seen,
    size_t depth)
{
    if (depth > 256u || !field.isDictionary())
        return false;
    if (field.isIndirect() && !seen->insert(field.getObjGen()).second)
        return false;
    QPDFObjectHandle type = field.getKey("/FT");
    if (type.isName())
        inherited_type = type.getName();
    QPDFObjectHandle value = field.getKey("/V");
    if (!value.isNull())
        inherited_value = value;
    if (inherited_type == "/Sig" && !inherited_value.isNull())
        return true;
    QPDFObjectHandle kids = field.getKey("/Kids");
    if (!kids.isArray())
        return false;
    int const count = kids.getArrayNItems();
    for (int index = 0; index < count; ++index) {
        if (quantapdf_qpdf_flatten_has_signed_field(
                kids.getArrayItem(index), inherited_type, inherited_value,
                seen, depth + 1u))
            return true;
    }
    return false;
}

static bool quantapdf_qpdf_flatten_is_signed(QPDF& pdf)
{
    QPDFObjectHandle root = pdf.getRoot();
    QPDFObjectHandle acroform = root.getKey("/AcroForm");
    if (!acroform.isDictionary())
        return false;
    QPDFObjectHandle fields = acroform.getKey("/Fields");
    if (!fields.isArray())
        return false;
    std::set<QPDFObjGen> seen;
    int const count = fields.getArrayNItems();
    for (int index = 0; index < count; ++index) {
        if (quantapdf_qpdf_flatten_has_signed_field(
                fields.getArrayItem(index), std::string(),
                QPDFObjectHandle::newNull(), &seen, 1u))
            return true;
    }
    return false;
}

static quantapdf_status quantapdf_qpdf_flatten_security(QPDF& pdf)
{
    if (pdf.isEncrypted())
        return QUANTAPDF_ERROR_UNSUPPORTED;
    QPDFObjectHandle root = pdf.getRoot();
    if (!root.isDictionary())
        return QUANTAPDF_ERROR_FORMAT;
    QPDFObjectHandle permissions = root.getKey("/Perms");
    if (!permissions.isNull()) {
        if (!permissions.isDictionary())
            return QUANTAPDF_ERROR_FORMAT;
        for (char const *key : {"/DocMDP", "/UR", "/UR3"}) {
            QPDFObjectHandle signature = permissions.getKey(key);
            if (signature.isNull())
                continue;
            if (!signature.isDictionary())
                return QUANTAPDF_ERROR_FORMAT;
            QPDFObjectHandle type = signature.getKey("/Type");
            if (!type.isNull() &&
                (!type.isName() || type.getName() != "/Sig"))
                return QUANTAPDF_ERROR_FORMAT;
            return QUANTAPDF_ERROR_UNSUPPORTED;
        }
    }
    return quantapdf_qpdf_flatten_is_signed(pdf) ?
        QUANTAPDF_ERROR_UNSUPPORTED : QUANTAPDF_OK;
}

static bool quantapdf_qpdf_flatten_initial_warnings_are_safe(QPDF& pdf)
{
    auto const warnings = pdf.getWarnings();
    for (QPDFExc const& warning : warnings) {
        std::string const message = warning.what();
        if (message.find("Resources is missing or invalid; repairing") ==
            std::string::npos)
            return false;
    }
    return true;
}

extern "C" quantapdf_status quantapdf_qpdf_flatten_interactive(
    quantapdf_qpdf_document *document,
    uint32_t flags,
    unsigned char **out_data,
    size_t *out_size)
{
    uint32_t const known =
        QUANTAPDF_FLATTEN_ANNOTATIONS | QUANTAPDF_FLATTEN_WIDGETS;
    if (out_data == nullptr || out_size == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_data = nullptr;
    *out_size = 0;
    if (document == nullptr || document->pdf == nullptr ||
        document->source_data == nullptr || document->source_size == 0 ||
        flags == 0 || (flags & ~known) != 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        auto pdf = QPDF::create();
        pdf->setSuppressWarnings(true);
        pdf->setAttemptRecovery(false);
        pdf->processMemoryFile(
            "quantapdf-flatten",
            reinterpret_cast<char const *>(document->source_data),
            document->source_size,
            document->password);
        quantapdf_status const security_status =
            quantapdf_qpdf_flatten_security(*pdf);
        if (security_status != QUANTAPDF_OK)
            return security_status;
        std::set<QPDFObjGen> raw_page_nodes;
        quantapdf_status const raw_page_status =
            quantapdf_qpdf_flatten_validate_raw_page_tree(
                pdf->getRoot().getKey("/Pages"),
                &raw_page_nodes,
                1u);
        if (raw_page_status != QUANTAPDF_OK)
            return raw_page_status;
        auto const& pages = pdf->getAllPages();
        (void)pdf->getAllObjects();
        if (!quantapdf_qpdf_flatten_initial_warnings_are_safe(*pdf))
            return QUANTAPDF_ERROR_FORMAT;

        std::vector<quantapdf_qpdf_flatten_page> plans;
        std::vector<QPDFObjectHandle> all_annotations;
        std::set<QPDFObjGen> annotation_identities;
        std::set<QPDFObjGen> selected_identities;
        std::set<QPDFObjGen> selected_widgets;
        bool const want_annotations =
            (flags & QUANTAPDF_FLATTEN_ANNOTATIONS) != 0u;
        bool const want_widgets =
            (flags & QUANTAPDF_FLATTEN_WIDGETS) != 0u;

        for (QPDFObjectHandle const& page : pages) {
            quantapdf_qpdf_flatten_page plan;
            plan.page = page;
            QPDFObjectHandle annotations = page.getKey("/Annots");
            if (annotations.isNull())
                continue;
            if (!annotations.isArray())
                return QUANTAPDF_ERROR_FORMAT;
            int const count = annotations.getArrayNItems();
            for (int index = 0; index < count; ++index) {
                QPDFObjectHandle annotation = annotations.getArrayItem(index);
                if (!annotation.isDictionary() || !annotation.isIndirect())
                    return QUANTAPDF_ERROR_FORMAT;
                if (!annotation_identities.insert(
                        annotation.getObjGen()).second)
                    return QUANTAPDF_ERROR_FORMAT;
                all_annotations.push_back(annotation);
                QPDFObjectHandle subtype_object = annotation.getKey("/Subtype");
                if (!subtype_object.isName())
                    return QUANTAPDF_ERROR_FORMAT;
                std::string const subtype = subtype_object.getName();
                bool const widget = subtype == "/Widget";
                bool const structural =
                    subtype == "/Link" || subtype == "/Popup";
                bool selected = widget ? want_widgets :
                    (want_annotations && !structural);
                if (want_annotations && !widget && !structural &&
                    !quantapdf_qpdf_flatten_supported_annotation(subtype))
                    return QUANTAPDF_ERROR_UNSUPPORTED;
                if (selected) {
                    if (widget) {
                        QPDFObjectHandle owner = annotation.getKey("/P");
                        if (!owner.isNull() &&
                            !quantapdf_qpdf_same_object(owner, page))
                            return QUANTAPDF_ERROR_FORMAT;
                    }
                    QPDFObjectHandle appearance;
                    quantapdf_status const status =
                        quantapdf_qpdf_flatten_appearance(
                            *pdf, annotation, &appearance);
                    if (status != QUANTAPDF_OK)
                        return status;
                    selected_identities.insert(annotation.getObjGen());
                    if (widget)
                        selected_widgets.insert(annotation.getObjGen());
                    plan.selected.push_back({annotation, appearance});
                } else {
                    plan.survivors.push_back(annotation);
                }
            }
            if (plan.selected.empty())
                continue;

            for (int index = 0; index < count; ++index) {
                QPDFObjectHandle annotation = annotations.getArrayItem(index);
                QPDFObjectHandle subtype_object = annotation.getKey("/Subtype");
                if (!subtype_object.isName())
                    return QUANTAPDF_ERROR_FORMAT;
                std::string const subtype = subtype_object.getName();
                if (subtype == "/Link") {
                    quantapdf_status const status =
                        quantapdf_qpdf_flatten_neutral_link(annotation);
                    if (status != QUANTAPDF_OK)
                        return status;
                } else if ((subtype == "/Widget" && !want_widgets) ||
                           (subtype != "/Widget" && subtype != "/Popup" &&
                            !want_annotations)) {
                    return QUANTAPDF_ERROR_UNSUPPORTED;
                }
            }
            quantapdf_status const contents_status =
                quantapdf_qpdf_flatten_validate_page_contents(*pdf, page);
            if (contents_status != QUANTAPDF_OK)
                return contents_status;
            QPDFPageObjectHelper page_helper(page);
            QPDFObjectHandle resources =
                page_helper.getAttribute("/Resources", false);
            if (!resources.isNull() && !resources.isDictionary())
                return QUANTAPDF_ERROR_FORMAT;
            QPDFObjectHandle rotate = page_helper.getAttribute("/Rotate", false);
            if (!rotate.isNull()) {
                if (!rotate.isInteger())
                    return QUANTAPDF_ERROR_FORMAT;
                long long const value = rotate.getIntValue();
                if (value < INT_MIN || value > INT_MAX || value % 90 != 0)
                    return QUANTAPDF_ERROR_FORMAT;
                plan.rotate = static_cast<int>(value);
            }
            plans.push_back(std::move(plan));
        }

        if (!plans.empty()) {
            if (!pdf->getRoot().getKey("/StructTreeRoot").isNull())
                return QUANTAPDF_ERROR_UNSUPPORTED;
        }

        for (QPDFObjectHandle const& annotation : all_annotations) {
            for (char const *key : {"/Popup", "/IRT"}) {
                QPDFObjectHandle target = annotation.getKey(key);
                if ((selected_identities.count(annotation.getObjGen()) != 0u &&
                     !target.isNull()) ||
                    quantapdf_qpdf_flatten_points_to_selected(
                        target, selected_identities))
                    return QUANTAPDF_ERROR_UNSUPPORTED;
            }
            QPDFObjectHandle subtype = annotation.getKey("/Subtype");
            if (subtype.isName() && subtype.getName() == "/Popup" &&
                quantapdf_qpdf_flatten_points_to_selected(
                    annotation.getKey("/Parent"), selected_identities))
                return QUANTAPDF_ERROR_UNSUPPORTED;
        }

        quantapdf_qpdf_flatten_form_plan form_plan;
        if (want_widgets) {
            quantapdf_status const form_status =
                quantapdf_qpdf_flatten_plan_form(
                    *pdf, selected_widgets, &form_plan);
            if (form_status != QUANTAPDF_OK)
                return form_status;
        }

        for (auto& plan : plans) {
            QPDFPageObjectHelper page_helper(plan.page);
            QPDFObjectHandle resources =
                page_helper.getAttribute("/Resources", true);
            if (resources.isNull()) {
                resources = QPDFObjectHandle::newDictionary();
                plan.page.replaceKey("/Resources", resources);
            }
            QPDFObjectHandle xobjects = resources.getKey("/XObject");
            if (xobjects.isNull()) {
                xobjects = QPDFObjectHandle::newDictionary();
                resources.replaceKey("/XObject", xobjects);
            } else {
                if (!xobjects.isDictionary())
                    return QUANTAPDF_ERROR_FORMAT;
                xobjects = xobjects.shallowCopy();
                resources.replaceKey("/XObject", xobjects);
            }

            std::map<QPDFObjGen, std::string> aliases;
            int next_alias = 0;
            std::string content;
            for (auto const& item : plan.selected) {
                QPDFObjGen const identity = item.appearance.getObjGen();
                auto alias = aliases.find(identity);
                if (alias == aliases.end()) {
                    std::string const name =
                        resources.getUniqueResourceName("/EPB", next_alias);
                    xobjects.replaceKey(name, item.appearance);
                    alias = aliases.emplace(identity, name).first;
                    ++next_alias;
                }
                QPDFAnnotationObjectHelper annotation(item.annotation);
                std::string const placement =
                    annotation.getPageContentForAppearance(
                        alias->second, plan.rotate, 0, 0);
                if (placement.empty())
                    return QUANTAPDF_ERROR_UNSUPPORTED;
                content += placement;
            }
            if (plan.survivors.empty()) {
                plan.page.removeKey("/Annots");
            } else {
                plan.page.replaceKey(
                    "/Annots", QPDFObjectHandle::newArray(plan.survivors));
            }
            page_helper.addPageContents(pdf->newStream("q\n"), true);
            page_helper.addPageContents(
                pdf->newStream("\nQ\n" + content), false);
        }

        for (auto& change : form_plan.changes) {
            if (change.children.empty()) {
                change.field.removeKey("/Kids");
            } else {
                change.field.replaceKey(
                    "/Kids", QPDFObjectHandle::newArray(change.children));
            }
        }
        if (!selected_widgets.empty()) {
            if (form_plan.roots.empty()) {
                pdf->getRoot().removeKey("/AcroForm");
            } else {
                form_plan.acroform.replaceKey(
                    "/Fields", QPDFObjectHandle::newArray(form_plan.roots));
                if (form_plan.has_calculation_order) {
                    form_plan.acroform.replaceKey(
                        "/CO",
                        QPDFObjectHandle::newArray(
                            form_plan.calculation_order));
                }
            }
        }

        quantapdf_status const status = quantapdf_qpdf_write_memory(
            *pdf, out_data, out_size);
        if (status != QUANTAPDF_OK)
            return status;
        if (pdf->anyWarnings()) {
            std::free(*out_data);
            *out_data = nullptr;
            *out_size = 0;
            return QUANTAPDF_ERROR_FORMAT;
        }
        return QUANTAPDF_OK;
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

static char const *quantapdf_qpdf_annotation_name(
    quantapdf_annotation_type type) noexcept
{
    switch (type) {
    case QUANTAPDF_ANNOTATION_TEXT: return "/Text";
    case QUANTAPDF_ANNOTATION_FREE_TEXT: return "/FreeText";
    case QUANTAPDF_ANNOTATION_LINE: return "/Line";
    case QUANTAPDF_ANNOTATION_SQUARE: return "/Square";
    case QUANTAPDF_ANNOTATION_CIRCLE: return "/Circle";
    case QUANTAPDF_ANNOTATION_POLYGON: return "/Polygon";
    case QUANTAPDF_ANNOTATION_POLY_LINE: return "/PolyLine";
    case QUANTAPDF_ANNOTATION_HIGHLIGHT: return "/Highlight";
    case QUANTAPDF_ANNOTATION_UNDERLINE: return "/Underline";
    case QUANTAPDF_ANNOTATION_SQUIGGLY: return "/Squiggly";
    case QUANTAPDF_ANNOTATION_STRIKE_OUT: return "/StrikeOut";
    case QUANTAPDF_ANNOTATION_REDACT: return "/Redact";
    case QUANTAPDF_ANNOTATION_STAMP: return "/Stamp";
    case QUANTAPDF_ANNOTATION_CARET: return "/Caret";
    case QUANTAPDF_ANNOTATION_INK: return "/Ink";
    case QUANTAPDF_ANNOTATION_FILE_ATTACHMENT: return "/FileAttachment";
    case QUANTAPDF_ANNOTATION_SOUND: return "/Sound";
    case QUANTAPDF_ANNOTATION_MOVIE: return "/Movie";
    case QUANTAPDF_ANNOTATION_RICH_MEDIA: return "/RichMedia";
    case QUANTAPDF_ANNOTATION_SCREEN: return "/Screen";
    case QUANTAPDF_ANNOTATION_PRINTER_MARK: return "/PrinterMark";
    case QUANTAPDF_ANNOTATION_TRAP_NET: return "/TrapNet";
    case QUANTAPDF_ANNOTATION_WATERMARK: return "/Watermark";
    case QUANTAPDF_ANNOTATION_3D: return "/3D";
    case QUANTAPDF_ANNOTATION_PROJECTION: return "/Projection";
    case QUANTAPDF_ANNOTATION_UNKNOWN:
    default: return nullptr;
    }
}

static bool quantapdf_qpdf_valid_utf8(char const *data, size_t size)
{
    if (data == nullptr)
        return size == 0u;
    std::string const text(data, size);
    size_t position = 0;
    while (position < text.size()) {
        bool error = false;
        unsigned long const codepoint =
            QUtil::get_next_utf8_codepoint(text, position, error);
        if (error || codepoint == 0u)
            return false;
    }
    return true;
}

static bool quantapdf_qpdf_edit_annotation_visible(
    QPDFObjectHandle const& object,
    quantapdf_annotation_type *out_type)
{
    *out_type = QUANTAPDF_ANNOTATION_UNKNOWN;
    QPDFObjectHandle subtype = object.getKey("/Subtype");
    if (subtype.isNull())
        return true;
    if (!subtype.isName())
        return true;
    std::string const name = subtype.getName();
    if (name == "/Link" || name == "/Popup" || name == "/Widget")
        return false;
    *out_type = quantapdf_qpdf_annotation_type(name);
    return true;
}

static quantapdf_status quantapdf_qpdf_edit_annotation_view(
    QPDF& pdf,
    int page_index,
    QPDFObjectHandle const& object,
    quantapdf_annotation_info *out_info,
    std::string *out_contents,
    bool *out_has_contents)
{
    quantapdf_annotation_type type;
    if (!object.isDictionary() ||
        !quantapdf_qpdf_edit_annotation_visible(object, &type))
        return QUANTAPDF_ERROR_FORMAT;
    quantapdf_qpdf_page_geometry geometry = {};
    quantapdf_status status = quantapdf_qpdf_load_page_geometry(
        pdf, page_index, &geometry);
    if (status != QUANTAPDF_OK)
        return status;
    double raw[4];
    status = quantapdf_qpdf_read_rectangle(object.getKey("/Rect"), raw);
    if (status != QUANTAPDF_OK)
        return status;
    QPDFObjectHandle flags = object.getKey("/F");
    uint32_t flag_value = 0;
    if (!flags.isNull()) {
        if (!flags.isInteger())
            return QUANTAPDF_ERROR_FORMAT;
        long long const value = flags.getIntValue();
        if (value < 0 || static_cast<unsigned long long>(value) > UINT32_MAX)
            return QUANTAPDF_ERROR_FORMAT;
        flag_value = static_cast<uint32_t>(value);
    }
    if (out_info != nullptr) {
        out_info->type = type;
        out_info->bounds = quantapdf_qpdf_pdf_rectangle_to_public(
            geometry, raw);
        out_info->flags = flag_value;
    }
    QPDFObjectHandle contents = object.getKey("/Contents");
    if (out_has_contents != nullptr)
        *out_has_contents = !contents.isNull();
    if (!contents.isNull()) {
        if (!contents.isString())
            return QUANTAPDF_ERROR_FORMAT;
        if (out_contents != nullptr)
            *out_contents = contents.getUTF8Value();
    } else if (out_contents != nullptr) {
        out_contents->clear();
    }
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_qpdf_edit_scan_annotations(
    quantapdf_qpdf_edit *edit,
    int page_index,
    std::vector<QPDFObjectHandle> *out_objects)
{
    if (edit == nullptr || edit->pdf == nullptr || page_index < 0)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (edit->rollback_failed)
        return QUANTAPDF_ERROR_STATE;
    auto const& pages = edit->pdf->getAllPages();
    if (static_cast<size_t>(page_index) >= pages.size())
        return QUANTAPDF_ERROR_ARGUMENT;
    QPDFObjectHandle annots = pages[static_cast<size_t>(page_index)]
        .getKey("/Annots");
    if (!annots.isArray())
        return QUANTAPDF_OK;
    int const count = annots.getArrayNItems();
    for (int index = 0; index < count; ++index) {
        QPDFObjectHandle object = annots.getArrayItem(index);
        if (!object.isDictionary())
            continue;
        quantapdf_annotation_type type;
        if (!quantapdf_qpdf_edit_annotation_visible(object, &type))
            continue;
        quantapdf_status status = quantapdf_qpdf_edit_annotation_view(
            *edit->pdf, page_index, object, nullptr, nullptr, nullptr);
        if (status != QUANTAPDF_OK)
            return status;
        out_objects->push_back(object);
    }
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_qpdf_edit_make_annotation_ref(
    quantapdf_qpdf_edit const& edit,
    size_t slot,
    quantapdf_annotation_ref *out_ref) noexcept
{
    quantapdf_local_id id = 0;
    quantapdf_status status = quantapdf_local_id_from_index(slot, &id);
    if (status != QUANTAPDF_OK)
        return status;
    return quantapdf_reference_make(edit.annotation_scope, id, out_ref->opaque);
}

static quantapdf_status quantapdf_qpdf_edit_resolve_annotation(
    quantapdf_qpdf_edit *edit,
    quantapdf_annotation_ref const *ref,
    quantapdf_qpdf_edit_annotation_entry **out_entry)
{
    if (edit == nullptr || ref == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (edit->rollback_failed)
        return QUANTAPDF_ERROR_STATE;
    size_t slot = SIZE_MAX;
    quantapdf_status status = quantapdf_reference_resolve(
        edit->annotation_scope, edit->annotations.size(), ref->opaque, &slot);
    if (status != QUANTAPDF_OK)
        return status;
    if (!edit->annotations[slot].live)
        return QUANTAPDF_ERROR_STATE;
    *out_entry = &edit->annotations[slot];
    return QUANTAPDF_OK;
}

extern "C" quantapdf_status quantapdf_qpdf_edit_begin(
    quantapdf_qpdf_document *source,
    quantapdf_qpdf_edit **out_edit)
{
    if (out_edit == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_edit = nullptr;
    if (source == nullptr || source->pdf == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    try {
        auto edit = std::make_unique<quantapdf_qpdf_edit>();
        edit->source_bytes.assign(
            source->source_data, source->source_data + source->source_size);
        edit->pdf = QPDF::create();
        edit->pdf->processMemoryFile(
            "quantapdf-edit",
            reinterpret_cast<char const *>(edit->source_bytes.data()),
            edit->source_bytes.size(), source->password);
        if (quantapdf_qpdf_rewrite_forbidden(*edit->pdf))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        quantapdf_status status = quantapdf_reference_scope_create(&edit->annotation_scope);
        if (status == QUANTAPDF_OK)
            status = quantapdf_reference_scope_create(&edit->form_scope);
        if (status != QUANTAPDF_OK)
            return status;
        *out_edit = edit.release();
        return QUANTAPDF_OK;
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_edit_snapshot(
    quantapdf_qpdf_edit *edit,
    int *test_fault,
    unsigned char **out_data,
    size_t *out_size)
{
    if (out_data == nullptr || out_size == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_data = nullptr;
    *out_size = 0;
    if (edit == nullptr || edit->pdf == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (edit->rollback_failed)
        return QUANTAPDF_ERROR_STATE;
    if (test_fault != nullptr &&
        *test_fault == QUANTAPDF_TEST_PDF_EDIT_FAULT_SNAPSHOT_BEFORE_PUBLISH) {
        *test_fault = 0;
        return QUANTAPDF_ERROR_BACKEND;
    }
    try {
        return quantapdf_qpdf_write_memory(*edit->pdf, out_data, out_size);
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_edit_form_snapshot(
    quantapdf_qpdf_edit *edit,
    quantapdf_pdf_form_model **out_model)
{
    if (out_model == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_model = nullptr;
    if (edit == nullptr || edit->pdf == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (edit->rollback_failed)
        return QUANTAPDF_ERROR_STATE;
    quantapdf_qpdf_document view = {};
    view.pdf = edit->pdf;
    return quantapdf_qpdf_extract_form(&view, out_model);
}

struct quantapdf_qpdf_edit_live_form_field {
    QPDFObjectHandle head = QPDFObjectHandle::newNull();
    std::vector<QPDFObjectHandle> nodes;
    std::vector<QPDFObjectHandle> widgets;
};

static quantapdf_status quantapdf_qpdf_edit_collect_live_form_fields(
    QPDF& pdf,
    std::vector<quantapdf_qpdf_edit_live_form_field> *out_fields)
{
    QPDFObjectHandle acroform = pdf.getRoot().getKey("/AcroForm");
    if (!acroform.isDictionary())
        return acroform.isNull() ? QUANTAPDF_OK : QUANTAPDF_ERROR_FORMAT;
    QPDFObjectHandle roots = acroform.getKey("/Fields");
    if (roots.isNull())
        return QUANTAPDF_OK;
    if (!roots.isArray())
        return QUANTAPDF_ERROR_FORMAT;

    struct group {
        QPDFObjectHandle head = QPDFObjectHandle::newNull();
        bool has_named_child = false;
        std::vector<QPDFObjectHandle> nodes;
        std::vector<QPDFObjectHandle> widgets;
    };
    std::vector<group> groups;
    std::set<QPDFObjGen> seen;
    quantapdf_status status = QUANTAPDF_OK;
    std::function<void(QPDFObjectHandle, size_t, size_t)> visit;
    visit = [&](QPDFObjectHandle object, size_t inherited_group, size_t depth) {
        if (status != QUANTAPDF_OK)
            return;
        if (depth > 256u) {
            status = QUANTAPDF_ERROR_UNSUPPORTED;
            return;
        }
        if (!object.isDictionary() ||
            (object.isIndirect() &&
             !seen.insert(object.getObjGen()).second)) {
            status = QUANTAPDF_ERROR_FORMAT;
            return;
        }
        size_t group_index = inherited_group;
        if (!object.getKey("/T").isNull()) {
            if (inherited_group != SIZE_MAX)
                groups[inherited_group].has_named_child = true;
            group_index = groups.size();
            group new_group;
            new_group.head = object;
            groups.push_back(std::move(new_group));
        } else if (group_index == SIZE_MAX) {
            group_index = groups.size();
            group new_group;
            new_group.head = object;
            groups.push_back(std::move(new_group));
        }
        groups[group_index].nodes.push_back(object);
        QPDFObjectHandle subtype = object.getKey("/Subtype");
        if (subtype.isName() && subtype.getName() == "/Widget")
            groups[group_index].widgets.push_back(object);
        QPDFObjectHandle kids = object.getKey("/Kids");
        if (kids.isNull())
            return;
        if (!kids.isArray()) {
            status = QUANTAPDF_ERROR_FORMAT;
            return;
        }
        int const count = kids.getArrayNItems();
        for (int index = 0; index < count; ++index)
            visit(kids.getArrayItem(index), group_index, depth + 1u);
    };
    int const root_count = roots.getArrayNItems();
    for (int index = 0; index < root_count; ++index)
        visit(roots.getArrayItem(index), SIZE_MAX, 1u);
    if (status != QUANTAPDF_OK)
        return status;
    for (auto& group : groups) {
        if (group.has_named_child)
            continue;
        quantapdf_qpdf_edit_live_form_field field;
        field.head = group.head;
        field.nodes = std::move(group.nodes);
        field.widgets = std::move(group.widgets);
        out_fields->push_back(std::move(field));
    }
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_qpdf_edit_make_form_ref(
    quantapdf_qpdf_edit const& edit,
    size_t slot,
    quantapdf_form_field_ref *out_ref) noexcept
{
    quantapdf_local_id id = 0;
    quantapdf_status status = quantapdf_local_id_from_index(slot, &id);
    if (status != QUANTAPDF_OK)
        return status;
    return quantapdf_reference_make(edit.form_scope, id, out_ref->opaque);
}

static quantapdf_status quantapdf_qpdf_edit_resolve_form_ref(
    quantapdf_qpdf_edit *edit,
    quantapdf_form_field_ref const *ref,
    quantapdf_qpdf_edit_form_entry **out_entry)
{
    if (edit == nullptr || ref == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (edit->rollback_failed)
        return QUANTAPDF_ERROR_STATE;
    size_t slot = SIZE_MAX;
    quantapdf_status status = quantapdf_reference_resolve(
        edit->form_scope, edit->forms.size(), ref->opaque, &slot);
    if (status != QUANTAPDF_OK)
        return status;
    *out_entry = &edit->forms[slot];
    return QUANTAPDF_OK;
}

struct quantapdf_qpdf_edit_change {
    QPDFObjectHandle object;
    std::string key;
    QPDFObjectHandle old_value;
    bool present;
    int array_size = -1;
};

class quantapdf_qpdf_edit_transaction {
  public:
    quantapdf_qpdf_edit_transaction(
        quantapdf_qpdf_edit& owner, size_t limit, int *fault) :
        owner(owner), limit(limit), fault(fault)
    {
    }

    quantapdf_qpdf_edit_transaction(quantapdf_qpdf_edit_transaction const&) = delete;
    quantapdf_qpdf_edit_transaction& operator=(
        quantapdf_qpdf_edit_transaction const&) = delete;

    void save(QPDFObjectHandle object, char const *key)
    {
        for (auto const& change : changes) {
            if (change.key == key &&
                quantapdf_qpdf_same_object(change.object, object))
                return;
        }
        if (changes.size() == limit)
            throw std::bad_alloc();
        changes.push_back(
            {object, key, object.getKey(key), object.hasKey(key)});
    }

    // Only appends are allowed after saving a tail. Retain array identity so
    // aliases observe the same committed update and the same rollback.
    void save_array_tail(QPDFObjectHandle array)
    {
        if (changes.size() == limit)
            throw std::bad_alloc();
        changes.push_back({array, {}, {}, false, array.getArrayNItems()});
    }

    void commit() noexcept { committed = true; }

    ~quantapdf_qpdf_edit_transaction() noexcept
    {
        if (committed)
            return;
        for (auto change = changes.rbegin(); change != changes.rend();
             ++change) {
            try {
                if (fault != nullptr &&
                    *fault == QUANTAPDF_TEST_PDF_EDIT_FAULT_ROLLBACK) {
                    *fault = QUANTAPDF_TEST_PDF_EDIT_FAULT_NONE;
                    throw std::bad_alloc();
                }
                if (change->array_size >= 0) {
                    int count = change->object.getArrayNItems();
                    if (count < change->array_size) {
                        owner.rollback_failed = true;
                        continue;
                    }
                    while (count > change->array_size)
                        change->object.eraseItem(--count);
                } else if (change->present) {
                    change->object.replaceKey(change->key, change->old_value);
                } else {
                    change->object.removeKey(change->key);
                }
            } catch (...) {
                // A failed restoration must never be hidden by a later write
                // or snapshot. Continue restoring the other independent keys.
                owner.rollback_failed = true;
            }
        }
    }

  private:
    quantapdf_qpdf_edit& owner;
    size_t limit;
    int *fault;
    std::vector<quantapdf_qpdf_edit_change> changes;
    bool committed = false;
};

static bool quantapdf_qpdf_edit_live_form_contains(
    quantapdf_qpdf_edit_live_form_field const& field,
    QPDFObjectHandle const& object)
{
    for (auto const& node : field.nodes) {
        if (quantapdf_qpdf_same_object(node, object))
            return true;
    }
    return false;
}

static QPDFObjectHandle quantapdf_qpdf_edit_inherited_owner(
    QPDFObjectHandle object,
    char const *key)
{
    std::set<QPDFObjGen> seen;
    while (object.isDictionary()) {
        if (object.hasKey(key))
            return object;
        if (!object.isIndirect() || !seen.insert(object.getObjGen()).second)
            break;
        object = object.getKey("/Parent");
    }
    return QPDFObjectHandle::newNull();
}

static quantapdf_status quantapdf_qpdf_edit_form_preflight(QPDF& pdf)
{
    QPDFObjectHandle acroform = pdf.getRoot().getKey("/AcroForm");
    if (!acroform.isDictionary())
        return QUANTAPDF_ERROR_FORMAT;
    if (acroform.hasKey("/XFA"))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (acroform.hasKey("/NeedAppearances")) {
        QPDFObjectHandle value = acroform.getKey("/NeedAppearances");
        if (!value.isBool())
            return QUANTAPDF_ERROR_FORMAT;
        if (value.getBoolValue())
            return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    return QUANTAPDF_OK;
}

static bool quantapdf_qpdf_edit_form_value_input_valid(
    quantapdf_form_value_input const& value)
{
    return value.struct_size >= QUANTAPDF_FORM_VALUE_INPUT_V1_MIN_SIZE &&
        value.struct_size <= QUANTAPDF_FORM_VALUE_INPUT_V1_SIZE;
}

static std::string quantapdf_qpdf_edit_form_option_export(
    quantapdf_pdf_form_model const& model,
    quantapdf_pdf_form_field_internal const& field,
    size_t option_index)
{
    auto const& option = model.options[field.first_option + option_index];
    return std::string(
        model.strings + option.export_text.offset, option.export_text.size);
}

static void quantapdf_qpdf_edit_form_write_scalar(
    quantapdf_qpdf_edit_live_form_field& live,
    char const *key,
    QPDFObjectHandle value,
    bool missing)
{
    if (!missing)
        live.head.replaceKey(key, value);
    for (auto node : live.nodes) {
        if (missing || !quantapdf_qpdf_same_object(node, live.head))
            node.removeKey(key);
    }
}

static void quantapdf_qpdf_edit_form_refresh_appearance(
    QPDF& pdf,
    QPDFObjectHandle widget,
    std::string const& seed)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    for (unsigned char value : seed) {
        hash ^= value;
        hash *= UINT64_C(1099511628211);
    }
    static char const *colors[] = {
        "0.86 0.94 1 rg", "0.90 1 0.86 rg", "1 0.91 0.84 rg",
        "0.96 0.86 1 rg", "1 0.98 0.78 rg"};
    std::string contents;
    QPDFObjectHandle old_normal = widget.getKey("/AP").getKey("/N");
    if (old_normal.isStream()) {
        std::shared_ptr<Buffer> old_data = old_normal.getStreamData();
        if (old_data != nullptr && old_data->getBuffer() != nullptr)
            contents.assign(
                reinterpret_cast<char const *>(old_data->getBuffer()),
                old_data->getSize());
        contents.push_back('\n');
    }
    contents += "q\n";
    contents += colors[hash % (sizeof(colors) / sizeof(colors[0]))];
    contents += "\n0 0 10000 10000 re f\n0 G 1 w 0 0 10000 10000 re S\nQ\n";
    QPDFObjectHandle stream = pdf.newStream(contents);
    QPDFObjectHandle dictionary = stream.getDict();
    dictionary.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
    dictionary.replaceKey("/Subtype", QPDFObjectHandle::newName("/Form"));
    dictionary.replaceKey("/Resources", QPDFObjectHandle::newDictionary());
    double width = 1.0;
    double height = 1.0;
    QPDFObjectHandle rect = widget.getKey("/Rect");
    if (rect.isArray() && rect.getArrayNItems() == 4 &&
        rect.getArrayItem(0).isNumber() && rect.getArrayItem(1).isNumber() &&
        rect.getArrayItem(2).isNumber() && rect.getArrayItem(3).isNumber()) {
        width = std::fabs(rect.getArrayItem(2).getNumericValue() -
                          rect.getArrayItem(0).getNumericValue());
        height = std::fabs(rect.getArrayItem(3).getNumericValue() -
                           rect.getArrayItem(1).getNumericValue());
    }
    dictionary.replaceKey("/BBox", QPDFObjectHandle::newArray({
        QPDFObjectHandle::newInteger(0), QPDFObjectHandle::newInteger(0),
        QPDFObjectHandle::newReal(width, 6, true),
        QPDFObjectHandle::newReal(height, 6, true)}));
    QPDFObjectHandle appearance = widget.getKey("/AP");
    appearance = appearance.isDictionary() ? appearance.shallowCopy() :
        QPDFObjectHandle::newDictionary();
    appearance.replaceKey("/N", stream);
    widget.replaceKey("/AP", appearance);
}

extern "C" quantapdf_status quantapdf_qpdf_edit_form_ref_at(
    quantapdf_qpdf_edit *edit,
    size_t field_index,
    quantapdf_form_field_ref *out_ref)
{
    if (out_ref == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    std::memset(out_ref, 0, sizeof(*out_ref));
    if (edit == nullptr || edit->pdf == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    try {
        quantapdf_pdf_form_model *raw_model = nullptr;
        quantapdf_status status = quantapdf_qpdf_edit_form_snapshot(
            edit, &raw_model);
        std::unique_ptr<quantapdf_pdf_form_model,
                        void (*)(quantapdf_pdf_form_model *)>
            model(raw_model, quantapdf_qpdf_dispose_form_model);
        if (status != QUANTAPDF_OK)
            return status;
        std::vector<quantapdf_qpdf_edit_live_form_field> fields;
        status = quantapdf_qpdf_edit_collect_live_form_fields(
            *edit->pdf, &fields);
        if (status != QUANTAPDF_OK)
            return status;
        if (fields.size() != model->field_count)
            return QUANTAPDF_ERROR_FORMAT;
        if (field_index >= fields.size())
            return QUANTAPDF_ERROR_ARGUMENT;
        QPDFObjectHandle object = fields[field_index].head;
        for (size_t slot = 0; slot < edit->forms.size(); ++slot) {
            if (quantapdf_qpdf_same_object(edit->forms[slot].object, object)) {
                return quantapdf_qpdf_edit_make_form_ref(*edit, slot, out_ref);
            }
        }
        if (edit->forms.size() >= UINT32_MAX - 1u)
            return QUANTAPDF_ERROR_NOMEM;
        size_t const slot = edit->forms.size();
        edit->forms.push_back({object});
        return quantapdf_qpdf_edit_make_form_ref(*edit, slot, out_ref);
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_edit_form_set_values(
    quantapdf_qpdf_edit *edit,
    const quantapdf_form_field_ref *ref,
    const quantapdf_form_value_update *update,
    int *test_fault)
{
    size_t const update_minimum =
        offsetof(quantapdf_form_value_update, value_count) +
        sizeof(update->value_count);
    if (edit == nullptr || ref == nullptr || update == nullptr ||
        update->struct_size < update_minimum ||
        (update->presence != QUANTAPDF_FORM_VALUE_MISSING &&
         update->presence != QUANTAPDF_FORM_VALUE_PRESENT) ||
        (update->value_count != 0u && update->values == nullptr))
        return QUANTAPDF_ERROR_ARGUMENT;
    try {
        quantapdf_qpdf_edit_form_entry *entry = nullptr;
        quantapdf_status status = quantapdf_qpdf_edit_resolve_form_ref(
            edit, ref, &entry);
        if (status != QUANTAPDF_OK)
            return status;
        quantapdf_pdf_form_model *raw_model = nullptr;
        status = quantapdf_qpdf_edit_form_snapshot(edit, &raw_model);
        std::unique_ptr<quantapdf_pdf_form_model,
                        void (*)(quantapdf_pdf_form_model *)>
            model(raw_model, quantapdf_qpdf_dispose_form_model);
        if (status != QUANTAPDF_OK)
            return status;
        std::vector<quantapdf_qpdf_edit_live_form_field> fields;
        status = quantapdf_qpdf_edit_collect_live_form_fields(
            *edit->pdf, &fields);
        if (status != QUANTAPDF_OK || fields.size() != model->field_count)
            return status != QUANTAPDF_OK ? status : QUANTAPDF_ERROR_FORMAT;
        size_t field_index = SIZE_MAX;
        for (size_t index = 0; index < fields.size(); ++index) {
            if (quantapdf_qpdf_same_object(
                    fields[index].head, entry->object)) {
                field_index = index;
                break;
            }
        }
        if (field_index == SIZE_MAX)
            return QUANTAPDF_ERROR_STATE;
        auto const& field = model->fields[field_index];
        auto& live = fields[field_index];
        status = quantapdf_qpdf_edit_form_preflight(*edit->pdf);
        if (status != QUANTAPDF_OK)
            return status;
        if ((field.flags & 1u) != 0u)
            return QUANTAPDF_ERROR_STATE;

        bool const missing =
            update->presence == QUANTAPDF_FORM_VALUE_MISSING;
        bool custom_utf8 = false;
        bool off = false;
        std::string text;
        std::vector<size_t> option_indices;
        if (missing && update->value_count != 0u)
            return QUANTAPDF_ERROR_ARGUMENT;

        if (field.type == QUANTAPDF_FORM_FIELD_TEXT) {
            if ((field.flags & ((1u << 20) | (1u << 25))) != 0u)
                return QUANTAPDF_ERROR_UNSUPPORTED;
            if (!missing) {
                if (update->value_count != 1u)
                    return QUANTAPDF_ERROR_ARGUMENT;
                auto const& value = update->values[0];
                if (!quantapdf_qpdf_edit_form_value_input_valid(value) ||
                    value.kind != QUANTAPDF_FORM_VALUE_UTF8 ||
                    value.option_index != SIZE_MAX || value.utf8 == nullptr ||
                    !quantapdf_qpdf_valid_utf8(value.utf8, value.utf8_size))
                    return QUANTAPDF_ERROR_ARGUMENT;
                text.assign(value.utf8, value.utf8_size);
            }
            if (missing) {
                if (field.value_presence == QUANTAPDF_FORM_VALUE_MISSING)
                    return QUANTAPDF_OK;
            } else if (field.value_presence == QUANTAPDF_FORM_VALUE_PRESENT &&
                       field.value_count == 1u) {
                auto const& current = model->values[field.first_value];
                if (current.kind == QUANTAPDF_FORM_VALUE_UTF8 &&
                    current.utf8.size == text.size() &&
                    std::memcmp(model->strings + current.utf8.offset,
                                text.data(), text.size()) == 0)
                    return QUANTAPDF_OK;
            }
        } else if (field.type == QUANTAPDF_FORM_FIELD_CHECKBOX ||
                   field.type == QUANTAPDF_FORM_FIELD_RADIO_BUTTON) {
            if (!missing && update->value_count == 0u) {
                off = true;
            } else if (!missing) {
                if (update->value_count != 1u)
                    return QUANTAPDF_ERROR_ARGUMENT;
                auto const& value = update->values[0];
                if (!quantapdf_qpdf_edit_form_value_input_valid(value) ||
                    value.kind != QUANTAPDF_FORM_VALUE_OPTION ||
                    value.utf8 != nullptr || value.utf8_size != 0u ||
                    value.option_index >= field.option_count)
                    return QUANTAPDF_ERROR_ARGUMENT;
                option_indices.push_back(value.option_index);
            }
        } else if (field.type == QUANTAPDF_FORM_FIELD_COMBO_BOX ||
                   field.type == QUANTAPDF_FORM_FIELD_LIST_BOX) {
            if (!missing && field.type == QUANTAPDF_FORM_FIELD_COMBO_BOX &&
                update->value_count != 1u)
                return QUANTAPDF_ERROR_ARGUMENT;
            if (!missing && field.type == QUANTAPDF_FORM_FIELD_LIST_BOX &&
                !field.is_multiselect && update->value_count != 1u)
                return QUANTAPDF_ERROR_ARGUMENT;
            for (size_t index = 0; index < update->value_count; ++index) {
                auto const& value = update->values[index];
                if (value.kind == QUANTAPDF_FORM_VALUE_UTF8 &&
                    field.type == QUANTAPDF_FORM_FIELD_COMBO_BOX &&
                    (field.flags & (1u << 18)) != 0u) {
                    if (index != 0u ||
                        !quantapdf_qpdf_edit_form_value_input_valid(value) ||
                        value.option_index != SIZE_MAX || value.utf8 == nullptr ||
                        !quantapdf_qpdf_valid_utf8(
                            value.utf8, value.utf8_size))
                        return QUANTAPDF_ERROR_ARGUMENT;
                    custom_utf8 = true;
                    text.assign(value.utf8, value.utf8_size);
                    continue;
                }
                if (!quantapdf_qpdf_edit_form_value_input_valid(value) ||
                    value.kind != QUANTAPDF_FORM_VALUE_OPTION ||
                    value.utf8 != nullptr || value.utf8_size != 0u ||
                    value.option_index >= field.option_count ||
                    std::find(option_indices.begin(), option_indices.end(),
                              value.option_index) != option_indices.end())
                    return QUANTAPDF_ERROR_ARGUMENT;
                option_indices.push_back(value.option_index);
            }
        } else {
            return QUANTAPDF_ERROR_UNSUPPORTED;
        }

        if (missing) {
            QPDFObjectHandle owner = quantapdf_qpdf_edit_inherited_owner(
                live.head, "/V");
            if (!owner.isNull() &&
                !quantapdf_qpdf_edit_live_form_contains(live, owner))
                return QUANTAPDF_ERROR_UNSUPPORTED;
        }
        if (test_fault != nullptr &&
            *test_fault == QUANTAPDF_TEST_PDF_EDIT_FAULT_FORM_AFTER_WIDGET_PREPARE) {
            *test_fault = 0;
            return QUANTAPDF_ERROR_BACKEND;
        }

        if (live.widgets.size() > SIZE_MAX / 2u ||
            live.nodes.size() > SIZE_MAX / 2u - live.widgets.size())
            return QUANTAPDF_ERROR_NOMEM;
        quantapdf_qpdf_edit_transaction transaction(
            *edit, 2u * (live.nodes.size() + live.widgets.size()), test_fault);
        for (auto const& node : live.nodes) {
            transaction.save(node, "/V");
            transaction.save(node, "/I");
        }
        for (auto const& widget : live.widgets) {
            transaction.save(widget, "/AS");
            transaction.save(widget, "/AP");
        }

        std::string appearance_seed;
        if (field.type == QUANTAPDF_FORM_FIELD_TEXT) {
            quantapdf_qpdf_edit_form_write_scalar(
                live, "/V", QPDFObjectHandle::newUnicodeString(text), missing);
            appearance_seed = missing ? "missing" : "text:" + text;
        } else if (field.type == QUANTAPDF_FORM_FIELD_CHECKBOX ||
                   field.type == QUANTAPDF_FORM_FIELD_RADIO_BUTTON) {
            std::string selected = "Off";
            if (!missing && !off) {
                char const *state = model->options[
                    field.first_option + option_indices[0]].button_state;
                if (state == nullptr)
                    return QUANTAPDF_ERROR_STATE;
                selected = state;
            }
            quantapdf_qpdf_edit_form_write_scalar(
                live, "/V", QPDFObjectHandle::newName("/" + selected), missing);
            size_t widget_index = 0;
            for (auto widget : live.widgets) {
                std::string state = "Off";
                QPDFObjectHandle normal = widget.getKey("/AP").getKey("/N");
                if (!missing && !off && normal.isDictionary() &&
                    normal.hasKey("/" + selected))
                    state = selected;
                widget.replaceKey(
                    "/AS", QPDFObjectHandle::newName("/" + state));
                if (test_fault != nullptr &&
                    *test_fault == QUANTAPDF_TEST_PDF_EDIT_FAULT_FORM_AFTER_FIRST_WIDGET_STATE &&
                    widget_index == 0u) {
                    *test_fault = 0;
                    return QUANTAPDF_ERROR_BACKEND;
                }
                ++widget_index;
            }
        } else {
            if (missing) {
                quantapdf_qpdf_edit_form_write_scalar(
                    live, "/V", QPDFObjectHandle::newNull(), true);
                quantapdf_qpdf_edit_form_write_scalar(
                    live, "/I", QPDFObjectHandle::newNull(), true);
                appearance_seed = "missing";
            } else if (custom_utf8) {
                quantapdf_qpdf_edit_form_write_scalar(
                    live, "/V", QPDFObjectHandle::newUnicodeString(text), false);
                quantapdf_qpdf_edit_form_write_scalar(
                    live, "/I", QPDFObjectHandle::newNull(), true);
                appearance_seed = "custom:" + text;
            } else {
                std::vector<QPDFObjectHandle> values;
                std::vector<QPDFObjectHandle> indices;
                for (size_t option_index : option_indices) {
                    std::string value = quantapdf_qpdf_edit_form_option_export(
                        *model, field, option_index);
                    values.push_back(QPDFObjectHandle::newUnicodeString(value));
                    indices.push_back(QPDFObjectHandle::newInteger(
                        static_cast<long long>(option_index)));
                    appearance_seed += value;
                    appearance_seed.push_back('\0');
                }
                QPDFObjectHandle value =
                    field.type == QUANTAPDF_FORM_FIELD_LIST_BOX &&
                    field.is_multiselect ? QPDFObjectHandle::newArray(values) :
                    values[0];
                quantapdf_qpdf_edit_form_write_scalar(live, "/V", value, false);
                quantapdf_qpdf_edit_form_write_scalar(
                    live, "/I", QPDFObjectHandle::newArray(indices), false);
            }
        }
        if (test_fault != nullptr &&
            *test_fault == QUANTAPDF_TEST_PDF_EDIT_FAULT_ROLLBACK)
            return QUANTAPDF_ERROR_BACKEND;
        if (test_fault != nullptr &&
            *test_fault == QUANTAPDF_TEST_PDF_EDIT_FAULT_FORM_AFTER_SEMANTIC_WRITE) {
            *test_fault = 0;
            return QUANTAPDF_ERROR_BACKEND;
        }
        if (field.type != QUANTAPDF_FORM_FIELD_CHECKBOX &&
            field.type != QUANTAPDF_FORM_FIELD_RADIO_BUTTON) {
            size_t widget_index = 0;
            for (auto widget : live.widgets) {
                quantapdf_qpdf_edit_form_refresh_appearance(
                    *edit->pdf, widget, appearance_seed);
                if (test_fault != nullptr &&
                    *test_fault == QUANTAPDF_TEST_PDF_EDIT_FAULT_FORM_AFTER_FIRST_AP_REFRESH &&
                    widget_index == 0u) {
                    *test_fault = 0;
                    return QUANTAPDF_ERROR_BACKEND;
                }
                ++widget_index;
            }
        }
        transaction.commit();
        return QUANTAPDF_OK;
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_edit_annotation_count(
    quantapdf_qpdf_edit *edit,
    int page_index,
    size_t *out_count)
{
    if (out_count == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_count = 0;
    try {
        std::vector<QPDFObjectHandle> objects;
        quantapdf_status status = quantapdf_qpdf_edit_scan_annotations(
            edit, page_index, &objects);
        if (status == QUANTAPDF_OK)
            *out_count = objects.size();
        return status;
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_edit_annotation_ref_at(
    quantapdf_qpdf_edit *edit,
    int page_index,
    size_t index,
    quantapdf_annotation_ref *out_ref)
{
    if (out_ref == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    std::memset(out_ref, 0, sizeof(*out_ref));
    try {
        std::vector<QPDFObjectHandle> objects;
        quantapdf_status status = quantapdf_qpdf_edit_scan_annotations(
            edit, page_index, &objects);
        if (status != QUANTAPDF_OK)
            return status;
        if (index >= objects.size())
            return QUANTAPDF_ERROR_ARGUMENT;
        QPDFObjectHandle object = objects[index];
        for (size_t slot = 0; slot < edit->annotations.size(); ++slot) {
            auto const& entry = edit->annotations[slot];
            if (entry.live && quantapdf_qpdf_same_object(entry.object, object)) {
                return quantapdf_qpdf_edit_make_annotation_ref(*edit, slot, out_ref);
            }
        }
        if (!object.isIndirect() || edit->annotations.size() >= UINT32_MAX - 1u)
            return QUANTAPDF_ERROR_NOMEM;
        size_t const slot = edit->annotations.size();
        edit->annotations.push_back({
            object, page_index, true});
        return quantapdf_qpdf_edit_make_annotation_ref(*edit, slot, out_ref);
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_edit_annotation_get_info(
    quantapdf_qpdf_edit *edit,
    const quantapdf_annotation_ref *ref,
    quantapdf_annotation_info *out_info)
{
    if (out_info == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    size_t const minimum = offsetof(quantapdf_annotation_info, flags) +
        sizeof(out_info->flags);
    if (out_info->struct_size < minimum)
        return QUANTAPDF_ERROR_ARGUMENT;
    out_info->type = QUANTAPDF_ANNOTATION_UNKNOWN;
    out_info->bounds = {};
    out_info->flags = 0;
    try {
        quantapdf_qpdf_edit_annotation_entry *entry = nullptr;
        quantapdf_status status = quantapdf_qpdf_edit_resolve_annotation(
            edit, ref, &entry);
        if (status != QUANTAPDF_OK)
            return status;
        return quantapdf_qpdf_edit_annotation_view(
            *edit->pdf, entry->page_index, entry->object,
            out_info, nullptr, nullptr);
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_edit_annotation_contents(
    quantapdf_qpdf_edit *edit,
    const quantapdf_annotation_ref *ref,
    char **out_utf8,
    size_t *out_size)
{
    if (out_utf8 != nullptr)
        *out_utf8 = nullptr;
    if (out_size != nullptr)
        *out_size = 0;
    if (out_utf8 == nullptr || out_size == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    try {
        quantapdf_qpdf_edit_annotation_entry *entry = nullptr;
        quantapdf_status status = quantapdf_qpdf_edit_resolve_annotation(
            edit, ref, &entry);
        if (status != QUANTAPDF_OK)
            return status;
        std::string contents;
        bool present = false;
        status = quantapdf_qpdf_edit_annotation_view(
            *edit->pdf, entry->page_index, entry->object,
            nullptr, &contents, &present);
        if (status != QUANTAPDF_OK || !present)
            return status;
        auto *copy = static_cast<char *>(std::malloc(contents.size() + 1u));
        if (copy == nullptr)
            return QUANTAPDF_ERROR_NOMEM;
        std::memcpy(copy, contents.c_str(), contents.size() + 1u);
        *out_utf8 = copy;
        *out_size = contents.size();
        return QUANTAPDF_OK;
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

static quantapdf_status quantapdf_qpdf_edit_set_annotation_rect(
    QPDF& pdf,
    int page_index,
    QPDFObjectHandle& object,
    quantapdf_rect bounds)
{
    if (!std::isfinite(bounds.x0) || !std::isfinite(bounds.y0) ||
        !std::isfinite(bounds.x1) || !std::isfinite(bounds.y1) ||
        bounds.x1 < bounds.x0 || bounds.y1 < bounds.y0)
        return QUANTAPDF_ERROR_ARGUMENT;
    quantapdf_qpdf_page_geometry geometry = {};
    quantapdf_status status = quantapdf_qpdf_load_page_geometry(
        pdf, page_index, &geometry);
    if (status != QUANTAPDF_OK)
        return status;
    double raw[4];
    status = quantapdf_qpdf_public_crop_to_pdf(geometry, bounds, raw);
    if (status != QUANTAPDF_OK)
        return status;
    std::vector<QPDFObjectHandle> values;
    for (double value : raw)
        values.push_back(QPDFObjectHandle::newReal(value, 6, true));
    object.replaceKey("/Rect", QPDFObjectHandle::newArray(values));
    return QUANTAPDF_OK;
}

extern "C" quantapdf_status quantapdf_qpdf_edit_annotation_create(
    quantapdf_qpdf_edit *edit,
    int page_index,
    const quantapdf_annotation_create_options *options,
    quantapdf_annotation_ref *out_ref,
    int *test_fault)
{
    if (out_ref == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    std::memset(out_ref, 0, sizeof(*out_ref));
    size_t const minimum =
        offsetof(quantapdf_annotation_create_options, contents_size) +
        sizeof(options->contents_size);
    if (edit == nullptr || options == nullptr ||
        options->struct_size < minimum || page_index < 0)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (options->type != QUANTAPDF_ANNOTATION_TEXT &&
        options->type != QUANTAPDF_ANNOTATION_FREE_TEXT &&
        options->type != QUANTAPDF_ANNOTATION_SQUARE &&
        options->type != QUANTAPDF_ANNOTATION_CIRCLE)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (options->contents_utf8 == nullptr && options->contents_size != 0u)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (options->contents_utf8 != nullptr &&
        !quantapdf_qpdf_valid_utf8(
            options->contents_utf8, options->contents_size))
        return QUANTAPDF_ERROR_ARGUMENT;
    if (edit->rollback_failed)
        return QUANTAPDF_ERROR_STATE;
    try {
        auto const& pages = edit->pdf->getAllPages();
        if (static_cast<size_t>(page_index) >= pages.size())
            return QUANTAPDF_ERROR_ARGUMENT;
        size_t const slot = edit->annotations.size();
        if (slot >= UINT32_MAX - 1u)
            return QUANTAPDF_ERROR_NOMEM;
        quantapdf_annotation_ref reference{};
        quantapdf_status status = quantapdf_qpdf_edit_make_annotation_ref(
            *edit, slot, &reference);
        if (status != QUANTAPDF_OK)
            return status;
        QPDFObjectHandle object = edit->pdf->makeIndirectObject(
            QPDFObjectHandle::newDictionary());
        object.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
        object.replaceKey(
            "/Subtype", QPDFObjectHandle::newName(
                quantapdf_qpdf_annotation_name(options->type)));
        status = quantapdf_qpdf_edit_set_annotation_rect(
            *edit->pdf, page_index, object, options->bounds);
        if (status != QUANTAPDF_OK)
            return status;
        object.replaceKey(
            "/F", QPDFObjectHandle::newInteger(options->flags));
        if (options->contents_utf8 != nullptr) {
            object.replaceKey(
                "/Contents", QPDFObjectHandle::newUnicodeString(
                    std::string(options->contents_utf8,
                                options->contents_size)));
        }
        QPDFObjectHandle page = pages[static_cast<size_t>(page_index)];
        QPDFObjectHandle annots = page.getKey("/Annots");
        quantapdf_qpdf_edit_transaction transaction(*edit, 2u, test_fault);
        if (!annots.isArray()) {
            transaction.save(page, "/Annots");
            annots = QPDFObjectHandle::newArray();
            page.replaceKey("/Annots", annots);
        }
        if (annots.getArrayNItems() == INT_MAX)
            return QUANTAPDF_ERROR_NOMEM;
        transaction.save_array_tail(annots);
        annots.appendItem(object);
        if (test_fault != nullptr &&
            *test_fault == QUANTAPDF_TEST_PDF_EDIT_FAULT_AFTER_CREATE_MUTATION) {
            *test_fault = 0;
            return QUANTAPDF_ERROR_BACKEND;
        }
        if (test_fault != nullptr &&
            *test_fault == QUANTAPDF_TEST_PDF_EDIT_FAULT_CREATE_NOMEM) {
            *test_fault = 0;
            throw std::bad_alloc();
        }
        edit->annotations.push_back({
            object, page_index, true});
        transaction.commit();
        *out_ref = reference;
        return QUANTAPDF_OK;
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_edit_annotation_update(
    quantapdf_qpdf_edit *edit,
    const quantapdf_annotation_ref *ref,
    const quantapdf_annotation_update *update,
    int *test_fault)
{
    size_t const minimum = offsetof(quantapdf_annotation_update, contents_size) +
        sizeof(update->contents_size);
    if (edit == nullptr || ref == nullptr || update == nullptr ||
        update->struct_size < minimum ||
        (update->fields & ~(QUANTAPDF_ANNOTATION_UPDATE_BOUNDS |
                            QUANTAPDF_ANNOTATION_UPDATE_FLAGS |
                            QUANTAPDF_ANNOTATION_UPDATE_CONTENTS)) != 0u)
        return QUANTAPDF_ERROR_ARGUMENT;
    if ((update->fields & QUANTAPDF_ANNOTATION_UPDATE_CONTENTS) != 0u) {
        if (update->contents_utf8 == nullptr && update->contents_size != 0u)
            return QUANTAPDF_ERROR_ARGUMENT;
        if (update->contents_utf8 != nullptr &&
            !quantapdf_qpdf_valid_utf8(
                update->contents_utf8, update->contents_size))
            return QUANTAPDF_ERROR_ARGUMENT;
    }
    try {
        quantapdf_qpdf_edit_annotation_entry *entry = nullptr;
        quantapdf_status status = quantapdf_qpdf_edit_resolve_annotation(
            edit, ref, &entry);
        if (status != QUANTAPDF_OK)
            return status;
        quantapdf_annotation_info info = {};
        status = quantapdf_qpdf_edit_annotation_view(
            *edit->pdf, entry->page_index, entry->object,
            &info, nullptr, nullptr);
        if (status != QUANTAPDF_OK)
            return status;
        if ((update->fields & QUANTAPDF_ANNOTATION_UPDATE_BOUNDS) != 0u &&
            info.type != QUANTAPDF_ANNOTATION_TEXT &&
            info.type != QUANTAPDF_ANNOTATION_FREE_TEXT &&
            info.type != QUANTAPDF_ANNOTATION_SQUARE &&
            info.type != QUANTAPDF_ANNOTATION_CIRCLE)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        quantapdf_qpdf_edit_transaction transaction(*edit, 3u, test_fault);
        if ((update->fields & QUANTAPDF_ANNOTATION_UPDATE_BOUNDS) != 0u)
            transaction.save(entry->object, "/Rect");
        if ((update->fields & QUANTAPDF_ANNOTATION_UPDATE_FLAGS) != 0u)
            transaction.save(entry->object, "/F");
        if ((update->fields & QUANTAPDF_ANNOTATION_UPDATE_CONTENTS) != 0u)
            transaction.save(entry->object, "/Contents");
        if ((update->fields & QUANTAPDF_ANNOTATION_UPDATE_BOUNDS) != 0u) {
            status = quantapdf_qpdf_edit_set_annotation_rect(
                *edit->pdf, entry->page_index, entry->object, update->bounds);
            if (status != QUANTAPDF_OK)
                return status;
        }
        if ((update->fields & QUANTAPDF_ANNOTATION_UPDATE_FLAGS) != 0u)
            entry->object.replaceKey(
                "/F", QPDFObjectHandle::newInteger(update->flags));
        if (test_fault != nullptr &&
            *test_fault == QUANTAPDF_TEST_PDF_EDIT_FAULT_UPDATE_NOMEM) {
            *test_fault = 0;
            throw std::bad_alloc();
        }
        if ((update->fields & QUANTAPDF_ANNOTATION_UPDATE_CONTENTS) != 0u) {
            if (update->contents_utf8 == nullptr) {
                entry->object.removeKey("/Contents");
            } else {
                entry->object.replaceKey(
                    "/Contents", QPDFObjectHandle::newUnicodeString(
                        std::string(update->contents_utf8,
                                    update->contents_size)));
            }
        }
        if (test_fault != nullptr &&
            *test_fault == QUANTAPDF_TEST_PDF_EDIT_FAULT_ROLLBACK)
            return QUANTAPDF_ERROR_BACKEND;
        if (test_fault != nullptr &&
            *test_fault == QUANTAPDF_TEST_PDF_EDIT_FAULT_AFTER_FIRST_UPDATE_FIELD) {
            *test_fault = 0;
            return QUANTAPDF_ERROR_BACKEND;
        }
        transaction.commit();
        return QUANTAPDF_OK;
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_edit_annotation_delete(
    quantapdf_qpdf_edit *edit,
    const quantapdf_annotation_ref *ref)
{
    try {
        quantapdf_qpdf_edit_annotation_entry *entry = nullptr;
        quantapdf_status status = quantapdf_qpdf_edit_resolve_annotation(
            edit, ref, &entry);
        if (status != QUANTAPDF_OK)
            return status;
        auto const& pages = edit->pdf->getAllPages();
        if (entry->page_index < 0 ||
            static_cast<size_t>(entry->page_index) >= pages.size())
            return QUANTAPDF_ERROR_STATE;
        QPDFObjectHandle annots = pages[static_cast<size_t>(entry->page_index)]
            .getKey("/Annots");
        if (!annots.isArray())
            return QUANTAPDF_ERROR_STATE;
        int const count = annots.getArrayNItems();
        bool removed = false;
        for (int index = count - 1; index >= 0; --index) {
            if (quantapdf_qpdf_same_object(
                    annots.getArrayItem(index), entry->object)) {
                annots.eraseItem(index);
                removed = true;
            }
        }
        if (!removed)
            return QUANTAPDF_ERROR_STATE;
        entry->live = false;
        return QUANTAPDF_OK;
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" void quantapdf_qpdf_edit_drop(quantapdf_qpdf_edit *edit)
{
    delete edit;
}

namespace {

enum class quantapdf_qpdf_security_operation {
    encrypt,
    decrypt,
    reencrypt
};

struct quantapdf_qpdf_random_context {
    int test_fault;
    int phase;
    quantapdf_qpdf_security_test_stats *test_stats;
};

thread_local quantapdf_qpdf_random_context *
    quantapdf_qpdf_current_random_context = nullptr;

class quantapdf_qpdf_random_provider final : public RandomDataProvider
{
  public:
    void provideRandomData(unsigned char *data, size_t size) override
    {
        quantapdf_qpdf_random_context *const context =
            quantapdf_qpdf_current_random_context;
        if (context != nullptr && context->test_stats != nullptr) {
            if (context->phase == 1)
                ++context->test_stats->configure_requests;
            else
                ++context->test_stats->write_requests;
        }
        if (context != nullptr &&
            ((context->test_fault == 1 && context->phase == 1) ||
             (context->test_fault == 2 && context->phase == 2)))
            throw std::runtime_error("QuantaPDF test entropy failure");
        quantapdf_status const status = quantapdf_secure_random(data, size);
        if (status != QUANTAPDF_OK)
            throw std::runtime_error("QuantaPDF secure random failed");
    }
};

quantapdf_qpdf_random_provider *quantapdf_qpdf_process_random_provider()
{
    // qpdf retains this raw pointer globally; it must outlive every qpdf call.
    static auto *provider = new quantapdf_qpdf_random_provider();
    return provider;
}

class quantapdf_qpdf_random_provider_initializer final
{
  public:
    quantapdf_qpdf_random_provider_initializer()
    {
        QUtil::setRandomDataProvider(
            quantapdf_qpdf_process_random_provider());
    }
};

quantapdf_qpdf_random_provider_initializer
    quantapdf_qpdf_random_provider_process_initializer;

class quantapdf_qpdf_random_context_guard final
{
  public:
    quantapdf_qpdf_random_context_guard(
        int test_fault,
        quantapdf_qpdf_security_test_stats *test_stats) :
        context{test_fault, 1, test_stats},
        previous(quantapdf_qpdf_current_random_context),
        test_stats(test_stats)
    {
        quantapdf_qpdf_current_random_context = &context;
        if (test_stats != nullptr)
            ++test_stats->context_entries;
    }

    ~quantapdf_qpdf_random_context_guard()
    {
        quantapdf_qpdf_current_random_context = previous;
        if (test_stats != nullptr)
            ++test_stats->context_exits;
    }

    quantapdf_qpdf_random_context_guard(
        quantapdf_qpdf_random_context_guard const&) = delete;
    quantapdf_qpdf_random_context_guard& operator=(
        quantapdf_qpdf_random_context_guard const&) = delete;

    void enter_write_phase() noexcept
    {
        context.phase = 2;
    }

  private:
    quantapdf_qpdf_random_context context;
    quantapdf_qpdf_random_context *previous;
    quantapdf_qpdf_security_test_stats *test_stats;
};

quantapdf_status quantapdf_qpdf_security_signature_preflight(
    QPDF& pdf,
    size_t source_size)
{
    uint32_t findings = 0;
    quantapdf_status status = quantapdf_qpdf_audit_document(
        pdf, source_size, &findings);
    if (status != QUANTAPDF_OK)
        return status;
    quantapdf_validation_report report{};
    status = quantapdf_validate_security(findings, QUANTAPDF_AUDIT_SIGNATURE, &report);
    if (status != QUANTAPDF_OK)
        return status;

    for (QPDFObjectHandle object : pdf.getAllObjects()) {
        if (object.isStream())
            object = object.getDict();
        if (!object.isDictionary())
            continue;
        bool is_signature = false;
        status = quantapdf_qpdf_security_signature_dictionary(
            object, &is_signature);
        if (status != QUANTAPDF_OK)
            return status;
        QPDFObjectHandle field_type = object.getKey("/FT");
        QPDFObjectHandle value = object.getKey("/V");
        if (field_type.isName() && field_type.getName() == "/Sig" &&
            !value.isNull()) {
            if (!value.isDictionary())
                return QUANTAPDF_ERROR_FORMAT;
            status = quantapdf_qpdf_security_signature_dictionary(
                value, &is_signature);
            if (status == QUANTAPDF_OK || !is_signature)
                return QUANTAPDF_ERROR_FORMAT;
            return status;
        }
    }
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_qpdf_security_validate_id(
    QPDFObjectHandle trailer,
    bool create_if_missing)
{
    if (!trailer.hasKey("/ID")) {
        if (!create_if_missing)
            return QUANTAPDF_OK;
        std::array<unsigned char, 16> random_id{};
        quantapdf_status const status = quantapdf_secure_random(
            random_id.data(), random_id.size());
        if (status != QUANTAPDF_OK)
            return status;
        std::string value(
            reinterpret_cast<char const *>(random_id.data()),
            random_id.size());
        QPDFObjectHandle identifiers = QPDFObjectHandle::newArray();
        identifiers.appendItem(QPDFObjectHandle::newString(value));
        identifiers.appendItem(QPDFObjectHandle::newString(value));
        trailer.replaceKey("/ID", identifiers);
        return QUANTAPDF_OK;
    }

    QPDFObjectHandle identifiers = trailer.getKey("/ID");
    if (!identifiers.isArray() || identifiers.getArrayNItems() != 2)
        return QUANTAPDF_ERROR_FORMAT;
    QPDFObjectHandle first = identifiers.getArrayItem(0);
    QPDFObjectHandle second = identifiers.getArrayItem(1);
    if (!first.isString() || first.getStringValue().empty() ||
        !second.isString())
        return QUANTAPDF_ERROR_FORMAT;
    return QUANTAPDF_OK;
}

std::string quantapdf_qpdf_security_random_hex()
{
    static constexpr char digits[] = "0123456789abcdef";
    std::array<unsigned char, 32> random{};
    quantapdf_status const status = quantapdf_secure_random(
        random.data(), random.size());
    if (status != QUANTAPDF_OK)
        throw std::runtime_error("QuantaPDF secure random failed");
    std::string hex(random.size() * 2u, '0');
    for (size_t index = 0; index < random.size(); ++index) {
        hex[index * 2u] = digits[random[index] >> 4u];
        hex[index * 2u + 1u] = digits[random[index] & 0x0fu];
    }
    return hex;
}

quantapdf_status quantapdf_qpdf_security_copy_output(
    QPDFWriter& writer,
    unsigned char **out_data,
    size_t *out_size)
{
    std::unique_ptr<Buffer> buffer(writer.getBuffer());
    if (buffer == nullptr || buffer->getSize() == 0 ||
        buffer->getBuffer() == nullptr)
        return QUANTAPDF_ERROR_BACKEND;
    auto *copy = static_cast<unsigned char *>(
        std::malloc(buffer->getSize()));
    if (copy == nullptr)
        return QUANTAPDF_ERROR_NOMEM;
    std::memcpy(copy, buffer->getBuffer(), buffer->getSize());
    *out_data = copy;
    *out_size = buffer->getSize();
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_qpdf_security_rewrite(
    quantapdf_qpdf_document *document,
    quantapdf_qpdf_security_operation operation,
    quantapdf_encryption_options const *options,
    int test_fault,
    quantapdf_qpdf_security_test_stats *test_stats,
    unsigned char **out_data,
    size_t *out_size)
{
    if (out_data == nullptr || out_size == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_data = nullptr;
    *out_size = 0;
    if (document == nullptr || document->source_data == nullptr ||
        document->source_size == 0 || document->password == nullptr ||
        (operation != quantapdf_qpdf_security_operation::decrypt &&
         options == nullptr))
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        auto pdf = QPDF::create();
        pdf->setSuppressWarnings(true);
        pdf->setAttemptRecovery(false);
        pdf->processMemoryFile(
            "quantapdf-security-rewrite",
            reinterpret_cast<char const *>(document->source_data),
            document->source_size,
            document->password);
        (void)pdf->getAllPages();
        (void)pdf->getAllObjects();
        if (pdf->anyWarnings())
            return QUANTAPDF_ERROR_FORMAT;

        quantapdf_status status =
            quantapdf_qpdf_security_signature_preflight(
                *pdf, document->source_size);
        if (status != QUANTAPDF_OK)
            return status;
        bool const encrypted = pdf->isEncrypted();
        if ((operation == quantapdf_qpdf_security_operation::encrypt &&
             encrypted) ||
            (operation != quantapdf_qpdf_security_operation::encrypt &&
             !encrypted))
            return QUANTAPDF_ERROR_STATE;

        status = quantapdf_qpdf_security_validate_id(
            pdf->getTrailer(),
            operation != quantapdf_qpdf_security_operation::decrypt);
        if (status != QUANTAPDF_OK)
            return status;

        QPDFWriter writer(*pdf);
        writer.setOutputMemory();
        writer.setObjectStreamMode(qpdf_o_disable);
        writer.setStreamDataMode(qpdf_s_preserve);
        writer.setPreserveUnreferencedObjects(false);

        if (operation == quantapdf_qpdf_security_operation::decrypt) {
            writer.setPreserveEncryption(false);
            writer.setDeterministicID(true);
            writer.write();
        } else {
            if (QUtil::getRandomDataProvider() !=
                quantapdf_qpdf_process_random_provider())
                return QUANTAPDF_ERROR_UNSUPPORTED;
            quantapdf_qpdf_random_context_guard random_context_guard(
                test_fault, test_stats);
            QPDFObjectHandle trailer = pdf->getTrailer();
            bool const had_info = trailer.hasKey("/Info");
            QPDFObjectHandle original_info = trailer.getKey("/Info");
            QPDFObjectHandle private_info = QPDFObjectHandle::newDictionary({
                {"/QuantaPDFSecuritySeed",
                 QPDFObjectHandle::newString(
                     quantapdf_qpdf_security_random_hex())}});
            trailer.replaceKey("/Info", private_info);

            qpdf_r3_print_e print = qpdf_r3p_none;
            if ((options->permissions &
                 QUANTAPDF_PERMISSION_PRINT_HIGH_QUALITY) != 0u)
                print = qpdf_r3p_full;
            else if ((options->permissions &
                      QUANTAPDF_PERMISSION_PRINT_LOW_RESOLUTION) != 0u)
                print = qpdf_r3p_low;
            writer.setR6EncryptionParameters(
                options->user_password_utf8,
                options->owner_password_utf8,
                true,
                (options->permissions & QUANTAPDF_PERMISSION_COPY) != 0u,
                (options->permissions & QUANTAPDF_PERMISSION_ASSEMBLE) != 0u,
                (options->permissions &
                 QUANTAPDF_PERMISSION_ANNOTATE_AND_FILL_FORMS) != 0u,
                (options->permissions &
                 QUANTAPDF_PERMISSION_FILL_FORMS) != 0u,
                (options->permissions &
                 QUANTAPDF_PERMISSION_MODIFY_OTHER) != 0u,
                print,
                options->encrypt_metadata != 0);

            if (had_info)
                trailer.replaceKey("/Info", original_info);
            else
                trailer.removeKey("/Info");
            random_context_guard.enter_write_phase();
            writer.write();
        }

        status = quantapdf_qpdf_security_copy_output(
            writer, out_data, out_size);
        if (status != QUANTAPDF_OK)
            return status;
        if (pdf->anyWarnings()) {
            std::free(*out_data);
            *out_data = nullptr;
            *out_size = 0;
            return QUANTAPDF_ERROR_FORMAT;
        }
        return QUANTAPDF_OK;
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

} // namespace

extern "C" quantapdf_status quantapdf_qpdf_encrypt_pdf(
    quantapdf_qpdf_document *document,
    quantapdf_encryption_options const *options,
    int test_fault,
    quantapdf_qpdf_security_test_stats *test_stats,
    unsigned char **out_data,
    size_t *out_size)
{
    return quantapdf_qpdf_security_rewrite(
        document, quantapdf_qpdf_security_operation::encrypt,
        options, test_fault, test_stats, out_data, out_size);
}

extern "C" quantapdf_status quantapdf_qpdf_decrypt_pdf(
    quantapdf_qpdf_document *document,
    unsigned char **out_data,
    size_t *out_size)
{
    return quantapdf_qpdf_security_rewrite(
        document, quantapdf_qpdf_security_operation::decrypt,
        nullptr, 0, nullptr, out_data, out_size);
}

extern "C" quantapdf_status quantapdf_qpdf_reencrypt_pdf(
    quantapdf_qpdf_document *document,
    quantapdf_encryption_options const *options,
    int test_fault,
    quantapdf_qpdf_security_test_stats *test_stats,
    unsigned char **out_data,
    size_t *out_size)
{
    return quantapdf_qpdf_security_rewrite(
        document, quantapdf_qpdf_security_operation::reencrypt,
        options, test_fault, test_stats, out_data, out_size);
}

extern "C" quantapdf_status quantapdf_qpdf_capture_graph(
    quantapdf_qpdf_document *document, const quantapdf_graph_limits *limits,
    quantapdf_graph **out_graph)
{
    if (out_graph == nullptr) return QUANTAPDF_ERROR_ARGUMENT;
    *out_graph = nullptr;
    if (document == nullptr || limits == nullptr) return QUANTAPDF_ERROR_ARGUMENT;
    try {
        auto pdf = quantapdf_qpdf_fresh_document(*document, "quantapdf graph snapshot");
        return quantapdf_qpdf_import_graph(*pdf, limits, out_graph);
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}
