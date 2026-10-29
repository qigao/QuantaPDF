/* Original PDF resource ancestry and fail-closed page isolation.
 *
 * One 12-page document owns shared indirect Image and /Resources objects.
 * Pages resolve nearest inherited or locally overridden /XObject entries;
 * broken dictionaries, unresolved names, invalid image entries, default
 * color-space overrides, and late unsupported Text all refuse atomically.
 *
 * The production Source V10 and sole Native C11 renderer are unchanged. */
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

#define CHECK(e) do { if(!(e)) { \
    std::fprintf(stderr,"native resource ancestry:%d: %s\n", \
                 __LINE__,#e); return EXIT_FAILURE; \
} } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)

using Obj=QPDFObjectHandle;
static const char *const filename="native-source-resource-ancestry.pdf";
static constexpr size_t page_count=12u;

struct rendered {
    int width=0,height=0;
    std::vector<unsigned char> rgb;
};

static Obj image(QPDF& pdf,unsigned char r,unsigned char g,unsigned char b)
{
    std::string data(3,'\0');
    data[0]=static_cast<char>(r);
    data[1]=static_cast<char>(g);
    data[2]=static_cast<char>(b);
    auto stream=pdf.newStream(data);
    auto dict=stream.getDict();
    dict.replaceKey("/Type",Obj::newName("/XObject"));
    dict.replaceKey("/Subtype",Obj::newName("/Image"));
    dict.replaceKey("/Width",Obj::newInteger(1));
    dict.replaceKey("/Height",Obj::newInteger(1));
    dict.replaceKey("/BitsPerComponent",Obj::newInteger(8));
    dict.replaceKey("/ColorSpace",Obj::newName("/DeviceRGB"));
    return stream;
}

/* Keep both levels indirect. PageTree branches and leaves must continue
 * to refer to the SAME dictionary and IMAGE identities after writing. */
static Obj resource(QPDF& pdf,Obj named)
{
    auto xobjects=pdf.makeIndirectObject(Obj::newDictionary());
    xobjects.replaceKey("/I",named);
    auto resources=pdf.makeIndirectObject(Obj::newDictionary());
    resources.replaceKey("/XObject",xobjects);
    return resources;
}

static std::string source_pdf()
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    root.replaceKey("/MediaBox",Obj::parse("[0 0 16 12]"));

    auto red=image(*pdf,255u,0u,0u);
    auto blue=image(*pdf,0u,0u,255u);
    auto green=image(*pdf,0u,255u,0u);
    auto root_resources=resource(*pdf,red);
    auto blue_resources=resource(*pdf,blue);
    auto branch_resources=resource(*pdf,green);
    root.replaceKey("/Resources",root_resources);

    auto branch=pdf->makeIndirectObject(Obj::parse("<< /Type /Pages >>"));
    branch.replaceKey("/Parent",root);
    branch.replaceKey("/Resources",branch_resources);

    const std::string draw="q 16 0 0 12 0 0 cm /I Do Q ";
    const std::string clipped=
        "q 2 2 10 8 re W n "+draw+"Q ";
    const std::array<std::string,page_count> content={{
        clipped,                            /* 0 inherited root red */
        clipped,                            /* 1 local /I blue */
        draw,                               /* 2 inherited red full page */
        "q 16 0 0 12 0 0 cm /Missing Do Q ", /* 3 missing name */
        draw,                               /* 4 local empty /XObject */
        clipped,                            /* 5 nested green */
        clipped+" BT ET",                    /* 6 late unsupported Text */
        draw,                               /* 7 nested green full page */
        draw,                               /* 8 /Resources is Integer */
        draw,                               /* 9 /XObject is Integer */
        draw,                               /* 10 forbidden /DefaultRGB */
        draw                                /* 11 /I is not an Image */
    }};

    std::vector<Obj> root_kids,branch_kids;
    for(size_t i=0u;i<page_count;++i) {
        auto page=pdf->makeIndirectObject(Obj::parse("<< /Type /Page >>"));
        const bool nested=i>=5u && i<=7u;
        page.replaceKey("/Parent",nested?branch:root);
        page.replaceKey("/Contents",pdf->newStream(content[i]));
        if(i==1u)page.replaceKey("/Resources",blue_resources);
        if(i==4u) {
            /* Nearest dictionary shadows the root; no key merging. */
            auto resources=pdf->makeIndirectObject(Obj::newDictionary());
            resources.replaceKey("/XObject",Obj::newDictionary());
            page.replaceKey("/Resources",resources);
        }
        if(i==8u)page.replaceKey("/Resources",Obj::newInteger(5));
        if(i==9u) {
            auto resources=Obj::newDictionary();
            resources.replaceKey("/XObject",Obj::newInteger(7));
            page.replaceKey("/Resources",resources);
        }
        if(i==10u) {
            auto resources=Obj::newDictionary();
            resources.replaceKey("/ColorSpace",Obj::parse(
                "<< /DefaultRGB /DeviceGray >>"));
            page.replaceKey("/Resources",resources);
        }
        if(i==11u)page.replaceKey(
            "/Resources",resource(*pdf,Obj::newInteger(13)));
        if(nested)branch_kids.push_back(page);
        else root_kids.push_back(page);
        if(i==5u)root_kids.push_back(branch);
    }
    branch.replaceKey("/Kids",Obj::newArray(branch_kids));
    branch.replaceKey("/Count",Obj::newInteger(3));
    root.replaceKey("/Kids",Obj::newArray(root_kids));
    root.replaceKey("/Count",Obj::newInteger(
        static_cast<int>(page_count)));

    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    auto encoded=writer.getBufferSharedPointer();
    return std::string(
        reinterpret_cast<const char *>(encoded->getBuffer()),
        encoded->getSize());
}

