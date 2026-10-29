/* Original PDF/xref PageTree integrity: no writer normalization of
 * deliberately malformed /Kids, /Count, /Parent or page identities.
 * Source V10 MUST inspect beyond an already selected valid first page.
 * Both the exact production Source and the public facade are exercised. */
#include "backend/qpdf_native_image_ir.h"
#include <quantapdf/quantapdf.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define CHECK(e) do { if(!(e)) { \
    std::fprintf(stderr,"native Source PageTree:%d: %s\n",__LINE__,#e); \
    return EXIT_FAILURE; } } while(0)
static constexpr quantapdf_qpdf_source_raw_image_limits limits={
    2u*1024u*1024u,65536u,4u,4u,1000000u,1000000u,4000000u,8u
};
static const char *const filename="native-source-pagetree-contract.pdf";
static constexpr size_t object_count=9u;

enum class mutation {
    none,duplicate_leaf,nested_count,root_count,wrong_parent,
    parent_cycle,null_child,kids_cycle,unknown_type,missing_parent,
    root_cycle,missing_kids,wrong_kids_type,empty_kids,big_count,
    wrong_count_type,leaf_kids,leaf_count,zero_count,direct_child
};
struct case_entry {const char *name;mutation fault;quantapdf_status expected;};
static constexpr case_entry cases[]={
    {"duplicate later leaf",mutation::duplicate_leaf,QUANTAPDF_ERROR_FORMAT},
    {"nested Count mismatch",mutation::nested_count,QUANTAPDF_ERROR_FORMAT},
    {"root Count mismatch",mutation::root_count,QUANTAPDF_ERROR_FORMAT},
    {"later wrong Parent",mutation::wrong_parent,QUANTAPDF_ERROR_FORMAT},
    {"later Parent self cycle",mutation::parent_cycle,QUANTAPDF_ERROR_FORMAT},
    {"later null Kid",mutation::null_child,QUANTAPDF_ERROR_FORMAT},
    {"later Kids cycle",mutation::kids_cycle,QUANTAPDF_ERROR_FORMAT},
    {"later invalid Type",mutation::unknown_type,QUANTAPDF_ERROR_FORMAT},
    {"later missing Parent",mutation::missing_parent,QUANTAPDF_ERROR_FORMAT},
    {"root Parent cycle",mutation::root_cycle,QUANTAPDF_ERROR_FORMAT},
    {"later missing Kids",mutation::missing_kids,QUANTAPDF_ERROR_FORMAT},
    {"later Kids nonarray",mutation::wrong_kids_type,QUANTAPDF_ERROR_FORMAT},
    {"later Kids empty",mutation::empty_kids,QUANTAPDF_ERROR_UNSUPPORTED},
    {"later Count exceeds cap",mutation::big_count,QUANTAPDF_ERROR_UNSUPPORTED},
    {"later Count not integer",mutation::wrong_count_type,QUANTAPDF_ERROR_FORMAT},
    {"later Page declares Kids",mutation::leaf_kids,QUANTAPDF_ERROR_FORMAT},
    {"later Page declares Count",mutation::leaf_count,QUANTAPDF_ERROR_FORMAT},
    {"root Count zero",mutation::zero_count,QUANTAPDF_ERROR_UNSUPPORTED},
    {"later Kid is direct number",mutation::direct_child,QUANTAPDF_ERROR_FORMAT}
};
static std::string page_object(unsigned parent,unsigned stream)
{
    return "<< /Type /Page /Parent "+std::to_string(parent)+
           " 0 R /Contents "+std::to_string(stream)+" 0 R >>";
}

/* Object order: Catalog 1, root Pages 2, nested Pages 3, final Page 4,
 * nested leaf 5 (red), leaf 6 (green), three Contents streams 7/8/9.
 * Logical page index: 5=0, 6=1, 4=2 (blue). */
