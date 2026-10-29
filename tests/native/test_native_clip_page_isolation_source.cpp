/* Original-PDF multi-page Clip isolation on ONE open native Document.
 * Alternate accepted sibling, unclipped, empty, disjoint and blank pages
 * with a rejected Text page. No page may inherit another page's Clip,
 * Source state, decoded owner or partially published output. */
#include <quantapdf/quantapdf.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define CHECK(x) do { if(!(x)) { \
    std::fprintf(stderr,"native Clip page isolation:%d: %s\n", \
                 __LINE__,#x); return EXIT_FAILURE; \
} } while(0)
#define OK(x) CHECK((x)==QUANTAPDF_OK)

using Obj=QPDFObjectHandle;
static const char *const filename="native-clip-page-isolation.pdf";

struct rendered {
    int width=0,height=0;
    std::vector<unsigned char> rgb;
};

static const std::string full_red="1 0 0 rg 0 0 16 12 re f ";
static const std::string full_blue="0 0 1 rg 0 0 16 12 re f* ";
static const std::string full_green="0 1 0 rg 0 0 16 12 re f ";

static std::string scope(const std::string& clip,
                         const std::string& draw,bool even=false)
{
    return "q "+clip+(even?" W* n ":" W n ")+draw+"Q ";
}

static std::string original()
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto pages=pdf->getRoot().getKey("/Pages");
    /* Inherited MediaBox/Resources are resolved independently for each
     * selected leaf, rather than copied into every page dictionary. */
    pages.replaceKey("/MediaBox",Obj::parse("[0 0 16 12]"));
    pages.replaceKey("/Resources",Obj::newDictionary());
    const std::vector<std::vector<std::string>> streams={
        {scope("2 2 8 7 re",full_red),
         scope("6 4 8 6 re",full_blue)},
        {full_green},
        {"BT ET"}, /* Actual unsupported page, not a blank stand-in. */
        {""},
        {scope("",full_red),scope("",full_blue,true)},
        {scope("1 1 2 2 re",full_red),
         scope("12 9 2 2 re",full_blue,true)}
    };
    std::vector<Obj> kids;
    for(const auto& content:streams) {
        auto page=pdf->makeIndirectObject(
            Obj::parse("<< /Type /Page >>"));
        page.replaceKey("/Parent",pages);
        if(content.size()==1u)
            page.replaceKey("/Contents",pdf->newStream(content[0]));
        else {
            std::vector<Obj> parts;
            for(const auto& bytes:content)
                parts.push_back(pdf->newStream(bytes));
            page.replaceKey("/Contents",Obj::newArray(parts));
        }
        kids.push_back(page);
    }
    pages.replaceKey("/Kids",Obj::newArray(kids));
    pages.replaceKey("/Count",Obj::newInteger(6));
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    const auto bytes=writer.getBufferSharedPointer();
    return std::string(
        reinterpret_cast<const char *>(bytes->getBuffer()),
        bytes->getSize());
}

static bool write_pdf(const std::string& bytes)
{
    FILE *f=nullptr;
#ifdef _WIN32
    if(fopen_s(&f,filename,"wb")!=0)return false;
#else
    f=std::fopen(filename,"wb");
#endif
    if(f==nullptr)return false;
    const bool written=std::fwrite(bytes.data(),1u,bytes.size(),f)
                       ==bytes.size();
    return std::fclose(f)==0 && written;
}

static uint32_t pixel(const rendered& image,int x,int y)
{
    const size_t i=(static_cast<size_t>(y)*
        static_cast<size_t>(image.width)+static_cast<size_t>(x))*3u;
    return (static_cast<uint32_t>(image.rgb.at(i))<<16u) |
           (static_cast<uint32_t>(image.rgb.at(i+1u))<<8u) |
            static_cast<uint32_t>(image.rgb.at(i+2u));
}

static bool solid(const rendered& image,uint32_t color)
{
    if(image.rgb.empty() || image.rgb.size()%3u!=0u)return false;
    for(size_t i=0;i<image.rgb.size();i+=3u) {
        const uint32_t rgb=(static_cast<uint32_t>(image.rgb[i])<<16u) |
                           (static_cast<uint32_t>(image.rgb[i+1u])<<8u) |
                            static_cast<uint32_t>(image.rgb[i+2u]);
        if(rgb!=color)return false;
    }
    return true;
}

static bool expected(const rendered& r,int index,int dpi)
{
    if(r.width!=(16*dpi+71)/72 || r.height!=(12*dpi+71)/72 ||
       r.rgb.size()!=static_cast<size_t>(r.width)*
                     static_cast<size_t>(r.height)*3u)
        return false;
    if(index==1) {
        if(dpi!=96)return solid(r,UINT32_C(0x00ff00));
        /* At 96 DPI 16pt becomes 21+1/3 physical pixels. The last
         * device column is necessarily partially covered by the
         * original MediaBox viewport, not solid green. */
        for(int y=0;y<r.height;++y)
            for(int x=0;x<r.width-1;++x)
                if(pixel(r,x,y)!=UINT32_C(0x00ff00))return false;
        const uint32_t edge=pixel(r,r.width-1,r.height/2);
        const unsigned a=(edge>>16u)&255u,g=(edge>>8u)&255u,b=edge&255u;
        return a>0u && a<255u && g==255u && b==a;
    }
    if(index==3 || index==4)return solid(r,UINT32_C(0xffffff));
    if(index!=0 && index!=5)return false;
    if(dpi==72) {
        if(index==0)
            return pixel(r,3,8)==UINT32_C(0xff0000) &&
                   pixel(r,12,5)==UINT32_C(0x0000ff) &&
                   pixel(r,0,0)==UINT32_C(0xffffff);
        return pixel(r,2,10)==UINT32_C(0xff0000) &&
               pixel(r,13,2)==UINT32_C(0x0000ff) &&
               pixel(r,8,5)==UINT32_C(0xffffff);
    }
    return !solid(r,UINT32_C(0xffffff));
}

