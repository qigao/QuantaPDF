/* Independent test-only C11 rational-page-grid candidate admission (#360).
 * No PDFium, Graph, C++ or runtime providers; does NOT approve cutover. */
#include "decimal_rational_probe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr,"%s:%d rational candidate: %s\n",__FILE__,__LINE__,#x); \
    exit(EXIT_FAILURE); \
} } while(0)

static size_t accepted=0u, rejected=0u, monotonic=0u;
static size_t decimal_units_accepted=0u, decimal_units_rejected=0u;
static size_t unit_monotonic=0u;

static void accepted_decimal_unit(const char *box, int rotation,
                                  const char *unit, unsigned dpi,
                                  size_t capacity, size_t width, size_t height)
{
    quantapdf_test_rational_grid actual={777u,777u};
    CHECK(quantapdf_test_rational_page_grid_decimal_unit(
          box,rotation,unit,dpi,capacity,&actual));
    CHECK(actual.width==width && actual.height==height);
    ++decimal_units_accepted;
}

static void rejected_decimal_unit(const char *box, int rotation,
                                  const char *unit, unsigned dpi,
                                  size_t capacity)
{
    quantapdf_test_rational_grid actual={777u,777u};
    CHECK(!quantapdf_test_rational_page_grid_decimal_unit(
           box,rotation,unit,dpi,capacity,&actual));
    CHECK(actual.width==0u && actual.height==0u);
    ++decimal_units_rejected;
}


static void accepted_grid(const char *box,int rot,unsigned dpi,
                          size_t cap,size_t width,size_t height)
{
    quantapdf_test_rational_grid result={999u,999u};
    CHECK(quantapdf_test_rational_page_grid(box,rot,7u,dpi,cap,&result));
    CHECK(result.width==width && result.height==height);
    ++accepted;
}

static void rejected_grid(const char *box,int rot,unsigned unit,
                          unsigned dpi,size_t cap)
{
    quantapdf_test_rational_grid result={999u,999u};
    CHECK(!quantapdf_test_rational_page_grid(
          box,rot,unit,dpi,cap,&result));
    CHECK(result.width==0u && result.height==0u);
    ++rejected;
}


/* Independent deterministic integer-lattice oracle:
 * Generate source CropBox decimals and UserUnit from 1/1000 units, then
 * derive expected integer pixels DIRECTLY from the generated integer spans,
 * NOT by decoding decimal tokens or calling the candidate a second time.
 * No platform RNG, PDFium, locale, runtime provider or dynamic allocation. */
static uint32_t probe_next_u32(uint32_t *state)
{
    uint32_t x=*state;
    x^=x<<13;
    x^=x>>17;
    x^=x<<5;
    *state=x;
    return x;
}

static void probe_format_milli(int32_t milli,char out[32])
{
    const uint32_t mag=milli<0?(uint32_t)(-(int64_t)milli):(uint32_t)milli;
    const int n=snprintf(out,32,"%s%u.%03u",
        milli<0?"-":"",mag/1000u,mag%1000u);
    CHECK(n>0 && n<32);
}

static size_t probe_reference_grid(uint32_t span_milli,
                                    uint32_t unit_milli,unsigned dpi)
{
    const uint64_t numerator=(uint64_t)span_milli *
                             (uint64_t)unit_milli * (uint64_t)dpi;
    const uint64_t denominator=UINT64_C(1000)*UINT64_C(1000)*UINT64_C(72);
    CHECK(numerator>0u && numerator<UINT64_C(500000000000));
    return (size_t)(numerator/denominator +
                    (numerator%denominator!=0u));
}

static void probe_checksum_word(uint64_t *digest,uint64_t word)
{
    for(unsigned n=0u;n<8u;++n) {
        *digest^=(word>>(n*8u))&UINT64_C(255);
        *digest*=UINT64_C(1099511628211);
    }
}

