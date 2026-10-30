/* Native V10 exact external opaque-black PDF Source qualification.
 * The already reviewed 923-byte original remains unchanged: no PDFium,
 * Graph/Composer retry, second raster, relaxed quota or ABI expansion.
 * Generated negatives exercise typed inert /a0 gs and /Group admission
 * independently, including late unsupported operators and NULL Bitmap.
 */
#include <quantapdf/quantapdf.h>
#include <qpdf/Pipeline.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#define CHECK(e) do { if(!(e)) { \
    std::fprintf(stderr,"Native external opaque gs:%d: %s\n", \
                 __LINE__,#e);return EXIT_FAILURE; \
} } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)
using Obj=QPDFObjectHandle;
static constexpr const char *generated_file="native-opaque-gs-input.pdf";

struct collect final : Pipeline {
    collect():Pipeline("opaque Source V10 diagnostic",nullptr){}
    std::string bytes;
    void write(unsigned char const *data,size_t size) override {
        if(bytes.size()+size<=1024u)
            bytes.append(reinterpret_cast<const char*>(data),size);
    }
    void finish() override {}
};

/* Only when the ORIGINAL public Native V10 admission fails: dump the
 * independent source PDF's actual Group, GS resource and decoded first
 * Contents. This is diagnostic evidence, never a reference renderer. */
static void dump_original(const std::string& filename)
{
    try {
        auto pdf=QPDF::create();
        pdf->setAttemptRecovery(false);
        pdf->setSuppressWarnings(true);
        pdf->processFile(filename.c_str());
        const auto pages=pdf->getAllPages();
        if(pages.empty())return;
        const Obj page=pages.front();
        Obj resources=page.getKey("/Resources");
        if(resources.isNull()) {
            Obj parent=page.getKey("/Parent");
            if(parent.isDictionary())
                resources=parent.getKey("/Resources");
        }
        const Obj group=page.getKey("/Group");
        const Obj states=resources.isDictionary()?
            resources.getKey("/ExtGState"):Obj::newNull();
        std::fprintf(stderr,"opaque source Group: %s\n",
                     group.unparse().c_str());
        std::fprintf(stderr,"opaque source ExtGState: %s\n",
                     states.unparse().c_str());
        Obj contents=page.getKey("/Contents");
        if(contents.isStream()) {
            collect sink;
            bool filtered=false;
            if(contents.pipeStreamData(&sink,&filtered,0,
                qpdf_dl_specialized,true,true))
                std::fprintf(stderr,"opaque decoded Content: %s\n",
                             sink.bytes.c_str());
        }
    } catch(const std::exception& e) {
        std::fprintf(stderr,"opaque Source diagnostic: %s\n",e.what());
    }
}

static bool write_pdf(const std::string& data)
{
    FILE *file=nullptr;
#ifdef _WIN32
    if(fopen_s(&file,generated_file,"wb")!=0)return false;
#else
    file=std::fopen(generated_file,"wb");
#endif
    if(file==nullptr)return false;
    const bool written=std::fwrite(data.data(),1u,data.size(),file)==
                       data.size();
    return std::fclose(file)==0 && written;
}

/* The tiny synthetic matrix is NOT an external corpus fixture and is
 * never counted as another reviewed original. QPDF creates only
 * original test-input PDF bytes, not native output or reference pixels. */
static std::string generated(const char *group,const char *state,
                             const std::string& ops,bool indirect=false)
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    auto page=pdf->makeIndirectObject(Obj::parse(
        "<< /Type /Page /MediaBox [0 0 16 12] >>"));
    page.replaceKey("/Parent",root);
    page.replaceKey("/Contents",pdf->newStream(ops));
    root.replaceKey("/Kids",Obj::newArray({page}));
    root.replaceKey("/Count",Obj::newInteger(1));
    if(group!=nullptr) {
        Obj object=Obj::parse(group);
        if(indirect)object=pdf->makeIndirectObject(object);
        page.replaceKey("/Group",object);
    }
    if(state!=nullptr) {
        Obj resources=Obj::newDictionary();
        Obj ext=Obj::newDictionary();
        ext.replaceKey("/a0",Obj::parse(state));
        resources.replaceKey("/ExtGState",ext);
        page.replaceKey("/Resources",resources);
    } else page.replaceKey("/Resources",Obj::newDictionary());
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    const auto buffer=writer.getBufferSharedPointer();
    return std::string(
        reinterpret_cast<const char*>(buffer->getBuffer()),
        buffer->getSize());
}

