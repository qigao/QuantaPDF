/* Original-PDF MediaBox/CropBox geometry ancestry and atomic failures.
 * An immutable 20-page PDF interleaves valid local/parent box overrides,
 * negative origins, fractional CropBoxes, rotated and scaled pages with
 * malformed or outside-V10 geometry. No producer, limit or ABI changes. */
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
    std::fprintf(stderr,"native page geometry ancestry:%d: %s\n", \
                 __LINE__,#e); return EXIT_FAILURE; \
} } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)

using Obj=QPDFObjectHandle;
static const char *const filename="native-page-geometry-ancestry.pdf";
static constexpr size_t page_count=20u;
static const std::string green="0 1 0 rg 0 0 16 12 re f ";

struct rendered {
    int width=0,height=0;
    std::vector<unsigned char> rgb;
};
struct expected_geometry {
    unsigned width_quarters=0u,height_quarters=0u,user_quarters=4u;
};
static constexpr std::array<expected_geometry,page_count> geometry={{
    {64u,48u,4u}, /*  0 inherited root MediaBox */
    {32u,24u,4u}, /*  1 local CropBox */
    {40u,32u,4u}, /*  2 branch inherited CropBox */
    {32u,24u,4u}, /*  3 local override of branch CropBox */
    {64u,48u,4u}, /*  4 original negative MediaBox origin */
    {42u,35u,4u}, /*  5 fractional CropBox edges */
    {64u,48u,4u}, /*  6 exact original Rotate 180 */
    {64u,48u,5u}, /*  7 original /UserUnit 1.25 */
    {},{},{},{},{},{},{},{}, /*  8..15 invalid */
    {},             /* 16 invalid branch inherited CropBox */
    {32u,24u,4u},  /* 17 local valid CropBox shadows invalid parent */
    {},             /* 18 invalid branch inherited MediaBox */
    {64u,48u,4u}   /* 19 local valid MediaBox shadows invalid parent */
}};

static bool accepted(int i)
{
    return i>=0 && i<static_cast<int>(page_count) &&
           geometry[static_cast<size_t>(i)].width_quarters!=0u;
}
static quantapdf_status expected_refusal(int i)
{
    /* V10 exact quarter lattice and physical UserUnit lease are separate
     * strict Unsupported conditions; malformed/empty boxes and Rotate
     * are Format errors in the existing public QPDF geometry reader. */
    return i==12 || i==15 ?
        QUANTAPDF_ERROR_UNSUPPORTED:QUANTAPDF_ERROR_FORMAT;
}

