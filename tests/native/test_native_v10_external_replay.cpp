/* Real external PDF / Native V10 admission, WITHOUT a PDFium oracle.
 * The existing external_pdf_corpus.py verifies all seven independently
 * sourced PDFs, licensing-review metadata and exact SHA-256 before this
 * public C11 replay; reconciliation verifies them again afterward.
 * Only these three explicitly qualified original PDFs are counted. */
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
    std::fprintf(stderr,"native external V10:%d: %s\n",__LINE__,#e); \
    return EXIT_FAILURE; \
} } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)

struct case_entry {
    const char *id;
    const char *outcome;
    quantapdf_status expected;
    bool white;
};
static constexpr std::array<case_entry,3u> cases={{
    {"tinytoolkit.sample-blank-letter-v1","OK",QUANTAPDF_OK,true},
    {"tinytoolkit.sample-1-page-v1","UNSUPPORTED",
        QUANTAPDF_ERROR_UNSUPPORTED,false},
    {"pdfium.wstar-late-text-v1","UNSUPPORTED",
        QUANTAPDF_ERROR_UNSUPPORTED,false}
}};
static constexpr std::array<uint32_t,3u> seeds={{
    0u,UINT32_C(0x4cf5ad11),UINT32_C(0x9e3779b9)
}};
static const char *const pass_names[]={"canonical","shuffled-a","shuffled-b"};
static const char *const seed_labels[]={"none","0x4cf5ad11","0x9e3779b9"};

