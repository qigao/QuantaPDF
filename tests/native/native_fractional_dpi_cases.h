#ifndef QUANTAPDF_NATIVE_FRACTIONAL_DPI_CASES_H
#define QUANTAPDF_NATIVE_FRACTIONAL_DPI_CASES_H
#include "native_clip_graph_fixture.h"
#include "native_page_text_fixture.h"
#include <memory>
#include <limits>
#include <cmath>

static std::string fractional_dpi_fill_original(unsigned extent)
{
    using O=QPDFObjectHandle;
    auto pdf=QPDF::create();pdf->emptyPDF();auto root=pdf->getRoot().getKey("/Pages");
    auto page=pdf->makeIndirectObject(O::parse("<< /Type /Page /MediaBox [0 0 "+
        std::to_string(extent)+" "+std::to_string(extent)+"] /Resources << >> >>"));
    page.replaceKey("/Parent",root);root.replaceKey("/Kids",O::newArray({page}));root.replaceKey("/Count",O::newInteger(1));
    page.replaceKey("/Contents",pdf->newStream("0 1 0 rg 0 0 "+std::to_string(extent)+" "+std::to_string(extent)+" re f"));
    QPDFWriter writer(*pdf);writer.setOutputMemory();writer.write();const auto bytes=writer.getBufferSharedPointer();
    return {reinterpret_cast<const char*>(bytes->getBuffer()),bytes->getSize()};
}

static int fractional_dpi_boundary_cases(const std::string& filename)
{
    using bitmap_owner=std::unique_ptr<quantapdf_bitmap,decltype(&quantapdf_drop_bitmap)>;
    size_t renders=0u,refusals=0u;
    const auto exercise=[&](unsigned extent,const std::vector<float>& rates,
        const std::vector<size_t>& widths,const std::vector<unsigned>& columns)->int {
        CHECK(write_pdf(filename,fractional_dpi_fill_original(extent)));
        quantapdf_document *raw_doc=nullptr;OK(quantapdf_open(filename.c_str(),nullptr,&raw_doc));
        std::unique_ptr<quantapdf_document,decltype(&quantapdf_close)> doc(raw_doc,quantapdf_close);
        CHECK(std::remove(filename.c_str())==0);quantapdf_page *raw_page=nullptr;OK(quantapdf_load_page(doc.get(),0,&raw_page));
        std::unique_ptr<quantapdf_page,decltype(&quantapdf_drop_page)> page(raw_page,quantapdf_drop_page);doc.reset();
        std::vector<bitmap_owner> held;std::vector<std::vector<unsigned char>> expected;
        for(size_t i=0u;i<rates.size();++i) {
            quantapdf_render_options opts{};opts.dpi=rates[i];quantapdf_bitmap *raw=nullptr;
            OK(quantapdf_render_page_with_options(page.get(),&opts,&raw));bitmap_owner image(raw,quantapdf_drop_bitmap);
            int w=0,h=0,stride=0,c=0;const unsigned char *pixels=nullptr;size_t size=0u;
            OK(quantapdf_bitmap_dimensions(image.get(),&w,&h,&stride,&c));OK(quantapdf_bitmap_data(image.get(),&pixels,&size));
            CHECK(w==int(widths[i]) && h==w && stride==3*w && c==3 && size==widths[i]*widths[i]*3u);
            std::vector<unsigned char> rgb(size,255u);
            for(size_t y=0u;y<widths[i];++y)for(size_t x=0u;x<widths[i];++x) {
                const unsigned nx=columns[i]<=4u*x?0u:std::min(4u,columns[i]-unsigned(4u*x));
                const unsigned ny=columns[i]<=4u*y?0u:std::min(4u,columns[i]-unsigned(4u*y));
                const auto value=static_cast<unsigned char>((255u*(16u-nx*ny)+8u)/16u);
                rgb[3u*(y*widths[i]+x)]=rgb[3u*(y*widths[i]+x)+2u]=value;
            }
            CHECK(std::memcmp(pixels,rgb.data(),size)==0);held.push_back(std::move(image));expected.push_back(std::move(rgb));++renders;
        }
        if(extent==1u) {
            for(float dpi:{0.0f,-1.0f,std::numeric_limits<float>::quiet_NaN(),
                std::numeric_limits<float>::infinity(),std::numeric_limits<float>::max()}) {
                quantapdf_render_options opts{};opts.dpi=dpi;quantapdf_bitmap *image=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
                CHECK(quantapdf_render_page_with_options(page.get(),&opts,&image)==
                    (!std::isfinite(dpi) || dpi<=0.0f?QUANTAPDF_ERROR_ARGUMENT:QUANTAPDF_ERROR_UNSUPPORTED) && image==nullptr);++refusals;
            }
            quantapdf_bitmap *raw=nullptr;OK(quantapdf_render_page(page.get(),&raw));bitmap_owner recovered(raw,quantapdf_drop_bitmap);
            const unsigned char *pixels=nullptr;size_t size=0u;OK(quantapdf_bitmap_data(recovered.get(),&pixels,&size));
            CHECK(size==3u && pixels[0]==0u && pixels[1]==255u && pixels[2]==0u);++renders;
        }
        page.reset();
        for(size_t i=0u;i<held.size();++i) {
            const unsigned char *pixels=nullptr;size_t size=0u;OK(quantapdf_bitmap_data(held[i].get(),&pixels,&size));
            CHECK(size==expected[i].size() && std::memcmp(pixels,expected[i].data(),size)==0);
        }
        return EXIT_SUCCESS;
    };
    CHECK(exercise(1u,{std::nextafter(9.0f,0.0f),9.0f,std::nextafter(9.0f,INFINITY),
        std::nextafter(72.0f,0.0f),72.0f,std::nextafter(72.0f,INFINITY),0.5f,0.1f,
        std::numeric_limits<float>::denorm_min(),std::numeric_limits<float>::min()},
        {1u,1u,1u,1u,1u,2u,1u,1u,1u,1u},{0u,0u,1u,4u,4u,4u,0u,0u,0u,0u})==EXIT_SUCCESS);
    // Actual 0.1f is 13421773/134217728, strictly above decimal 0.1.
    // The 810pt canvas therefore includes sample 1.125 on both axes.
    CHECK(exercise(810u,{0.1f},{2u},{5u})==EXIT_SUCCESS);
    CHECK(renders==12u && refusals==5u);
    std::puts("binary32 DPI: full RGB at adjacent pixel/sample boundaries, 0.1f, subnormal/normal minimum, atomic refusals and detached Bitmaps");
    return EXIT_SUCCESS;
}