static std::string original()
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    root.replaceKey("/MediaBox",Obj::parse("[0 0 16 12]"));

    auto crop_branch=pdf->makeIndirectObject(Obj::parse(
        "<< /Type /Pages /CropBox [3 2 13 10] >>"));
    auto bad_crop_branch=pdf->makeIndirectObject(Obj::parse(
        "<< /Type /Pages /CropBox [1 1 1 10] >>"));
    auto bad_media_branch=pdf->makeIndirectObject(Obj::parse(
        "<< /Type /Pages /MediaBox [0 0 0 12] >>"));
    crop_branch.replaceKey("/Parent",root);
    bad_crop_branch.replaceKey("/Parent",root);
    bad_media_branch.replaceKey("/Parent",root);

    std::vector<Obj> kids,crop_kids,bad_crop_kids,bad_media_kids;
    for(size_t i=0u;i<page_count;++i) {
        const int group=(i==2u || i==3u)?1:
                        (i==16u || i==17u)?2:
                        (i==18u || i==19u)?3:0;
        Obj parent=group==1?crop_branch:
                   group==2?bad_crop_branch:
                   group==3?bad_media_branch:root;
        if(i==2u)kids.push_back(crop_branch);
        if(i==16u)kids.push_back(bad_crop_branch);
        if(i==18u)kids.push_back(bad_media_branch);
        auto page=pdf->makeIndirectObject(
            Obj::parse("<< /Type /Page >>"));
        page.replaceKey("/Parent",parent);
        page.replaceKey("/Contents",pdf->newStream(i==4u ?
            "0 1 0 rg -4 -3 16 12 re f " : green));

        switch(i) {
        case 1u:case 3u:case 17u:
            page.replaceKey("/CropBox",Obj::parse("[4 3 12 9]"));
            break;
        case 4u:
            page.replaceKey("/MediaBox",Obj::parse("[-4 -3 12 9]"));
            break;
        case 5u:
            page.replaceKey("/CropBox",Obj::parse(
                "[2.75 1.5 13.25 10.25]"));
            break;
        case 6u:
            page.replaceKey("/Rotate",Obj::newInteger(180));
            break;
        case 7u:
            page.replaceKey("/UserUnit",Obj::parse("1.25"));
            break;
        case 8u:
            page.replaceKey("/MediaBox",Obj::parse("[0 0 0 12]"));
            break;
        case 9u:
            page.replaceKey("/MediaBox",Obj::newInteger(7));
            break;
        case 10u:
            page.replaceKey("/CropBox",Obj::parse("[2 2 2 8]"));
            break;
        case 11u:
            page.replaceKey("/CropBox",Obj::parse("[30 30 40 40]"));
            break;
        case 12u:
            page.replaceKey("/MediaBox",Obj::parse("[0 0 16.1 12]"));
            break;
        case 13u:
            page.replaceKey("/Rotate",Obj::newInteger(45));
            break;
        case 14u:
            page.replaceKey("/UserUnit",Obj::newInteger(0));
            break;
        case 15u:
            page.replaceKey("/UserUnit",Obj::parse("4.25"));
            break;
        case 19u:
            page.replaceKey("/MediaBox",Obj::parse("[0 0 16 12]"));
            break;
        default:break;
        }
        if(group==1)crop_kids.push_back(page);
        else if(group==2)bad_crop_kids.push_back(page);
        else if(group==3)bad_media_kids.push_back(page);
        else kids.push_back(page);
    }

    crop_branch.replaceKey("/Kids",Obj::newArray(crop_kids));
    crop_branch.replaceKey("/Count",Obj::newInteger(2));
    bad_crop_branch.replaceKey("/Kids",Obj::newArray(bad_crop_kids));
    bad_crop_branch.replaceKey("/Count",Obj::newInteger(2));
    bad_media_branch.replaceKey("/Kids",Obj::newArray(bad_media_kids));
    bad_media_branch.replaceKey("/Count",Obj::newInteger(2));
    root.replaceKey("/Kids",Obj::newArray(kids));
    root.replaceKey("/Count",Obj::newInteger(
        static_cast<int>(page_count)));

    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    auto buffer=writer.getBufferSharedPointer();
    return std::string(
        reinterpret_cast<const char *>(buffer->getBuffer()),
        buffer->getSize());
}

static bool write_original(const std::string& data)
{
    FILE *file=nullptr;
#ifdef _WIN32
    if(fopen_s(&file,filename,"wb")!=0)return false;
#else
    file=std::fopen(filename,"wb");
#endif
    if(file==nullptr)return false;
    const bool written=std::fwrite(data.data(),1u,data.size(),file)==
                       data.size();
    return std::fclose(file)==0 && written;
}

static uint32_t pixel(const rendered& image,int x,int y)
{
    const size_t i=(static_cast<size_t>(y)*
        static_cast<size_t>(image.width)+static_cast<size_t>(x))*3u;
    return (static_cast<uint32_t>(image.rgb.at(i))<<16u) |
           (static_cast<uint32_t>(image.rgb.at(i+1u))<<8u) |
            static_cast<uint32_t>(image.rgb.at(i+2u));
}
static bool expected_pixels(const rendered& image,int index,int dpi)
{
    const auto& geo=geometry[static_cast<size_t>(index)];
    const unsigned w=(geo.width_quarters*geo.user_quarters*
                      static_cast<unsigned>(dpi)+1151u)/1152u;
    const unsigned h=(geo.height_quarters*geo.user_quarters*
                      static_cast<unsigned>(dpi)+1151u)/1152u;
    if(image.width!=static_cast<int>(w) ||
       image.height!=static_cast<int>(h) ||
       image.rgb.size()!=static_cast<size_t>(w)*
                          static_cast<size_t>(h)*3u)
        return false;
    /* The original source rectangle paints the entire effective Media
     * intersection. Fractional final rows/columns may be antialiased,
     * so assert fully covered interiors, not invented boundary colors. */
    const uint32_t green_rgb=UINT32_C(0x00ff00);
    return pixel(image,1,1)==green_rgb &&
           pixel(image,image.width/2,image.height/2)==green_rgb;
}

