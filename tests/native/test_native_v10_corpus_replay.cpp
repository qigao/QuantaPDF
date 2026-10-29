/* Cross-document public Native V10 acceptance of SHA-pinned, test-authored
 * original PDF bytes. The separate Python gate verifies the originals and
 * reconciles this actual in-process 3-pass, three-DPI execution receipt.
 * No QPDF/PDFium oracle, provider fallback, extra parser or public ABI. */
#include <quantapdf/quantapdf.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#define CHECK(e) do { if(!(e)) { \
    std::fprintf(stderr,"native cross-document corpus:%d: %s\n", \
                 __LINE__,#e);return EXIT_FAILURE; \
} } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)

struct case_entry {
    const char *id;
    quantapdf_status status;
    const char *status_text;
    uint32_t center;
    bool clipped;
    bool blank;
};
static constexpr std::array<case_entry,12u> cases={{
    {"path-red",QUANTAPDF_OK,"OK",UINT32_C(0xff0000),false,false},
    {"path-blue",QUANTAPDF_OK,"OK",UINT32_C(0x0000ff),false,false},
    {"image-red",QUANTAPDF_OK,"OK",UINT32_C(0xff0000),false,false},
    {"image-green",QUANTAPDF_OK,"OK",UINT32_C(0x00ff00),false,false},
    {"image-clipped-blue",QUANTAPDF_OK,"OK",UINT32_C(0x0000ff),true,false},
    {"blank-page",QUANTAPDF_OK,"OK",UINT32_C(0xffffff),false,true},
    {"reject-late-text",QUANTAPDF_ERROR_UNSUPPORTED,"UNSUPPORTED",0u,false,false},
    {"reject-missing-name",QUANTAPDF_ERROR_FORMAT,"FORMAT",0u,false,false},
    {"reject-form-xobject",QUANTAPDF_ERROR_UNSUPPORTED,"UNSUPPORTED",0u,false,false},
    {"reject-default-rgb",QUANTAPDF_ERROR_UNSUPPORTED,"UNSUPPORTED",0u,false,false},
    {"reject-image-late-text",QUANTAPDF_ERROR_UNSUPPORTED,"UNSUPPORTED",0u,false,false},
    {"reject-cmyk-image",QUANTAPDF_ERROR_UNSUPPORTED,"UNSUPPORTED",0u,false,false}
}};
static constexpr std::array<int,3u> dpis={{72,96,144}};
static constexpr std::array<uint32_t,3u> seeds={{0u,UINT32_C(0x4cf5ad11),
                                                 UINT32_C(0x9e3779b9)}};
static const char *const pass_names[]={"canonical","shuffled-a","shuffled-b"};
static const char *const seed_labels[]={"none","0x4cf5ad11","0x9e3779b9"};

struct pixels {
    int width=0,height=0;
    std::vector<unsigned char> data;
};
static uint32_t pixel(const pixels& p,int x,int y)
{
    const size_t base=(static_cast<size_t>(y)*
        static_cast<size_t>(p.width)+static_cast<size_t>(x))*3u;
    return (static_cast<uint32_t>(p.data.at(base))<<16u)|
           (static_cast<uint32_t>(p.data.at(base+1u))<<8u)|
            static_cast<uint32_t>(p.data.at(base+2u));
}
static bool exact_pixels(const case_entry& info,const pixels& p,int dpi)
{
    const int w=(16*dpi+71)/72,h=(12*dpi+71)/72;
    if(p.width!=w||p.height!=h||p.data.size()!=
       static_cast<size_t>(w)*static_cast<size_t>(h)*3u)return false;
    if(pixel(p,p.width/2,p.height/2)!=info.center)return false;
    if(info.blank) {
        for(unsigned char c:p.data)if(c!=255u)return false;
    } else if(info.clipped) {
        if(pixel(p,0,0)!=UINT32_C(0xffffff)||
           pixel(p,p.width-2,1)!=UINT32_C(0xffffff))return false;
    } else if(pixel(p,0,0)!=info.center)return false;
    return true;
}

