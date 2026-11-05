#ifndef QUANTAPDF_NATIVE_SOURCE_FILL_EQUIVALENCE_H
#define QUANTAPDF_NATIVE_SOURCE_FILL_EQUIVALENCE_H
#include "backend/native_prepared_page_v1.h"

// The including fixture supplies source() and its existing budgets.
// Compare fully authored PDF originals through detached RGBA bytes.
static bool source_pdf_equivalent(const std::string& left,const std::string& right)
{
    quantapdf_ir a{},b{};quantapdf_ir_raster_image x{},y{};
    CHECK(source(left,source_budget,a)==QUANTAPDF_OK);
    CHECK(source(right,source_budget,b)==QUANTAPDF_OK);
    const auto draw=[](const quantapdf_ir& ir,quantapdf_ir_raster_image& out) {
        quantapdf_native_page_device device{};device.dpi=96.0f;
        quantapdf_native_prepared_page_v1 prepared{};
        auto status=quantapdf_native_prepare_page_v1(&ir,&raster_budget,&device,&prepared);
        if(status==QUANTAPDF_OK)status=quantapdf_native_execute_page_v1(&prepared,nullptr,&out);
        return status;
    };
    CHECK(draw(a,x)==QUANTAPDF_OK && draw(b,y)==QUANTAPDF_OK);
    quantapdf_ir_dispose(&a);quantapdf_ir_dispose(&b);
    CHECK(x.width==y.width && x.height==y.height && x.size==y.size && std::memcmp(x.pixels,y.pixels,x.size)==0);
    quantapdf_ir_raster_image_dispose(&x);quantapdf_ir_raster_image_dispose(&y);
    return true;
}
template<class Scenario>
static bool source_fill_equivalent(const std::string& left,const std::string& right,
    const Scenario& sc)
{
    return source_pdf_equivalent(original_pdf(left,sc,false),original_pdf(right,sc,false));
}
#endif