static int probe_seeded_differential(const char *report_path)
{
    CHECK(report_path && *report_path);
    const char *const tmp_suffix=".tmp";
    char staged[4096];
    const int sn=snprintf(staged,sizeof(staged),"%s%s",report_path,tmp_suffix);
    CHECK(sn>0 && (size_t)sn<sizeof(staged));
    (void)remove(report_path);
    (void)remove(staged);
    uint32_t seed=UINT32_C(0x510d0360);
    uint64_t checksum=UINT64_C(14695981039346656037);
    size_t passes=0u,denials=0u,monotonic_pairs=0u,checked=0u;
    const unsigned dpis[]={72u,96u,144u};
    for(unsigned i=0u;i<4096u;++i) {
        const int32_t x=(int32_t)(probe_next_u32(&seed)%200001u)-100000;
        const int32_t y=(int32_t)(probe_next_u32(&seed)%200001u)-100000;
        const uint32_t span_w=1u+probe_next_u32(&seed)%7000u;
        const uint32_t span_h=1u+probe_next_u32(&seed)%7000u;
        const uint32_t unit_milli=1u+probe_next_u32(&seed)%18000u;
        char x0[32],y0[32],x1[32],y1[32],unit[32],box[160];
        probe_format_milli(x,x0);
        probe_format_milli(y,y0);
        probe_format_milli(x+(int32_t)span_w,x1);
        probe_format_milli(y+(int32_t)span_h,y1);
        const int n=snprintf(box,sizeof(box),"%s %s %s %s",
                             x0,y0,x1,y1);
        CHECK(n>0 && (size_t)n<sizeof(box) && n<=96);
        const int un=snprintf(unit,sizeof(unit),"%u.%03u",
                              unit_milli/1000u,unit_milli%1000u);
        CHECK(un>0 && un<32);
        for(unsigned d=0u;d<3u;++d) {
            const size_t base_w=probe_reference_grid(
                span_w,unit_milli,dpis[d]);
            const size_t base_h=probe_reference_grid(
                span_h,unit_milli,dpis[d]);
            const uint64_t area=(uint64_t)base_w*(uint64_t)base_h;
            CHECK(area>0u && area<UINT64_C(1000000));
            for(unsigned r=0u;r<4u;++r) {
                const size_t expected_w=(r==1u||r==3u)?base_h:base_w;
                const size_t expected_h=(r==1u||r==3u)?base_w:base_h;
                size_t capacity=1000000u;
                /* Test both exact-capacity admission and one-pixel denial,
                 * independently of decimal conversion and raster pixels. */
                if ((i+d+r)%17u==0u) capacity=(size_t)area-1u;
                else if ((i+d+r)%19u==0u) capacity=(size_t)area;
                else if ((i+d+r)%23u==0u) capacity=0u;
                const int should_admit=
                    capacity>0u && capacity<=1000000u &&
                    expected_h<=capacity &&
                    expected_w<=capacity/expected_h;
                quantapdf_test_rational_grid output={999u,999u};
                const int admitted=quantapdf_test_rational_page_grid_decimal_unit(
                    box,(int)(r*90u),unit,dpis[d],capacity,&output);
                CHECK(admitted==should_admit);
                if(admitted){
                    CHECK(output.width==expected_w && output.height==expected_h);
                    ++passes;
                }else{
                    CHECK(output.width==0u && output.height==0u);
                    ++denials;
                }
                ++checked;
                probe_checksum_word(&checksum,(uint64_t)i);
                probe_checksum_word(&checksum,(uint64_t)expected_w);
                probe_checksum_word(&checksum,(uint64_t)expected_h);
                probe_checksum_word(&checksum,(uint64_t)capacity);
                probe_checksum_word(&checksum,(uint64_t)admitted);
            }
            /* For an identical source extent, raising positive decimal
             * UserUnit by one 0.001 unit must not shrink the accepted grid. */
            if(unit_milli<18000u){
                char larger[32];
                const unsigned plus=unit_milli+1u;
                const int z=snprintf(larger,sizeof(larger),"%u.%03u",
                                      plus/1000u,plus%1000u);
                CHECK(z>0 && z<32);
                quantapdf_test_rational_grid prior={0u,0u},next={0u,0u};
                CHECK(quantapdf_test_rational_page_grid_decimal_unit(
                    box,0,unit,dpis[d],1000000u,&prior));
                CHECK(quantapdf_test_rational_page_grid_decimal_unit(
                    box,0,larger,dpis[d],1000000u,&next));
                CHECK(next.width>=prior.width && next.height>=prior.height);
                CHECK(next.width==probe_reference_grid(span_w,plus,dpis[d]) &&
                      next.height==probe_reference_grid(span_h,plus,dpis[d]));
                ++monotonic_pairs;
                probe_checksum_word(&checksum,(uint64_t)next.width);
                probe_checksum_word(&checksum,(uint64_t)next.height);
            }
        }
    }
    CHECK(checked==4096u*3u*4u && passes>0u && denials>0u);
    CHECK(monotonic_pairs>0u && passes+denials==checked);
    FILE *out=fopen(staged,"wb");
    CHECK(out);
    fprintf(out,
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"test_id\": \"issue-360-c11-rational-seeded-differential\",\n"
        "  \"seed\": \"510d0360\",\n"
        "  \"test_generated_source_tuples\": 4096,\n"
        "  \"dpi_values\": [72,96,144],\n"
        "  \"rotation_values\": [0,90,180,270],\n"
        "  \"grid_checks\": %zu,\n"
        "  \"admitted_grid_checks\": %zu,\n"
        "  \"zero_output_rejections\": %zu,\n"
        "  \"unit_monotonicity_pairs\": %zu,\n"
        "  \"grid_vs_independent_integer_model_mismatches\": 0,\n"
        "  \"deterministic_fnv64\": \"%016llx\",\n"
        "  \"test_only\": true,\n"
        "  \"external_source_positive_increment\": 0,\n"
        "  \"production_parser_changed\": false,\n"
        "  \"native_device_grid_qualified\": false,\n"
        "  \"result\": \"passed\"\n"
        "}\n",
        checked,passes,denials,monotonic_pairs,
        (unsigned long long)checksum);
    CHECK(ferror(out)==0 && fclose(out)==0);
    CHECK(rename(staged,report_path)==0);
    fprintf(stderr,
        "[issue-360] C11 seeded differential: %zu checks, %zu accepted, "
        "%zu rejected, %zu unit monotonic pairs\n",
        checked,passes,denials,monotonic_pairs);
    return EXIT_SUCCESS;
}