static uint32_t next_xorshift(uint32_t state)
{
    state^=state<<13u;
    state^=state>>17u;
    state^=state<<5u;
    return state;
}
static std::array<unsigned,12u> order_for(size_t pass)
{
    std::array<unsigned,12u> order{};
    for(size_t i=0u;i<order.size();++i)order[i]=static_cast<unsigned>(i);
    if(pass==0u)return order;
    uint32_t state=seeds[pass];
    for(size_t count=order.size();count>1u;--count) {
        state=next_xorshift(state);
        const size_t j=state%count;
        const unsigned temp=order[count-1u];
        order[count-1u]=order[j];order[j]=temp;
    }
    return order;
}
static uint64_t order_fingerprint(const std::array<unsigned,12u>& order)
{
    uint64_t hash=UINT64_C(0xcbf29ce484222325);
    for(unsigned index:order) {
        const uint32_t value=index+1u;
        for(unsigned shift=0u;shift<=24u;shift+=8u) {
            hash^=(value>>shift)&255u;
            hash*=UINT64_C(0x100000001b3);
        }
    }
    return hash;
}
static std::string fingerprint(uint64_t value)
{
    char buffer[24]={0};
    const int n=std::snprintf(buffer,sizeof(buffer),"%016llx",
        static_cast<unsigned long long>(value));
    return n==16?std::string(buffer):std::string();
}