static std::array<std::string,object_count+1u> objects(mutation f)
{
    std::array<std::string,object_count+1u> o{};
    o[1]="<< /Type /Catalog /Pages 2 0 R >>";
    o[2]="<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 3 "
         "/MediaBox [0 0 16 12] /Resources << >> >>";
    o[3]="<< /Type /Pages /Parent 2 0 R "
         "/Kids [5 0 R 6 0 R] /Count 2 >>";
    o[4]=page_object(2u,9u);
    o[5]=page_object(3u,7u);
    o[6]=page_object(3u,8u);
    const std::array<std::string,3> streams={{
        "1 0 0 rg 0 0 16 12 re f ",
        "0 1 0 rg 0 0 16 12 re f ",
        "0 0 1 rg 0 0 16 12 re f "
    }};
    for(size_t i=0u;i<streams.size();++i)
        o[i+7u]="<< /Length "+std::to_string(streams[i].size())+
                " >>\nstream\n"+streams[i]+"\nendstream";
    switch(f) {
    case mutation::none:break;
    case mutation::duplicate_leaf:
        o[3]="<< /Type /Pages /Parent 2 0 R "
             "/Kids [5 0 R 5 0 R] /Count 2 >>";break;
    case mutation::nested_count:
        o[3]="<< /Type /Pages /Parent 2 0 R "
             "/Kids [5 0 R 6 0 R] /Count 1 >>";break;
    case mutation::root_count:
        o[2]="<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 "
             "/MediaBox [0 0 16 12] /Resources << >> >>";break;
    case mutation::wrong_parent:o[6]=page_object(2u,8u);break;
    case mutation::parent_cycle:o[6]=page_object(6u,8u);break;
    case mutation::null_child:
        o[3]="<< /Type /Pages /Parent 2 0 R "
             "/Kids [5 0 R null] /Count 2 >>";break;
    case mutation::kids_cycle:
        o[3]="<< /Type /Pages /Parent 2 0 R "
             "/Kids [5 0 R 3 0 R] /Count 2 >>";break;
    case mutation::unknown_type:
        o[6]="<< /Type /Bogus /Parent 3 0 R /Contents 8 0 R >>";break;
    case mutation::missing_parent:
        o[6]="<< /Type /Page /Contents 8 0 R >>";break;
    case mutation::root_cycle:
        o[2]="<< /Type /Pages /Parent 2 0 R "
             "/Kids [3 0 R 4 0 R] /Count 3 "
             "/MediaBox [0 0 16 12] /Resources << >> >>";break;
    case mutation::missing_kids:
        o[4]="<< /Type /Pages /Parent 2 0 R /Count 1 >>";break;
    case mutation::wrong_kids_type:
        o[4]="<< /Type /Pages /Parent 2 0 R /Kids 5 /Count 1 >>";break;
    case mutation::empty_kids:
        o[4]="<< /Type /Pages /Parent 2 0 R /Kids [] /Count 1 >>";break;
    case mutation::big_count:
        o[4]="<< /Type /Pages /Parent 2 0 R "
             "/Kids [6 0 R] /Count 129 >>";break;
    case mutation::wrong_count_type:
        o[4]="<< /Type /Pages /Parent 2 0 R "
             "/Kids [6 0 R] /Count /Wrong >>";break;
    case mutation::leaf_kids:
        o[4]="<< /Type /Page /Parent 2 0 R "
             "/Contents 9 0 R /Kids [] >>";break;
    case mutation::leaf_count:
        o[4]="<< /Type /Page /Parent 2 0 R "
             "/Contents 9 0 R /Count 0 >>";break;
    case mutation::zero_count:
        o[2]="<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 0 "
             "/MediaBox [0 0 16 12] /Resources << >> >>";break;
    case mutation::direct_child:
        o[3]="<< /Type /Pages /Parent 2 0 R "
             "/Kids [5 0 R 17] /Count 2 >>";break;
    }
    return o;
}

/* Correct xref offsets/stream lengths for ALL cases; malformed evidence
 * is the PageTree semantics, never damaged syntax or a recovery attempt. */