int main(int argc,char **argv)
{
    if(argc==3 && strcmp(argv[1],"--seeded-differential")==0)
        return probe_seeded_differential(argv[2]);
    CHECK(argc==1);
    static const char *const f[]={"125","25","375","5","625","75","875"};
    static const unsigned dpi[]={72u,96u,144u};
    static const size_t expected_widths[]={70u,94u,140u};
    static const size_t expected_heights[]={56u,75u,112u};
    char box[128];
    size_t i,d;
    /* 7 eighth fractions × 4 rotations × 3 supported DPI, exact widths. */
    for(i=0u;i<7u;++i) {
        int n=snprintf(box,sizeof(box),"2.%s 3.%s 12.%s 11.%s",
                       f[i],f[i],f[i],f[i]);
        CHECK(n>0 && (size_t)n<sizeof(box));
        for(d=0u;d<3u;++d) {
            accepted_grid(box,0,dpi[d],100000u,
                          expected_widths[d],expected_heights[d]);
            accepted_grid(box,180,dpi[d],100000u,
                          expected_widths[d],expected_heights[d]);
            accepted_grid(box,90,dpi[d],100000u,
                          expected_heights[d],expected_widths[d]);
            accepted_grid(box,270,dpi[d],100000u,
                          expected_heights[d],expected_widths[d]);
        }
    }
    /* Non-dyadic translated origins must preserve EXACT rational spans. */
    accepted_grid("6.1 3.375 16.1 11.375",0,72u,100000u,70u,56u);
    accepted_grid("6.10 3.3750 16.10 11.3750",0,72u,100000u,70u,56u);
    accepted_grid("6.1 3.375 16.1 11.375",90,96u,100000u,75u,94u);
    accepted_grid("6.1 3.375 16.1 11.375",270,144u,100000u,112u,140u);
    /* Strictly below/exactly on/above the boundary, source-rational only. */
    static const char *const x[]={
        "12.374875","12.374999999999","12.375",
        "12.375000000001","12.375125"
    };
    static const char *const y[]={
        "11.374875","11.374999999999","11.375",
        "11.375000000001","11.375125"
    };
    static const size_t want_w[3][5]={
        {70u,70u,70u,71u,71u},
        {94u,94u,94u,94u,94u},
        {140u,140u,140u,141u,141u}
    };
    static const size_t want_h[3][5]={
        {56u,56u,56u,57u,57u},
        {75u,75u,75u,75u,75u},
        {112u,112u,112u,113u,113u}
    };
    for(d=0u;d<3u;++d){
        size_t prev_w=0u,prev_h=0u;
        for(i=0u;i<5u;++i) {
            quantapdf_test_rational_grid grid={0u,0u};
            CHECK(snprintf(box,sizeof(box),"2.375 3.375 %s %s",
                           x[i],y[i])>0);
            CHECK(quantapdf_test_rational_page_grid(
                box,0,7u,dpi[d],100000u,&grid));
            CHECK(grid.width==want_w[d][i] && grid.height==want_h[d][i]);
            CHECK(grid.width>=prev_w && grid.height>=prev_h);
            prev_w=grid.width;prev_h=grid.height;
            ++monotonic;
        }
    }
    accepted_grid("6.1 3.375 16.1 11.375",0,72u,3920u,70u,56u);
    static const char *const invalid[]={
        "", "6.1 3.375 16.1", "6.1 3.375 16.1 11.375 1",
        "6.1 3.375 6.1 11.375", "6.1 3.375 16.1 3.375",
        "6.1 NaN 16.1 11.375", "6.1 3.375 16.1 11.375e2",
        "6.1 3.375 16.1 11.375/", "6.1 3.375 16.1 11.375\n",
        "6.1\t3.375 16.1 11.375", "6..1 3.375 16.1 11.375",
        "0 0 9007199254740992 1", "0 0 9007199254740991 1",
        "-9007199254740991 0 9007199254740991 1",
        "0 0 0.0000000000001 1", "0 0 0 1",
        "0 0 1 0", "0 0 -1 1"
    };
    for(i=0u;i<sizeof(invalid)/sizeof(invalid[0]);++i)
        rejected_grid(invalid[i],0,7u,72u,100000u);
    char oversized[98];
    memset(oversized,'9',sizeof(oversized));
    oversized[97]='\0';
    rejected_grid(oversized,0,7u,72u,100000u);
    rejected_grid(NULL,0,7u,72u,100000u);
    rejected_grid("6.1 3.375 16.1 11.375",45,7u,72u,100000u);
    rejected_grid("6.1 3.375 16.1 11.375",0,0u,72u,100000u);
    rejected_grid("6.1 3.375 16.1 11.375",0,129u,72u,100000u);
    rejected_grid("6.1 3.375 16.1 11.375",0,7u,120u,100000u);
    rejected_grid("6.1 3.375 16.1 11.375",0,7u,72u,0u);
    rejected_grid("6.1 3.375 16.1 11.375",0,7u,72u,3919u);
    rejected_grid("6.1 3.375 16.1 11.375",0,7u,72u,1000001u);
    {
        quantapdf_test_rational_grid result={99u,99u};
        CHECK(!quantapdf_test_rational_page_grid(
                  "6.1 3.375 16.1 11.375",0,7u,72u,100000u,NULL));
        CHECK(!quantapdf_test_rational_page_grid(
                  NULL,0,7u,72u,100000u,&result));
        CHECK(result.width==0u && result.height==0u);
    }
    /* Positive decimal UserUnit remains an exact rational until final
     * grid ceil, never converting the independent CropBox endpoints to
     * double. These values are computed from 10×8 source point extents. */
    static const char *const fractional_units[]={
        "0.5","1.1","1.25","1.5","0.875","0.125","0.1"
    };
    static const size_t expected_w[7][3]={
        {5u,7u,10u}, {11u,15u,22u}, {13u,17u,25u},
        {15u,20u,30u}, {9u,12u,18u}, {2u,2u,3u},
        {1u,2u,2u}
    };
    static const size_t expected_h[7][3]={
        {4u,6u,8u}, {9u,12u,18u}, {10u,14u,20u},
        {12u,16u,24u}, {7u,10u,14u}, {1u,2u,2u},
        {1u,2u,2u}
    };
    for(i=0u;i<7u;++i)
        for(d=0u;d<3u;++d) {
            accepted_decimal_unit("6.1 3.375 16.1 11.375",0,
                fractional_units[i],dpi[d],100000u,
                expected_w[i][d],expected_h[i][d]);
            accepted_decimal_unit("6.1 3.375 16.1 11.375",90,
                fractional_units[i],dpi[d],100000u,
                expected_h[i][d],expected_w[i][d]);
        }
    accepted_decimal_unit("6.1 3.375 16.1 11.375",0,
                          "0.50",72u,100000u,5u,4u);
    accepted_decimal_unit("6.10 3.3750 16.10 11.3750",0,
                          "1.10",72u,100000u,11u,9u);
    accepted_decimal_unit("6.1 3.375 16.1 11.375",0,
                          "1.250",72u,100000u,13u,10u);
    accepted_decimal_unit("6.1 3.375 16.1 11.375",0,
                          "0.000000000001",72u,100000u,1u,1u);
    accepted_decimal_unit("6.1 3.375 16.1 11.375",0,
                          "1.000000000000",72u,100000u,10u,8u);
    accepted_decimal_unit("6.1 3.375 16.1 11.375",0,
                          "1.000000000001",72u,100000u,11u,9u);
    accepted_decimal_unit("0 0 1 1",0,
                          "128.000000000000",72u,100000u,128u,128u);
    /* A literal decimal "7.0" must preserve the old integer API. */
    for(d=0u;d<3u;++d) {
        quantapdf_test_rational_grid integer={0u,0u};
        quantapdf_test_rational_grid decimal={0u,0u};
        CHECK(quantapdf_test_rational_page_grid(
              "6.1 3.375 16.1 11.375",270,7u,dpi[d],100000u,&integer));
        CHECK(quantapdf_test_rational_page_grid_decimal_unit(
              "6.1 3.375 16.1 11.375",270,"+07.0",dpi[d],100000u,&decimal));
        CHECK(integer.width==decimal.width && integer.height==decimal.height);
        ++decimal_units_accepted;
    }
    /* Below/exactly on/above the UserUnit=1 boundary. */
    static const char *const around_one[]={
        "0.999999999999","1","1.000000000001"
    };
    static const size_t boundary_w[3][3]={
        {10u,10u,11u}, {14u,14u,14u}, {20u,20u,21u}
    };
    static const size_t boundary_h[3][3]={
        {8u,8u,9u}, {11u,11u,11u}, {16u,16u,17u}
    };
    for(d=0u;d<3u;++d) {
        quantapdf_test_rational_grid previous={0u,0u};
        for(i=0u;i<3u;++i) {
            quantapdf_test_rational_grid result={0u,0u};
            CHECK(quantapdf_test_rational_page_grid_decimal_unit(
                  "6.1 3.375 16.1 11.375",0,around_one[i],
                  dpi[d],100000u,&result));
            CHECK(result.width==boundary_w[d][i] &&
                  result.height==boundary_h[d][i] &&
                  result.width>=previous.width &&
                  result.height>=previous.height);
            previous=result;
            ++unit_monotonic;
        }
    }
    static const char *const invalid_units[]={
        "", "+", "-", ".", "-.", "0", "-0", "+0.0",
        "-0.000000000001", "-1", "129", "128.000000000001",
        "1e0", "1E+1", "NaN", "Inf", "1..0", "1/2",
        " 1", "1 ", "1\n", "1\t",
        "0.0000000000000", "0.0000000000001",
        "9007199254740992", "18446744073709551616",
        "123456789012345678901"
    };
    for(i=0u;i<sizeof(invalid_units)/sizeof(invalid_units[0]);++i)
        rejected_decimal_unit("6.1 3.375 16.1 11.375",0,
                              invalid_units[i],72u,100000u);
    rejected_decimal_unit("6.1 3.375 16.1 11.375",0,
                          NULL,72u,100000u);
    rejected_decimal_unit("6.1 3.375 16.1 11.375",45,
                          "1.25",72u,100000u);
    rejected_decimal_unit("6.1 3.375 16.1 11.375",0,
                          "1.25",120u,100000u);
    rejected_decimal_unit("6.1 3.375 16.1 11.375",0,
                          "1.25",72u,0u);
    rejected_decimal_unit("6.1 3.375 16.1 11.375",0,
                          "1.25",72u,129u); /* 13×10 > 129 pixels */
    rejected_decimal_unit("0.000000000001 0 1.000000000001 1",0,
                          "0.000000000001",72u,100000u);
    {
        quantapdf_test_rational_grid sentinel={99u,99u};
        CHECK(!quantapdf_test_rational_page_grid_decimal_unit(
              NULL,0,"1.25",72u,100000u,&sentinel));
        CHECK(sentinel.width==0u && sentinel.height==0u);
        ++decimal_units_rejected;
        CHECK(!quantapdf_test_rational_page_grid_decimal_unit(
              "6.1 3.375 16.1 11.375",0,"1.25",72u,100000u,NULL));
        ++decimal_units_rejected;
    }
    CHECK(decimal_units_accepted==7u*3u*2u+7u+3u);
    CHECK(unit_monotonic==9u);
    CHECK(decimal_units_rejected==
        sizeof(invalid_units)/sizeof(invalid_units[0])+6u+2u);

    CHECK(accepted==7u*4u*3u+4u+1u);
    CHECK(monotonic==15u);
    fprintf(stderr,
            "[issue-360] rational candidate PASS accepted=%zu rejected=%zu "
            "monotonic=%zu decimal-unit-accepted=%zu "
            "decimal-unit-rejected=%zu unit-monotonic=%zu; "
            "production unchanged\n",
            accepted,rejected,monotonic,
            decimal_units_accepted,decimal_units_rejected,unit_monotonic);
    return EXIT_SUCCESS;
}