static int fractional_dpi_curve_cases(const std::string& filename)
{
    using rate=native_clip_graph::dpi_rate;using O=QPDFObjectHandle;
    struct specimen {rate dpi;unsigned quarters;int rotation;const char *golden;};
    const specimen cases[]={
        {{185,8},4u,0,"roboto-hello-23.125-m0.rgba.z"},
        {{147,2},4u,0,"roboto-hello-73.5-m0.rgba.z"},
        {{1537,16},4u,0,"roboto-hello-96.0625-m0.rgba.z"},
        {{147,2},5u,90,"roboto-hello-91.875-m0-rotate90.rgba.z"},
        {{1537,16},3u,270,"roboto-hello-72.046875-m0-rotate270.rgba.z"},
        {{185,8},7u,180,"roboto-hello-40.46875-m0-rotate180.rgba.z"}
    };
    size_t renders=0u,turns=0u;
    for(const auto& c:cases)for(bool inherited:{false,true})for(bool encrypted:{false,true}) {
        auto pdf=QPDF::create();const auto base=native_ttf_fixture();pdf->processMemoryFile("fractional DPI Hello",base.data(),base.size());
        auto root=pdf->getRoot().getKey("/Pages"),page=root.getKey("/Kids").getArrayItem(0),geometry=inherited?root:page;
        geometry.replaceKey("/MediaBox",O::parse("[-4.5 2.25 123.5 66.25]"));geometry.replaceKey("/Rotate",O::newInteger(c.rotation));
        if(inherited)page.removeKey("/MediaBox");
        page.replaceKey("/UserUnit",O::parse(std::to_string(double(c.quarters)/4.0)));
        const auto content=page.getKey("/Contents").getStreamData();
        const std::string text(reinterpret_cast<const char*>(content->getBuffer()),content->getSize());
        page.replaceKey("/Contents",pdf->newStream("q 1 0 0 1 -4.5 2.25 cm "+text+" Q"));
        QPDFWriter writer(*pdf);writer.setOutputMemory();
        if(encrypted)writer.setR6EncryptionParameters("reader","owner",true,true,true,true,true,true,qpdf_r3p_full,true);
        writer.write();const auto original=writer.getBufferSharedPointer();
        CHECK(write_pdf(filename,std::string(reinterpret_cast<const char*>(original->getBuffer()),original->getSize())));
        quantapdf_document *raw_doc=nullptr;
        if(encrypted)CHECK(quantapdf_open(filename.c_str(),"wrong",&raw_doc)==QUANTAPDF_ERROR_PASSWORD && raw_doc==nullptr);
        OK(quantapdf_open(filename.c_str(),encrypted?"reader":nullptr,&raw_doc));
        std::unique_ptr<quantapdf_document,decltype(&quantapdf_close)> doc(raw_doc,quantapdf_close);
        CHECK(std::remove(filename.c_str())==0);quantapdf_page *raw_page=nullptr;OK(quantapdf_load_page(doc.get(),0,&raw_page));
        std::unique_ptr<quantapdf_page,decltype(&quantapdf_drop_page)> selected(raw_page,quantapdf_drop_page);doc.reset();
        const bool swap=c.rotation==90 || c.rotation==270;
        const uint64_t denominator=288u*uint64_t(c.dpi.denominator);
        const size_t w=size_t(((swap?64u:128u)*c.quarters*uint64_t(c.dpi.numerator)+denominator-1u)/denominator);
        const size_t h=size_t(((swap?128u:64u)*c.quarters*uint64_t(c.dpi.numerator)+denominator-1u)/denominator);
        std::ifstream file(std::filesystem::path(__FILE__).parent_path()/"goldens"/c.golden,std::ios::binary);
        const std::vector<unsigned char> encoded{std::istreambuf_iterator<char>(file),{}};
        std::vector<unsigned char> rgba(w*h*4u),expected(w*h*3u);uLongf bytes=static_cast<uLongf>(rgba.size());
        CHECK(uncompress(rgba.data(),&bytes,encoded.data(),static_cast<uLong>(encoded.size()))==Z_OK && bytes==rgba.size());
        for(size_t i=0u;i<w*h;++i) {CHECK(rgba[4u*i+3u]==255u);std::copy_n(rgba.data()+4u*i,3u,expected.data()+3u*i);}
        using bitmap_owner=std::unique_ptr<quantapdf_bitmap,decltype(&quantapdf_drop_bitmap)>;
        std::vector<bitmap_owner> held;std::vector<std::vector<unsigned char>> references;
        for(unsigned display:{0u,90u,180u,270u}) {
            const auto reference=native_page_text::turn(expected,w,h,display);
            const quantapdf_render_options opts{float(c.dpi),float(display),0,{},0};quantapdf_bitmap *raw=nullptr;
            const auto status=quantapdf_render_page_with_options(selected.get(),&opts,&raw);bitmap_owner bitmap(raw,quantapdf_drop_bitmap);
            if(status!=QUANTAPDF_OK)std::fprintf(stderr,"fractional curved Hello dpi=%g unit=%u/4 rotate=%d display=%u status=%d\n",double(float(c.dpi)),c.quarters,c.rotation,display,int(status));
            CHECK(status==QUANTAPDF_OK && bitmap!=nullptr);const unsigned char *pixels=nullptr;size_t size=0u;
            int actual_w=0,actual_h=0,stride=0,components=0;OK(quantapdf_bitmap_dimensions(bitmap.get(),&actual_w,&actual_h,&stride,&components));
            const bool display_swap=display==90u || display==270u;
            CHECK(actual_w==int(display_swap?h:w) && actual_h==int(display_swap?w:h) && stride==actual_w*3 && components==3);
            OK(quantapdf_bitmap_data(bitmap.get(),&pixels,&size));CHECK(size==reference.size() && std::memcmp(pixels,reference.data(),size)==0);
            held.push_back(std::move(bitmap));references.push_back(reference);if(display==0u)++renders;else ++turns;
        }
        selected.reset();
        for(size_t i=0u;i<held.size();++i) {
            const unsigned char *pixels=nullptr;size_t size=0u;OK(quantapdf_bitmap_data(held[i].get(),&pixels,&size));
            CHECK(size==references[i].size() && std::memcmp(pixels,references[i].data(),size)==0);
        }
    }
    CHECK(renders==24u && turns==72u);
    std::puts("fractional DPI original quadratic Hello: 24 independent RGB and 72 display turns, original Rotate/UserUnit, inherited signed origin, encryption and detached Bitmaps");
    return EXIT_SUCCESS;
}
#endif