static bool write_original(const std::string& bytes)
{
    FILE *file=nullptr;
#ifdef _WIN32
    if(fopen_s(&file,filename,"wb")!=0)return false;
#else
    file=std::fopen(filename,"wb");
#endif
    if(file==nullptr)return false;
    const bool written=std::fwrite(bytes.data(),1u,bytes.size(),file)
                       ==bytes.size();
    return std::fclose(file)==0 && written;
}

static uint32_t pixel(const rendered& r,int x,int y)
{
    const size_t offset=(static_cast<size_t>(y)*
        static_cast<size_t>(r.width)+static_cast<size_t>(x))*3u;
    return (static_cast<uint32_t>(r.rgb.at(offset))<<16u) |
           (static_cast<uint32_t>(r.rgb.at(offset+1u))<<8u) |
            static_cast<uint32_t>(r.rgb.at(offset+2u));
}

static bool expected(const rendered& r,size_t index,int dpi)
{
    if(r.width!=(16*dpi+71)/72 || r.height!=(12*dpi+71)/72 ||
       r.rgb.size()!=static_cast<size_t>(r.width)*
                     static_cast<size_t>(r.height)*3u)
        return false;
    const uint32_t color=(index==1u)?UINT32_C(0x0000ff):
                         (index==5u || index==7u)?
                          UINT32_C(0x00ff00):UINT32_C(0xff0000);
    const uint32_t white=UINT32_C(0xffffff);
    const bool clipped=index==0u || index==1u || index==5u;
    if(pixel(r,r.width/2,r.height/2)!=color ||
       pixel(r,2,2)!=(clipped?white:color))
        return false;
    if(clipped) {
        if(pixel(r,0,0)!=white || pixel(r,r.width-2,1)!=white)
            return false;
    } else {
        /* Interior and the penultimate full-coverage column must
         * retain the original Image's RGB across 72/96/144 DPI.
         * At 96 DPI, the fractional last column is viewport-masked. */
        if(pixel(r,r.width-2,r.height/2)!=color)return false;
    }
    return true;
}

static bool is_accepted(int index)
{
    return index==0 || index==1 || index==2 ||
           index==5 || index==7;
}

static quantapdf_status refusal_status(int index)
{
    /* Format means the reference/type is ill-formed; valid but
     * unsupported operators and color defaults have distinct status. */
    return (index==6 || index==10)?
        QUANTAPDF_ERROR_UNSUPPORTED:QUANTAPDF_ERROR_FORMAT;
}