static int run(const std::filesystem::path& assets,
               const std::filesystem::path& report)
{
    std::array<std::array<pixels,3u>,cases.size()> goldens{};
    std::array<unsigned,cases.size()> calls{},pixels_checked{};
    std::array<std::array<unsigned,12u>,3u> permutations{};
    std::array<std::string,3u> fingerprints{};
    quantapdf_context *context=nullptr;
    quantapdf_bitmap *held=nullptr;
    std::vector<unsigned char> held_copy;
    size_t positives=0u,refusals=0u;
    OK(quantapdf_context_create(&context));

    /* Three distinct complete cross-document permutations. Every PDF is
     * reopened through the same Context, then retired before the next
     * document; only one independently retained Bitmap remains live. */
    for(size_t pass=0u;pass<3u;++pass) {
        permutations[pass]=order_for(pass);
        fingerprints[pass]=fingerprint(order_fingerprint(permutations[pass]));
        CHECK(!fingerprints[pass].empty());
        for(size_t di=0u;di<dpis.size();++di) {
            for(unsigned index:permutations[pass]) {
                const case_entry& entry=cases[index];
                const auto filename=assets/(std::string(entry.id)+".pdf");
                const std::string path=filename.string();
                quantapdf_document *doc=nullptr;
                quantapdf_page *page=nullptr;
                quantapdf_bitmap *bitmap=
                    reinterpret_cast<quantapdf_bitmap *>(uintptr_t(1));
                if(quantapdf_open_with_context(context,path.c_str(),nullptr,
                                               &doc)!=QUANTAPDF_OK || doc==nullptr) {
                    std::fprintf(stderr,"open refused source %s\n",entry.id);
                    CHECK(false);
                }
                int pages=0;
                OK(quantapdf_page_count(doc,&pages));
                CHECK(pages==1);
                OK(quantapdf_load_page(doc,0,&page));
                CHECK(page!=nullptr);
                quantapdf_render_options options{};
                options.struct_size=sizeof(options);
                options.dpi=static_cast<float>(dpis[di]);
                const auto status=quantapdf_render_page_with_options(
                    page,&options,&bitmap);
                if(status!=entry.status ||
                   (status==QUANTAPDF_OK?bitmap==nullptr:bitmap!=nullptr)) {
                    std::fprintf(stderr,"case=%s pass=%zu dpi=%d "
                        "status=%d expected=%d bitmap=%p\n",entry.id,
                        pass,dpis[di],static_cast<int>(status),
                        static_cast<int>(entry.status),
                        static_cast<void *>(bitmap));
                    CHECK(false);
                }
                if(status==QUANTAPDF_OK) {
                    pixels result;
                    const unsigned char *bytes=nullptr;
                    size_t size=0u;
                    int stride=0,channels=0;
                    OK(quantapdf_bitmap_dimensions(bitmap,&result.width,
                         &result.height,&stride,&channels));
                    OK(quantapdf_bitmap_data(bitmap,&bytes,&size));
                    CHECK(bytes!=nullptr && stride==result.width*3 &&
                          channels==3 &&
                          size==static_cast<size_t>(stride)*
                                static_cast<size_t>(result.height));
                    result.data.assign(bytes,bytes+size);
                    if(!exact_pixels(entry,result,dpis[di])) {
                        std::fprintf(stderr,"pixel mismatch: case=%s "
                            "pass=%zu dpi=%d dims=%dx%d center=%06x\n",
                            entry.id,pass,dpis[di],result.width,result.height,
                            static_cast<unsigned>(pixel(result,result.width/2,
                                                           result.height/2)));
                        CHECK(false);
                    }
                    pixels& baseline=goldens[index][di];
                    if(pass!=0u)CHECK(baseline.width==result.width &&
                                       baseline.height==result.height &&
                                       baseline.data==result.data);
                    else baseline=std::move(result);
                    ++positives;++pixels_checked[index];
                    if(held==nullptr) {
                        held=bitmap;held_copy.assign(bytes,bytes+size);
                    } else quantapdf_drop_bitmap(bitmap);
                } else ++refusals;
                ++calls[index];
                quantapdf_drop_page(page);
                quantapdf_close(doc);
                if(held!=nullptr) {
                    const unsigned char *retained=nullptr;
                    size_t retained_size=0u;
                    OK(quantapdf_bitmap_data(held,&retained,&retained_size));
                    CHECK(retained!=nullptr && retained_size==held_copy.size() &&
                          std::memcmp(retained,held_copy.data(),retained_size)==0);
                }
            }
        }
    }
    quantapdf_drop_context(context);
    CHECK(held!=nullptr);
    const unsigned char *remaining=nullptr;
    size_t remaining_size=0u;
    OK(quantapdf_bitmap_data(held,&remaining,&remaining_size));
    CHECK(remaining!=nullptr && remaining_size==held_copy.size() &&
          std::memcmp(remaining,held_copy.data(),remaining_size)==0);
    quantapdf_drop_bitmap(held);

    CHECK(positives==54u && refusals==54u);
    for(size_t i=0u;i<cases.size();++i)
        CHECK(calls[i]==9u && pixels_checked[i]==
              (cases[i].status==QUANTAPDF_OK?9u:0u));

    /* Single atomic receipt written only after all 108 API calls, byte-
     * exact repeated images and post-Document retention assertions pass.
     * Python independently reconciles every row and permutation against
     * the separately SHA-256-pinned original-source manifest. */
    std::ofstream out(report,std::ios::binary|std::ios::trunc);
    CHECK(out.good());
    out<<"{\"schema_version\":1,\"corpus_id\":\"native-v10-cross-document-v1\","
       <<"\"fixture_kind\":\"generated-test-owned\",\"result\":\"passed\","
       <<"\"document_count\":12,\"dpi\":[72,96,144],"
       <<"\"execution_count\":108,\"positive_count\":54,\"refusal_count\":54,"
       <<"\"pass_results\":[";
    for(size_t pass=0u;pass<3u;++pass) {
        if(pass!=0u)out<<',';
        out<<"{\"name\":\""<<pass_names[pass]<<"\",\"seed\":\""
           <<seed_labels[pass]<<"\",\"fingerprint_fnv64\":\""
           <<fingerprints[pass]<<"\",\"order\":[";
        for(size_t i=0u;i<cases.size();++i) {
            if(i!=0u)out<<',';
            out<<'\"'<<cases[permutations[pass][i]].id<<'\"';
        }
        out<<"],\"executions\":36,\"positive\":18,\"refusals\":18}";
    }
    out<<"],\"documents\":[";
    for(size_t i=0u;i<cases.size();++i) {
        if(i!=0u)out<<',';
        out<<"{\"id\":\""<<cases[i].id<<"\",\"observed_status\":\""
           <<cases[i].status_text<<"\",\"executions\":"<<calls[i]
           <<",\"pixel_checks\":"<<pixels_checked[i]
           <<",\"repeat_identical\":true}";
    }
    out<<"]}\n";
    out.flush();
    CHECK(out.good());
    std::printf("Native V10 cross-document corpus: 12 SHA-pinned originals, "
                "108 executions, 54 RGB positives, 54 atomic refusals, "
                "three complete orders, Bitmap retained\n");
    return EXIT_SUCCESS;
}
int main(int argc,char **argv)
{
    if(argc!=3 || argv[1]==nullptr || argv[2]==nullptr)return EXIT_FAILURE;
    return run(std::filesystem::path(argv[1]),std::filesystem::path(argv[2]));
}
