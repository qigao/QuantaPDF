#ifndef QUANTAPDF_NATIVE_GRAPHICS_STATE_CASES_H
#define QUANTAPDF_NATIVE_GRAPHICS_STATE_CASES_H
#include "native_graphics_state_fixture.h"
#include <cmath>
#include <iterator>
static int alpha_group_cases(const std::string& filename)
{
    using namespace native_graphics_fixture;
    const auto render=[&](const original& sample,unsigned dpi,std::vector<unsigned char>& pixels,
                          quantapdf_status wanted=QUANTAPDF_OK)->int {
        CHECK(write_pdf(filename,sample.bytes));
        quantapdf_document *document=nullptr;
        quantapdf_page *page=nullptr;
        OK(quantapdf_open(filename.c_str(),nullptr,&document));
        CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(document,0,&page));
        quantapdf_close(document);
        quantapdf_bitmap *bitmap=nullptr;
        quantapdf_render_options options{};
        options.dpi=static_cast<float>(dpi);
        const auto status=quantapdf_render_page_with_options(page,&options,&bitmap);
        quantapdf_drop_page(page);
        if(status!=wanted)
            std::fprintf(stderr,"Alpha group: render status %d, expected %d\n",int(status),int(wanted));
        CHECK(status==wanted);
        if(wanted!=QUANTAPDF_OK) {
            CHECK(bitmap==nullptr);
            return EXIT_SUCCESS;
        }
        int w=0,h=0,stride=0,channels=0;
        const unsigned char *data=nullptr;
        size_t size=0u;
        OK(quantapdf_bitmap_dimensions(bitmap,&w,&h,&stride,&channels));
        OK(quantapdf_bitmap_data(bitmap,&data,&size));
        CHECK(w==int(12u*dpi/72u) && h==int(4u*dpi/72u) && stride==w*3 && channels==3 &&
              size==static_cast<size_t>(h*stride));
        pixels.assign(data,data+size);
        quantapdf_drop_bitmap(bitmap);
        return EXIT_SUCCESS;
    };
    constexpr const char *group = "q 0 0 3 4 re W n 1 0 0 rg -2 0 10 3 re f Q";
    const auto sample=create_alpha_group(
        "0 1 0 rg 0 0 12 4 re f "
        "q 0 0 4.25 4 re W n "
        "q 1 0 0 1 1 0 cm /Alpha gs BT ET /Inert gs 1 0 0 1 2 0 cm "
        "0 0 1 rg -3 0 12 4 re f Q 1 0 0 rg 0 3 12 1 re f Q "
        "q 1 0 0 1 5 0 cm /Alpha gs q 3 0 0 4 0 0 cm /Plain Do Q "
        "q 1 0 0 4 3 0 cm /Own Do Q /Clear gs 0 g 4 0 1 4 re f Q", group);
    for(unsigned dpi:{72u,144u}) {
        std::vector<unsigned char> pixels;
        CHECK(render(sample,dpi,pixels)==EXIT_SUCCESS);
        const size_t w=12u*dpi/72u,h=4u*dpi/72u;
        const std::array<unsigned,3> green{0u,255u,0u};
        const std::array<unsigned,3> blue{0u,0u,255u},red{255u,0u,0u},
            yellow{255u,255u,0u},black{0u,0u,0u};
        for(size_t y=0u;y<h;++y)for(size_t x=0u;x<w;++x) {
            auto expected=green;
            const auto paint=[&](const std::array<unsigned,3>& rgb,double left,
                                 double right,double bottom,double top) {
                unsigned hits=0u;
                for(unsigned sy=0u;sy<4u;++sy)for(unsigned sx=0u;sx<4u;++sx) {
                    const double px=(double(x)+(sx+0.5)/4.0)*72.0/dpi;
                    const double py=4.0-(double(y)+(sy+0.5)/4.0)*72.0/dpi;
                    hits+=unsigned(px>=left && px<right && py>=bottom && py<top);
                }
                for(size_t c=0u;c<expected.size();++c)
                    expected[c]=(rgb[c]*hits+expected[c]*(16u-hits)+8u)/16u;
            };
            paint(blue,1.5,4.25,0.0,2.5);
            paint(red,0.0,4.25,3.0,4.0);
            paint(yellow,5.5,8.0,0.0,2.5);
            paint(yellow,8.0,9.0,0.0,4.0);
            paint(black,9.0,10.0,0.0,4.0);
            for(size_t c=0u;c<expected.size();++c)
                CHECK(pixels[(y*w+x)*3u+c]==expected[c]);
        }
    }

    const auto multiple=create_alpha_group(
        "0 1 0 rg 0 0 12 4 re f q 0.75 0 3 2.25 re W n /Alpha gs "
        "0 0 1 rg 0 1 12 3 re f q 12 0 0 1 0 0 cm /Plain Do Q Q",
        "q 0 0 2 2 re W n -1 -1 4 4 re f Q "
        "q 1.25 0 1.5 3 re W n 3 0.5 -2 2 re f* Q "
        "3.5 0 1 3 re f");
    for(unsigned dpi:{72u,144u}) {
        std::vector<unsigned char> pixels;
        CHECK(render(multiple,dpi,pixels)==EXIT_SUCCESS);
        const size_t w=12u*dpi/72u,h=4u*dpi/72u;
        for(size_t y=0u;y<h;++y)for(size_t x=0u;x<w;++x) {
            unsigned blue=0u,yellow=0u;
            for(unsigned sy=0u;sy<4u;++sy)for(unsigned sx=0u;sx<4u;++sx) {
                const double px=(double(x)+(sx+0.5)/4.0)*72.0/dpi;
                const double py=4.0-(double(y)+(sy+0.5)/4.0)*72.0/dpi;
                const bool first=px>=0.5 && px<2.5 && py>=0.0 && py<2.0;
                const bool second=px>=1.75 && px<3.25 && py>=0.5 && py<2.5;
                if(px>=0.75 && px<3.75 && py<2.25 && (first || second)) {
                    blue+=unsigned(py>=1.0);
                    yellow+=unsigned(py<1.0);
                }
            }
            const size_t index=(y*w+x)*3u;
            CHECK(pixels[index]==(255u*yellow+8u)/16u);
            CHECK(pixels[index+1u]==(255u*(16u-blue)+8u)/16u);
            CHECK(pixels[index+2u]==(255u*blue+8u)/16u);
        }
    }

    for(bool knockout:{false,true}) {
        const auto partial=create_alpha_group(
            "0 1 0 rg 0 0 12 4 re f q 0.75 0 3 3.25 re W n /Alpha gs "
            "0 0 1 rg 0 1 12 3 re f q 12 0 0 1 0 0 cm /Plain Do Q Q",
            "q /Half gs 0 0 2 3 re W n /StrokeOnly gs 0 0 3 4 re f Q "
            "q /Quarter gs 1.25 0 2 2.5 re f* Q "
            "q /Zero gs 1.5 0 0.5 1 re f Q "
            "3 2 1 1 re f", knockout?alpha_knockout_form:alpha_form);
        for(unsigned dpi:{72u,144u}) {
            std::vector<unsigned char> pixels;
            CHECK(render(partial,dpi,pixels)==EXIT_SUCCESS);
            const size_t w=12u*dpi/72u,h=4u*dpi/72u;
            for(size_t y=0u;y<h;++y)for(size_t x=0u;x<w;++x) {
                unsigned blue=0u,yellow=0u;
                for(unsigned sy=0u;sy<4u;++sy)for(unsigned sx=0u;sx<4u;++sx) {
                    const double px=(double(x)+(sx+0.5)/4.0)*72.0/dpi;
                    const double py=4.0-(double(y)+(sy+0.5)/4.0)*72.0/dpi;
                    if(px<0.75 || px>=3.75 || py>=2.5)continue;
                    const bool first=px<2.5;
                    const bool second=px>=1.75;
                    const bool zero=px>=2.0 && px<2.5 && py<1.0;
                    const bool opaque=px>=3.5 && py>=2.0;
                    unsigned opacity;
                    if(opaque)opacity=8u;
                    else if(knockout)opacity=zero?0u:second?2u:first?4u:0u;
                    else opacity=first?(second?5u:4u):second?2u:0u;
                    if(py>=1.0)blue+=opacity;
                    else yellow+=opacity;
                }
                const size_t index=(y*w+x)*3u;
                CHECK(pixels[index]==(255u*yellow+64u)/128u);
                CHECK(pixels[index+1u]==(255u*(128u-blue)+64u)/128u);
                CHECK(pixels[index+2u]==(255u*blue+64u)/128u);
            }
        }
    }

    for(bool knockout:{false,true}) {
        const auto luminosity=create_alpha_group(
            "0 1 0 rg 0 0 12 4 re f q 0.75 0 8 4 re W n /Alpha gs "
            "0 0 1 rg 0 1 12 3 re f q 12 0 0 1 0 0 cm /Plain Do Q Q",
            ".8 g q /Half gs 0 0 2 3 re W n 0 0 3 4 re f Q "
            "q /Quarter gs 1 0 0 rg 1.25 0 2 2.5 re f* Q "
            "q /Zero gs .1 g 1.5 0 0.5 1 re f Q 3 2 1 1 re f",
            luminosity_form("/DeviceRGB",knockout).c_str(),
            "<< /S /Luminosity /BC [.2 .4 .1] /TR /Identity >>");
        for(unsigned dpi:{72u,144u}) {
            std::vector<unsigned char> pixels;
            CHECK(render(luminosity,dpi,pixels)==EXIT_SUCCESS);
            const size_t w=12u*dpi/72u,h=4u*dpi/72u;
            constexpr unsigned backdrop=2456u,half_gray=4428u,quarter_red=2442u,
                overlap=3921u,gray=6400u,scale=8000u*16u;
            for(size_t y=0u;y<h;++y)for(size_t x=0u;x<w;++x) {
                unsigned blue=0u,yellow=0u;
                for(unsigned sy=0u;sy<4u;++sy)for(unsigned sx=0u;sx<4u;++sx) {
                    const double px=(double(x)+(sx+0.5)/4.0)*72.0/dpi;
                    const double py=4.0-(double(y)+(sy+0.5)/4.0)*72.0/dpi;
                    if(px<0.75 || px>=8.75)continue;
                    unsigned value=backdrop;
                    if(px<4.5 && py<2.5) {
                        const bool first=px<2.5;
                        const bool second=px>=1.75 && px<3.75;
                        const bool zero=px>=2.0 && px<2.5 && py<1.0;
                        if(px>=3.5 && py>=2.0)value=gray;
                        else if(knockout)value=zero?backdrop:second?quarter_red:first?half_gray:backdrop;
                        else value=first?(second?overlap:half_gray):second?quarter_red:backdrop;
                    }
                    if(py>=1.0)blue+=value;
                    else yellow+=value;
                }
                const size_t index=(y*w+x)*3u;
                CHECK(pixels[index]==(255u*yellow+scale/2u)/scale);
                CHECK(pixels[index+1u]==(255u*(scale-blue)+scale/2u)/scale);
                CHECK(pixels[index+2u]==(255u*blue+scale/2u)/scale);
            }
        }
    }

    // A real outlined glyph: use an independently authored Clip as the
    // expected geometry, and require both ink and a measurable mask effect.
    const auto text=create_alpha_group(
        "BT /F 4 Tf /Alpha gs 1 1 Td <002d> Tj ET",group);
    const auto clipped=create_alpha_group(
        "0.5 0 3 2.5 re W n BT /F 4 Tf 1 1 Td <002d> Tj ET",group);
    const auto plain=create_alpha_group(
        "BT /F 4 Tf 1 1 Td <002d> Tj ET",group);
    std::vector<unsigned char> actual,reference,unmasked;
    CHECK(render(text,144u,actual)==EXIT_SUCCESS);
    CHECK(render(clipped,144u,reference)==EXIT_SUCCESS);
    CHECK(render(plain,144u,unmasked)==EXIT_SUCCESS);
    CHECK(actual==reference && actual!=unmasked &&
          std::any_of(actual.begin(),actual.end(),[](unsigned char v){return v!=255u;}));

    const auto overlapping_glyphs=create_alpha_group(
        "/Half gs BT /F 4 Tf /StrokeOnly gs 1 1 Td [<002d> 713 <002d>] TJ "
        "1 0 0 1 1 1 Tm <002d> Tj ET","");
    CHECK(render(overlapping_glyphs,144u,actual)==EXIT_SUCCESS);
    for(size_t i=0u;i<actual.size();++i) {
        const unsigned glyph_samples=((255u-unmasked[i])*16u+127u)/255u;
        CHECK(actual[i]==((32u-glyph_samples)*255u+16u)/32u);
    }
    {
        // Varying paint states inside a knockout (/TK true) BT/ET text object:
        // later shows knock out earlier shows against the BT-entry backdrop.
        std::vector<unsigned char> ko_actual,ko_ref;
        const auto half_then_opaque=create_alpha_group(
            "/Half gs BT /F 4 Tf 1 1 Td <002d> Tj /Inert gs 1 0 0 1 1 1 Tm <002d> Tj ET","");
        CHECK(render(half_then_opaque,144u,ko_actual)==EXIT_SUCCESS && ko_actual==unmasked);

        const auto opaque_then_half=create_alpha_group(
            "/Half gs BT /F 4 Tf 1 1 Td /Inert gs <002d> Tj /Half gs 1 0 0 1 1 1 Tm <002d> Tj ET","");
        CHECK(render(opaque_then_half,144u,ko_actual)==EXIT_SUCCESS && ko_actual==actual);

        // Opacity-zero glyph knocks out an earlier opaque glyph back to the white backdrop.
        const auto opaque_then_zero=create_alpha_group(
            "BT /F 4 Tf 1 1 Td /Inert gs <002d> Tj /Zero gs 1 0 0 1 1 1 Tm <002d> Tj ET","");
        CHECK(render(opaque_then_zero,144u,ko_actual)==EXIT_SUCCESS &&
              std::all_of(ko_actual.begin(),ko_actual.end(),[](unsigned char v){return v==255u;}));

        // Disjoint sequential shows with varying /ca match /Separate (/TK false).
        for(const char *shows : {"<002d> Tj /Inert gs <002d> Tj","/Inert gs <002d> Tj /Half gs <002d> Tj"}) {
            CHECK(render(create_alpha_group(
                std::string("/Half gs BT /F 4 Tf 1 1 Td ")+shows+" ET",""),144u,ko_actual)==EXIT_SUCCESS);
            CHECK(render(create_alpha_group(
                std::string("/Separate gs /Half gs BT /F 4 Tf 1 1 Td ")+shows+" ET",""),144u,ko_ref)==EXIT_SUCCESS);
            CHECK(ko_actual==ko_ref);
        }
    }

    const auto multiple_text=create_alpha_group(
        "BT /F 4 Tf /Alpha gs 1 1 Td <002d> Tj ET",
        "0 0 2 3 re f 3 0 -1.5 3 re f*");
    CHECK(render(multiple_text,144u,actual)==EXIT_SUCCESS && actual==reference);
    const auto partial_text=create_alpha_group(
        "BT /F 4 Tf /Alpha gs 1 1 Td <002d> Tj ET","/Half gs 0 0 3 2.5 re f");
    CHECK(render(partial_text,144u,actual)==EXIT_SUCCESS);
    for(size_t i=0u;i<actual.size();++i) {
        const unsigned glyph_samples=((255u-reference[i])*16u+127u)/255u;
        CHECK(actual[i]==((32u-glyph_samples)*255u+16u)/32u);
    }

    const auto luminosity_text=create_alpha_group(
        "BT /F 4 Tf /Alpha gs 1 1 Td <002d> Tj ET",".5 g 0 0 3 2.5 re f",
        luminosity_form("/DeviceGray").c_str(),"<< /S /Luminosity >>");
    CHECK(render(luminosity_text,144u,reference)==EXIT_SUCCESS && actual==reference);

    const auto empty_luminosity=create_alpha_group("/Alpha gs 0 0 12 4 re f","",
        luminosity_form("/DeviceGray").c_str(),"<< /S /Luminosity /BC [.375] >>");
    CHECK(render(empty_luminosity,72u,actual)==EXIT_SUCCESS);
    CHECK(std::all_of(actual.begin(),actual.end(),[](unsigned char v){return v==159u;}));

    const auto squared_alpha=create_alpha_group(
        "/Alpha gs 0 0 12 4 re f",
        "/Half gs 0 0 2 2 re f /Quarter gs 1 0 2 2 re f",
        alpha_form,"<< /S /Alpha /TR << /FunctionType 2 /Domain [0 1] /N 2 >> >>");
    for(unsigned dpi:{72u,144u}) {
        CHECK(render(squared_alpha,dpi,actual)==EXIT_SUCCESS);
        const size_t w=12u*dpi/72u,h=4u*dpi/72u;
        constexpr unsigned divisor=16u*64u;
        for(size_t y=0u;y<h;++y)for(size_t x=0u;x<w;++x) {
            unsigned opacity=0u;
            for(unsigned sy=0u;sy<4u;++sy)for(unsigned sx=0u;sx<4u;++sx) {
                const double px=(double(x)+(sx+0.5)/4.0)*72.0/dpi;
                const double py=4.0-(double(y)+(sy+0.5)/4.0)*72.0/dpi;
                if(py>=2.0)continue;
                const bool first=px>=0.5 && px<2.5,second=px>=1.5 && px<3.5;
                const unsigned alpha=first?(second?5u:4u):second?2u:0u;
                opacity+=alpha*alpha;
            }
            for(size_t channel=0u;channel<3u;++channel)
                CHECK(actual[3u*(y*w+x)+channel]==(255u*(divisor-opacity)+divisor/2u)/divisor);
        }
    }

    const auto inverted_alpha=create_alpha_group(
        "/Alpha gs 0 0 12 4 re f","0 0 4 3 re f",
        alpha_form,"<< /S /Alpha /TR << /FunctionType 2 /Domain [0 1] /C0 [1] /C1 [0] /N 1 >> >>");
    CHECK(render(inverted_alpha,144u,actual)==EXIT_SUCCESS);
    for(size_t i=0u;i<actual.size();++i) {
        const size_t pixel=i/3u,x=pixel%24u,y=pixel/24u;
        CHECK(actual[i]==(x>=1u && x<9u && y>=3u?255u:0u));
    }

    const auto clamped_luminosity=create_alpha_group(
        "/Alpha gs 0 0 12 4 re f",".125 g .5 0 1 2.5 re f 1 g 1.5 0 1 2.5 re f",
        luminosity_form("/DeviceGray").c_str(),
        "<< /S /Luminosity /BC [.5] /TR << /FunctionType 2 /Domain [.25 .75] /Range [.125 .5] /N 2 >> >>");
    CHECK(render(clamped_luminosity,72u,actual)==EXIT_SUCCESS);
    for(size_t i=0u;i<actual.size();++i) {
        const size_t pixel=i/3u,x=pixel%12u,y=pixel/12u;
        unsigned wanted=191u;
        if(y>=1u && x==1u)wanted=y==1u?207u:223u;
        if(y>=1u && x==2u)wanted=y==1u?159u:128u;
        CHECK(actual[i]==wanted);
    }

    const auto transferred_empty=create_alpha_group(
        "/Alpha gs 0 0 12 4 re f","",alpha_form,
        "<< /S /Alpha /TR << /FunctionType 2 /Domain [-1 -.5] /C0 [1] /C1 [1.25] /N -1 >> >>");
    CHECK(render(transferred_empty,72u,actual)==EXIT_SUCCESS);
    CHECK(std::all_of(actual.begin(),actual.end(),[](unsigned char v){return v==128u;}));

    // Transfer output is always clipped to unit opacity, even with no /Range.
    const auto clipped_output=create_alpha_group(
        "/Alpha gs 0 0 12 4 re f","0 0 4 3 re f",alpha_form,
        "<< /S /Alpha /TR << /FunctionType 2 /Domain [0 1] /C0 [2] /C1 [-1] /N 1 >> >>");
    CHECK(render(clipped_output,144u,reference)==EXIT_SUCCESS && reference.size()==24u*8u*3u);
    for(size_t i=0u;i<reference.size();++i) {
        const size_t pixel=i/3u,x=pixel%24u,y=pixel/24u;
        CHECK(reference[i]==(x>=1u && x<9u && y>=3u?255u:0u));
    }

    const auto stitched_luminosity=create_alpha_group(
        "/Alpha gs 0 0 12 4 re f",stitched_luminosity_fills,
        luminosity_form("/DeviceGray").c_str(),stitched_luminosity_mask);
    for(unsigned dpi:{72u,144u}) {
        CHECK(render(stitched_luminosity,dpi,actual)==EXIT_SUCCESS);
        const size_t w=12u*dpi/72u,h=4u*dpi/72u;
        constexpr unsigned divisor=16u*64u;
        for(size_t y=0u;y<h;++y)for(size_t x=0u;x<w;++x) {
            unsigned opacity=0u;
            for(unsigned sy=0u;sy<4u;++sy)for(unsigned sx=0u;sx<4u;++sx) {
                const double px=(double(x)+(sx+0.5)/4.0)*72.0/dpi;
                const double py=4.0-(double(y)+(sy+0.5)/4.0)*72.0/dpi;
                unsigned value=16u;
                if(py<2.5 && px>=1.0 && px<4.5) {
                    constexpr unsigned strips[]={24u,36u,56u,48u};
                    value=strips[static_cast<size_t>(px)-1u];
                }
                opacity+=value;
            }
            for(size_t channel=0u;channel<3u;++channel)
                CHECK(actual[3u*(y*w+x)+channel]==(255u*(divisor-opacity)+divisor/2u)/divisor);
        }
    }

    const auto stitched_alpha=create_alpha_group(
        "/Alpha gs 0 0 12 4 re f","0 0 4 3 re f",alpha_form,stitched_alpha_mask);
    CHECK(render(stitched_alpha,144u,actual)==EXIT_SUCCESS);
    for(size_t i=0u;i<actual.size();++i) {
        const size_t pixel=i/3u,x=pixel%24u,y=pixel/24u;
        CHECK(actual[i]==(x>=1u && x<9u && y>=3u?32u:191u));
    }

    for(unsigned bits:{1u,2u,4u,8u,12u,16u,24u,32u}) {
        const auto maximum=static_cast<uint32_t>((UINT64_C(1)<<bits)-1u);
        const auto sampled_alpha=create_alpha_group(
            "/Alpha gs 0 0 12 4 re f","/Half gs 0 0 2 3 re f /Inert gs 2 0 2 3 re f",
            alpha_form,"<< /S /Alpha >>",false,
            {{"<< /FunctionType 0 /Domain [0 1] /Range [0 1] /Size [2] /Order 3 /BitsPerSample "+
              std::to_string(bits)+" >>",pack_samples(bits,{maximum,0u})}});
        CHECK(render(sampled_alpha,144u,actual)==EXIT_SUCCESS);
        for(size_t i=0u;i<actual.size();++i) {
            const size_t pixel=i/3u,x=pixel%24u,y=pixel/24u;
            CHECK(actual[i]==(x>=1u && x<9u && y>=3u?(x<5u?128u:255u):0u));
        }
    }

    const auto one_sample=create_alpha_group("/Alpha gs 0 0 12 4 re f","",alpha_form,
        "<< /S /Alpha >>",false,
        {{"<< /FunctionType 0 /Domain [0 1] /Range [.125 .625] /Size [1]"
          " /BitsPerSample 8 /Order 3 >>",pack_samples(8u,{255u})}});
    CHECK(render(one_sample,72u,actual)==EXIT_SUCCESS);
    CHECK(std::all_of(actual.begin(),actual.end(),[](unsigned char v){return v==96u;}));

    const auto sampled_stitch=create_alpha_group("/Alpha gs 0 0 12 4 re f",sampled_stitch_fills,
        luminosity_form("/DeviceGray").c_str(),sampled_stitch_mask,false,sampled_stitch_tables());
    for(unsigned dpi:{72u,144u}) {
        CHECK(render(sampled_stitch,dpi,actual)==EXIT_SUCCESS);
        const size_t w=12u*dpi/72u,h=4u*dpi/72u;
        constexpr unsigned divisor=16u*16u;
        for(size_t y=0u;y<h;++y)for(size_t x=0u;x<w;++x) {
            unsigned opacity=0u;
            for(unsigned sy=0u;sy<4u;++sy)for(unsigned sx=0u;sx<4u;++sx) {
                const double px=(double(x)+(sx+0.5)/4.0)*72.0/dpi;
                const double py=4.0-(double(y)+(sy+0.5)/4.0)*72.0/dpi;
                unsigned value=16u;
                if(py<2.5 && px>=0.5 && px<4.5) {
                    constexpr unsigned strips[]={2u,12u,8u,0u,9u,8u,7u,16u};
                    value=strips[static_cast<size_t>(px*2.0)-1u];
                }
                opacity+=value;
            }
            for(size_t channel=0u;channel<3u;++channel)
                CHECK(actual[3u*(y*w+x)+channel]==(255u*(divisor-opacity)+divisor/2u)/divisor);
        }
    }

    const auto calculator_alpha=create_alpha_group("/Alpha gs 0 0 12 4 re f",
        "/Quarter gs 0 0 1 3 re f /Half gs 1 0 1 3 re f /Inert gs 2 0 2 3 re f",
        alpha_form,"<< /S /Alpha >>",false,{calculator_function(calculator_piecewise)});
    CHECK(render(calculator_alpha,144u,actual)==EXIT_SUCCESS);
    for(size_t i=0u;i<actual.size();++i) {
        const size_t pixel=i/3u,x=pixel%24u,y=pixel/24u;
        const unsigned expected=y>=3u && x>=1u && x<5u?(x<3u?239u:128u):255u;
        CHECK(actual[i]==expected);
    }

    const auto calculator_stitch=create_alpha_group("/Alpha gs 0 0 12 4 re f",
        ".125 g .5 0 1 2.5 re f .5 g 1.5 0 1 2.5 re f .75 g 2.5 0 1 2.5 re f",
        luminosity_form("/DeviceGray").c_str(),calculator_stitch_mask,false,calculator_stitch_streams());
    CHECK(render(calculator_stitch,144u,actual)==EXIT_SUCCESS);
    for(size_t i=0u;i<actual.size();++i) {
        const size_t pixel=i/3u,x=pixel%24u,y=pixel/24u;
        const unsigned expected=y>=3u && x>=2u && x<6u?(x<4u?239u:0u):128u;
        CHECK(actual[i]==expected);
    }

    const auto calculator_ops=create_alpha_group("/Alpha gs 0 0 12 4 re f","",
        alpha_form,"<< /S /Alpha >>",false,{calculator_function(
        "{ pop true "
        "2 3 add 5 eq and 3 2 sub 1 eq and 2 3 mul 6 eq and 1 2 div .5 eq and "
        "-3 abs 3 eq and -3 neg 3 eq and -.5 round 0 eq and "
        ".5 ceiling 1 eq and -.5 floor -1 eq and -1.5 truncate -1 eq and "
        "+04.9 cvi 2 idiv 2 eq and 3 cvr .5 add 3.5 eq and "
        "8 3 mod 2 eq and -5 2 idiv -2 eq and "
        "30 sin .49 gt and 60 cos .51 lt and 1 0 atan 89 gt and "
        "2 3 exp 8 eq and 4 sqrt 2 eq and 10 log 1 eq and 1 ln 0 eq and "
        "-1 -31 bitshift 1 eq and -2147483648 -2147483648 bitshift 0 eq and "
        "-1 not 0 eq and 5 3 and 1 eq and 4 2 or 6 eq and 5 3 xor 6 eq and "
        "true false or and true true xor not and 1 true ne and 1 1 ge and 1 1 le and "
        "2 1 copy eq and 2 3 exch 2 eq exch 3 eq and and "
        "2 3 1 index 2 eq exch 3 eq and exch 2 eq and and "
        "1 2 3 3 1 roll 2 eq exch 1 eq and exch 3 eq and and "
        "1 2 3 3 -1 roll 1 eq exch 3 eq and exch 2 eq and and "
        "{ false { 1 0 div } if .5 } { .125 } ifelse }")});
    CHECK(render(calculator_ops,72u,actual)==EXIT_SUCCESS);
    CHECK(std::all_of(actual.begin(),actual.end(),[](unsigned char v){return v==128u;}));

    std::string many_fills;
    for(unsigned i=0u;i<67u;++i)many_fills+="10 0 1 1 re f ";
    many_fills+="0 0 1 1 re f";
    CHECK(render(create_alpha_group("/Alpha gs 0 0 12 4 re f",many_fills),72u,actual)==EXIT_SUCCESS);
    for(size_t i=0u;i<actual.size();++i)
        CHECK(actual[i]==(i/3u/12u==3u && i/3u%12u<2u?128u:255u));

    // The second gs replaces the old mask. Reflection is in Form space.
    const auto replacement=create_alpha_group(
        "q /Alpha gs 1 0 0 1 4 0 cm /Alpha gs -4 0 12 4 re f Q",
        "0 0 1 4 re f",
        "<< /Subtype /Form /BBox [0 0 2 4] /Matrix [-1 0 0 1 2 0] /Group << /S /Transparency >> >>");
    CHECK(render(replacement,72u,actual)==EXIT_SUCCESS);
    for(size_t i=0u;i<actual.size();++i)
        CHECK(actual[i]==(i/3u%12u==5u?0u:255u));

    CHECK(render(create_alpha_group("/Alpha gs 0 0 12 4 re f",""),72u,actual)==EXIT_SUCCESS);
    CHECK(std::all_of(actual.begin(),actual.end(),[](unsigned char v){return v==255u;}));
    CHECK(render(create_alpha_group("/Half gs 0 0 12 4 re f",group),72u,actual)==EXIT_SUCCESS);
    CHECK(std::all_of(actual.begin(),actual.end(),[](unsigned char v){return v==128u;}));
    const auto page_opacity=create_alpha_group(
        "q /Half gs 1 0 0 rg 0 0 2 4 re f "
        "BT ET q /Quarter gs 0 0 1 rg 1 0 2 4 re f Q "
        "0 g 3 0 1 4 re f Q 0 g 4 0 1 4 re f "
        "q /Half gs /Alpha gs 5 0 1 4 re f "
        "q 1 0 0 4 6 0 cm /Plain Do Q "
        "q 1 0 0 4 7 0 cm /Own Do Q "
        "/Clear gs 8 0 1 4 re f /Zero gs 9 0 1 4 re f "
        "q 1 0 0 4 10 0 cm /Plain Do Q Q",
        "/Quarter gs 5 0 2 4 re f",
        "<< /Subtype /Form /BBox [0 0 12 4] /Group << /S /Transparency /I true >> >>",
        "<< /S /Alpha >>",false,{},128u);
    const unsigned char expected_row[12][3]={
        {255,128,128},{191,96,160},{191,191,255},{128,128,128},{0,0,0},
        {223,223,223},{255,255,223},{255,255,191},{128,128,128},{255,255,255},
        {255,255,255},{255,255,255}};
    for(unsigned dpi:{72u,144u}) {
        const size_t scale=dpi/72u,w=12u*scale,h=4u*scale;
        CHECK(render(page_opacity,dpi,actual)==EXIT_SUCCESS);
        for(size_t y=0u;y<h;++y)for(size_t x=0u;x<w;++x)
            for(size_t c=0u;c<3u;++c)
                CHECK(actual[(y*w+x)*3u+c]==expected_row[x/scale][c]);
    }
    CHECK(render(create_alpha_group("/Bad gs 0 0 12 4 re f",""),
        72u,actual,QUANTAPDF_ERROR_UNSUPPORTED)==EXIT_SUCCESS);
    const auto rgb_lum=[](std::array<double,3> c) -> double {
        return 0.3*c[0]+0.59*c[1]+0.11*c[2];
    };
    const auto rgb_sat=[](std::array<double,3> c) -> double {
        return std::max({c[0],c[1],c[2]})-std::min({c[0],c[1],c[2]});
    };
    const auto rgb_clip_color=[&rgb_lum](std::array<double,3> c) -> std::array<double,3> {
        const double l=rgb_lum(c);
        const double n=std::min({c[0],c[1],c[2]});
        const double x=std::max({c[0],c[1],c[2]});
        if(n<0.0)for(size_t i=0u;i<3u;++i)c[i]=l+((c[i]-l)*l)/(l-n);
        if(x>1.0)for(size_t i=0u;i<3u;++i)c[i]=l+((c[i]-l)*(1.0-l))/(x-l);
        for(size_t i=0u;i<3u;++i)c[i]=std::clamp(c[i],0.0,1.0);
        return c;
    };
    const auto rgb_set_lum=[&rgb_lum,&rgb_clip_color](std::array<double,3> c,double l) -> std::array<double,3> {
        const double delta=l-rgb_lum(c);
        return rgb_clip_color({c[0]+delta,c[1]+delta,c[2]+delta});
    };
    const auto rgb_set_sat=[](std::array<double,3> c,double s) -> std::array<double,3> {
        size_t idx[3]={0u,1u,2u};
        if(c[idx[0]]>c[idx[1]])std::swap(idx[0],idx[1]);
        if(c[idx[1]]>c[idx[2]])std::swap(idx[1],idx[2]);
        if(c[idx[0]]>c[idx[1]])std::swap(idx[0],idx[1]);
        const double lo=c[idx[0]],mid=c[idx[1]],hi=c[idx[2]];
        std::array<double,3> out{};
        if(hi>lo) {
            out[idx[1]]=((mid-lo)*s)/(hi-lo);
            out[idx[2]]=s;
        }
        return out;
    };
    const auto expected_blend_rgb=[&](size_t mode_index,std::array<double,3> d_rgb,
        std::array<double,3> s_rgb) -> std::array<double,3> {
        if(mode_index>=11u && mode_index<=14u) {
            const std::array<double,3> b{d_rgb[0]/255.0,d_rgb[1]/255.0,d_rgb[2]/255.0};
            const std::array<double,3> src{s_rgb[0]/255.0,s_rgb[1]/255.0,s_rgb[2]/255.0};
            std::array<double,3> res=src;
            if(mode_index==11u)res=rgb_set_lum(rgb_set_sat(src,rgb_sat(b)),rgb_lum(b));
            else if(mode_index==12u)res=rgb_set_lum(rgb_set_sat(b,rgb_sat(src)),rgb_lum(b));
            else if(mode_index==13u)res=rgb_set_lum(src,rgb_lum(b));
            else res=rgb_set_lum(b,rgb_lum(src));
            return {res[0]*255.0,res[1]*255.0,res[2]*255.0};
        }
        std::array<double,3> out{};
        for(size_t c=0u;c<3u;++c) {
            const double d=d_rgb[c],s=s_rgb[c],product=d*s/255.0;
            switch(mode_index) {
            case 0u: out[c]=product; break;
            case 1u: out[c]=d+s-product; break;
            case 2u: out[c]=d<127.5 ? 2.0*product : 255.0-2.0*(255.0-d)*(255.0-s)/255.0; break;
            case 3u: out[c]=std::min(d,s); break;
            case 4u: out[c]=std::max(d,s); break;
            case 5u:
                out[c]=d==0.0 ? 0.0 : (s>=255.0 ? 255.0 : std::min(255.0,d*255.0/(255.0-s)));
                break;
            case 6u:
                out[c]=d==255.0 ? 255.0 : (s<=0.0 ? 0.0 : 255.0-std::min(255.0,(255.0-d)*255.0/s));
                break;
            case 7u: out[c]=s<=127.5 ? 2.0*product : 255.0-2.0*(255.0-d)*(255.0-s)/255.0; break;
            case 8u: {
                const double b=d/255.0,src=s/255.0;
                if(src<=0.5)out[c]=(b-(1.0-2.0*src)*b*(1.0-b))*255.0;
                else {
                    const double curve=b<=0.25 ? ((16.0*b-12.0)*b+4.0)*b : std::sqrt(b);
                    out[c]=(b+(2.0*src-1.0)*(curve-b))*255.0;
                }
                break;
            }
            case 9u: out[c]=std::abs(d-s); break;
            case 10u: out[c]=d+s-2.0*product; break;
            default: out[c]=s; break;
            }
        }
        return out;
    };
    const struct {
        const char *name;
        std::array<double,3> blended;
        std::array<unsigned,3> partial_backdrop;
    } blends[]={
        {"/Multiply",{40.8,61.2,40.8},{137,142,138}},
        {"/Screen",{214.2,193.8,214.2},{180,176,181}},
        {"/Overlay",{81.6,132.6,173.4},{147,160,171}},
        {"/Darken",{51,102,51},{140,153,140}},
        {"/Lighten",{204,153,204},{178,165,178}},
        {"/ColorDodge",{255,255,255},{191,191,191}},
        {"/ColorBurn",{0,0,0},{127,127,127}},
        {"/HardLight",{173.4,122.4,81.6},{171,158,147}},
        {"/SoftLight",{88.944,140.76,179.52},{149,162,172}},
        {"/Difference",{153,51,153},{165,140,165}},
        {"/Exclusion",{173.4,132.6,173.4},{171,160,171}},
        {"/Hue",{205.02,103.02,52.02},{178,153,140}},
        {"/Saturation",{51,153,204},{140,165,178}},
        {"/Color",{205.02,103.02,52.02},{178,153,140}},
        {"/Luminosity",{49.98,151.98,202.98},{139,165,178}},
    };
    for(const auto& mode:blends) {
        const std::string gs=std::string(mode.name)+" gs ";
        const auto page=create_alpha_group(
            ".2 .6 .8 rg 0 0 8 4 re f q "+gs+".8 .4 .2 rg 8 0 1 4 re f Q "
            "q "+gs+"BT ET .8 .4 .2 rg /Half gs 0 0 1 4 re f "
            "q /Reset gs 1 0 1 4 re f Q /StrokeOnly gs 2 0 1 4 re f* "
            "q 3.25 0 1 4 re W n /Alpha gs 3 0 2 4 re f Q Q "
            ".8 .4 .2 rg 5 0 1 4 re f "
            "q /Half gs .2 .6 .8 rg 9 0 1 4 re f "+gs+".8 .4 .2 rg 9 0 1 4 re f Q",
            "/Quarter gs 0 0 12 4 re f",
            "<< /Subtype /Form /BBox [0 0 12 4] /Group << /S /Transparency /I true >> >>");
        constexpr std::array<unsigned,3> base{51,153,204},source{204,102,51};
        for(unsigned dpi:{72u,144u}) {
            const size_t scale=dpi/72u,w=12u*scale,h=4u*scale;
            CHECK(render(page,dpi,actual)==EXIT_SUCCESS);
            for(size_t y=0u;y<h;++y)for(size_t x=0u;x<w;++x) {
                const size_t column=x/scale;
                const double left=double(x)/scale,right=double(x+1u)/scale;
                const double clipped=std::max(0.0,std::min(right,4.25)-std::max(left,3.25))*scale;
                const double alpha=(column==0u || column==2u)?0.5:clipped*0.125;
                for(size_t c=0u;c<3u;++c) {
                    unsigned expected=255u;
                    if(column<8u)expected=static_cast<unsigned>(
                        (1.0-alpha)*base[c]+alpha*mode.blended[c]+0.5);
                    if(column==1u)expected=128u;
                    if(column==5u || column==8u)expected=source[c];
                    if(column==9u)expected=mode.partial_backdrop[c];
                    CHECK(actual[(y*w+x)*3u+c]==expected);
                }
            }
        }
    }
    for(size_t mode=0u;mode<std::size(blends);++mode) {
        const std::string gs=std::string(blends[mode].name)+" gs ";
        const auto page=create_alpha_group(
            ".2 .6 .8 rg 0 0 10 4 re f 10 2 1 2 re f 11 0 1 4 re f "
            "q "+gs+"1 0 0 4 10 0 cm /Plain Do Q "
            "q "+gs+"/Half gs /Alpha gs "
            "q .25 0 3.5 4 re W n 4 0 0 4 0 0 cm /Interpolated Do Q BT ET "
            "q 4 0 0 4 4 0 cm /WideMask Do Q "
            "q 2 0 0 4 8 0 cm /Keyed Do Q Q "
            "q 1 0 0 4 11 0 cm /Plain Do Q",
            "/Quarter gs 0 0 4 4 re f",
            "<< /Subtype /Form /BBox [0 0 12 4] /Group << /S /Transparency /I true >> >>",
            "<< /S /Alpha >>",false,{},255u,true);
        const std::array<double,3> backdrop{51,153,204},left{204,102,51},
            right{51,204,102},yellow{255,255,0};
        for(unsigned dpi:{72u,144u}) {
            const size_t scale=dpi/72u,w=12u*scale,h=4u*scale;
            CHECK(render(page,dpi,actual)==EXIT_SUCCESS);
            for(size_t y=0u;y<h;++y)for(size_t x=0u;x<w;++x) {
                const double lower=double(x)/scale,upper=double(x+1u)/scale,center=(lower+upper)/2.0;
                const size_t column=x/scale;
                double t=0.0,opacity=1.0;
                if(column<4u) {
                    t=std::clamp((center-1.0)/2.0,0.0,1.0);
                    const double clipped=(std::min(upper,3.75)-std::max(lower,0.25))*scale;
                    opacity=clipped/8.0;
                } else if(column<8u) {
                    t=std::clamp((center-5.0)/2.0,0.0,1.0);
                    opacity=t/2.0;
                } else if(column<10u) {
                    t=std::clamp(center-8.5,0.0,1.0);
                    opacity=t/2.0;
                }
                std::array<double,3> source_rgb{};
                for(size_t c=0u;c<3u;++c) {
                    double source=yellow[c];
                    if(column<8u)source=left[c]*(1.0-t)+right[c]*t;
                    if(column>=4u && column<8u) {
                        const double wide_left=left[c]*257.0+(c==0u?1.0:0.0);
                        source=std::round(wide_left*(1.0-t)+right[c]*257.0*t)/257.0;
                    }
                    if(column>=8u && column<10u)source=right[c];
                    source_rgb[c]=source;
                }
                const auto mixed=expected_blend_rgb(mode,backdrop,source_rgb);
                for(size_t c=0u;c<3u;++c) {
                    const double d=backdrop[c];
                    const auto expected=static_cast<unsigned>(column==11u || (column==10u && y>=2u*scale)?
                        source_rgb[c]+0.5:d+(mixed[c]-d)*opacity+0.5);
                    CHECK(actual[(y*w+x)*3u+c]==expected);
                }
            }
        }
    }
    constexpr const char *text_mask_form=
        "<< /Subtype /Form /BBox [0 0 12 2.5] /Group << /S /Transparency /I true >> >>";
    const auto restored_text=create_alpha_group("BT /F 4 Tf 7 1 Td <002d> Tj ET","");
    for(unsigned dpi:{72u,144u})for(bool masked:{false,true}) {
        const auto original_text=create_alpha_group(masked?
            "1.75 0 2.25 2.5 re W n BT /F 4 Tf 1 1 Td <002d> Tj ET":
            "1.75 0 2.25 4 re W n BT /F 4 Tf 1 1 Td <002d> Tj ET","");
        CHECK(render(original_text,dpi,reference)==EXIT_SUCCESS);
        CHECK(std::any_of(reference.begin(),reference.end(),[](unsigned char v){return v!=255u;}));
        CHECK(render(restored_text,dpi,unmasked)==EXIT_SUCCESS);
        for(const auto& mode:blends) {
            const auto page=create_alpha_group(
                std::string(".2 .6 .8 rg 0 0 12 4 re f q 1.75 0 2.25 4 re W n ")+mode.name+
                " gs "+(masked?"/Half gs /Alpha gs ":"")+
                ".8 .4 .2 rg BT /F 4 Tf 1 1 Td [<002d> 713 <002d>] TJ "
                "1 0 0 1 1 1 Tm <002d> Tj ET Q "
                "0 g BT /F 4 Tf 7 1 Td <002d> Tj ET",
                "/Half gs 0 0 12 4 re f",text_mask_form);
            CHECK(render(page,dpi,actual)==EXIT_SUCCESS);
            const unsigned base[]={51u,153u,204u};
            for(size_t i=0u;i<actual.size();++i) {
                const auto c=i%3u;
                const unsigned samples=((255u-reference[i])*16u+127u)/255u;
                const unsigned restored_samples=((255u-unmasked[i])*16u+127u)/255u;
                const double alpha=double(samples)/(masked?64.0:16.0);
                const auto tone=static_cast<unsigned>((1.0-alpha)*base[c]+alpha*mode.blended[c]+0.5);
                CHECK(actual[i]==(tone*(16u-restored_samples)+8u)/16u);
            }
        }
    }
    const auto clipped_glyph=create_alpha_group(
        "1.75 0 2.25 2.5 re W n BT /F 4 Tf 1 1 Td <002d> Tj ET","");
    for(unsigned dpi:{72u,144u}) {
        CHECK(render(clipped_glyph,dpi,reference)==EXIT_SUCCESS);
        CHECK(render(restored_text,dpi,unmasked)==EXIT_SUCCESS);
        for(size_t mode=0u;mode<=std::size(blends);++mode) {
            const char *blend_name=mode==std::size(blends)?"/Reset":blends[mode].name;
            const auto page=create_alpha_group(
                std::string(".2 .6 .8 rg 0 0 12 4 re f q /Separate gs /Half gs /Alpha gs "
                            "1.75 0 2.25 4 re W n ")+blend_name+
                " gs .8 .4 .2 rg BT /F 4 Tf 1 1 Td [<002d> 713 <002d>] TJ "
                "/Inert gs /Screen gs 0 0 1 rg 1 0 0 1 1 1 Tm <002d> Tj ET Q "
                "/Half gs 0 g BT /F 4 Tf 7 1 Td [<002d> 713 <002d>] TJ ET",
                "/Half gs 0 0 12 4 re f",text_mask_form);
            CHECK(render(page,dpi,actual)==EXIT_SUCCESS);
            const std::array<double,3> color{204.0,102.0,51.0};
            for(size_t px=0u;px<actual.size();px+=3u) {
                const unsigned samples=((255u-reference[px])*16u+127u)/255u;
                const unsigned restored_samples=((255u-unmasked[px])*16u+127u)/255u;
                std::array<unsigned,3> tone{51u,153u,204u};
                for(unsigned glyph=0u;glyph<2u;++glyph) {
                    const std::array<double,3> d{double(tone[0]),double(tone[1]),double(tone[2])};
                    const auto mixed=expected_blend_rgb(mode,d,color);
                    for(size_t c=0u;c<3u;++c)
                        tone[c]=static_cast<unsigned>(d[c]+(mixed[c]-d[c])*samples/64.0+0.5);
                }
                // Screen with blue has no red or green contribution, and sends blue to 255.
                tone[2]=(tone[2]*(32u-samples)+255u*samples+16u)/32u;
                for(size_t c=0u;c<3u;++c)
                    CHECK(actual[px+c]==(tone[c]*(32u-restored_samples)+16u)/32u);
            }
        }
    }

    const auto show_glyph=[](const char *x,bool doubled) {
        return std::string("BT /F 4 Tf ")+x+" 1 Td "+
            (doubled?"[<002d> 713 <002d>] TJ ET ":"<002d> Tj ET ");
    };
    const auto glyph_scope=create_alpha_group(
        "q /Separate gs /Half gs "+show_glyph("1",true)+
        "q /Knockout gs "+show_glyph("4",true)+"Q "+show_glyph("7",true)+
        "Q /Half gs "+show_glyph("9.5",true),"");
    const auto independent_scope=create_alpha_group(
        "/Half gs "+show_glyph("1",false)+show_glyph("1",false)+show_glyph("4",false)+
        show_glyph("7",false)+show_glyph("7",false)+show_glyph("9.5",false),"");
    CHECK(render(glyph_scope,144u,actual)==EXIT_SUCCESS);
    CHECK(render(independent_scope,144u,reference)==EXIT_SUCCESS && actual==reference);
    for(const char *malformed:{"/BadTK gs","BT ET /BadTK gs",
        "BT /Separate gs ET","/Separate gs BT /Knockout gs ET"}) {
        CHECK(render(create_alpha_group(malformed,""),72u,actual,
            QUANTAPDF_ERROR_FORMAT)==EXIT_SUCCESS);
    }

    // Verify non-saturated ColorDodge/ColorBurn, SoftLight <=0.25/>0.25, and /BM array fallback
    {
        const auto dodge_direct=create_alpha_group(
            ".2 .2 .2 rg 0 0 12 4 re f /ColorDodge gs .4 .6 .8 rg 0 0 12 4 re f","");
        const auto dodge_array=create_alpha_group(
            ".2 .2 .2 rg 0 0 12 4 re f /FallbackDodge gs .4 .6 .8 rg 0 0 12 4 re f","");
        CHECK(render(dodge_direct,72u,actual)==EXIT_SUCCESS);
        CHECK(render(dodge_array,72u,reference)==EXIT_SUCCESS && actual==reference);
        for(size_t px=0u;px<actual.size();px+=3u) {
            CHECK(actual[px]==85u && actual[px+1u]==128u && actual[px+2u]==255u);
        }
        const auto burn_direct=create_alpha_group(
            ".6 .8 .2 rg 0 0 12 4 re f /ColorBurn gs .8 .6 .2 rg 0 0 12 4 re f","");
        const auto burn_array=create_alpha_group(
            ".6 .8 .2 rg 0 0 12 4 re f /FallbackBurn gs .8 .6 .2 rg 0 0 12 4 re f","");
        CHECK(render(burn_direct,72u,actual)==EXIT_SUCCESS);
        CHECK(render(burn_array,72u,reference)==EXIT_SUCCESS && actual==reference);
        for(size_t px=0u;px<actual.size();px+=3u) {
            CHECK(actual[px]==128u && actual[px+1u]==170u && actual[px+2u]==0u);
        }
        const auto soft_branches=create_alpha_group(
            ".2 .8 .2 rg 0 0 12 4 re f /SoftLight gs .4 .8 .8 rg 0 0 12 4 re f","");
        CHECK(render(soft_branches,72u,actual)==EXIT_SUCCESS);
        const std::array<double,3> soft_d{51.0,204.0,51.0},soft_s{102.0,204.0,204.0};
        const auto soft_expected=expected_blend_rgb(8u,soft_d,soft_s);
        for(size_t c=0u;c<3u;++c) {
            CHECK(actual[c]==static_cast<unsigned>(soft_expected[c]+0.5));
        }
        // Verify distinct-saturation non-separable discrimination and ClipColor clamping
        const struct {
            const char *gs;
            const char *bg;
            const char *src;
            unsigned r;
            unsigned g;
            unsigned b;
        } nonsep_checks[]={
            {"/Hue",".2 .4 .9 rg",".9 .6 .4 rg",184u,76u,5u},
            {"/Saturation",".2 .4 .9 rg",".9 .6 .4 rg",65u,102u,193u},
            {"/Color",".2 .4 .9 rg",".9 .6 .4 rg",160u,83u,32u},
            {"/Luminosity",".2 .4 .9 rg",".9 .6 .4 rg",138u,171u,255u},
            {"/Luminosity",".1 .2 .9 rg",".95 .95 .95 rg",239u,241u,255u},
            {"/Color",".1 .1 .2 rg",".9 .1 .1 rg",96u,0u,0u},
        };
        for(const auto& item : nonsep_checks) {
            const auto rendered=create_alpha_group(
                std::string(item.bg)+" 0 0 12 4 re f "+item.gs+" gs "+item.src+" 0 0 12 4 re f","");
            CHECK(render(rendered,72u,actual)==EXIT_SUCCESS);
            for(size_t px=0u;px<actual.size();px+=3u) {
                CHECK(actual[px]==item.r && actual[px+1u]==item.g && actual[px+2u]==item.b);
            }
        }
        const auto img_array=create_alpha_group(
            ".2 .6 .8 rg 0 0 12 4 re f /FallbackDodge gs 4 0 0 4 0 0 cm /Interpolated Do","",
            alpha_form,"<< /S /Alpha /TR /Identity >>",false,{},255u,true);
        const auto img_direct=create_alpha_group(
            ".2 .6 .8 rg 0 0 12 4 re f /ColorDodge gs 4 0 0 4 0 0 cm /Interpolated Do","",
            alpha_form,"<< /S /Alpha /TR /Identity >>",false,{},255u,true);
        CHECK(render(img_array,72u,actual)==EXIT_SUCCESS &&
              render(img_direct,72u,reference)==EXIT_SUCCESS && actual==reference);
        const auto hue_img_array=create_alpha_group(
            ".2 .4 .9 rg 0 0 12 4 re f /FallbackHue gs 4 0 0 4 0 0 cm /Interpolated Do","",
            alpha_form,"<< /S /Alpha /TR /Identity >>",false,{},255u,true);
        const auto hue_img_direct=create_alpha_group(
            ".2 .4 .9 rg 0 0 12 4 re f /Hue gs 4 0 0 4 0 0 cm /Interpolated Do","",
            alpha_form,"<< /S /Alpha /TR /Identity >>",false,{},255u,true);
        CHECK(render(hue_img_array,72u,actual)==EXIT_SUCCESS &&
              render(hue_img_direct,72u,reference)==EXIT_SUCCESS && actual==reference);
        const auto text_array=create_alpha_group(
            ".2 .6 .8 rg 0 0 12 4 re f /FallbackBurn gs .8 .4 .2 rg BT /F 4 Tf 1 1 Td <002d> Tj ET","");
        const auto text_direct=create_alpha_group(
            ".2 .6 .8 rg 0 0 12 4 re f /ColorBurn gs .8 .4 .2 rg BT /F 4 Tf 1 1 Td <002d> Tj ET","");
        CHECK(render(text_array,72u,actual)==EXIT_SUCCESS &&
              render(text_direct,72u,reference)==EXIT_SUCCESS && actual==reference);
    }
    for(const char *malformed : {"/BadArrEmpty gs 0 0 12 4 re f","/BadArrType gs 0 0 12 4 re f",
        "/BadBMNum gs 0 0 12 4 re f"}) {
        CHECK(render(create_alpha_group(malformed,""),72u,actual,
            QUANTAPDF_ERROR_FORMAT)==EXIT_SUCCESS);
    }
    {
        // Varying blend, color, clip, and soft-mask states inside a knockout (/TK true) BT/ET object.
        std::vector<unsigned char> ko_actual,ko_ref,ko_sep;
        for(const char *seq : {"/Multiply gs BT /F 4 Tf 1 1 Td <002d> Tj /Reset gs <002d> Tj ET",
            "/Multiply gs BT /F 4 Tf 1 1 Td <002d> Tj 1 0 0 rg <002d> Tj ET"}) {
            CHECK(render(create_alpha_group(seq,""),72u,ko_actual)==EXIT_SUCCESS);
            CHECK(render(create_alpha_group(std::string("/Separate gs ")+seq,""),72u,ko_sep)==EXIT_SUCCESS);
            CHECK(ko_actual==ko_sep);
        }
        for(unsigned dpi : {72u,144u}) {
            // Overlapping Multiply -> Hue in /TK true knocks out Multiply against the .2 .6 .8 backdrop.
            const auto blend_ko=create_alpha_group(
                ".2 .6 .8 rg 0 0 12 4 re f /Multiply gs .8 .4 .2 rg "
                "BT /F 4 Tf 1 1 Td <002d> Tj /Hue gs 1 0 0 rg 1 0 0 1 1 1 Tm <002d> Tj ET","");
            const auto blend_solo=create_alpha_group(
                ".2 .6 .8 rg 0 0 12 4 re f /Hue gs 1 0 0 rg BT /F 4 Tf 1 1 Td <002d> Tj ET","");
            const auto blend_sep=create_alpha_group(
                ".2 .6 .8 rg 0 0 12 4 re f /Separate gs /Multiply gs .8 .4 .2 rg "
                "BT /F 4 Tf 1 1 Td <002d> Tj /Hue gs 1 0 0 rg 1 0 0 1 1 1 Tm <002d> Tj ET","");
            CHECK(render(blend_ko,dpi,ko_actual)==EXIT_SUCCESS &&
                  render(blend_solo,dpi,ko_ref)==EXIT_SUCCESS &&
                  render(blend_sep,dpi,ko_sep)==EXIT_SUCCESS);
            CHECK(ko_actual==ko_ref && ko_actual!=ko_sep);

            // Clipped + soft-masked Color blend knocks out earlier opaque blue glyph inside clip.
            const auto masked_ko=create_alpha_group(
                ".2 .6 .8 rg 0 0 12 4 re f 1.75 0 2.25 4 re W n "
                "BT /F 4 Tf /Inert gs 0 0 1 rg 1 1 Td <002d> Tj "
                "/Half gs /Alpha gs /Color gs 1 0 0 rg 1 0 0 1 1 1 Tm <002d> Tj ET",
                "/Half gs 0 0 12 4 re f",text_mask_form);
            const auto masked_solo=create_alpha_group(
                ".2 .6 .8 rg 0 0 12 4 re f 1.75 0 2.25 4 re W n "
                "/Half gs /Alpha gs /Color gs 1 0 0 rg BT /F 4 Tf 1 1 Td <002d> Tj ET",
                "/Half gs 0 0 12 4 re f",text_mask_form);
            const auto masked_sep=create_alpha_group(
                ".2 .6 .8 rg 0 0 12 4 re f /Separate gs 1.75 0 2.25 4 re W n "
                "BT /F 4 Tf /Inert gs 0 0 1 rg 1 1 Td <002d> Tj "
                "/Half gs /Alpha gs /Color gs 1 0 0 rg 1 0 0 1 1 1 Tm <002d> Tj ET",
                "/Half gs 0 0 12 4 re f",text_mask_form);
            CHECK(render(masked_ko,dpi,ko_actual)==EXIT_SUCCESS &&
                  render(masked_solo,dpi,ko_ref)==EXIT_SUCCESS &&
                  render(masked_sep,dpi,ko_sep)==EXIT_SUCCESS);
            CHECK(ko_actual==ko_ref && ko_actual!=ko_sep);

            // Three interleaved states (A -> B -> C) preserve paint order: C knocks out B and A.
            const auto triple_ko=create_alpha_group(
                ".2 .6 .8 rg 0 0 12 4 re f BT /F 4 Tf "
                "/Multiply gs 1 0 0 rg 1 1 Td <002d> Tj "
                "/Half gs /Reset gs 0 1 0 rg 1 0 0 1 1 1 Tm <002d> Tj "
                "/Inert gs /Multiply gs 0 0 1 rg 1 0 0 1 1 1 Tm <002d> Tj ET","");
            const auto triple_solo=create_alpha_group(
                ".2 .6 .8 rg 0 0 12 4 re f /Multiply gs 0 0 1 rg BT /F 4 Tf 1 1 Td <002d> Tj ET","");
            CHECK(render(triple_ko,dpi,ko_actual)==EXIT_SUCCESS &&
                  render(triple_solo,dpi,ko_ref)==EXIT_SUCCESS && ko_actual==ko_ref);
        }

        // Partial overlap at 144 DPI: non-overlapping subpixels keep each glyph's own state while
        // overlapping subpixels diverge cleanly from /Separate (/TK false).
        std::vector<unsigned char> g1_mask,g2_mask,solo1,solo2;
        const auto partial_ko=create_alpha_group(
            ".2 .6 .8 rg 0 0 12 4 re f /Half gs 1 0 0 rg "
            "BT /F 4 Tf 1 1 Td <002d> Tj /Screen gs 0 1 0 rg 1 0 0 1 1.5 1 Tm <002d> Tj ET","");
        const auto partial_sep=create_alpha_group(
            ".2 .6 .8 rg 0 0 12 4 re f /Separate gs /Half gs 1 0 0 rg "
            "BT /F 4 Tf 1 1 Td <002d> Tj /Screen gs 0 1 0 rg 1 0 0 1 1.5 1 Tm <002d> Tj ET","");
        const auto solo_first=create_alpha_group(
            ".2 .6 .8 rg 0 0 12 4 re f /Half gs 1 0 0 rg BT /F 4 Tf 1 1 Td <002d> Tj ET","");
        const auto solo_second=create_alpha_group(
            ".2 .6 .8 rg 0 0 12 4 re f /Half gs /Screen gs 0 1 0 rg BT /F 4 Tf 1.5 1 Td <002d> Tj ET","");
        const auto mask_first=create_alpha_group("BT /F 4 Tf 1 1 Td <002d> Tj ET","");
        const auto mask_second=create_alpha_group("BT /F 4 Tf 1.5 1 Td <002d> Tj ET","");
        CHECK(render(partial_ko,144u,ko_actual)==EXIT_SUCCESS &&
              render(partial_sep,144u,ko_sep)==EXIT_SUCCESS &&
              render(solo_first,144u,solo1)==EXIT_SUCCESS &&
              render(solo_second,144u,solo2)==EXIT_SUCCESS &&
              render(mask_first,144u,g1_mask)==EXIT_SUCCESS &&
              render(mask_second,144u,g2_mask)==EXIT_SUCCESS);
        CHECK(ko_actual!=ko_sep);
        for(size_t px=0u;px<ko_actual.size();px+=3u) {
            if(g2_mask[px]==255u) {
                CHECK(ko_actual[px]==solo1[px] && ko_actual[px+1u]==solo1[px+1u] && ko_actual[px+2u]==solo1[px+2u]);
            } else if(g1_mask[px]==255u || g2_mask[px]==0u) {
                CHECK(ko_actual[px]==solo2[px] && ko_actual[px+1u]==solo2[px+1u] && ko_actual[px+2u]==solo2[px+2u]);
            }
        }
    }
    for(const char *unsupported : {"/CustomOnly gs 0 0 12 4 re f","/UnsupportedArr gs /Plain Do",
        "/CustomOnly gs BT /F 4 Tf 1 1 Td <002d> Tj ET"}) {
        CHECK(render(create_alpha_group(unsupported,""),72u,actual,
            QUANTAPDF_ERROR_UNSUPPORTED)==EXIT_SUCCESS);
    }
    CHECK(render(create_alpha_group("/Alpha gs 0 0 12 4 re f",
        "/Multiply gs 0 0 2 2 re f"),72u,actual,QUANTAPDF_ERROR_UNSUPPORTED)==EXIT_SUCCESS);
    for(const char *unsupported : {"/Alpha gs", "/Bad gs 0 0 1 1 re f", "0 0 1 1 re S"}) {
        CHECK(render(create_alpha_group("/Alpha gs 0 0 12 4 re f",unsupported),
            72u,actual,QUANTAPDF_ERROR_UNSUPPORTED)==EXIT_SUCCESS);
    }
    CHECK(render(create_alpha_group("/Alpha gs 0 0 12 4 re f",group,alpha_form,"<< /S /Luminosity >>"),
        72u,actual,QUANTAPDF_ERROR_UNSUPPORTED)==EXIT_SUCCESS);
    CHECK(render(create_alpha_group("/Alpha gs 0 0 12 4 re f",group,
        luminosity_form("/DeviceCMYK").c_str(),"<< /S /Luminosity >>"),
        72u,actual,QUANTAPDF_ERROR_UNSUPPORTED)==EXIT_SUCCESS);
    CHECK(render(create_alpha_group("/Alpha gs 0 0 12 4 re f",group,
        luminosity_form("/DeviceGray").c_str(),"<< /S /Luminosity /BC [.5 .5 .5] >>"),
        72u,actual,QUANTAPDF_ERROR_FORMAT)==EXIT_SUCCESS);
    CHECK(render(create_alpha_group("/Alpha gs 0 0 12 4 re f",group,alpha_form,
        "<< /S /Alpha /TR << /FunctionType 2 /Domain [0 1] /N -1 >> >>"),
        72u,actual,QUANTAPDF_ERROR_FORMAT)==EXIT_SUCCESS);
    CHECK(render(create_alpha_group("/Alpha gs 0 0 12 4 re f",group,alpha_form,
        "<< /S /Alpha /TR << /FunctionType 2 /Domain [0 1] /C0 [0 1] /N 1 >> >>"),
        72u,actual,QUANTAPDF_ERROR_FORMAT)==EXIT_SUCCESS);
    for(const char *bad : {"<< /S /Alpha /TR << /FunctionType 3 /Domain [0 1] >> >>",
        "<< /S /Alpha /TR << /FunctionType 3 /Domain [0 1] /Bounds [.5] /Encode [0 1]"
        " /Functions [<< /FunctionType 2 /Domain [0 1] /N 1 >>] >> >>"}) {
        CHECK(render(create_alpha_group("/Alpha gs 0 0 12 4 re f",group,alpha_form,bad),
            72u,actual,QUANTAPDF_ERROR_FORMAT)==EXIT_SUCCESS);
    }
    CHECK(render(create_alpha_group("/Alpha gs 0 0 12 4 re f",group,alpha_form,
        "<< /S /Alpha /TR << /FunctionType 3 /Domain [0 1] /Bounds [] /Encode [0 1]"
        " /Functions [null] >> >>",true),72u,actual,QUANTAPDF_ERROR_FORMAT)==EXIT_SUCCESS);
    CHECK(render(create_alpha_group("/Alpha gs 0 0 12 4 re f",group,alpha_form,
        "<< /S /Alpha /TR << /FunctionType 3 /Domain [0 1] /Bounds [] /Encode [0 1]"
        " /Functions [<< /FunctionType 4 >>] >> >>"),72u,actual,QUANTAPDF_ERROR_FORMAT)==EXIT_SUCCESS);
    for(const char* invalid : {"{ 1 0 div }","{ pop pop }","{ pop true }","{ pop 2 cvr 2 idiv }",
        "{ 1 }","{ true { .25 } }","{ 1e-2 mul }","{ missing }"}) {
        CHECK(render(create_alpha_group("/Alpha gs 0 0 12 4 re f",group,alpha_form,
            "<< /S /Alpha >>",false,{calculator_function(invalid)}),
            72u,actual,QUANTAPDF_ERROR_FORMAT)==EXIT_SUCCESS);
    }
    std::string full_stack="{";
    for(unsigned i=0u;i<99u;++i)full_stack+=" dup";
    for(unsigned i=0u;i<99u;++i)full_stack+=" pop";
    CHECK(render(create_alpha_group("/Alpha gs 0 0 12 4 re f",group,alpha_form,
        "<< /S /Alpha >>",false,{calculator_function(full_stack+" }")}),72u,actual)==EXIT_SUCCESS);
    CHECK(render(create_alpha_group("/Alpha gs 0 0 12 4 re f",group,alpha_form,
        "<< /S /Alpha >>",false,{calculator_function("{ dup "+full_stack.substr(1u)+" }")}),
        72u,actual,QUANTAPDF_ERROR_FORMAT)==EXIT_SUCCESS);
    CHECK(render(create_alpha_group("/Alpha gs 0 0 12 4 re f",group,alpha_form,"<< /S /Alpha >>",false,
        {{"<< /FunctionType 0 /Domain [0 1] /Range [0 1] /Size [3] /BitsPerSample 12 >>",
          pack_samples(12u,{4095u,0u})}}),72u,actual,QUANTAPDF_ERROR_FORMAT)==EXIT_SUCCESS);
    CHECK(render(create_alpha_group("/Alpha gs 0 0 12 4 re f",group,alpha_form,"<< /S /Alpha >>",false,
        {{"<< /FunctionType 0 /Domain [0 1] /Range [0 1] /Size [1048577] /BitsPerSample 8 >>",""}}),
        72u,actual,QUANTAPDF_ERROR_UNSUPPORTED)==EXIT_SUCCESS);
    return EXIT_SUCCESS;
}

