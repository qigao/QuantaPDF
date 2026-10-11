#include "qpdf_text.h"

#include <qpdf/QPDFObjectHandle.hh>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>
#include <string>

quantapdf_status quantapdf_qpdf_copy_text(QPDFObjectHandle value,
    char **out_utf8, size_t *out_size)
{
    auto text = value.getUTF8Value();
    if (text.size() == SIZE_MAX) return QUANTAPDF_ERROR_UNSUPPORTED;
    auto copy = static_cast<char *>(std::malloc(text.size() + 1));
    if (copy == nullptr) return QUANTAPDF_ERROR_NOMEM;
    if (!text.empty()) std::memcpy(copy, text.data(), text.size());
    copy[text.size()] = 0;
    *out_utf8 = copy; *out_size = text.size();
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_qpdf_decode_text(const unsigned char *data, size_t size,
    char **out_utf8, size_t *out_size)
{
    if (out_utf8 != nullptr) *out_utf8 = nullptr;
    if (out_size != nullptr) *out_size = 0;
    if (out_utf8 == nullptr || out_size == nullptr || (data == nullptr && size != 0))
        return QUANTAPDF_ERROR_ARGUMENT;
    try {
        auto raw = size == 0 ? std::string{} : std::string(reinterpret_cast<char const *>(data), size);
        return quantapdf_qpdf_copy_text(QPDFObjectHandle::newString(raw), out_utf8, out_size);
    } catch (std::bad_alloc const&) { return QUANTAPDF_ERROR_NOMEM; }
      catch (std::length_error const&) { return QUANTAPDF_ERROR_UNSUPPORTED; }
      catch (...) { return QUANTAPDF_ERROR_BACKEND; }
}