static std::string pdf(mutation f)
{
    const auto o=objects(f);
    std::array<size_t,object_count+1u> offsets{};
    std::string bytes="%PDF-1.7\n";
    for(size_t i=1u;i<=object_count;++i) {
        offsets[i]=bytes.size();
        bytes+=std::to_string(i)+" 0 obj\n"+o[i]+"\nendobj\n";
    }
    const size_t xref_at=bytes.size();
    bytes+="xref\n0 "+std::to_string(object_count+1u)+
           "\n0000000000 65535 f \n";
    for(size_t i=1u;i<=object_count;++i) {
        char line[32]={};
        if(std::snprintf(line,sizeof(line),
                         "%010zu 00000 n \n",offsets[i])!=20)return {};
        bytes+=line;
    }
    bytes+="trailer\n<< /Size "+std::to_string(object_count+1u)+
           " /Root 1 0 R >>\nstartxref\n"+
           std::to_string(xref_at)+"\n%%EOF\n";
    return bytes;
}
static bool zero_ir(const quantapdf_ir& ir)
{
    const auto *bytes=reinterpret_cast<const unsigned char *>(&ir);
    for(size_t i=0u;i<sizeof(ir);++i)
        if(bytes[i]!=0u)return false;
    return true;
}
static quantapdf_status parse(const std::string& bytes,size_t selected,
                               size_t parent_limit,quantapdf_ir *out)
{
    auto bounded=limits;bounded.max_parent_steps=parent_limit;
    return quantapdf_qpdf_source_scoped_mixed_v10_selected_to_ir(
        reinterpret_cast<const unsigned char *>(bytes.data()),bytes.size(),
        &bounded,selected,out);
}

