/* Original, provenance-reviewed PDF files: public Native V10 fail-closed
 * qualification of the four external originals NOT accepted in PR #544.
 * This is not a PDFium parity test and grants no capability by itself.
 *
 * Every negative Document is immediately followed by the same reviewed
 * blank external PDF. One Context spans the whole seeded replay, but each
 * original is opened and retired independently. The first blank Bitmap
 * remains owned and readable after every negative and Context retirement.
 */
#include <quantapdf/quantapdf.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#define CHECK(e) do { if(!(e)) { \
    std::fprintf(stderr,"native external remainder:%d: %s\n", \
                 __LINE__,#e); return EXIT_FAILURE; \
} } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)

static constexpr std::array<const char*,4u> ids={{
    "pdfium.black-original-v1",
    "pdfium.clip-path-v1",
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
static std::array<unsigned,4u> order_for(size_t pass)
{
    std::array<unsigned,4u> order={{0u,1u,2u,3u}};
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
static std::string fingerprint(const std::array<unsigned,4u>& order)
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
static int replay(const std::filesystem::path& root,
                  const std::filesystem::path& receipt)
{
    quantapdf_context *ctx=nullptr;
    quantapdf_bitmap *held=nullptr;
    std::vector<unsigned char> retained;
    std::array<unsigned,4u> observed={{0u,0u,0u,0u}};
    std::array<std::array<unsigned,4u>,3u> orders{};
    std::array<std::string,3u> fingerprints{};
    size_t accepted=0u,refused=0u;
    OK(quantapdf_context_create(&ctx));

    for(size_t pass=0u;pass<3u;++pass) {
        orders[pass]=order_for(pass);
        fingerprints[pass]=fingerprint(orders[pass]);
        CHECK(!fingerprints[pass].empty());
        for(unsigned index:orders[pass]) {
            const char *id=ids[index];
            const std::string filename=
                (root/(std::string(id)+".pdf")).string();
            quantapdf_document *doc=nullptr;
            quantapdf_page *page=nullptr;
            quantapdf_bitmap *bitmap=
                reinterpret_cast<quantapdf_bitmap *>(uintptr_t(1));
            const auto open_status=quantapdf_open_with_context(
                ctx,filename.c_str(),nullptr,&doc);
            if(open_status!=QUANTAPDF_OK || doc==nullptr) {
                std::fprintf(stderr,"external open id=%s status=%d\n",
                             id,static_cast<int>(open_status));
                CHECK(false);
            }
            int page_count=0;
            OK(quantapdf_page_count(doc,&page_count));
            CHECK(page_count>0);
            OK(quantapdf_load_page(doc,0,&page));
            CHECK(page!=nullptr);
            quantapdf_render_options options{};
            options.struct_size=sizeof(options);
            options.dpi=72.0f;
            const quantapdf_status status=
                quantapdf_render_page_with_options(page,&options,&bitmap);
            if(status!=QUANTAPDF_ERROR_UNSUPPORTED || bitmap!=nullptr) {
                std::fprintf(stderr,"external public V10 case=%s pass=%zu "
                    "actual_status=%d expected_UNSUPPORTED=%d bitmap=%p\n",
                    id,pass,static_cast<int>(status),
                    static_cast<int>(QUANTAPDF_ERROR_UNSUPPORTED),
                    static_cast<void *>(bitmap));
                if(status==QUANTAPDF_OK && bitmap!=nullptr)
                    quantapdf_drop_bitmap(bitmap);
                quantapdf_drop_page(page);
                quantapdf_close(doc);
                CHECK(false);
            }
            ++observed[index];++refused;
            quantapdf_drop_page(page);
            quantapdf_close(doc);

            /* A documented and already-qualified positive must continue
             * to render after EVERY refused independent original. */
            const std::string blank_path=
                (root/(std::string(blank_id)+".pdf")).string();
            doc=nullptr;page=nullptr;bitmap=nullptr;
            OK(quantapdf_open_with_context(
                ctx,blank_path.c_str(),nullptr,&doc));
            CHECK(doc!=nullptr);
            OK(quantapdf_load_page(doc,0,&page));
            CHECK(page!=nullptr);
            const auto good=quantapdf_render_page_with_options(
                page,&options,&bitmap);
            if(good!=QUANTAPDF_OK || bitmap==nullptr) {
                std::fprintf(stderr,"blank recovery after %s pass=%zu "
                    "status=%d\n",id,pass,static_cast<int>(good));
                CHECK(false);
            }
            std::vector<unsigned char> white;
            CHECK(white_bitmap(bitmap,white));
            if(held==nullptr) {
                held=bitmap;
                retained=white;
            } else {
                CHECK(white==retained);
                quantapdf_drop_bitmap(bitmap);
            }
            ++accepted;
            quantapdf_drop_page(page);
            quantapdf_close(doc);
            const unsigned char *live=nullptr;
            size_t size=0u;
            OK(quantapdf_bitmap_data(held,&live,&size));
            CHECK(live!=nullptr && size==retained.size() &&
                  std::memcmp(live,retained.data(),size)==0);
        }
    }
    quantapdf_drop_context(ctx);
    CHECK(held!=nullptr);
    const unsigned char *live=nullptr;
    size_t size=0u;
    OK(quantapdf_bitmap_data(held,&live,&size));
    CHECK(live!=nullptr && size==retained.size() &&
          std::memcmp(live,retained.data(),size)==0);
    quantapdf_drop_bitmap(held);
    CHECK(refused==12u && accepted==12u);
    for(unsigned n:observed)CHECK(n==3u);

    std::ofstream out(receipt,std::ios::binary|std::ios::trunc);
    CHECK(out.good());
    out<<"{\"schema_version\":1,\"corpus_id\":\"external-source-pdf\","
       <<"\"qualification\":\"native-v10-remaining-fail-closed\","
       <<"\"reference_oracle\":\"none\",\"result\":\"passed\","
       <<"\"manifest_document_count\":7,\"negative_document_count\":4,"
       <<"\"recovery_document_id\":\""<<blank_id<<"\","
       <<"\"dpi\":72,\"execution_count\":24,\"positive_count\":12,"
       <<"\"refusal_count\":12,\"passes\":[";
    for(size_t p=0u;p<orders.size();++p) {
        if(p)out<<',';
        out<<"{\"name\":\""<<names[p]<<"\",\"seed\":\""<<labels[p]
           <<"\",\"fingerprint_fnv64\":\""<<fingerprints[p]
           <<"\",\"ids\":[";
        for(size_t i=0u;i<orders[p].size();++i) {
            if(i)out<<',';
            out<<'\"'<<ids[orders[p][i]]<<'\"';
        }
        out<<"],\"refused\":4,\"recovered\":4}";
    }
    out<<"],\"documents\":[";
    for(size_t i=0u;i<ids.size();++i) {
        if(i)out<<',';
        out<<"{\"id\":\""<<ids[i]
           <<"\",\"observed_status\":\"UNSUPPORTED\","
           <<"\"executions\":"<<observed[i]
           <<",\"atomic_null_bitmap\":true}";
    }
    out<<"],\"recovery\":{\"id\":\""<<blank_id
       <<"\",\"observed_status\":\"OK\",\"executions\":12,"
       <<"\"all_white\":true,\"bitmap_survived_context\":true}}\n";
    out.flush();
    CHECK(out.good());
    std::printf("Native V10 remaining external: 4 originals x 3 orders,"
                " 12 atomic UNSUPPORTED refusals and 12 separately"
                " reopened blank recovery renders\n");
    return EXIT_SUCCESS;
}
int main(int argc,char **argv)
{
    if(argc!=3 || !argv[1] || !argv[2])return EXIT_FAILURE;
    return replay(std::filesystem::path(argv[1]),
                  std::filesystem::path(argv[2]));
}