static int page_isolation()
{
    CHECK(write_pdf(original()));
    quantapdf_context *context=nullptr;
    quantapdf_document *doc=nullptr;
    quantapdf_bitmap *held=nullptr;
    std::vector<unsigned char> held_copy;
    OK(quantapdf_context_create(&context));
    OK(quantapdf_open_with_context(context,filename,nullptr,&doc));
    CHECK(std::remove(filename)==0);

    /* Rejected pages occur first and repeatedly between valid pages.
     * Split Content streams cross original Q restoration boundaries.
     * Revisit every accepted page with one live Context and Document. */
    const int order[]={2,0,1,4,5,3,0,2,5,4,1,3,0};
    size_t positives=0u,refusals=0u;
    for(int dpi:{72,96,144}) {
        std::array<rendered,6> earlier{};
        std::array<bool,6> visited{};
        for(const int index:order) {
            quantapdf_page *page=nullptr;
            quantapdf_bitmap *bitmap=
                reinterpret_cast<quantapdf_bitmap *>(uintptr_t(1));
            OK(quantapdf_load_page(doc,index,&page));
            quantapdf_render_options options{};
            options.struct_size=sizeof(options);
            options.dpi=static_cast<float>(dpi);
            const quantapdf_status status=
                quantapdf_render_page_with_options(page,&options,&bitmap);
            if(index==2) {
                CHECK(status!=QUANTAPDF_OK && bitmap==nullptr);
                ++refusals;
            } else {
                CHECK(status==QUANTAPDF_OK && bitmap!=nullptr);
                rendered current;
                int stride=0,channels=0;
                const unsigned char *data=nullptr;
                size_t count=0u;
                OK(quantapdf_bitmap_dimensions(bitmap,
                    &current.width,&current.height,&stride,&channels));
                OK(quantapdf_bitmap_data(bitmap,&data,&count));
                CHECK(stride==current.width*3 && channels==3 &&
                      data!=nullptr &&
                      count==static_cast<size_t>(stride)*
                             static_cast<size_t>(current.height));
                current.rgb.assign(data,data+count);
                if(!expected(current,index,dpi)) {
                    const uint32_t middle=pixel(current,
                        current.width/2,current.height/2);
                    const uint32_t edge=pixel(current,
                        current.width-1,current.height/2);
                    std::fprintf(stderr,"page pixel mismatch: index=%d "
                        "dpi=%d dims=%dx%d middle=%06x edge=%06x\n",
                        index,dpi,current.width,current.height,
                        static_cast<unsigned>(middle),
                        static_cast<unsigned>(edge));
                    CHECK(false);
                }
                if(visited[static_cast<size_t>(index)])
                    CHECK(earlier[static_cast<size_t>(index)].width==
                          current.width &&
                          earlier[static_cast<size_t>(index)].height==
                          current.height &&
                          earlier[static_cast<size_t>(index)].rgb==current.rgb);
                else {
                    earlier[static_cast<size_t>(index)]=current;
                    visited[static_cast<size_t>(index)]=true;
                }
                if(held==nullptr) {
                    held=bitmap;
                    held_copy=current.rgb;
                } else quantapdf_drop_bitmap(bitmap);
                ++positives;
            }
            quantapdf_drop_page(page);
            if(held!=nullptr) {
                const unsigned char *retained=nullptr;
                size_t retained_count=0u;
                OK(quantapdf_bitmap_data(held,&retained,&retained_count));
                CHECK(retained!=nullptr && retained_count==held_copy.size() &&
                      std::memcmp(retained,held_copy.data(),retained_count)==0);
            }
        }
        CHECK(visited[0] && visited[1] && !visited[2] &&
              visited[3] && visited[4] && visited[5]);
    }
    quantapdf_close(doc);
    quantapdf_drop_context(context);
    CHECK(held!=nullptr);
    const unsigned char *retained=nullptr;
    size_t count=0u;
    OK(quantapdf_bitmap_data(held,&retained,&count));
    CHECK(retained!=nullptr && count==held_copy.size() &&
          std::memcmp(retained,held_copy.data(),count)==0);
    quantapdf_drop_bitmap(held);
    CHECK(positives==33u && refusals==6u);
    std::printf("Native Clip page isolation: %zu rendered, "
                "%zu atomic refusals, one retired Document\n",
                positives,refusals);
    return EXIT_SUCCESS;
}

int main()
{
    return page_isolation();
}
