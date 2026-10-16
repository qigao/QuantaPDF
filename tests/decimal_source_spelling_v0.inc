/* #360 test-owned literal-PDF Source->Graph diagnostic, not production.
 * Included only by test_native_source_qq_oracle.cpp, before main(). */
namespace {
struct decimal360_case {
    const char *name;
    const char *token[4];
    int64_t scaled10000[4];
    int rotation;
    bool inherited;
    bool fractional_span;
};
static const decimal360_case decimal360_cases[] = {
    {"baseline",      {"2.375","3.375","12.375","11.375"},          {23750,33750,123750,113750},0,true,false},
    {"trailing-zero", {"2.3750","3.37500","12.3750","11.37500"},     {23750,33750,123750,113750},90,false,false},
    {"leading-zero",  {"02.375","03.375","012.375","011.375"},       {23750,33750,123750,113750},180,true,false},
    {"positive-sign", {"+2.375","+3.375","+12.375","+11.375"},       {23750,33750,123750,113750},270,false,false},
    {"eighth",        {"2.125","3.125","12.125","11.125"},           {21250,31250,121250,111250},0,false,false},
    {"quarter",       {"2.25","3.25","12.25","11.25"},               {22500,32500,122500,112500},90,true,false},
    {"half",          {"2.5","3.5","12.5","11.5"},                   {25000,35000,125000,115000},180,false,false},
    {"five-eighth",   {"2.625","3.625","12.625","11.625"},           {26250,36250,126250,116250},270,true,false},
    {"three-quarter", {"2.75","3.75","12.75","11.75"},               {27500,37500,127500,117500},0,false,false},
    {"seven-eighth",  {"2.875","3.875","12.875","11.875"},           {28750,38750,128750,118750},90,true,false},
    {"negative",      {"-2.375","-3.375","7.625","4.625"},          {-23750,-33750,76250,46250},180,false,false},
    {"correctround-ulp",{"6.004","3.004","16.004","11.004"},           {60040,30040,160040,110040},0,true,false},
    {"fractional",    {"2.375","3.375","12.3751","11.3751"},         {23750,33750,123751,113751},0,true,true},
    {"fractional-90", {"2.3750","3.3750","12.3751","11.3751"},      {23750,33750,123751,113751},270,false,true}
};
static std::vector<unsigned char> decimal360_read_bytes(const char *path)
{
    FILE *f=std::fopen(path,"rb"); CHECK(f!=nullptr);
    CHECK(std::fseek(f,0,SEEK_END)==0);
    const long length=std::ftell(f);
    CHECK(length>0 && length<=65536 && std::fseek(f,0,SEEK_SET)==0);
    std::vector<unsigned char> bytes((size_t)length);
    CHECK(std::fread(bytes.data(),1,bytes.size(),f)==bytes.size());
    CHECK(std::fclose(f)==0);
    return bytes;
}
static uint64_t decimal360_fnv64(const std::vector<unsigned char>& bytes)
{
    uint64_t v=UINT64_C(14695981039346656037);
    for(unsigned char c:bytes){ v^=c; v*=UINT64_C(1099511628211); }
    return v;
}
static std::string decimal360_crop(const decimal360_case& c)
{
    std::string s="[";
    for(unsigned i=0;i<4;++i) {
        if(i) s+=" ";
        s+=c.token[i];
    }
    return s+"]";
}
/* Small fully authored PDF ORIGINAL, no QPDF reserialization or downstream IR
 * export. Correct byte offsets and stream length are written once. */
static void decimal360_write_pdf(const char *path,const decimal360_case& c)
{
    const std::string crop=decimal360_crop(c);
    const std::string commands="1 0 0 rg 4 5 6 4 re f\n";
    const std::string rotate="/Rotate "+std::to_string(c.rotation);
    const std::string pages="<< /Type /Pages /Kids [3 0 R] /Count 1"
        " /MediaBox [-20 -20 20 30]"+
        (c.inherited?" /CropBox "+crop+" "+rotate:"")+ " >>";
    const std::string page="<< /Type /Page /Parent 2 0 R"
        " /Resources << >> /Contents 4 0 R /UserUnit 7"+
        (c.inherited?std::string(""):" /CropBox "+crop+" "+rotate)+" >>";
    const std::string objects[4]={
        "<< /Type /Catalog /Pages 2 0 R >>",
        pages,
        page,
        "<< /Length "+std::to_string(commands.size())+" >>\nstream\n"+
            commands+"endstream"
    };
    std::string pdf="%PDF-1.7\n%QuantaPDF test-original #360\n";
    size_t positions[5]={0u,0u,0u,0u,0u};
    for(unsigned i=1;i<=4;++i) {
        positions[i]=pdf.size();
        pdf+=std::to_string(i)+" 0 obj\n"+objects[i-1u]+"\nendobj\n";
    }
    const size_t xref=pdf.size();
    pdf+="xref\n0 5\n0000000000 65535 f \n";
    for(unsigned i=1;i<=4;++i) {
        char line[40];
        const int n=std::snprintf(line,sizeof(line),
            "%010llu 00000 n \n",(unsigned long long)positions[i]);
        CHECK(n>0 && (size_t)n<sizeof(line));
        pdf.append(line,(size_t)n);
    }
    pdf+="trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n"+
        std::to_string(xref)+"\n%%EOF\n";
    FILE *f=std::fopen(path,"wb"); CHECK(f!=nullptr);
    CHECK(std::fwrite(pdf.data(),1,pdf.size(),f)==pdf.size());
    CHECK(std::fclose(f)==0);
}
static uint64_t decimal360_bits(double x)
{
    static_assert(sizeof(double)==sizeof(uint64_t),"binary64 required");
    uint64_t v=0; std::memcpy(&v,&x,sizeof(v)); return v;
}
static uint32_t decimal360_expected_edge(int64_t lo,int64_t hi,unsigned dpi)
{
    CHECK(hi>lo && hi-lo<=2000000);
    const uint64_t n=(uint64_t)(hi-lo)*7u*dpi;
    const uint64_t d=10000u*72u;
    const uint64_t result=n/d+(n%d!=0u);
    CHECK(result>0u && result<500u);
    return (uint32_t)result;
}
#include "decimal_q4_consumer_v0.inc"

static int test_decimal360_source_spelling(const char *report_path)
{
    CHECK(report_path!=nullptr && *report_path);
    const char *source=GENERATED_UNIT7_DECIMAL_SPELL_PDF;
    CHECK(source!=nullptr && *source && std::strcmp(source,report_path)!=0);
    const std::string stage=std::string(report_path)+".tmp";
    CHECK(stage!=source);
    (void)std::remove(report_path); (void)std::remove(stage.c_str());
    const size_t cases=sizeof(decimal360_cases)/sizeof(decimal360_cases[0]);
    const unsigned dpis[]={72u,96u,144u};
    /* Even an independently correctly-rounded decimal-to-binary64 lexer
     * cannot guarantee that endpoint subtraction is an exact PDF span.
     * Exact textual 6.004 -> 16.004 is 10, yet these correctly-rounded
     * binary64 endpoints subtract to one positive ULP above 10. */
    const uint64_t lo_bits=UINT64_C(0x401804189374bc6a);
    const uint64_t hi_bits=UINT64_C(0x4030010624dd2f1b);
    double lo=0.0,hi=0.0;
    std::memcpy(&lo,&lo_bits,sizeof(lo));
    std::memcpy(&hi,&hi_bits,sizeof(hi));
    CHECK(decimal360_bits(lo)==lo_bits && decimal360_bits(hi)==hi_bits);
    CHECK(hi-lo>10.0 && std::ceil((hi-lo)*7.0)==71.0);
    size_t source_ok=0u,spell_ok=0u,repeat_ok=0u,frame_count=0u;
    size_t exact_pdfium_ok=0u,raw_grid_diff=0u;
    size_t fractional_pdfium_diff=0u,unsupported_zero=0u;
    size_t graph_exact_pdfium_ok=0u,graph_exact_denied=0u;
    size_t handoff_repeated=0u,handoff_after_drop=0u;
    size_t handoff_corner_edges=0u,handoff_legacy_affine_equal=0u;
    size_t handoff_unsupported_zero=0u,handoff_budget_zero=0u;
    size_t handoff_invalid_argument_zero=0u;
    size_t q4_consumer_frames=0u,q4_valid_samples=0u;
    size_t q4_legacy_exact_samples=0u,q4_discrepant_samples=0u;
    size_t q4_sample_zero_refusals=0u,q4_work_acceptances=0u;
    size_t q4_work_budget_refusals=0u,q4_edge_budget_refusals=0u;
    size_t graph_exact_budget_refusals=0u;
    size_t negative_lexer_refusals=0u;
    const char *const invalid_numbers[]={
        "", "-", "+", "-.", ".", "1e2", "NaN", "Inf",
        "1,5", "1.2.3", "1 2", "1/2", "1.12345678901",
        "999999999999999999999999"
    };
    for(const char *invalid:invalid_numbers) {
        int64_t units=INT64_C(1234);
        unsigned scale=987u;
        const quantapdf_status st=quantapdf_test_decimal_exact_token_v0(
            (const unsigned char *)invalid,std::strlen(invalid),
            &units,&scale);
        CHECK((st==QUANTAPDF_ERROR_FORMAT ||
               st==QUANTAPDF_ERROR_UNSUPPORTED) &&
              units==0 && scale==0);
        ++negative_lexer_refusals;
    }
    {
        char overlength[66];
        std::memset(overlength,'1',65u);
        overlength[65]='\0';
        int64_t units=INT64_C(1234); unsigned scale=987u;
        CHECK(quantapdf_test_decimal_exact_token_v0(
            (const unsigned char *)overlength,65u,
            &units,&scale)==QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(units==0 && scale==0);
        ++negative_lexer_refusals;
    }
    {
        int64_t units=0; unsigned scale=987u;
        const char *negative_zero="-0.000000000000000000";
        OK(quantapdf_test_decimal_exact_token_v0(
            (const unsigned char *)negative_zero,std::strlen(negative_zero),
            &units,&scale));
        CHECK(units==0 && scale==0u);
    }
    FILE *out=std::fopen(stage.c_str(),"wb");CHECK(out!=nullptr);
    std::fprintf(out,"{\n \"schema_version\":1,\n"
        " \"source_kind\":\"test_generated_original_pdf\",\n"
        " \"origin\":\"literal_pdf_bytes_not_reexport\",\n"
        " \"decimal_contract_test_only\":true,\n"
        " \"correct_rounded_double_still_inflates\":true,\n"
        " \"native_render\":\"not_run\",\n"
        " \"public_sdk_backend_switched\":false,\n"
        " \"external_native_positive_increment\":0,\n"
        " \"cases\":[\n");
    for(size_t k=0;k<cases;++k){
        const auto& c=decimal360_cases[k];
        decimal360_write_pdf(source,c);
        const auto original=decimal360_read_bytes(source);
        const std::string literal=decimal360_crop(c);
        CHECK(std::search(original.begin(),original.end(),
            literal.begin(),literal.end())!=original.end());
        const uint64_t file_hash=decimal360_fnv64(original);
        quantapdf_document *doc=nullptr;
        quantapdf_graph_snapshot *snapshot=nullptr;
        OK(quantapdf_open(source,nullptr,&doc));
        OK(quantapdf_document_capture_graph(doc,&graph_limits,&snapshot));
        quantapdf_close(doc);
        CHECK(snapshot && snapshot->graph);
        const quantapdf_graph_model *model=
            quantapdf_graph_view(snapshot->graph);
        CHECK(model && model->page_count==1u);
        const quantapdf_graph_edge *tree=quantapdf_graph_find(
            model,model->catalog,"/Pages",6u);
        CHECK(tree!=nullptr);
        const quantapdf_local_id leaf=
            model->edges[model->page_offset].target;
        const quantapdf_graph_edge *box=quantapdf_graph_find(
            model,c.inherited?tree->target:leaf,"/CropBox",8u);
        CHECK(box!=nullptr);
        const quantapdf_graph_node *box_node=
            quantapdf_graph_node_at(model,box->target);
        CHECK(box_node && box_node->kind==QUANTAPDF_GRAPH_ARRAY &&
            box_node->edge_count==4u);
        for(size_t edge=0;edge<4u;++edge){
            const quantapdf_graph_node *v=quantapdf_graph_node_at(
                model,model->edges[box_node->edge_offset+edge].target);
            CHECK(v && v->kind==QUANTAPDF_GRAPH_REAL &&
                  v->data_size==std::strlen(c.token[edge]) &&
                  std::memcmp(model->bytes+v->data_offset,
                      c.token[edge],v->data_size)==0);
            int64_t units=0; unsigned scale=0;
            OK(quantapdf_test_decimal_exact_token_v0(
                model->bytes+v->data_offset,v->data_size,
                &units,&scale));
            const int64_t pow10_scaled[]={1,10,100,1000,10000};
            CHECK(scale<=4u &&
                  units*pow10_scaled[4u-scale]==c.scaled10000[edge]);
            ++spell_ok;
        }
        quantapdf_graph_page_geometry geometry{};
        OK(quantapdf_graph_query_page_geometry(snapshot->graph,0u,4u,&geometry));
        CHECK(geometry.rotation==c.rotation && geometry.user_unit==7.0 &&
              geometry.media_box[0]==-20.0 &&
              geometry.media_box[1]==-20.0 &&
              geometry.media_box[2]==20.0 &&
              geometry.media_box[3]==30.0);
        uint64_t raw_bits[4]={};
        for(unsigned edge=0;edge<4;++edge){
            CHECK(std::isfinite(geometry.crop_box[edge]) &&
                  std::fabs(geometry.crop_box[edge]-
                    (double)c.scaled10000[edge]/10000.0)<1e-9);
            raw_bits[edge]=decimal360_bits(geometry.crop_box[edge]);
        }
        /* Prepare value-only metadata BEFORE the Graph Snapshot dies.
         * No source/provider/DSO-owned pointer is allowed to escape. */
        quantapdf_graph_device_grid handoff[3]={};
        uint64_t affine_bits[3][6]={{0}};
        for(unsigned j=0u;j<3u;++j) {
            quantapdf_graph_device_grid again{};
            std::memset(&again,0xa5,sizeof(again));
            OK(quantapdf_test_graph_device_handoff_v0(
                snapshot->graph,0u,4u,dpis[j],20000u,60000u,
                &handoff[j]));
            OK(quantapdf_test_graph_device_handoff_v0(
                snapshot->graph,0u,4u,dpis[j],20000u,60000u,
                &again));
            CHECK(handoff[j].width==again.width &&
                  handoff[j].height==again.height &&
                  handoff[j].stride_bytes==again.stride_bytes &&
                  handoff[j].output_bytes==again.output_bytes);
            for(unsigned component=0u;component<6u;++component) {
                affine_bits[j][component]=
                    decimal360_bits(handoff[j].from_source[component]);
                CHECK(std::isfinite(handoff[j].from_source[component]) &&
                      affine_bits[j][component]==
                        decimal360_bits(again.from_source[component]));
            }
            ++handoff_repeated;
        }
        quantapdf_test_decimal_grid_v0 graph_exact[3]={};
        for(unsigned j=0u;j<3u;++j) {
            OK(quantapdf_test_graph_exact_decimal_grid_v0(
                snapshot->graph,0u,4u,dpis[j],20000u,60000u,
                &graph_exact[j]));
            CHECK(graph_exact[j].width>0u &&
                  graph_exact[j].height>0u &&
                  graph_exact[j].stride_bytes==
                      3u*(size_t)graph_exact[j].width &&
                  graph_exact[j].output_bytes==
                      graph_exact[j].stride_bytes*
                      (size_t)graph_exact[j].height);
        }
        for(unsigned forbidden:{120u,168u}) {
            quantapdf_test_decimal_grid_v0 denied{},zero{};
            std::memset(&denied,0xa5,sizeof(denied));
            CHECK(quantapdf_test_graph_exact_decimal_grid_v0(
                snapshot->graph,0u,4u,forbidden,20000u,60000u,
                &denied)==QUANTAPDF_ERROR_UNSUPPORTED);
            CHECK(std::memcmp(&denied,&zero,sizeof(denied))==0);
            ++graph_exact_denied;
        }
        for(unsigned budget_case=0u;budget_case<2u;++budget_case) {
            quantapdf_test_decimal_grid_v0 denied{},zero{};
            std::memset(&denied,0xa5,sizeof(denied));
            CHECK(quantapdf_test_graph_exact_decimal_grid_v0(
                snapshot->graph,0u,4u,72u,
                budget_case==0u?1u:20000u,
                budget_case==1u?1u:60000u,
                &denied)==QUANTAPDF_ERROR_UNSUPPORTED);
            CHECK(std::memcmp(&denied,&zero,sizeof(denied))==0);
            ++graph_exact_budget_refusals;
        }
        /* Independent unsupported/budget/argument failure classes: the
         * entire published metadata structure stays zero, not just size. */
        for(unsigned forbidden:{120u,168u}) {
            quantapdf_graph_device_grid denied{},zero{};
            std::memset(&denied,0xa5,sizeof(denied));
            CHECK(quantapdf_test_graph_device_handoff_v0(
                snapshot->graph,0u,4u,forbidden,20000u,60000u,
                &denied)==QUANTAPDF_ERROR_UNSUPPORTED);
            CHECK(std::memcmp(&denied,&zero,sizeof(denied))==0);
            ++handoff_unsupported_zero;
        }
        for(unsigned budget=0u;budget<2u;++budget) {
            quantapdf_graph_device_grid denied{},zero{};
            std::memset(&denied,0xa5,sizeof(denied));
            CHECK(quantapdf_test_graph_device_handoff_v0(
                snapshot->graph,0u,4u,72u,
                budget==0u?1u:20000u,
                budget==1u?1u:60000u,&denied)==
                    QUANTAPDF_ERROR_UNSUPPORTED);
            CHECK(std::memcmp(&denied,&zero,sizeof(denied))==0);
            ++handoff_budget_zero;
        }
        for(unsigned invalid=0u;invalid<2u;++invalid) {
            quantapdf_graph_device_grid denied{},zero{};
            std::memset(&denied,0xa5,sizeof(denied));
            CHECK(quantapdf_test_graph_device_handoff_v0(
                snapshot->graph,invalid==0u?1u:0u,
                invalid==1u?0u:4u,72u,20000u,60000u,
                &denied)==QUANTAPDF_ERROR_ARGUMENT);
            CHECK(std::memcmp(&denied,&zero,sizeof(denied))==0);
            ++handoff_invalid_argument_zero;
        }
        quantapdf_graph_snapshot_drop(snapshot);
        snapshot=nullptr;
        /* Source and Graph ownership is now GONE; only copied doubles,
         * exact integer bounds and metadata remain. Exercise their affine
         * on all four CropBox corners without reaccessing any Graph data. */
        for(unsigned j=0u;j<3u;++j) {
            const auto& value=handoff[j];
            CHECK(value.width==graph_exact[j].width &&
                  value.height==graph_exact[j].height &&
                  value.stride_bytes==graph_exact[j].stride_bytes &&
                  value.output_bytes==graph_exact[j].output_bytes);
            double minx=1e100,miny=1e100,maxx=-1e100,maxy=-1e100;
            for(unsigned corner=0u;corner<4u;++corner) {
                const double x=geometry.crop_box[(corner&1u)?2u:0u];
                const double y=geometry.crop_box[(corner&2u)?3u:1u];
                const double px=value.from_source[0]*x+
                    value.from_source[2]*y+value.from_source[4];
                const double py=value.from_source[1]*x+
                    value.from_source[3]*y+value.from_source[5];
                CHECK(std::isfinite(px) && std::isfinite(py));
                minx=std::min(minx,px);
                maxx=std::max(maxx,px);
                miny=std::min(miny,py);
                maxy=std::max(maxy,py);
                ++handoff_corner_edges;
            }
            /* This is ONLY an affine edge sanity check, never a sizing
             * epsilon or pixel parity assumption. */
            CHECK(std::fabs(minx)<1e-9 && std::fabs(miny)<1e-9 &&
                  std::fabs(maxx-(double)value.width)<1e-9 &&
                  std::fabs(maxy-(double)value.height)<1e-9);
            for(unsigned component=0u;component<6u;++component)
                CHECK(affine_bits[j][component]==
                    decimal360_bits(value.from_source[component]));
            ++handoff_after_drop;
        }
        ++source_ok;
        const uint32_t w72=decimal360_expected_edge(
            c.scaled10000[0],c.scaled10000[2],72u);
        const uint32_t h72=decimal360_expected_edge(
            c.scaled10000[1],c.scaled10000[3],72u);
        CHECK(w72==(c.fractional_span?71u:70u) &&
              h72==(c.fractional_span?57u:56u));
        for(unsigned dpi:{120u,168u}){
            quantapdf_graph_device_grid denied{},zero{};
            std::memset(&denied,0xa5,sizeof(denied));
            CHECK(quantapdf_graph_page_device_grid(&geometry,
                dpi,20000u,60000u,&denied)==QUANTAPDF_ERROR_UNSUPPORTED);
            CHECK(std::memcmp(&denied,&zero,sizeof(denied))==0);
            ++unsupported_zero;
        }
        quantapdf_document *reference=nullptr;
        quantapdf_page *page=nullptr;
        OK(quantapdf_open(source,nullptr,&reference));
        OK(quantapdf_load_page(reference,0u,&page));
        std::fprintf(out,"  {\"case\":\"%s\",\"crop\":\"%s\","
            "\"rotation\":%d,\"inherited\":%s,"
            "\"original_bytes\":%zu,\"source_fnv64\":\"%016llx\","
            "\"raw_crop_binary64\":[\"%016llx\",\"%016llx\","
            "\"%016llx\",\"%016llx\"],\"frames\":[\n",
            c.name,literal.c_str(),c.rotation,c.inherited?"true":"false",
            original.size(),(unsigned long long)file_hash,
            (unsigned long long)raw_bits[0],(unsigned long long)raw_bits[1],
            (unsigned long long)raw_bits[2],(unsigned long long)raw_bits[3]);
        for(unsigned j=0u;j<3u;++j){
            const unsigned dpi=dpis[j];
            const uint32_t expected_w=decimal360_expected_edge(
                c.scaled10000[0],c.scaled10000[2],dpi);
            const uint32_t expected_h=decimal360_expected_edge(
                c.scaled10000[1],c.scaled10000[3],dpi);
            const uint32_t ew=(c.rotation%180)?expected_h:expected_w;
            const uint32_t eh=(c.rotation%180)?expected_w:expected_h;
            CHECK(graph_exact[j].width==ew &&
                  graph_exact[j].height==eh &&
                  handoff[j].width==ew && handoff[j].height==eh);
            quantapdf_graph_device_grid raw_grid{};
            OK(quantapdf_graph_page_device_grid(
                &geometry,dpi,20000u,60000u,&raw_grid));
            const quantapdf_render_options options={
                sizeof(quantapdf_render_options),(float)dpi,0.0f,0,{0},0
            };
            quantapdf_bitmap *first=nullptr,*second=nullptr;
            OK(quantapdf_render_page_with_options(page,&options,&first));
            OK(quantapdf_render_page_with_options(page,&options,&second));
            int pw=0,ph=0,ps=0,pc=0,sw=0,sh=0,ss=0,sc=0;
            OK(quantapdf_bitmap_dimensions(first,&pw,&ph,&ps,&pc));
            OK(quantapdf_bitmap_dimensions(second,&sw,&sh,&ss,&sc));
            CHECK(pw>0 && ph>0 && pc==3 && ps>=pw*3 &&
                  ps<=pw*3+4 && sw==pw && sh==ph && ss==ps && sc==pc);
            const unsigned char *a=nullptr,*b=nullptr;
            size_t na=0u,nb=0u;
            OK(quantapdf_bitmap_data(first,&a,&na));
            OK(quantapdf_bitmap_data(second,&b,&nb));
            CHECK(a && b && na==nb && na==(size_t)ps*(size_t)ph &&
                  std::memcmp(a,b,na)==0);
            ++repeat_ok; ++frame_count;
            CHECK(graph_exact[j].width==(uint32_t)pw &&
                  graph_exact[j].height==(uint32_t)ph);
            ++graph_exact_pdfium_ok;
            const bool pdfium_matches=(pw==(int)ew && ph==(int)eh);
            if(!c.fractional_span){
                CHECK(pdfium_matches);
                ++exact_pdfium_ok;
            } else if(!pdfium_matches) ++fractional_pdfium_diff;
            const bool raw_matches=(raw_grid.width==(uint32_t)pw &&
                                    raw_grid.height==(uint32_t)ph);
            if(!raw_matches)++raw_grid_diff;
            else {
                for(unsigned component=0u;component<6u;++component)
                    CHECK(decimal360_bits(raw_grid.from_source[component])==
                          affine_bits[j][component]);
                ++handoff_legacy_affine_equal;
            }
            /* This is the EXISTING private Q4 sampler/edge-work consumer.
             * Both calls run AFTER Snapshot drop using value-only metadata.
             * The 12 old-size-mismatch frames are evaluated in their new
             * integer pixel space, with no Fill/W pixel-parity claim. */
            const auto q4=decimal360_probe_existing_q4_consumer(
                handoff[j],raw_grid,geometry,raw_matches);
            ++q4_consumer_frames;
            q4_valid_samples+=q4.valid_samples;
            q4_legacy_exact_samples+=q4.legacy_exact_samples;
            q4_discrepant_samples+=q4.size_discrepant_samples;
            q4_sample_zero_refusals+=q4.sample_refusals;
            q4_work_acceptances+=q4.q4_work_acceptances;
            q4_work_budget_refusals+=q4.work_budget_refusals;
            q4_edge_budget_refusals+=q4.edge_budget_refusals;
            std::fprintf(out,"   {\"dpi\":%u,\"exact_grid\":[%u,%u],"
                "\"original_pdfium\":[%d,%d],\"raw_graph_grid\":[%u,%u],"
                "\"exact_graph_grid\":[%u,%u],\"handoff_grid\":[%u,%u],"
                "\"handoff_affine_hex\":[",
                dpi,ew,eh,pw,ph,raw_grid.width,raw_grid.height,
                graph_exact[j].width,graph_exact[j].height,
                handoff[j].width,handoff[j].height);
            for(unsigned component=0u;component<6u;++component)
                std::fprintf(out,"\"%016llx\"%s",
                    (unsigned long long)affine_bits[j][component],
                    component==5u?"":",");
            std::fprintf(out, "],\"q4_consumer_samples\":%zu,"
                "\"q4_legacy_identical\":%zu,"
                "\"q4_size_discrepant\":%zu,"
                "\"q4_sample_fnv64\":\"%016llx\","
                "\"pdfium_matches_exact\":%s,"
                "\"raw_matches_pdfium\":%s}%s\n",
                q4.valid_samples,q4.legacy_exact_samples,
                q4.size_discrepant_samples,
                (unsigned long long)q4.sample_fnv64,
                pdfium_matches?"true":"false",
                raw_matches?"true":"false",j==2u?"":",");
            quantapdf_drop_bitmap(second);
            quantapdf_drop_bitmap(first);
        }
        std::fprintf(out,"  ]}%s\n",k+1u==cases?"":",");
        quantapdf_drop_page(page);
        quantapdf_close(reference);
        CHECK(decimal360_read_bytes(source)==original);
        CHECK(std::remove(source)==0);
    }
    CHECK(source_ok==cases && spell_ok==cases*4u &&
          frame_count==cases*3u && repeat_ok==frame_count &&
          unsupported_zero==cases*2u &&
          graph_exact_denied==cases*2u &&
          graph_exact_budget_refusals==cases*2u &&
          graph_exact_pdfium_ok==frame_count &&
          handoff_repeated==frame_count &&
          handoff_after_drop==frame_count &&
          handoff_corner_edges==frame_count*4u &&
          handoff_legacy_affine_equal+raw_grid_diff==frame_count &&
          handoff_unsupported_zero==cases*2u &&
          handoff_budget_zero==cases*2u &&
          handoff_invalid_argument_zero==cases*2u &&
          q4_consumer_frames==frame_count &&
          q4_valid_samples==frame_count*80u &&
          q4_legacy_exact_samples==handoff_legacy_affine_equal*80u &&
          q4_discrepant_samples==raw_grid_diff*80u &&
          q4_sample_zero_refusals==frame_count*7u &&
          q4_work_acceptances==frame_count &&
          q4_work_budget_refusals==frame_count &&
          q4_edge_budget_refusals==frame_count &&
          negative_lexer_refusals==15u &&
          exact_pdfium_ok==(cases-2u)*3u);
    std::fprintf(out," ],\n \"original_source_count\":%zu,"
        "\"source_spelling_matches\":%zu,"
        "\"pdfium_bitmap_repeat_count\":%zu,"
        "\"frame_count\":%zu,"
        "\"integer_span_pdfium_matches\":%zu,"
        "\"fractional_pdfium_mismatches\":%zu,"
        "\"raw_grid_pdfium_mismatches\":%zu,"
        "\"unsupported_dpi_zero_refusals\":%zu,"
        "\"graph_exact_candidate_test_only\":true,"
        "\"graph_exact_pdfium_matches\":%zu,"
        "\"graph_exact_unsupported_zero_refusals\":%zu,"
        "\"graph_exact_negative_lexer_refusals\":%zu,"
        "\"graph_exact_budget_zero_refusals\":%zu,"
        "\"handoff_value_only_test\":true,"
        "\"handoff_repeat_matches\":%zu,"
        "\"handoff_after_graph_drop\":%zu,"
        "\"handoff_corner_edge_checks\":%zu,"
        "\"handoff_legacy_affine_equal\":%zu,"
        "\"handoff_unsupported_dpi_zero\":%zu,"
        "\"handoff_budget_zero\":%zu,"
        "\"handoff_argument_zero\":%zu,"
        "\"q4_consumer_only\":true,"
        "\"q4_consumer_frames\":%zu,"
        "\"q4_consumer_valid_samples\":%zu,"
        "\"q4_legacy_bit_identical_samples\":%zu,"
        "\"q4_dimension_discrepant_samples\":%zu,"
        "\"q4_sample_zero_refusals\":%zu,"
        "\"q4_work_acceptances\":%zu,"
        "\"q4_work_budget_refusals\":%zu,"
        "\"q4_edge_budget_refusals\":%zu\n}\n",
        source_ok,spell_ok,repeat_ok,frame_count,exact_pdfium_ok,
        fractional_pdfium_diff,raw_grid_diff,unsupported_zero,
        graph_exact_pdfium_ok,graph_exact_denied,negative_lexer_refusals,
        graph_exact_budget_refusals,
        handoff_repeated,handoff_after_drop,handoff_corner_edges,
        handoff_legacy_affine_equal,
        handoff_unsupported_zero,handoff_budget_zero,
        handoff_invalid_argument_zero,
        q4_consumer_frames,q4_valid_samples,
        q4_legacy_exact_samples,q4_discrepant_samples,
        q4_sample_zero_refusals,q4_work_acceptances,
        q4_work_budget_refusals,q4_edge_budget_refusals);
    CHECK(std::ferror(out)==0 && std::fclose(out)==0);
    CHECK(std::rename(stage.c_str(),report_path)==0);
    CHECK(corpus_fixture_executions==0u);
    return EXIT_SUCCESS;
}
} /* test-only decimal #360 */
