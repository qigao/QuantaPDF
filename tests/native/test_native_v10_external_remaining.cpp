/* Actual unchanged provenance-reviewed external originals: public
 * Source V10 72-DPI yWorks + full original TeXample RGB.
 * Both are replayed across original independent Document lifetimes and
 * followed by the already qualified external blank recovery PDF.
 * No PDFium oracle, replacement source file or second renderer. */
#include <quantapdf/quantapdf.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#define CHECK(e) do { if(!(e)) { \
    std::fprintf(stderr,"native external remainder:%d: %s\n", \
                 __LINE__,#e); return EXIT_FAILURE; \
} } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)

static constexpr std::array<const char*,2u> ids={{
    "yworks.clippath-cliprule-v1",
    "texample.intersecting-rings-v1"
}};
static const char *const blank_id="tinytoolkit.sample-blank-letter-v1";
static const char *const names[]={"canonical","shuffled-a","shuffled-b"};
static const char *const labels[]={"none","0x4cf5ad11","0x9e3779b9"};
static constexpr std::array<uint32_t,3u> seeds={{
    0u,UINT32_C(0x4cf5ad11),UINT32_C(0x9e3779b9)
}};

static uint32_t xorshift(uint32_t x)
{
    x^=x<<13u;
    x^=x>>17u;
    x^=x<<5u;
    return x;
}
static std::array<unsigned,2u> order_for(size_t pass)
{
    std::array<unsigned,2u> order={{0u,1u}};
    if(pass==0u)return order;
    uint32_t x=seeds[pass];
    for(size_t n=order.size();n>1u;--n) {
        x=xorshift(x);
        const size_t j=x%n;
        const unsigned t=order[n-1u];
        order[n-1u]=order[j];order[j]=t;
    }
    return order;
}
static std::string fingerprint(const std::array<unsigned,2u>& order)
{
    uint64_t hash=UINT64_C(0xcbf29ce484222325);
    for(unsigned idx:order) {
        const uint32_t value=idx+1u;
        for(unsigned shift=0u;shift<=24u;shift+=8u) {
            hash^=(value>>shift)&255u;
            hash*=UINT64_C(0x100000001b3);
        }
    }
    char b[32]={0};
    const int length=std::snprintf(b,sizeof(b),"%016llx",
        static_cast<unsigned long long>(hash));
    return length==16?std::string(b):std::string();
}
static bool white_bitmap(quantapdf_bitmap *bitmap,
                         std::vector<unsigned char>& rgb)
{
    int w=0,h=0,stride=0,channels=0;
    const unsigned char *bytes=nullptr;
    size_t count=0u;
    if(quantapdf_bitmap_dimensions(bitmap,&w,&h,&stride,&channels)!=
       QUANTAPDF_OK || quantapdf_bitmap_data(bitmap,&bytes,&count)!=
       QUANTAPDF_OK || w!=612 || h!=792 || stride!=w*3 ||
       channels!=3 || bytes==nullptr ||
       count!=static_cast<size_t>(stride)*static_cast<size_t>(h))
        return false;
    for(size_t i=0u;i<count;++i)
        if(bytes[i]!=255u)return false;
    rgb.assign(bytes,bytes+count);
    return true;
}
/* Exact analytical sample controls from the ORIGINAL PDF's two
 * nested returned rectangles, red one-pixel S, blue nonzero-W star
 * and blue even-odd-W* star. No PDFium or separate pixel renderer. */
static uint32_t color(const std::vector<unsigned char>& rgb,
                      int width,int x,int y)
{
    const size_t p=(static_cast<size_t>(y)*
        static_cast<size_t>(width)+static_cast<size_t>(x))*3u;
    return (static_cast<uint32_t>(rgb[p])<<16u)|
           (static_cast<uint32_t>(rgb[p+1u])<<8u)|
            static_cast<uint32_t>(rgb[p+2u]);
}
static bool yworks_bitmap(quantapdf_bitmap *bmp,
                         std::vector<unsigned char>& rgb)
{
    int w=0,h=0,stride=0,channels=0;
    const unsigned char *data=nullptr;
    size_t count=0u;
    if(quantapdf_bitmap_dimensions(bmp,&w,&h,&stride,&channels)!=
       QUANTAPDF_OK ||
       quantapdf_bitmap_data(bmp,&data,&count)!=QUANTAPDF_OK ||
       w!=600 || h!=600 || stride!=1800 || channels!=3 ||
       data==nullptr || count!=1080000u)
        return false;
    rgb.assign(data,data+count);
    return color(rgb,w,20,40)==UINT32_C(0xffffff) &&
           color(rgb,w,40,39)==UINT32_C(0xff7f7f) &&
           color(rgb,w,40,40)==UINT32_C(0xff7f7f) &&
           color(rgb,w,100,40)==UINT32_C(0xffffff) &&
           color(rgb,w,180,40)==UINT32_C(0xff7f7f) &&
           color(rgb,w,400,40)==UINT32_C(0xffffff) &&
           color(rgb,w,80,125)==UINT32_C(0x0000ff) &&
           color(rgb,w,160,125)==UINT32_C(0x0000ff) &&
           color(rgb,w,180,125)==UINT32_C(0xffffff) &&
           color(rgb,w,250,125)==UINT32_C(0xffffff) &&
           color(rgb,w,500,500)==UINT32_C(0xffffff);
}