static int qualify()
{
    CHECK(write_original(original()));
    quantapdf_context *ctx=nullptr;
    quantapdf_document *doc=nullptr;
    quantapdf_bitmap *held=nullptr;
    std::vector<unsigned char> held_rgb;
    OK(quantapdf_context_create(&ctx));
    OK(quantapdf_open_with_context(ctx,filename,nullptr,&doc));
    CHECK(std::remove(filename)==0);

    /* Geometry failures must neither erase previous valid output nor
     * silently poison a later page's ancestry/CTM or user-unit lease.
     * Repeated visits are compared byte-for-byte within each DPI. */
    const int order[]={
        8,0,2,9,1,10,3,11,4,12,5,13,6,14,7,15,
        17,16,19,18,2,11,5,14,0,17,19,8
    };
    size_t renders=0u,refusals=0u;
    for(int dpi:{72,96,144}) {
        std::array<rendered,page_count> baseline{};
        std::array<bool,page_count> seen{};
        for(const int index:order) {
            quantapdf_page *page=nullptr;
            quantapdf_bitmap *bitmap=
                reinterpret_cast<quantapdf_bitmap *>(uintptr_t(1));
            const auto loaded=quantapdf_load_page(doc,index,&page);
            if(loaded!=QUANTAPDF_OK || page==nullptr) {
                std::fprintf(stderr,"page load mismatch: index=%d dpi=%d "
                    "status=%d page=%p\n",index,dpi,
                    static_cast<int>(loaded),static_cast<void *>(page));
                CHECK(false);
            }
            quantapdf_render_options options{};
            options.struct_size=sizeof(options);
            options.dpi=static_cast<float>(dpi);
            const quantapdf_status status=
                quantapdf_render_page_with_options(page,&options,&bitmap);
            if(!accepted(index)) {
                if(status!=expected_refusal(index) || bitmap!=nullptr) {
                    std::fprintf(stderr,"geometry refusal mismatch: "
                        "index=%d dpi=%d status=%d expected=%d bitmap=%p\n",
                        index,dpi,static_cast<int>(status),
                        static_cast<int>(expected_refusal(index)),
                        static_cast<void *>(bitmap));
                    CHECK(false);
                }
                ++refusals;
            } else {
                if(status!=QUANTAPDF_OK || bitmap==nullptr) {
                    std::fprintf(stderr,"geometry positive rejected: "
                        "index=%d dpi=%d status=%d\n",index,dpi,
                        static_cast<int>(status));
                    CHECK(false);
                }
                rendered actual;
                int stride=0,channels=0;
                const unsigned char *data=nullptr;
                size_t size=0u;
                OK(quantapdf_bitmap_dimensions(bitmap,
                    &actual.width,&actual.height,&stride,&channels));
                OK(quantapdf_bitmap_data(bitmap,&data,&size));
                CHECK(stride==actual.width*3 && channels==3 &&
                      data!=nullptr &&
                      size==static_cast<size_t>(stride)*
                            static_cast<size_t>(actual.height));
                actual.rgb.assign(data,data+size);
                if(!expected_pixels(actual,index,dpi)) {
                    std::fprintf(stderr,"geometry pixel mismatch: "
                        "index=%d dpi=%d dims=%dx%d centre=%06x\n",
                        index,dpi,actual.width,actual.height,
                        static_cast<unsigned>(pixel(actual,
                            actual.width/2,actual.height/2)));
                    CHECK(false);
                }
                const size_t i=static_cast<size_t>(index);
                if(seen[i])
                    CHECK(baseline[i].width==actual.width &&
                          baseline[i].height==actual.height &&
                          baseline[i].rgb==actual.rgb);
                else {
                    baseline[i]=actual;
                    seen[i]=true;
                }
                if(held==nullptr) {
                    held=bitmap;
                    held_rgb=actual.rgb;
                } else quantapdf_drop_bitmap(bitmap);
                ++renders;
            }
            quantapdf_drop_page(page);
            if(held!=nullptr) {
                const unsigned char *data=nullptr;
                size_t n=0u;
                OK(quantapdf_bitmap_data(held,&data,&n));
                CHECK(data!=nullptr && n==held_rgb.size() &&
                      std::memcmp(data,held_rgb.data(),n)==0);
            }
        }
        for(int i:{0,1,2,3,4,5,6,7,17,19})
            CHECK(seen[static_cast<size_t>(i)]);
    }
    quantapdf_close(doc);
    quantapdf_drop_context(ctx);
    CHECK(held!=nullptr);
    const unsigned char *data=nullptr;
    size_t n=0u;
    OK(quantapdf_bitmap_data(held,&data,&n));
    CHECK(data!=nullptr && n==held_rgb.size() &&
          std::memcmp(data,held_rgb.data(),n)==0);
    quantapdf_drop_bitmap(held);
    CHECK(renders==45u && refusals==39u);
    std::printf("Native Source geometry ancestry: %zu renders, "
                "%zu atomic refusals\n",renders,refusals);
    return EXIT_SUCCESS;
}

int main()
{
    return qualify();
}