static int qualify()
{
    CHECK(write_original(source_pdf()));
    quantapdf_context *context=nullptr;
    quantapdf_document *document=nullptr;
    quantapdf_bitmap *held=nullptr;
    std::vector<unsigned char> held_bytes;
    OK(quantapdf_context_create(&context));
    OK(quantapdf_open_with_context(context,filename,nullptr,&document));
    CHECK(std::remove(filename)==0);

    /* Revisit shared, overridden and malformed resources repeatedly,
     * deliberately starting with a failed page. All visits share one
     * live Document; no replacement/open retry hides leaked state. */
    const int order[]={
        3,0,5,1,4,2,6,7,8,0,9,5,10,1,11,7,2,3,4,5,0
    };
    size_t positives=0u,refusals=0u;
    for(int dpi:{72,96,144}) {
        std::array<rendered,page_count> earlier{};
        std::array<bool,page_count> visited{};
        for(const int index:order) {
            quantapdf_page *page=nullptr;
            quantapdf_bitmap *bitmap=
                reinterpret_cast<quantapdf_bitmap *>(uintptr_t(1));
            OK(quantapdf_load_page(document,index,&page));
            quantapdf_render_options options{};
            options.struct_size=sizeof(options);
            options.dpi=static_cast<float>(dpi);
            const quantapdf_status status=
                quantapdf_render_page_with_options(page,&options,&bitmap);
            if(!is_accepted(index)) {
                if(status!=refusal_status(index) || bitmap!=nullptr) {
                    std::fprintf(stderr,"resource refusal: page=%d dpi=%d "
                        "status=%d expected=%d bitmap=%p\n",index,dpi,
                        static_cast<int>(status),
                        static_cast<int>(refusal_status(index)),
                        static_cast<void *>(bitmap));
                    CHECK(false);
                }
                ++refusals;
            } else {
                if(status!=QUANTAPDF_OK || bitmap==nullptr) {
                    std::fprintf(stderr,"resource positive rejected: "
                        "page=%d dpi=%d status=%d\n",
                        index,dpi,static_cast<int>(status));
                    CHECK(false);
                }
                rendered actual;
                int stride=0,channels=0;
                const unsigned char *bytes=nullptr;
                size_t count=0u;
                OK(quantapdf_bitmap_dimensions(bitmap,
                    &actual.width,&actual.height,&stride,&channels));
                OK(quantapdf_bitmap_data(bitmap,&bytes,&count));
                CHECK(stride==actual.width*3 && channels==3 &&
                      bytes!=nullptr &&
                      count==static_cast<size_t>(stride)*
                             static_cast<size_t>(actual.height));
                actual.rgb.assign(bytes,bytes+count);
                if(!expected(actual,static_cast<size_t>(index),dpi)) {
                    std::fprintf(stderr,"resource pixel mismatch: "
                        "page=%d dpi=%d dims=%dx%d centre=%06x\n",
                        index,dpi,actual.width,actual.height,
                        static_cast<unsigned>(pixel(actual,
                            actual.width/2,actual.height/2)));
                    CHECK(false);
                }
                const size_t selected=static_cast<size_t>(index);
                if(visited[selected]) {
                    CHECK(earlier[selected].width==actual.width &&
                          earlier[selected].height==actual.height &&
                          earlier[selected].rgb==actual.rgb);
                } else {
                    visited[selected]=true;
                    earlier[selected]=actual;
                }
                if(held==nullptr) {
                    held=bitmap;
                    held_bytes=actual.rgb;
                } else quantapdf_drop_bitmap(bitmap);
                ++positives;
            }
            quantapdf_drop_page(page);
            if(held!=nullptr) {
                const unsigned char *still=nullptr;
                size_t count=0u;
                OK(quantapdf_bitmap_data(held,&still,&count));
                CHECK(still!=nullptr && count==held_bytes.size() &&
                      std::memcmp(still,held_bytes.data(),count)==0);
            }
        }
        for(int i:{0,1,2,5,7})
            CHECK(visited[static_cast<size_t>(i)]);
    }
    quantapdf_close(document);
    quantapdf_drop_context(context);
    CHECK(held!=nullptr);
    const unsigned char *still=nullptr;
    size_t count=0u;
    OK(quantapdf_bitmap_data(held,&still,&count));
    CHECK(still!=nullptr && count==held_bytes.size() &&
          std::memcmp(still,held_bytes.data(),count)==0);
    quantapdf_drop_bitmap(held);
    CHECK(positives==36u && refusals==27u);
    std::printf("Native Source resource ancestry: %zu renders, "
                "%zu format/unsupported atomic refusals\n",
                positives,refusals);
    return EXIT_SUCCESS;
}

int main()
{
    return qualify();
}