/* Original-decimal polynomial reference is frozen independently of Source
 * float conversion and the production raster. Compare EVERY public RGB byte. */
static bool texample_bitmap(quantapdf_bitmap *bmp,
    const std::vector<unsigned char>& expected,std::vector<unsigned char>& rgb)
{
    int w=0,h=0,stride=0,channels=0;
    const unsigned char *data=nullptr;
    size_t count=0u;
    if(quantapdf_bitmap_dimensions(bmp,&w,&h,&stride,&channels)!=QUANTAPDF_OK ||
       quantapdf_bitmap_data(bmp,&data,&count)!=QUANTAPDF_OK ||
       w!=238 || h!=181 || stride!=714 || channels!=3 || data==nullptr ||
       count!=expected.size() || std::memcmp(data,expected.data(),count)!=0)
        return false;
    rgb.assign(data,data+count);
    return true;
}
static bool retained_bitmap(quantapdf_bitmap *bmp,
                            const std::vector<unsigned char>& expected)
{
    const unsigned char *live=nullptr;size_t size=0u;
    return bmp!=nullptr &&
        quantapdf_bitmap_data(bmp,&live,&size)==QUANTAPDF_OK &&
        live!=nullptr && size==expected.size() &&
        std::memcmp(live,expected.data(),size)==0;
}
static int replay(const std::filesystem::path& root,
                  const std::filesystem::path& receipt)
{
    const auto reference=root.parent_path().parent_path()/"native"/"goldens"/
        "texample72-m4b.rgba";
    std::ifstream input(reference,std::ios::binary);
    const std::vector<unsigned char> rgba{std::istreambuf_iterator<char>(input),
                                         std::istreambuf_iterator<char>()};
    CHECK(rgba.size()==238u*181u*4u);
    std::vector<unsigned char> expected;
    expected.reserve(238u*181u*3u);
    for(size_t i=0u;i<rgba.size();i+=4u) {
        CHECK(rgba[i+3u]==255u);
        for(size_t c=0u;c<3u;++c)expected.push_back(rgba[i+c]);
    }
    quantapdf_context *ctx=nullptr;
    quantapdf_bitmap *held=nullptr;
    std::array<quantapdf_bitmap*,2u> original_held={{nullptr,nullptr}};
    std::vector<unsigned char> retained;
    std::array<std::vector<unsigned char>,2u> original_retained;
    std::array<unsigned,2u> observed={{0u,0u}},dpi_refused={{0u,0u}};
    std::array<std::array<unsigned,2u>,3u> orders{};
    std::array<std::string,3u> fingerprints{};
    std::array<size_t,3u> pass_rgb{},pass_refused{},pass_recovery{};
    size_t accepted=0u,refused=0u;
    OK(quantapdf_context_create(&ctx));
    for(size_t pass=0u;pass<3u;++pass) {
        orders[pass]=order_for(pass);
        fingerprints[pass]=fingerprint(orders[pass]);
        CHECK(!fingerprints[pass].empty());
        for(unsigned index:orders[pass]) {
            const char *id=ids[index];
            const std::string filename=(root/(std::string(id)+".pdf")).string();
            quantapdf_document *doc=nullptr;
            quantapdf_page *page=nullptr;
            quantapdf_bitmap *bitmap=
                reinterpret_cast<quantapdf_bitmap *>(uintptr_t(1));
            OK(quantapdf_open_with_context(ctx,filename.c_str(),nullptr,&doc));
            CHECK(doc!=nullptr);
            int page_count=0;
            OK(quantapdf_page_count(doc,&page_count));CHECK(page_count>0);
            OK(quantapdf_load_page(doc,0,&page));CHECK(page!=nullptr);
            quantapdf_render_options options{};
            options.struct_size=sizeof(options);options.dpi=72.0f;
            const auto status=quantapdf_render_page_with_options(page,&options,&bitmap);
            if(status!=QUANTAPDF_OK || bitmap==nullptr) {
                std::fprintf(stderr,"original %s public status=%d pass=%zu\n",
                             id,static_cast<int>(status),pass);
                CHECK(false);
            }
            std::vector<unsigned char> rgb;
            CHECK(index==0u?yworks_bitmap(bitmap,rgb):texample_bitmap(bitmap,expected,rgb));
            if(original_held[index]==nullptr) {
                original_held[index]=bitmap;original_retained[index]=rgb;
            } else {
                CHECK(rgb==original_retained[index]);quantapdf_drop_bitmap(bitmap);
            }
            ++observed[index];++accepted;++pass_rgb[pass];
            /* Qualified physical Stroke coverage is 72 DPI only. Every
             * actual 96/144-DPI refusal belongs in the runtime receipt. */
            for(const float dpi:{96.0f,144.0f}) {
                options.dpi=dpi;
                quantapdf_bitmap *invalid=
                    reinterpret_cast<quantapdf_bitmap *>(uintptr_t(1));
                const auto denied=quantapdf_render_page_with_options(page,&options,&invalid);
                CHECK(denied==QUANTAPDF_ERROR_UNSUPPORTED && invalid==nullptr);
                ++dpi_refused[index];++refused;++pass_refused[pass];
            }
            options.dpi=72.0f;
            quantapdf_drop_page(page);quantapdf_close(doc);
            /* Independently reopen the existing external blank recovery. */
            const std::string blank_path=(root/(std::string(blank_id)+".pdf")).string();
            doc=nullptr;page=nullptr;bitmap=nullptr;
            OK(quantapdf_open_with_context(ctx,blank_path.c_str(),nullptr,&doc));
            CHECK(doc!=nullptr);
            OK(quantapdf_load_page(doc,0,&page));CHECK(page!=nullptr);
            OK(quantapdf_render_page_with_options(page,&options,&bitmap));
            CHECK(bitmap!=nullptr);
            std::vector<unsigned char> white;
            CHECK(white_bitmap(bitmap,white));
            if(held==nullptr) {held=bitmap;retained=white;}
            else {CHECK(white==retained);quantapdf_drop_bitmap(bitmap);}
            ++accepted;++pass_recovery[pass];
            quantapdf_drop_page(page);quantapdf_close(doc);
            CHECK(retained_bitmap(held,retained));
            for(size_t i=0u;i<ids.size();++i)
                if(original_held[i]!=nullptr)
                    CHECK(retained_bitmap(original_held[i],original_retained[i]));
        }
    }
    quantapdf_drop_context(ctx);
    CHECK(retained_bitmap(held,retained));quantapdf_drop_bitmap(held);
    for(size_t i=0u;i<ids.size();++i) {
        CHECK(retained_bitmap(original_held[i],original_retained[i]));
        quantapdf_drop_bitmap(original_held[i]);
        CHECK(observed[i]==3u && dpi_refused[i]==6u);
    }
    CHECK(refused==12u && accepted==12u);
    std::ofstream out(receipt,std::ios::binary|std::ios::trunc);CHECK(out.good());
    out<<"{\"schema_version\":1,\"corpus_id\":\"external-source-pdf\","
       <<"\"qualification\":\"native-v10-yworks-texample-rgb\","
       <<"\"reference_oracle\":\"none\",\"result\":\"passed\","
       <<"\"texample_rgba_reference_sha256\":\""
       <<"51e36fcf3a39e93724e1a3176286944883378df9b39c942147d52827607f957e\","
       <<"\"manifest_document_count\":7,\"negative_document_count\":0,"
       <<"\"recovery_document_id\":\""<<blank_id<<"\","
       <<"\"dpi\":72,\"execution_count\":"<<(accepted+refused)
       <<",\"positive_count\":"<<accepted<<",\"refusal_count\":"<<refused
       <<",\"passes\":[";
    for(size_t p=0u;p<orders.size();++p) {
        if(p)out<<',';
        out<<"{\"name\":\""<<names[p]<<"\",\"seed\":\""<<labels[p]
           <<"\",\"fingerprint_fnv64\":\""<<fingerprints[p]<<"\",\"ids\":[";
        for(size_t i=0u;i<orders[p].size();++i) {
            if(i)out<<',';
            out<<'\"'<<ids[orders[p][i]]<<'\"';
        }
        out<<"],\"refused\":"<<pass_refused[p]<<",\"accepted_rgb\":"<<pass_rgb[p]
           <<",\"recovered\":"<<pass_recovery[p]<<'}';
    }
    out<<"],\"documents\":[";
    for(size_t i=0u;i<ids.size();++i) {
        if(i)out<<',';
        out<<"{\"id\":\""<<ids[i]<<"\",\"observed_status\":\"OK\",\"executions\":"
           <<observed[i]<<",\"exact_rgb_pixels\":true,\"atomic_null_bitmap\":false,"
           <<"\"unsupported_dpi_executions\":"<<dpi_refused[i]
           <<",\"unsupported_dpi_atomic_null_bitmap\":true,\"bitmap_survived_context\":true}";
    }
    out<<"],\"recovery\":{\"id\":\""<<blank_id
       <<"\",\"observed_status\":\"OK\",\"executions\":6,"
       <<"\"all_white\":true,\"bitmap_survived_context\":true}}\n";
    out.flush();CHECK(out.good());
    std::printf("Native V10 complex originals: unchanged yWorks and TeXample RGB 3x each, "
                "96/144-DPI atomic refusals 12x, independent blank recovery 6x\n");
    return EXIT_SUCCESS;
}
int main(int argc,char **argv)
{
    if(argc!=3 || !argv[1] || !argv[2])return EXIT_FAILURE;
    return replay(std::filesystem::path(argv[1]),std::filesystem::path(argv[2]));
}