static int graphics_state_cases(const std::string& filename)
{
    size_t renders=0u,failures=0u;
    for(size_t depth:{2u,16u,17u,257u,4097u})for(bool clipped:{false,true})
    for(bool encrypted:{false,true})for(int kind:{0,1,2,3}) {
        const bool images=kind==1;
        const auto sample=native_graphics_fixture::create(depth,clipped,images,encrypted,kind==2,0,kind==3);
        CHECK(write_pdf(filename,sample.bytes));
        quantapdf_context *context=nullptr;quantapdf_document *document=nullptr;quantapdf_page *page=nullptr;
        size_t finalized=0u;OK(quantapdf_context_create(&context));context->test_finalizations=&finalized;
        OK(quantapdf_open_with_context(context,filename.c_str(),encrypted?"reader":nullptr,&document));
        CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(document,1,&page));quantapdf_close(document);quantapdf_drop_context(context);
        std::vector<quantapdf_bitmap*> held;
        for(unsigned dpi:{72u,96u,144u}) {
            quantapdf_bitmap *bitmap=nullptr;const quantapdf_render_options options{float(dpi),0.0f,0,{},0};
            const auto status=quantapdf_render_page_with_options(page,&options,&bitmap);
            if(status!=QUANTAPDF_OK)std::fprintf(stderr,"state depth=%zu clipped=%d image=%d late=%d encrypted=%d dpi=%u status=%d\n",depth,int(clipped),int(images),kind==2,int(encrypted),dpi,int(status));
            CHECK(status==QUANTAPDF_OK);
            int w=0,h=0,stride=0,components=0;const unsigned char *pixels=nullptr;size_t size=0u;
            OK(quantapdf_bitmap_dimensions(bitmap,&w,&h,&stride,&components));
            OK(quantapdf_bitmap_data(bitmap,&pixels,&size));
            const auto expected=native_graphics_fixture::rgb(dpi,clipped,images);
            CHECK(w==int(12u*dpi/72u) && h==int(9u*dpi/72u) && stride==w*3 && components==3 &&
                size==expected.size() && std::memcmp(pixels,expected.data(),size)==0);
            held.push_back(bitmap);++renders;
        }
        quantapdf_drop_page(page);CHECK(finalized==0u);
        for(size_t i=0u;i<held.size();++i) {
            const auto expected=native_graphics_fixture::rgb(i==0u?72u:i==1u?96u:144u,clipped,images);
            const unsigned char *pixels=nullptr;size_t size=0u;
            OK(quantapdf_bitmap_data(held[i],&pixels,&size));
            CHECK(size==expected.size() && std::memcmp(pixels,expected.data(),size)==0);
            quantapdf_drop_bitmap(held[i]);
        }
        CHECK(finalized==1u);
    }
    for(bool encrypted:{false,true})for(bool clipped:{false,true})for(int invalid:{1,2,3,4}) {
        const auto sample=native_graphics_fixture::create(257u,clipped,false,encrypted,true,invalid);
        CHECK(write_pdf(filename,sample.bytes));quantapdf_document *document=nullptr;quantapdf_page *page=nullptr;
        OK(quantapdf_open(filename.c_str(),encrypted?"reader":nullptr,&document));
        CHECK(std::remove(filename.c_str())==0);OK(quantapdf_load_page(document,1,&page));quantapdf_close(document);
        quantapdf_bitmap *bitmap=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
        CHECK(quantapdf_render_page(page,&bitmap)==(invalid==4?QUANTAPDF_ERROR_UNSUPPORTED:QUANTAPDF_ERROR_FORMAT) && bitmap==nullptr);
        quantapdf_drop_page(page);++failures;
    }
    CHECK(renders==240u && failures==16u);
    CHECK(alpha_group_cases(filename)==EXIT_SUCCESS);
    std::printf("Dynamic q/Q: %zu full RGB renders, 4097 frames, long state traffic, split/encrypted streams, %zu atomic errors\n",renders,failures);
    return EXIT_SUCCESS;
}
#endif