struct scenario {
    const char *name;
    const char *group;
    const char *state;
    const char *ops;
    quantapdf_status result;
    bool indirect;
};
static const char *const opaque_group=
    "<< /Type /Group /S /Transparency /I true /K false /CS /DeviceRGB >>";
static const char *const opaque_state=
    "<< /Type /ExtGState /CA 1 /ca 1 /BM /Normal /SMask /None >>";
static const char *const black_paint=
    "q /a0 gs 0 0 16 12 re f Q ";
static const scenario cases[]={
    {"no-group-inert-gs",nullptr,opaque_state,
        black_paint,QUANTAPDF_OK,false},
    {"strict-group-direct",opaque_group,opaque_state,
        black_paint,QUANTAPDF_OK,false},
    {"strict-group-indirect",opaque_group,opaque_state,
        black_paint,QUANTAPDF_OK,true},
    {"group-without-gs",opaque_group,nullptr,
        "0 0 16 12 re f ",QUANTAPDF_OK,false},
    {"group-no-K","<< /S /Transparency /I true /CS /DeviceRGB >>",
        opaque_state,black_paint,QUANTAPDF_OK,false},
    {"group-nonisolate","<< /S /Transparency /I false /CS /DeviceRGB >>",
        opaque_state,black_paint,QUANTAPDF_ERROR_UNSUPPORTED,false},
    {"group-knockout","<< /S /Transparency /I true /K true /CS /DeviceRGB >>",
        opaque_state,black_paint,QUANTAPDF_ERROR_UNSUPPORTED,false},
    {"group-cmyk","<< /S /Transparency /I true /CS /DeviceCMYK >>",
        opaque_state,black_paint,QUANTAPDF_ERROR_UNSUPPORTED,false},
    {"group-additional-key",
        "<< /S /Transparency /I true /CS /DeviceRGB /Extra 0 >>",
        opaque_state,black_paint,QUANTAPDF_ERROR_UNSUPPORTED,false},
    {"group-wrong-I",
        "<< /S /Transparency /I /true /CS /DeviceRGB >>",
        opaque_state,black_paint,QUANTAPDF_ERROR_FORMAT,false},
    {"group-wrong-Type",
        "<< /Type /Other /S /Transparency /I true /CS /DeviceRGB >>",
        opaque_state,black_paint,QUANTAPDF_ERROR_UNSUPPORTED,false},
    {"gs-missing",nullptr,nullptr,
        black_paint,QUANTAPDF_ERROR_FORMAT,false},
    {"gs-wrong-type",nullptr,"12",
        black_paint,QUANTAPDF_ERROR_FORMAT,false},
    {"gs-partial-opacity",opaque_group,"<< /CA 1 /ca 0.5 >>",
        black_paint,QUANTAPDF_ERROR_UNSUPPORTED,false},
    {"gs-stroke-opacity",opaque_group,"<< /CA 0.5 /ca 1 >>",
        black_paint,QUANTAPDF_ERROR_UNSUPPORTED,false},
    {"gs-multiply",opaque_group,"<< /CA 1 /ca 1 /BM /Multiply >>",
        black_paint,QUANTAPDF_ERROR_UNSUPPORTED,false},
    {"gs-soft-mask",opaque_group,
        "<< /CA 1 /ca 1 /SMask << /S /Alpha >> >>",
        black_paint,QUANTAPDF_ERROR_UNSUPPORTED,false},
    {"gs-unknown-key",opaque_group,"<< /CA 1 /ca 1 /TR /Identity >>",
        black_paint,QUANTAPDF_ERROR_UNSUPPORTED,false},
    {"gs-overprint",opaque_group,"<< /CA 1 /ca 1 /op true >>",
        black_paint,QUANTAPDF_ERROR_UNSUPPORTED,false},
    {"gs-invalid-alpha",opaque_group,"<< /ca /one >>",
        black_paint,QUANTAPDF_ERROR_FORMAT,false},
    {"gs-unsafe-AIS",opaque_group,"<< /ca 1 /AIS true >>",
        black_paint,QUANTAPDF_ERROR_UNSUPPORTED,false},
    {"gs-late-text",opaque_group,opaque_state,
        "q /a0 gs 0 0 16 12 re f Q BT ET",
        QUANTAPDF_ERROR_UNSUPPORTED,false},
    {"gs-late-stroke",opaque_group,opaque_state,
        "q /a0 gs 0 0 16 12 re f Q "
        "0 0 m 16 12 l S",QUANTAPDF_ERROR_UNSUPPORTED,false}
};