static int test_source()
{
    const std::string good=pdf(mutation::none);
    CHECK(!good.empty());
    const uint32_t colors[]={UINT32_C(0xffff0000),
                             UINT32_C(0xff00ff00),
                             UINT32_C(0xff0000ff)};
    size_t positive=0u,negative=0u;
    for(size_t i=0u;i<3u;++i) {
        quantapdf_ir ir{};
        const auto status=parse(good,i,8u,&ir);
        if(status!=QUANTAPDF_OK) {
            std::fprintf(stderr,"valid selected Page %zu status=%d\n",
                         i,static_cast<int>(status));
            CHECK(false);
        }
        CHECK(ir.page_count==1u && ir.pages!=nullptr &&
              ir.operation_count==1u && ir.operations!=nullptr &&
              ir.operations[0].kind==QUANTAPDF_COMPOSER_OPERATION_PATH &&
              ir.operations[0].value.path.options.fill_argb==colors[i]);
        quantapdf_ir_dispose(&ir);
        CHECK(zero_ir(ir));
        ++positive;
    }
    for(const auto& entry:cases) {
        const std::string original=pdf(entry.fault);
        CHECK(!original.empty());
        for(size_t selected:{0u,0u,1u}) {
            quantapdf_ir ir{};
            std::memset(&ir,0xa5,sizeof(ir));
            const auto status=parse(original,selected,8u,&ir);
            if(status!=entry.expected || !zero_ir(ir)) {
                std::fprintf(stderr,"bad PageTree %s selected=%zu "
                    "status=%d expected=%d zero=%d\n",entry.name,
                    selected,static_cast<int>(status),
                    static_cast<int>(entry.expected),zero_ir(ir)?1:0);
                if(status==QUANTAPDF_OK)quantapdf_ir_dispose(&ir);
                CHECK(false);
            }
            ++negative;
        }
    }
    const std::array<std::array<size_t,2u>,3u> budgets={{
        {{0u,2u}},{{3u,8u}},{{0u,0u}}
    }};
    for(const auto& b:budgets) {
        quantapdf_ir ir{};
        std::memset(&ir,0xa5,sizeof(ir));
        CHECK(parse(good,b[0],b[1],&ir)==QUANTAPDF_ERROR_UNSUPPORTED &&
              zero_ir(ir));
        ++negative;
    }
    CHECK(positive==3u && negative==60u);
    std::printf("Source PageTree: %zu positives / %zu atomic refusals\n",
                positive,negative);
    return EXIT_SUCCESS;
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
    const bool written=std::fwrite(bytes.data(),1u,bytes.size(),f)==
                       bytes.size();
    return std::fclose(f)==0 && written;
}
static uint32_t rgb(const unsigned char *b)
{
    return (static_cast<uint32_t>(b[0])<<16u) |
           (static_cast<uint32_t>(b[1])<<8u) |
            static_cast<uint32_t>(b[2]);
}
static int test_public_valid()
{
    CHECK(write_pdf(pdf(mutation::none)));
    quantapdf_document *doc=nullptr;
    CHECK(quantapdf_open(filename,nullptr,&doc)==QUANTAPDF_OK &&
          doc!=nullptr);
    CHECK(std::remove(filename)==0);
    int count=0;
    CHECK(quantapdf_page_count(doc,&count)==QUANTAPDF_OK && count==3);
    quantapdf_bitmap *held=nullptr;
    std::vector<unsigned char> copy;
    const uint32_t colors[]={UINT32_C(0xff0000),
                             UINT32_C(0x00ff00),
                             UINT32_C(0x0000ff)};
    for(int n:{0,1,2,0}) {
        quantapdf_page *page=nullptr;
        quantapdf_bitmap *bitmap=nullptr;
        CHECK(quantapdf_load_page(doc,n,&page)==QUANTAPDF_OK &&
              page!=nullptr);
        CHECK(quantapdf_render_page(page,&bitmap)==QUANTAPDF_OK &&
              bitmap!=nullptr);
        int w=0,h=0,stride=0,channels=0;
        const unsigned char *b=nullptr;
        size_t bytes=0u;
        CHECK(quantapdf_bitmap_dimensions(bitmap,&w,&h,&stride,&channels)==
              QUANTAPDF_OK &&
              w==16 && h==12 && stride==48 && channels==3);
        CHECK(quantapdf_bitmap_data(bitmap,&b,&bytes)==QUANTAPDF_OK &&
              b!=nullptr && bytes==576u);
        CHECK(rgb(b+(6u*16u+8u)*3u)==colors[n]);
        if(held==nullptr) {
            held=bitmap;copy.assign(b,b+bytes);
        } else {
            if(n==0)CHECK(std::memcmp(copy.data(),b,bytes)==0);
            quantapdf_drop_bitmap(bitmap);
        }
        quantapdf_drop_page(page);
    }
    quantapdf_close(doc);
    CHECK(held!=nullptr);
    const unsigned char *b=nullptr;
    size_t n=0u;
    CHECK(quantapdf_bitmap_data(held,&b,&n)==QUANTAPDF_OK &&
          n==copy.size() && std::memcmp(b,copy.data(),n)==0);
    quantapdf_drop_bitmap(held);
    return EXIT_SUCCESS;
}
static int test_public_invalid()
{
    size_t refused=0u;
    for(const auto& entry:cases) {
        CHECK(write_pdf(pdf(entry.fault)));
        quantapdf_document *doc=
            reinterpret_cast<quantapdf_document *>(uintptr_t(1));
        const auto open_status=quantapdf_open(filename,nullptr,&doc);
        CHECK(std::remove(filename)==0);
        if(open_status!=QUANTAPDF_OK) {
            CHECK(doc==nullptr);++refused;continue;
        }
        CHECK(doc!=nullptr);
        quantapdf_page *page=
            reinterpret_cast<quantapdf_page *>(uintptr_t(1));
        const auto load_status=quantapdf_load_page(doc,0,&page);
        if(load_status!=QUANTAPDF_OK)CHECK(page==nullptr);
        else {
            CHECK(page!=nullptr);
            quantapdf_bitmap *bitmap=
                reinterpret_cast<quantapdf_bitmap *>(uintptr_t(1));
            const auto status=quantapdf_render_page(page,&bitmap);
            if(status==QUANTAPDF_OK || bitmap!=nullptr) {
                std::fprintf(stderr,"public admitted malformed PageTree: "
                    "%s status=%d bitmap=%p\n",entry.name,
                    static_cast<int>(status),static_cast<void *>(bitmap));
                if(status==QUANTAPDF_OK && bitmap!=nullptr)
                    quantapdf_drop_bitmap(bitmap);
                quantapdf_drop_page(page);
                quantapdf_close(doc);
                CHECK(false);
            }
            quantapdf_drop_page(page);
        }
        quantapdf_close(doc);
        ++refused;
    }
    CHECK(refused==19u);
    std::printf("Public PageTree: %zu no-partial-bitmap refusals\n",refused);
    return EXIT_SUCCESS;
}
int main()
{
    CHECK(test_source()==EXIT_SUCCESS);
    CHECK(test_public_valid()==EXIT_SUCCESS);
    CHECK(test_public_invalid()==EXIT_SUCCESS);
    return EXIT_SUCCESS;
}
