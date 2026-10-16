/* #269: test-only isolation of EXACT source-rational Q4 samples vs the
 * existing owned-IR float affine handoff. Not a native rendering candidate.
 * This file is included inside the #269 test namespace. */
struct qp269_float_box_v0 { double x0,y0,x1,y1; };
struct qp269_float_projection_v0 {
    double pre_coeff[6];
    float post_coeff[6];
    double pre_vertex[4][2],post_vertex[4][2];
    qp269_float_box_v0 pre_box,post_box;
};
struct qp269_exact_q4_box_v0 {
    int64_t x0,x1,y0,y1,xd,yd;
};
struct qp269_float_handoff_audit_v0 {
    qp269_float_projection_v0 fill,clip;
    size_t exact_post_mask_flips=0u;
    size_t pre_post_mask_flips=0u;
    size_t pre_post_fill_only=0u,pre_post_clip_only=0u;
    size_t pre_post_both=0u;
    size_t exact_post_alpha_pixels=0u;
    size_t pre_post_alpha_pixels=0u;
    size_t native_rgba_mismatches=0u;
    uint64_t exact_rgb_abs=0u,pre_rgb_abs=0u,post_rgb_abs=0u;
};
static qp269_float_box_v0 qp269_bounds_v0(const double vertex[4][2])
{
    qp269_float_box_v0 out={vertex[0][0],vertex[0][1],
                             vertex[0][0],vertex[0][1]};
    for(unsigned i=1u;i<4u;++i){
        out.x0=std::min(out.x0,vertex[i][0]);
        out.x1=std::max(out.x1,vertex[i][0]);
        out.y0=std::min(out.y0,vertex[i][1]);
        out.y1=std::max(out.y1,vertex[i][1]);
    }
    CHECK(out.x1>out.x0 && out.y1>out.y0);
    return out;
}
static qp269_float_projection_v0 qp269_project_float_v0(
    const quantapdf_composer_path_command *commands,size_t count,
    const quantapdf_affine_transform& t,double sx,double sy)
{
    qp269_float_projection_v0 out{};
    CHECK(commands && count==5u);
    const double pre[6]={sx*(double)t.a,sy*(double)t.b,
        sx*(double)t.c,sy*(double)t.d,sx*(double)t.e,sy*(double)t.f};
    for(unsigned k=0u;k<6u;++k){
        CHECK(std::isfinite(pre[k]) && std::fabs(pre[k])<=65536.0);
        out.pre_coeff[k]=pre[k];
        out.post_coeff[k]=(float)pre[k];
        CHECK(std::isfinite(out.post_coeff[k]));
    }
    for(unsigned v=0u;v<4u;++v){
        const double x=(double)commands[v].point1.x;
        const double y=(double)commands[v].point1.y;
        out.pre_vertex[v][0]=pre[0]*x+pre[2]*y+pre[4];
        out.pre_vertex[v][1]=pre[1]*x+pre[3]*y+pre[5];
        out.post_vertex[v][0]=(double)out.post_coeff[0]*x+
            (double)out.post_coeff[2]*y+(double)out.post_coeff[4];
        out.post_vertex[v][1]=(double)out.post_coeff[1]*x+
            (double)out.post_coeff[3]*y+(double)out.post_coeff[5];
        CHECK(std::fabs(out.pre_vertex[v][0]-out.post_vertex[v][0])<=
                 1.0/4096.0 &&
              std::fabs(out.pre_vertex[v][1]-out.post_vertex[v][1])<=
                 1.0/4096.0);
    }
    out.pre_box=qp269_bounds_v0(out.pre_vertex);
    out.post_box=qp269_bounds_v0(out.post_vertex);
    return out;
}
static int64_t qp269_eighths_v0(double v)
{
    CHECK(std::isfinite(v) && v>=0.0 && v<=1000.0);
    const int64_t value=(int64_t)(v*8.0);
    CHECK((double)value==v*8.0);
    return value;
}
static qp269_exact_q4_box_v0 qp269_exact_source_box_v0(
    const qp269_heldout_rect_v0& source,unsigned crop,int rotation,
    uint32_t width,uint32_t height)
{
    /* These exact test-owned CropBox numbers match the raw PDF literals
     * in qp269_write_holdout_original_v0. No QPDF/PDFium pixel access. */
    const qp269_heldout_rect_v0 boxes[4]={
        {2.0,3.0,12.0,11.0},
        {2.125,3.0,12.125,11.0},
        {2.0,3.125,12.0,11.125},
        {2.375,3.375,12.375,11.375}};
    CHECK(crop<4u && width>0u && height>0u);
    const auto& cb=boxes[crop];
    const int64_t cx0=qp269_eighths_v0(cb.x0);
    const int64_t cx1=qp269_eighths_v0(cb.x1);
    const int64_t cy0=qp269_eighths_v0(cb.y0);
    const int64_t cy1=qp269_eighths_v0(cb.y1);
    const int64_t xx[2]={qp269_eighths_v0(source.x0),
                          qp269_eighths_v0(source.x1)};
    const int64_t yy[2]={qp269_eighths_v0(source.y0),
                          qp269_eighths_v0(source.y1)};
    qp269_exact_q4_box_v0 out{};
    const int64_t dx=cx1-cx0,dy=cy1-cy0;
    CHECK(dx>0 && dy>0);
    out.xd=(rotation==0 || rotation==180)?dx:dy;
    out.yd=(rotation==0 || rotation==180)?dy:dx;
    for(unsigned i=0u;i<4u;++i){
        const int64_t x=xx[i%2u],y=yy[i/2u];
        int64_t xn=0,yn=0;
        if(rotation==0){
            xn=(x-cx0)*(int64_t)width;
            yn=(cy1-y)*(int64_t)height;
        }else if(rotation==90){
            xn=(y-cy0)*(int64_t)width;
            yn=(x-cx0)*(int64_t)height;
        }else if(rotation==180){
            xn=(cx1-x)*(int64_t)width;
            yn=(y-cy0)*(int64_t)height;
        }else{
            CHECK(rotation==270);
            xn=(cy1-y)*(int64_t)width;
            yn=(cx1-x)*(int64_t)height;
        }
        if(i==0u){out.x0=out.x1=xn;out.y0=out.y1=yn;}
        else{
            out.x0=std::min(out.x0,xn);out.x1=std::max(out.x1,xn);
            out.y0=std::min(out.y0,yn);out.y1=std::max(out.y1,yn);
        }
    }
    CHECK(out.x1>out.x0 && out.y1>out.y0);
    return out;
}
static bool qp269_exact_inside_v0(const qp269_exact_q4_box_v0& r,
    size_t px,size_t py,unsigned sx,unsigned sy)
{
    CHECK(px<65536u && py<65536u && sx<4u && sy<4u);
    /* Integer Q4 sample coordinates, each in device-pixel eighths. */
    const int64_t x8=8*(int64_t)px+2*(int64_t)sx+1;
    const int64_t y8=8*(int64_t)py+2*(int64_t)sy+1;
    return x8*r.xd>=8*r.x0 && x8*r.xd<8*r.x1 &&
           y8*r.yd>=8*r.y0 && y8*r.yd<8*r.y1;
}
static bool qp269_float_inside_v0(const qp269_float_box_v0& r,
    double x,double y)
{
    return x>=r.x0 && x<r.x1 && y>=r.y0 && y<r.y1;
}
static uint32_t qp269_float_bits_v0(float f)
{
    uint32_t bits=0u;
    static_assert(sizeof(bits)==sizeof(f),"IEEE binary32 expected");
    std::memcpy(&bits,&f,sizeof(bits));
    return bits;
}
static qp269_float_handoff_audit_v0 qp269_audit_handoff_v0(
    const quantapdf_ir& ir,const quantapdf_graph_page_geometry& geometry,
    const quantapdf_graph_device_grid& grid,unsigned shape,unsigned crop,
    int rotation,const unsigned char *ref_bytes,int ref_stride,
    const quantapdf_ir_raster_image& native)
{
    CHECK(shape<2u && ir.operation_count==1u && ir.operations &&
          ir.resources.clip_count==1u && ir.resources.clips &&
          ref_bytes && ref_stride>0 && native.pixels &&
          native.width==(size_t)grid.width &&
          native.height==(size_t)grid.height);
    quantapdf_graph_page_display display{};
    OK(quantapdf_graph_page_display_map(&geometry,&display));
    const double sx=(double)grid.width/display.width_points;
    const double sy=(double)grid.height/display.height_points;
    CHECK(std::isfinite(sx) && std::isfinite(sy) && sx>0.0 && sy>0.0);
    const auto& path=ir.operations[0].value.path;
    const auto& clip=ir.resources.clips[0];
    qp269_float_handoff_audit_v0 audit{};
    audit.fill=qp269_project_float_v0(path.commands,path.command_count,
                                      path.options.transform,sx,sy);
    audit.clip=qp269_project_float_v0(clip.commands,clip.command_count,
                                      clip.transform,sx,sy);
    const qp269_exact_q4_box_v0 exact_fill=qp269_exact_source_box_v0(
        qp269_holdout_shapes_v0[shape].fill,crop,rotation,
        grid.width,grid.height);
    const qp269_exact_q4_box_v0 exact_clip=qp269_exact_source_box_v0(
        qp269_holdout_shapes_v0[shape].clip,crop,rotation,
        grid.width,grid.height);
    for(size_t y=0u;y<(size_t)grid.height;++y)
      for(size_t x=0u;x<(size_t)grid.width;++x){
        unsigned exact=0u,pre=0u,post=0u;
        for(unsigned j=0u;j<4u;++j)
          for(unsigned i=0u;i<4u;++i){
            const double fx=(double)x+((double)i+0.5)/4.0;
            const double fy=(double)y+((double)j+0.5)/4.0;
            const bool ef=qp269_exact_inside_v0(exact_fill,x,y,i,j);
            const bool ec=qp269_exact_inside_v0(exact_clip,x,y,i,j);
            const bool pf=qp269_float_inside_v0(audit.fill.pre_box,fx,fy);
            const bool pc=qp269_float_inside_v0(audit.clip.pre_box,fx,fy);
            const bool nf=qp269_float_inside_v0(audit.fill.post_box,fx,fy);
            const bool nc=qp269_float_inside_v0(audit.clip.post_box,fx,fy);
            exact+=(unsigned)(ef && ec);
            pre+=(unsigned)(pf && pc);
            post+=(unsigned)(nf && nc);
            audit.exact_post_mask_flips+=(unsigned)((ef && ec)!=(nf && nc));
            audit.pre_post_mask_flips+=(unsigned)((pf && pc)!=(nf && nc));
            if(pf!=nf || pc!=nc){
                if(pf!=nf && pc!=nc)++audit.pre_post_both;
                else if(pf!=nf)++audit.pre_post_fill_only;
                else ++audit.pre_post_clip_only;
            }
        }
        const unsigned aa[3]={ (255u*exact+8u)/16u,
                               (255u*pre+8u)/16u,
                               (255u*post+8u)/16u};
        audit.exact_post_alpha_pixels+=(unsigned)(aa[0]!=aa[2]);
        audit.pre_post_alpha_pixels+=(unsigned)(aa[1]!=aa[2]);
        const unsigned char *rgba=native.pixels+(y*(size_t)grid.width+x)*4u;
        if(rgba[3]!=aa[2] ||
           (aa[2]!=0u && (rgba[0]!=255u ||rgba[1]!=0u ||rgba[2]!=0u)) ||
           (aa[2]==0u && (rgba[0]!=0u ||rgba[1]!=0u ||rgba[2]!=0u)))
            ++audit.native_rgba_mismatches;
        const unsigned char *rgb=ref_bytes+y*(size_t)ref_stride+x*3u;
        CHECK(rgb[0]==255u && rgb[1]==rgb[2] &&
              (rgb[1]==0u || rgb[1]==255u));
        const uint64_t er[3]={
            2u*(uint64_t)std::abs((int)(255u-aa[0])-(int)rgb[1]),
            2u*(uint64_t)std::abs((int)(255u-aa[1])-(int)rgb[1]),
            2u*(uint64_t)std::abs((int)(255u-aa[2])-(int)rgb[1])};
        audit.exact_rgb_abs+=er[0];audit.pre_rgb_abs+=er[1];
        audit.post_rgb_abs+=er[2];
      }
    CHECK(audit.native_rgba_mismatches==0u);
    return audit;
}
static void qp269_float_handoff_print_v0(FILE *out,
    const qp269_float_handoff_audit_v0& audit)
{
    std::fprintf(out,"{\"exact_rgb_abs\":%llu,\"pre_rgb_abs\":%llu,"
        "\"post_rgb_abs\":%llu,\"native_rgba_mismatches\":%zu,"
        "\"exact_post_mask_flips\":%zu,\"pre_post_mask_flips\":%zu,"
        "\"pre_post_fill_only\":%zu,\"pre_post_clip_only\":%zu,"
        "\"pre_post_both\":%zu,\"exact_post_alpha_pixels\":%zu,"
        "\"pre_post_alpha_pixels\":%zu,",
        (unsigned long long)audit.exact_rgb_abs,
        (unsigned long long)audit.pre_rgb_abs,
        (unsigned long long)audit.post_rgb_abs,
        audit.native_rgba_mismatches,audit.exact_post_mask_flips,
        audit.pre_post_mask_flips,audit.pre_post_fill_only,
        audit.pre_post_clip_only,audit.pre_post_both,
        audit.exact_post_alpha_pixels,audit.pre_post_alpha_pixels);
    const qp269_float_projection_v0 *p[2]={&audit.fill,&audit.clip};
    for(unsigned t=0u;t<2u;++t){
        std::fprintf(out,"\"%s_coeff_bits\":[",t==0u?"fill":"clip");
        for(unsigned k=0u;k<6u;++k)
            std::fprintf(out,"\"%08x\"%s",
                qp269_float_bits_v0(p[t]->post_coeff[k]),k==5u?"":",");
        std::fprintf(out,"],\"%s_pre_vertices\":[",
            t==0u?"fill":"clip");
        for(unsigned v=0u;v<4u;++v)
            std::fprintf(out,"[%.17g,%.17g]%s",p[t]->pre_vertex[v][0],
                p[t]->pre_vertex[v][1],v==3u?"":",");
        std::fprintf(out,"],\"%s_post_vertices\":[",
            t==0u?"fill":"clip");
        for(unsigned v=0u;v<4u;++v)
            std::fprintf(out,"[%.17g,%.17g]%s",p[t]->post_vertex[v][0],
                p[t]->post_vertex[v][1],v==3u?"":",");
        std::fprintf(out,"]%s",t==1u?"":",");
    }
    std::fprintf(out,"}");
}