static int run(const std::string& original)
{
    quantapdf_context *context=nullptr;
    quantapdf_bitmap *retained=nullptr;
    std::vector<unsigned char> original_rgb;
    OK(quantapdf_context_create(&context));
    size_t positives=0u,refusals=0u;
    const size_t entries=sizeof(cases)/sizeof(cases[0]);

    for(size_t idx=0u;idx<=entries;++idx) {
        const bool external=idx==0u;
        const char *name=external?"external-black":cases[idx-1u].name;
        const quantapdf_status want=external?QUANTAPDF_OK:
            cases[idx-1u].result;
        const std::string bytes=external?std::string():
            generated(cases[idx-1u].group,cases[idx-1u].state,
                      cases[idx-1u].ops,cases[idx-1u].indirect);
        if(!external)CHECK(write_pdf(bytes));
        const std::string filename=external?original:
            std::string(generated_file);
        quantapdf_document *doc=nullptr;
        quantapdf_page *page=nullptr;
        quantapdf_bitmap *bitmap=
            reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
        OK(quantapdf_open_with_context(context,filename.c_str(),nullptr,&doc));
        CHECK(doc!=nullptr);
        if(!external)CHECK(std::remove(generated_file)==0);
        OK(quantapdf_load_page(doc,0,&page));
        CHECK(page!=nullptr);
        quantapdf_render_options options{};
        options.struct_size=sizeof(options);options.dpi=72.0f;
        const auto status=quantapdf_render_page_with_options(
            page,&options,&bitmap);
        if(status!=want ||
           (status==QUANTAPDF_OK?bitmap==nullptr:bitmap!=nullptr)) {
            std::fprintf(stderr,
                "opaque-source case=%s got=%d expected=%d bitmap=%p\n",
                name,static_cast<int>(status),static_cast<int>(want),
                static_cast<void*>(bitmap));
            if(external)dump_original(filename);
            if(status==QUANTAPDF_OK && bitmap!=nullptr)
                quantapdf_drop_bitmap(bitmap);
            quantapdf_drop_page(page);
            quantapdf_close(doc);
            CHECK(false);
        }
        if(status==QUANTAPDF_OK) {
            int w=0,h=0,stride=0,channels=0;
            const unsigned char *data=nullptr;
            size_t count=0u;
            OK(quantapdf_bitmap_dimensions(
                bitmap,&w,&h,&stride,&channels));
            OK(quantapdf_bitmap_data(bitmap,&data,&count));
            const int ew=external?612:16,eh=external?792:12;
            CHECK(w==ew && h==eh && stride==w*3 && channels==3 &&
                  data!=nullptr &&
                  count==static_cast<size_t>(stride)*
                         static_cast<size_t>(h));
            for(size_t i=0u;i<count;++i)
                CHECK(data[i]==0u); /* Independent solid-black golden. */
            if(retained==nullptr) {
                retained=bitmap;
                original_rgb.assign(data,data+count);
            } else quantapdf_drop_bitmap(bitmap);
            ++positives;
        } else ++refusals;
        quantapdf_drop_page(page);
        quantapdf_close(doc);
        if(retained!=nullptr) {
            const unsigned char *data=nullptr;
            size_t count=0u;
            OK(quantapdf_bitmap_data(retained,&data,&count));
            CHECK(data!=nullptr && count==original_rgb.size() &&
                  std::memcmp(data,original_rgb.data(),count)==0);
        }
    }
    quantapdf_drop_context(context);
    const unsigned char *data=nullptr;
    size_t count=0u;
    OK(quantapdf_bitmap_data(retained,&data,&count));
    CHECK(data!=nullptr && count==original_rgb.size() &&
          std::memcmp(data,original_rgb.data(),count)==0);
    quantapdf_drop_bitmap(retained);
    CHECK(positives==6u && refusals==18u);
    std::printf("Native original opaque gs/group: 1 real external solid-black, "
                "5 generated black positives, 18 exact atomic refusals\n");
    return EXIT_SUCCESS;
}
int main(int argc,char **argv)
{
    if(argc!=2 || argv[1]==nullptr)return EXIT_FAILURE;
    const auto external=std::filesystem::path(argv[1])/
        "pdfium.black-original-v1.pdf";
    return run(external.string());
}