static uint32_t xorshift(uint32_t n)
{
    n ^= n << 13u;
    n ^= n >> 17u;
    n ^= n << 5u;
    return n;
}
static std::array<unsigned,3u> order_for(size_t pass)
{
    std::array<unsigned,3u> order={{0u,1u,2u}};
    if(pass==0u)return order;
    uint32_t state=seeds[pass];
    for(size_t n=order.size();n>1u;--n) {
        state=xorshift(state);
        const size_t j=state%n;
        const unsigned tmp=order[n-1u];
        order[n-1u]=order[j];order[j]=tmp;
    }
    return order;
}
static uint64_t fingerprint(const std::array<unsigned,3u>& order)
{
    uint64_t h=UINT64_C(0xcbf29ce484222325);
    for(unsigned index:order) {
        const uint32_t v=index+1u;
        for(unsigned shift=0u;shift<=24u;shift+=8u) {
            h^=(v>>shift)&255u;
            h*=UINT64_C(0x100000001b3);
        }
    }
    return h;
}
static std::string hex64(uint64_t value)
{
    char b[24]={};
    const int count=std::snprintf(b,sizeof(b),"%016llx",
        static_cast<unsigned long long>(value));
    return count==16?std::string(b):std::string();
}
static int replay(const std::filesystem::path& assets,
                  const std::filesystem::path& receipt)
{
    quantapdf_context *context=nullptr;
    quantapdf_bitmap *retained=nullptr;
    std::vector<unsigned char> first_white;
    std::array<unsigned,3u> observations={{0u,0u,0u}};
    std::array<std::array<unsigned,3u>,3u> orders{};
    std::array<std::string,3u> fingerprints{};
    size_t positives=0u,refusals=0u;
    OK(quantapdf_context_create(&context));

    /* Every visited PDF has an independent original Document lifetime.
     * Deliberately retain the first accepted bitmap across all later
     * external documents, unsupported failures and Context retirement. */
    for(size_t pass=0u;pass<3u;++pass) {
        orders[pass]=order_for(pass);
        fingerprints[pass]=hex64(fingerprint(orders[pass]));
        CHECK(!fingerprints[pass].empty());
        for(unsigned index:orders[pass]) {
            const case_entry& item=cases[index];
            const std::string path=(assets/
                (std::string(item.id)+".pdf")).string();
            quantapdf_document *document=nullptr;
            const auto opened=quantapdf_open_with_context(
                context,path.c_str(),nullptr,&document);
            if(opened!=QUANTAPDF_OK || document==nullptr) {
                std::fprintf(stderr,"external %s open=%d\n",
                    item.id,static_cast<int>(opened));
                CHECK(false);
            }
            int count=0;
            OK(quantapdf_page_count(document,&count));
            CHECK(count>0);
            quantapdf_page *page=nullptr;
            OK(quantapdf_load_page(document,0,&page));
            CHECK(page!=nullptr);

            quantapdf_render_options options{};
            options.struct_size=sizeof(options);
            options.dpi=72.0f;
            quantapdf_bitmap *bitmap=
                reinterpret_cast<quantapdf_bitmap *>(uintptr_t(1));
            const quantapdf_status status=
                quantapdf_render_page_with_options(page,&options,&bitmap);
            if(status!=item.expected ||
               (status==QUANTAPDF_OK?bitmap==nullptr:bitmap!=nullptr)) {
                std::fprintf(stderr,
                    "external V10 mismatch: %s pass=%zu got=%d "
                    "expected=%d bitmap=%p\n",
                    item.id,pass,static_cast<int>(status),
                    static_cast<int>(item.expected),
                    static_cast<void *>(bitmap));
                CHECK(false);
            }

            if(status==QUANTAPDF_OK) {
                CHECK(item.white);
                int width=0,height=0,stride=0,channels=0;
                size_t bytes_count=0u;
                const unsigned char *bytes=nullptr;
                OK(quantapdf_bitmap_dimensions(
                    bitmap,&width,&height,&stride,&channels));
                OK(quantapdf_bitmap_data(bitmap,&bytes,&bytes_count));
                CHECK(width==612 && height==792 && stride==width*3 &&
                      channels==3 && bytes!=nullptr &&
                      bytes_count==static_cast<size_t>(stride)*
                                   static_cast<size_t>(height));
                for(size_t i=0u;i<bytes_count;++i)
                    CHECK(bytes[i]==255u);
                if(retained==nullptr) {
                    retained=bitmap;
                    first_white.assign(bytes,bytes+bytes_count);
                } else {
                    CHECK(first_white.size()==bytes_count &&
                          std::memcmp(bytes,first_white.data(),bytes_count)==0);
                    quantapdf_drop_bitmap(bitmap);
                }
                ++positives;
            } else ++refusals;
            ++observations[index];
            quantapdf_drop_page(page);
            quantapdf_close(document);

            if(retained!=nullptr) {
                const unsigned char *live=nullptr;
                size_t count_live=0u;
                OK(quantapdf_bitmap_data(retained,&live,&count_live));
                CHECK(live!=nullptr && count_live==first_white.size() &&
                      std::memcmp(live,first_white.data(),count_live)==0);
            }
        }
    }

    quantapdf_drop_context(context);
    CHECK(retained!=nullptr);
    const unsigned char *live=nullptr;
    size_t size=0u;
    OK(quantapdf_bitmap_data(retained,&live,&size));
    CHECK(size==first_white.size() && live!=nullptr &&
          std::memcmp(live,first_white.data(),size)==0);
    quantapdf_drop_bitmap(retained);
    CHECK(positives==3u && refusals==6u);
    for(unsigned n:observations)CHECK(n==3u);

    /* No receipt until every real public render and post-owner image
     * assertion succeeds. The external integrity receipt is NEVER
     * modified or counted as native semantic evidence. */
    std::ofstream report(receipt,std::ios::binary|std::ios::trunc);
    CHECK(report.good());
    report<<"{\"schema_version\":1,\"corpus_id\":\"external-source-pdf\","
          <<"\"fixture_kind\":\"external\",\"qualification\":"
          <<"\"native-v10-public\",\"reference_oracle\":\"none\","
          <<"\"result\":\"passed\",\"manifest_document_count\":7,"
          <<"\"qualified_document_count\":3,\"execution_count\":9,"
          <<"\"positive_count\":3,\"refusal_count\":6,"
          <<"\"dpi\":72,\"passes\":[";
    for(size_t i=0u;i<orders.size();++i) {
        if(i!=0u)report<<',';
        report<<"{\"name\":\""<<pass_names[i]<<"\",\"seed\":\""
              <<seed_labels[i]<<"\",\"fingerprint_fnv64\":\""
              <<fingerprints[i]<<"\",\"ids\":[";
        for(size_t j=0u;j<orders[i].size();++j) {
            if(j!=0u)report<<',';
            report<<'\"'<<cases[orders[i][j]].id<<'\"';
        }
        report<<"],\"executions\":3,\"positive\":1,\"refusals\":2}";
    }
    report<<"],\"documents\":[";
    for(size_t i=0u;i<cases.size();++i) {
        if(i!=0u)report<<',';
        report<<"{\"id\":\""<<cases[i].id<<"\",\"observed_status\":\""
              <<cases[i].outcome<<"\",\"executions\":"<<observations[i]
              <<",\"exact_blank_pixels\":"
              <<(cases[i].white?"true":"false")
              <<",\"atomic_null_bitmap\":"
              <<(cases[i].white?"false":"true")<<"}";
    }
    report<<"]}\n";
    report.flush();
    CHECK(report.good());
    std::printf("Native external V10: three reviewed originals, "
                "three passes, 3 white RGB renders, 6 exact "
                "UNSUPPORTED/null-bitmap refusals\n");
    return EXIT_SUCCESS;
}
int main(int argc,char **argv)
{
    if(argc!=3 || argv[1]==nullptr || argv[2]==nullptr)
        return EXIT_FAILURE;
    return replay(std::filesystem::path(argv[1]),
                  std::filesystem::path(argv[2]));
}
