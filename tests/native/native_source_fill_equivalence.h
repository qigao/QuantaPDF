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
    const auto draw=[](const quantapdf_ir& ir,quantapdf_ir_raster_image& out)->bool {
        size_t source_pixels=0u;
        for(size_t i=0u;i<ir.resources.image_count;++i)
            source_pixels+=size_t(ir.resources.images[i].width)*ir.resources.images[i].height;
        auto budget=raster_budget;
        if(source_pixels!=0u)budget.max_source_pixels=source_pixels;
        quantapdf_native_page_device device{};device.dpi=96.0f;
        quantapdf_native_prepared_page_v1 prepared{};
        CHECK(quantapdf_native_prepare_page_v1(&ir,&budget,&device,&prepared)==QUANTAPDF_OK);
        CHECK(quantapdf_native_execute_page_v1(&prepared,nullptr,&out)==QUANTAPDF_OK);
        if(source_pixels!=0u) {
            --budget.max_source_pixels;quantapdf_native_prepared_page_v1 denied{};
            CHECK(quantapdf_native_prepare_page_v1(&ir,&budget,&device,&denied)==QUANTAPDF_ERROR_UNSUPPORTED && denied.source==nullptr);
        }
        return true;
    };
    CHECK(draw(a,x) && draw(b,y));
    quantapdf_ir_dispose(&a);quantapdf_ir_dispose(&b);
    CHECK(x.width==y.width && x.height==y.height && x.size==y.size && std::memcmp(x.pixels,y.pixels,x.size)==0);
    quantapdf_ir_raster_image_dispose(&x);quantapdf_ir_raster_image_dispose(&y);
    return true;
}
template<class Scenario>
static bool source_fill_equivalent(const std::string& left,const std::string& right,
    const Scenario& sc)
{
    const bool equal=source_pdf_equivalent(original_pdf(left,sc,false),original_pdf(right,sc,false));
    if(!equal)std::fprintf(stderr,"Source equivalence original: %s\nReference: %s\n",left.c_str(),right.c_str());
    return equal;
}
#endif
