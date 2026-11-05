#ifndef QUANTAPDF_NATIVE_DECIMAL_PAGE_CASES_H
#define QUANTAPDF_NATIVE_DECIMAL_PAGE_CASES_H
#include "native_ttf_fixture.h"
#include <memory>

static std::string decimal_literal_original(unsigned kind,bool encrypted)
{
    using O=QPDFObjectHandle;
    const auto base=native_ttf_fixture();auto pdf=QPDF::create();
    pdf->processMemoryFile("ordinary decimal literal",base.data(),base.size());
    auto root=pdf->getRoot().getKey("/Pages"),page=root.getKey("/Kids").getArrayItem(0);
    auto resources=page.getKey("/Resources");
    auto image=pdf->newStream(std::string("\x99\x33\xcc",3));auto d=image.getDict();
    d.replaceKey("/Type",O::newName("/XObject"));d.replaceKey("/Subtype",O::newName("/Image"));
    d.replaceKey("/Width",O::newInteger(1));d.replaceKey("/Height",O::newInteger(1));
    d.replaceKey("/BitsPerComponent",O::newInteger(8));d.replaceKey("/ColorSpace",O::newName("/DeviceRGB"));
    auto xobjects=O::newDictionary();xobjects.replaceKey("/I",image);resources.replaceKey("/XObject",xobjects);
    std::string contents=kind==1u?"q BT /F 24 Tf 12 20 Td <002d004a005100510054> Tj ET Q ":"";
    if(kind!=0u)contents+="q 12 0 0 12 20 16 cm /I Do Q q 0 1 0 rg 12 20 7 10 re f Q ";
    if(kind==2u)contents+="q 0 0 1 rg 14 22 12 8 re f Q ";
    auto geometry=encrypted?root:page;geometry.replaceKey("/MediaBox",O::parse("[0 0 237.133 180.44]"));
    if(encrypted) {page.removeKey("/MediaBox");root.replaceKey("/Resources",resources);page.removeKey("/Resources");}
    page.replaceKey("/Contents",pdf->newStream(contents+"\n"));
    QPDFWriter writer(*pdf);writer.setOutputMemory();
    if(encrypted)writer.setR6EncryptionParameters("reader","owner",true,true,true,true,true,true,qpdf_r3p_full,true);
    writer.write();const auto bytes=writer.getBufferSharedPointer();
    return {reinterpret_cast<const char*>(bytes->getBuffer()),bytes->getSize()};
}
static int decimal_literal_cases(const std::string& filename)
{
    size_t renders=0u,turns=0u;
    for(unsigned kind=0u;kind<3u;++kind)for(bool encrypted:{false,true}) {
        const auto original=decimal_literal_original(kind,encrypted);CHECK(write_pdf(filename,original));
        quantapdf_document *raw_document=nullptr;
        if(encrypted)CHECK(quantapdf_open(filename.c_str(),"wrong",&raw_document)==QUANTAPDF_ERROR_PASSWORD && raw_document==nullptr);
        OK(quantapdf_open(filename.c_str(),encrypted?"reader":nullptr,&raw_document));
        std::unique_ptr<quantapdf_document,decltype(&quantapdf_close)> document(raw_document,quantapdf_close);
        CHECK(std::remove(filename.c_str())==0);quantapdf_page *raw_page=nullptr;OK(quantapdf_load_page(document.get(),0,&raw_page));
        std::unique_ptr<quantapdf_page,decltype(&quantapdf_drop_page)> page(raw_page,quantapdf_drop_page);document.reset();
        quantapdf_rect bounds{};OK(quantapdf_page_bounds(page.get(),&bounds));
        CHECK(std::abs(bounds.x1-237.133)<1e-12 && std::abs(bounds.y1-180.44)<1e-12);
        std::vector<std::unique_ptr<quantapdf_bitmap,decltype(&quantapdf_drop_bitmap)>> held;
        std::vector<std::vector<unsigned char>> references;
        for(unsigned dpi:{72u,96u,144u}) {
            const size_t w=(237133u*dpi+71999u)/72000u,h=(180440u*dpi+71999u)/72000u;
            std::vector<unsigned char> expected(3u*w*h,255u);
            if(kind!=0u) {
                const auto name=std::string(kind==1u?"roboto-decimal-":"roboto-decimal-shapes-")+std::to_string(dpi)+"-m0.rgba.z";
                std::ifstream in(std::filesystem::path(__FILE__).parent_path()/"goldens"/name,std::ios::binary);
                const std::vector<unsigned char> encoded{std::istreambuf_iterator<char>(in),{}};
                std::vector<unsigned char> rgba(4u*w*h);uLongf decoded=static_cast<uLongf>(rgba.size());
                CHECK(uncompress(rgba.data(),&decoded,encoded.data(),static_cast<uLong>(encoded.size()))==Z_OK && decoded==rgba.size());
                for(size_t i=0u;i<w*h;++i) {CHECK(rgba[4u*i+3u]==255u);std::copy_n(rgba.data()+4u*i,3u,expected.data()+3u*i);}
            }
            for(unsigned display:{0u,90u,180u,270u}) {
                if(display!=0u && (encrypted || dpi!=96u))continue;
                const auto reference=native_page_text::turn(expected,w,h,display);
                const bool swap=display==90u || display==270u;const size_t rw=swap?h:w,rh=swap?w:h;
                const quantapdf_render_options options{float(dpi),float(display),0,{},0};quantapdf_bitmap *raw=nullptr;
                const auto status=quantapdf_render_page_with_options(page.get(),&options,&raw);
                std::unique_ptr<quantapdf_bitmap,decltype(&quantapdf_drop_bitmap)> bitmap(raw,quantapdf_drop_bitmap);
                if(status!=QUANTAPDF_OK)std::fprintf(stderr,"decimal literal kind=%u dpi=%u status=%d\n",kind,dpi,int(status));
                CHECK(status==QUANTAPDF_OK && bitmap!=nullptr);
                int width=0,height=0,stride=0,channels=0;const unsigned char *pixels=nullptr;size_t size=0u;
                OK(quantapdf_bitmap_dimensions(bitmap.get(),&width,&height,&stride,&channels));OK(quantapdf_bitmap_data(bitmap.get(),&pixels,&size));
                CHECK(width==int(rw) && height==int(rh) && stride==3*width && channels==3 && size==reference.size());
                if(std::memcmp(pixels,reference.data(),size)!=0) {
                    for(size_t i=0u;i<size;++i)if(pixels[i]!=reference[i]) {
                        std::fprintf(stderr,"decimal literal RGB kind=%u dpi=%u display=%u byte=%zu got=%u expected=%u\n",kind,dpi,display,i,unsigned(pixels[i]),unsigned(reference[i]));break;
                    }
                    return EXIT_FAILURE;
                }
                held.push_back(std::move(bitmap));references.push_back(reference);if(display==0u)++renders;else ++turns;
            }
        }
        page.reset();
        for(size_t i=0u;i<held.size();++i) {
            const unsigned char *p=nullptr;size_t n=0u;OK(quantapdf_bitmap_data(held[i].get(),&p,&n));
            CHECK(n==references[i].size() && std::memcmp(p,references[i].data(),n)==0);
        }
    }
    CHECK(renders==18u && turns==9u);
    std::puts("Ordinary 237.133 x 180.44: 18 independent RGB, 9 display turns; blank, curved Text/Image/Fill and three non-Text paints");
    return EXIT_SUCCESS;
}
#endif
