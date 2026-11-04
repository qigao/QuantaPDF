#ifndef QUANTAPDF_NATIVE_SOURCE_TEST_PAGE_H
#define QUANTAPDF_NATIVE_SOURCE_TEST_PAGE_H
#include "backend/qpdf_page_ir.h"
#include <cstring>
#include <memory>
/* Fixture ownership, not a second production Source API: open through the real
 * Document implementation and borrow the same Page entry as public rendering. */
inline quantapdf_status native_test_page_ir(const unsigned char *bytes,size_t size,
    const quantapdf_qpdf_page_limits *limits,size_t page_index,quantapdf_ir *out)
{
    std::memset(out,0,sizeof(*out));
    quantapdf_qpdf_document *document=nullptr;
    const auto status=quantapdf_qpdf_open_memory(bytes,size,"",&document);
    if(status!=QUANTAPDF_OK)return status;
    std::unique_ptr<quantapdf_qpdf_document,decltype(&quantapdf_qpdf_close)>
        owner(document,quantapdf_qpdf_close);
    return quantapdf_qpdf_page_to_ir(owner.get(),page_index,limits,out);
}
#endif
