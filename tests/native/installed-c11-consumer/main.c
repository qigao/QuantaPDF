#include <quantapdf/quantapdf.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int write_blank_pdf(const char *path,int inherited_rotation)
{
    FILE *file=fopen(path,"wb");
    long offsets[4]={0,0,0,0};
    long xref=0;
    int good=0;
    if(file==NULL)return 0;
    if(fputs("%PDF-1.4\n",file)==EOF)goto done;
    offsets[1]=ftell(file);
    if(offsets[1]<0 || fputs(
        "1 0 obj\n"
        "<< /Type /Catalog /Pages 2 0 R >>\n"
        "endobj\n",file)==EOF)goto done;
    offsets[2]=ftell(file);
    if(offsets[2]<0 || (inherited_rotation ?
        fputs(
            "2 0 obj\n"
            "<< /Type /Pages /Kids [3 0 R] /Count 1 /Rotate 90 >>\n"
            "endobj\n",file) :
        fputs(
            "2 0 obj\n"
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>\n"
            "endobj\n",file))==EOF)goto done;
    offsets[3]=ftell(file);
    if(offsets[3]<0 || (inherited_rotation ?
        fputs(
            "3 0 obj\n"
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 8 6]"
            " /Resources << >> >>\n"
            "endobj\n",file) :
        fputs(
            "3 0 obj\n"
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 8 8]"
            " /Resources << >> >>\n"
            "endobj\n",file))==EOF)goto done;
    xref=ftell(file);
    if(xref<0 || fputs(
        "xref\n0 4\n"
        "0000000000 65535 f \n",file)==EOF)goto done;
    for(int i=1;i<4;++i)
        if(fprintf(file,"%010ld 00000 n \n",offsets[i])<0)goto done;
    if(fprintf(file,
        "trailer\n"
        "<< /Size 4 /Root 1 0 R >>\n"
        "startxref\n%ld\n%%%%EOF\n",xref)<0)goto done;
    good=1;
done:
    if(fclose(file)!=0)good=0;
    if(!good)remove(path);
    return good;
}

/* Independent installed-C11 original with THREE actual PageTree leaves.
 * Distinct sizes and a direct original /Rotate 90 on page index one
 * detect any unqualified substitution with page index zero. */
static int write_multi_pdf(const char *path,int shifted_origin)
{
    static const char *const object_text[6]={
        "",
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R] /Count 3 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 8 8]"
        " /Resources << >> >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 8 6]"
        " /Rotate 90 /Resources << >> >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 5 7]"
        " /Resources << >> >>"
    };
    long offsets[7]={0,0,0,0,0,0,0};
    long xref=0;
    int ok=0;
    FILE *file=fopen(path,"wb");
    if(file==NULL)return 0;
    if(fputs("%PDF-1.4\n",file)==EOF)goto done;
    for(int i=1;i<=5;++i) {
        offsets[i]=ftell(file);
        /* The installed standalone C11 consumer independently creates
         * a selected page with a true negative MediaBox origin, not a
         * test-only in-tree fixture or an altered library header. */
        const char *body=(shifted_origin>=4 && i==5) ?
            (shifted_origin==5 ?
             "<< /Type /Page /Parent 2 0 R"
             " /MediaBox [0 0 32 24]"
             " /CropBox [0 0 16.5 12.25] /Rotate 90"
             " /Contents 6 0 R /Resources << >> >>" :
             "<< /Type /Page /Parent 2 0 R"
             " /MediaBox [0 0 32 24]"
             " /CropBox [0 0 16.5 12.25]"
             " /Contents 6 0 R /Resources << >> >>") :
            ((shifted_origin==3 && i==5) ?
            "<< /Type /Page /Parent 2 0 R"
            " /MediaBox [0.25 0.5 32.25 24.5]"
            " /CropBox [8.25 4.5 24.25 20.5]"
            " /Contents 6 0 R /Resources << >> >>" :
            ((shifted_origin==2 && i==5) ?
            "<< /Type /Page /Parent 2 0 R /MediaBox [-9 5 23 29]"
            " /CropBox [-1 9 15 25] /Rotate 90"
            " /Contents 6 0 R /Resources << >> >>" :
            ((shifted_origin && i==5) ?
            "<< /Type /Page /Parent 2 0 R /MediaBox [-9 5 -4 12]"
            " /Resources << >> >>" : object_text[i])));
        if(offsets[i]<0 ||
           fprintf(file,"%d 0 obj\n%s\nendobj\n",
                   i,body)<0)goto done;
    }
    if(shifted_origin>=2) {
        /* The installed consumer authors REAL PDF PATH operators
         * crossing either the exact integer or quarter-point CropBox.
         * No synthetic IR input or copied provider-produced pixels. */
        static const char integer_paint[]="1 0 0 rg -5 10 12 7 re f\n";
        static const char quarter_paint[]="1 0 0 rg 7.5 4.5 12 7 re f\n";
        static const char full_paint[]="1 0 0 rg 0 0 32 24 re f\n";
        const char *paint=shifted_origin>=4?full_paint:
            shifted_origin==3?quarter_paint:integer_paint;
        offsets[6]=ftell(file);
        if(offsets[6]<0 ||
           fprintf(file,"6 0 obj\n<< /Length %zu >>\nstream\n",
                   strlen(paint))<0 ||
           fputs(paint,file)==EOF ||
           fputs("endstream\nendobj\n",file)==EOF)
            goto done;
    }
    xref=ftell(file);
    const int objects=shifted_origin>=2?7:6;
    if(xref<0 || fprintf(file,
        "xref\n0 %d\n0000000000 65535 f \n",objects)<0)
        goto done;
    for(int i=1;i<objects;++i)
        if(fprintf(file,"%010ld 00000 n \n",offsets[i])<0)
            goto done;
    if(fprintf(file,
        "trailer\n<< /Size %d /Root 1 0 R >>\n"
        "startxref\n%ld\n%%%%EOF\n",objects,xref)<0)goto done;
    ok=1;
done:
    if(fclose(file)!=0)ok=0;
    if(!ok)remove(path);
    return ok;
}

int main(void)
{
    const char *filename="quantapdf_installed_c11_smoke.pdf";
    quantapdf_context *context=NULL;
    quantapdf_document *document=NULL;
    quantapdf_page *page=NULL;
    quantapdf_bitmap *bitmap=NULL,*device_bitmap=NULL;
    const unsigned char *pixels=NULL;
    size_t size=0u;
    int pages=0,w=0,h=0,stride=0,channels=0;
    int result=EXIT_FAILURE;
    quantapdf_status status=QUANTAPDF_OK;

    if(!write_blank_pdf(filename,0)) {
        fputs("installed C11 consumer: failed to write original PDF\n",stderr);
        goto done;
    }
    status=quantapdf_context_create(&context);
    if(status!=QUANTAPDF_OK || context==NULL)goto fail;
    status=quantapdf_open_with_context(context,filename,NULL,&document);
    if(status!=QUANTAPDF_OK || document==NULL)goto fail;
    status=quantapdf_page_count(document,&pages);
    if(status!=QUANTAPDF_OK || pages!=1)goto fail;
    status=quantapdf_load_page(document,0,&page);
    if(status!=QUANTAPDF_OK || page==NULL)goto fail;
    status=quantapdf_render_page(page,&bitmap);
    if(status!=QUANTAPDF_OK || bitmap==NULL)goto fail;
    /* Standalone installed C11 consumer checks real native 96/144-DPI
     * device canvas through only exported API and CMake targets. */
    {
        const int dpis[2]={96,144};
        for(size_t k=0u;k<2u;++k) {
            int dw=0,dh=0,ds=0,dc=0;
            const unsigned char *samples=NULL;
            size_t count=0u;
            quantapdf_render_options options={0};
            options.struct_size=sizeof(options);
            options.dpi=(float)dpis[k];
            status=quantapdf_render_page_with_options(
                page,&options,&device_bitmap);
            if(status!=QUANTAPDF_OK || device_bitmap==NULL)goto fail;
            status=quantapdf_bitmap_dimensions(
                device_bitmap,&dw,&dh,&ds,&dc);
            if(status!=QUANTAPDF_OK ||
               dw!=(dpis[k]==96?11:16) ||
               dh!=(dpis[k]==96?11:16) ||
               ds!=dw*3 || dc!=3)goto fail;
            status=quantapdf_bitmap_data(device_bitmap,&samples,&count);
            if(status!=QUANTAPDF_OK || samples==NULL ||
               count!=(size_t)dw*(size_t)dh*3u)goto fail;
            for(size_t i=0u;i<count;++i)
                if(samples[i]!=255u)goto fail;
            quantapdf_drop_bitmap(device_bitmap);
            device_bitmap=NULL;
            options.rotation_degrees=90.0f;
            status=quantapdf_render_page_with_options(
                page,&options,&device_bitmap);
            if(status!=QUANTAPDF_OK || device_bitmap==NULL)goto fail;
            status=quantapdf_bitmap_dimensions(
                device_bitmap,&dw,&dh,&ds,&dc);
            if(status!=QUANTAPDF_OK ||
               dw!=(dpis[k]==96?11:16) ||
               dh!=(dpis[k]==96?11:16) ||
               ds!=dw*3 || dc!=3)goto fail;
            status=quantapdf_bitmap_data(
                device_bitmap,&samples,&count);
            if(status!=QUANTAPDF_OK || samples==NULL ||
               count!=(size_t)dw*(size_t)dh*3u)goto fail;
            for(size_t i=0u;i<count;++i)
                if(samples[i]!=255u)goto fail;
            quantapdf_drop_bitmap(device_bitmap);
            device_bitmap=NULL;
        }
    }

    /* The successful Bitmap retains its Context. No document, page or
     * source fixture remains live while the public C11 pixel view is used. */
    quantapdf_drop_page(page);page=NULL;
    quantapdf_close(document);document=NULL;
    quantapdf_drop_context(context);context=NULL;

    status=quantapdf_bitmap_dimensions(
        bitmap,&w,&h,&stride,&channels);
    if(status!=QUANTAPDF_OK || w!=8 || h!=8 ||
       stride!=24 || channels!=3)goto fail;
    status=quantapdf_bitmap_data(bitmap,&pixels,&size);
    if(status!=QUANTAPDF_OK || pixels==NULL || size!=192u)goto fail;
    for(size_t i=0u;i<size;++i) {
        if(pixels[i]!=255u) {
            fputs("installed C11 consumer: blank PDF not opaque white\n",stderr);
            goto done;
        }
    }
    /* The installed consumer generates a second ORIGINAL PDF with
     * inherited page-tree /Rotate 90, not an option-only rotation.
     * /MediaBox is 8x6; effective native page is 6x8. The same
     * instance of the installed public API must honor both rotations. */
    quantapdf_drop_bitmap(bitmap);bitmap=NULL;
    if(!write_blank_pdf(filename,1))goto done;
    status=quantapdf_context_create(&context);
    if(status!=QUANTAPDF_OK || context==NULL)goto fail;
    status=quantapdf_open_with_context(
        context,filename,NULL,&document);
    if(status!=QUANTAPDF_OK || document==NULL)goto fail;
    status=quantapdf_load_page(document,0,&page);
    if(status!=QUANTAPDF_OK || page==NULL)goto fail;
    {
        quantapdf_rect bounds={0};
        status=quantapdf_page_bounds(page,&bounds);
        if(status!=QUANTAPDF_OK || bounds.x0!=0.0f ||
           bounds.y0!=0.0f || bounds.x1!=6.0f ||
           bounds.y1!=8.0f)goto fail;
        status=quantapdf_render_page(page,&bitmap);
        if(status!=QUANTAPDF_OK || bitmap==NULL)goto fail;
        status=quantapdf_bitmap_dimensions(
            bitmap,&w,&h,&stride,&channels);
        if(status!=QUANTAPDF_OK || w!=6 || h!=8 ||
           stride!=18 || channels!=3)goto fail;
        const int dpis[2]={96,144};
        for(size_t k=0u;k<2u;++k) {
            quantapdf_render_options options={0};
            options.struct_size=sizeof(options);
            options.dpi=(float)dpis[k];
            options.rotation_degrees=90.0f;
            status=quantapdf_render_page_with_options(
                page,&options,&device_bitmap);
            if(status!=QUANTAPDF_OK || device_bitmap==NULL)goto fail;
            status=quantapdf_bitmap_dimensions(
                device_bitmap,&w,&h,&stride,&channels);
            if(status!=QUANTAPDF_OK ||
               w!=(dpis[k]==96?11:16) ||
               h!=(dpis[k]==96?8:12) ||
               stride!=w*3 || channels!=3)goto fail;
            status=quantapdf_bitmap_data(
                device_bitmap,&pixels,&size);
            if(status!=QUANTAPDF_OK || pixels==NULL ||
               size!=(size_t)w*(size_t)h*3u)goto fail;
            for(size_t i=0u;i<size;++i)
                if(pixels[i]!=255u)goto fail;
            quantapdf_drop_bitmap(device_bitmap);device_bitmap=NULL;
        }
    }
    quantapdf_drop_page(page);page=NULL;
    quantapdf_close(document);document=NULL;
    quantapdf_drop_context(context);context=NULL;
    status=quantapdf_bitmap_data(bitmap,&pixels,&size);
    if(status!=QUANTAPDF_OK || pixels==NULL || size!=144u)
        goto fail;
    for(size_t i=0u;i<size;++i)
        if(pixels[i]!=255u)goto fail;
    /* Standalone C11 multi-page selection test with truly distinct
     * PDF leaf dimensions, inherited-free original rotation and
     * display options on the selected (not first) backend Page lease. */
    quantapdf_drop_bitmap(bitmap);bitmap=NULL;
    if(!write_multi_pdf(filename,0))goto done;
    status=quantapdf_context_create(&context);
    if(status!=QUANTAPDF_OK || context==NULL)goto fail;
    status=quantapdf_open_with_context(
        context,filename,NULL,&document);
    if(status!=QUANTAPDF_OK || document==NULL)goto fail;
    status=quantapdf_page_count(document,&pages);
    if(status!=QUANTAPDF_OK || pages!=3)goto fail;
    {
        quantapdf_page *invalid=(quantapdf_page *)(uintptr_t)1u;
        status=quantapdf_load_page(document,3,&invalid);
        if(status==QUANTAPDF_OK || invalid!=NULL)goto fail;
        status=quantapdf_load_page(document,-1,&invalid);
        if(status!=QUANTAPDF_ERROR_ARGUMENT || invalid!=NULL)goto fail;
    }
    status=quantapdf_load_page(document,2,&page);
    if(status!=QUANTAPDF_OK || page==NULL)goto fail;
    status=quantapdf_render_page(page,&bitmap);
    if(status!=QUANTAPDF_OK || bitmap==NULL)goto fail;
    status=quantapdf_bitmap_dimensions(
        bitmap,&w,&h,&stride,&channels);
    if(status!=QUANTAPDF_OK ||
       w!=5 || h!=7 || stride!=15 || channels!=3)goto fail;
    quantapdf_drop_page(page);page=NULL;
    quantapdf_drop_bitmap(bitmap);bitmap=NULL;
    status=quantapdf_load_page(document,1,&page);
    if(status!=QUANTAPDF_OK || page==NULL)goto fail;
    {
        quantapdf_rect bounds={0};
        status=quantapdf_page_bounds(page,&bounds);
        if(status!=QUANTAPDF_OK || bounds.x0!=0.0f ||
           bounds.y0!=0.0f || bounds.x1!=6.0f ||
           bounds.y1!=8.0f)goto fail;
        const int dpis[2]={96,144};
        for(size_t k=0u;k<2u;++k) {
            quantapdf_render_options options={0};
            options.struct_size=sizeof(options);
            options.dpi=(float)dpis[k];
            options.rotation_degrees=90.0f;
            status=quantapdf_render_page_with_options(
                page,&options,&device_bitmap);
            if(status!=QUANTAPDF_OK || device_bitmap==NULL)goto fail;
            status=quantapdf_bitmap_dimensions(
                device_bitmap,&w,&h,&stride,&channels);
            if(status!=QUANTAPDF_OK ||
               w!=(dpis[k]==96?11:16) ||
               h!=(dpis[k]==96?8:12) ||
               stride!=w*3 || channels!=3)goto fail;
            status=quantapdf_bitmap_data(
                device_bitmap,&pixels,&size);
            if(status!=QUANTAPDF_OK || pixels==NULL ||
               size!=(size_t)w*(size_t)h*3u)goto fail;
            for(size_t i=0u;i<size;++i)
                if(pixels[i]!=255u)goto fail;
            quantapdf_drop_bitmap(device_bitmap);device_bitmap=NULL;
        }
    }
    status=quantapdf_render_page(page,&bitmap);
    if(status!=QUANTAPDF_OK || bitmap==NULL)goto fail;
    quantapdf_drop_page(page);page=NULL;
    quantapdf_close(document);document=NULL;
    quantapdf_drop_context(context);context=NULL;
    status=quantapdf_bitmap_dimensions(
        bitmap,&w,&h,&stride,&channels);
    if(status!=QUANTAPDF_OK || w!=6 || h!=8 ||
       stride!=18 || channels!=3)goto fail;
    status=quantapdf_bitmap_data(bitmap,&pixels,&size);
    if(status!=QUANTAPDF_OK || size!=144u || pixels==NULL)goto fail;
    for(size_t i=0u;i<size;++i)
        if(pixels[i]!=255u)goto fail;

    /* Negative nonzero MediaBox offset at a REAL nonzero selected Page:
     * the page remains 5x7 and its all-white RGB Bitmap is retained
     * after the independently installed C11 SDK owner chain drops. */
    quantapdf_drop_bitmap(bitmap);bitmap=NULL;
    if(!write_multi_pdf(filename,1))goto done;
    status=quantapdf_context_create(&context);
    if(status!=QUANTAPDF_OK || context==NULL)goto fail;
    status=quantapdf_open_with_context(
        context,filename,NULL,&document);
    if(status!=QUANTAPDF_OK || document==NULL)goto fail;
    status=quantapdf_load_page(document,2,&page);
    if(status!=QUANTAPDF_OK || page==NULL)goto fail;
    {
        quantapdf_rect bounds={0};
        status=quantapdf_page_bounds(page,&bounds);
        if(status!=QUANTAPDF_OK ||
           bounds.x0!=0.0f || bounds.y0!=0.0f ||
           bounds.x1!=5.0f || bounds.y1!=7.0f)goto fail;
    }
    status=quantapdf_render_page(page,&bitmap);
    if(status!=QUANTAPDF_OK || bitmap==NULL)goto fail;
    status=quantapdf_bitmap_dimensions(
        bitmap,&w,&h,&stride,&channels);
    if(status!=QUANTAPDF_OK || w!=5 || h!=7 ||
       stride!=15 || channels!=3)goto fail;
    quantapdf_drop_page(page);page=NULL;
    quantapdf_close(document);document=NULL;
    quantapdf_drop_context(context);context=NULL;
    status=quantapdf_bitmap_data(bitmap,&pixels,&size);
    if(status!=QUANTAPDF_OK || pixels==NULL || size!=105u)goto fail;
    for(size_t i=0u;i<size;++i)
        if(pixels[i]!=255u)goto fail;

    /* A fully independent INSTALLED C11 SDK consumer authors a PDF
     * with selected index 2, signed nonzero MediaBox, non-full CropBox
     * and original /Rotate90. A real red PATH crosses the crop left
     * edge. Source coordinates must be interpreted BEFORE C11 paint. */
    quantapdf_drop_bitmap(bitmap);bitmap=NULL;
    if(!write_multi_pdf(filename,2))goto done;
    status=quantapdf_context_create(&context);
    if(status!=QUANTAPDF_OK || context==NULL)goto fail;
    status=quantapdf_open_with_context(
        context,filename,NULL,&document);
    if(status!=QUANTAPDF_OK || document==NULL)goto fail;
    status=quantapdf_page_count(document,&pages);
    if(status!=QUANTAPDF_OK || pages!=3)goto fail;
    status=quantapdf_load_page(document,2,&page);
    if(status!=QUANTAPDF_OK || page==NULL)goto fail;
    {
        quantapdf_rect crop={0};
        status=quantapdf_page_bounds(page,&crop);
        if(status!=QUANTAPDF_OK || crop.x0!=0.0f ||
           crop.y0!=0.0f || crop.x1!=16.0f ||
           crop.y1!=16.0f)goto fail;
    }
    status=quantapdf_render_page(page,&bitmap);
    if(status!=QUANTAPDF_OK || bitmap==NULL)goto fail;
    status=quantapdf_bitmap_dimensions(
        bitmap,&w,&h,&stride,&channels);
    if(status!=QUANTAPDF_OK || w!=16 || h!=16 ||
       stride!=48 || channels!=3)goto fail;
    status=quantapdf_bitmap_data(bitmap,&pixels,&size);
    if(status!=QUANTAPDF_OK || pixels==NULL ||
       size!=768u)goto fail;
    {
        const unsigned char *red=pixels+2u*48u+5u*3u;
        const unsigned char *white=pixels+12u*48u+12u*3u;
        if(red[0]!=255u || red[1]!=0u || red[2]!=0u ||
           white[0]!=255u || white[1]!=255u || white[2]!=255u)
            goto fail;
    }
    {
        const int dpis[2]={96,144};
        for(size_t k=0u;k<2u;++k) {
            quantapdf_render_options opts={0};
            opts.struct_size=sizeof(opts);
            opts.dpi=(float)dpis[k];
            status=quantapdf_render_page_with_options(
                page,&opts,&device_bitmap);
            if(status!=QUANTAPDF_OK || device_bitmap==NULL)goto fail;
            status=quantapdf_bitmap_dimensions(
                device_bitmap,&w,&h,&stride,&channels);
            if(status!=QUANTAPDF_OK ||
               w!=(dpis[k]==96?22:32) || h!=w ||
               stride!=w*3 || channels!=3)goto fail;
            status=quantapdf_bitmap_data(
                device_bitmap,&pixels,&size);
            if(status!=QUANTAPDF_OK || pixels==NULL ||
               size!=(size_t)w*(size_t)h*3u)goto fail;
            {
                const size_t x=dpis[k]==96?6u:11u;
                const size_t y=4u;
                const unsigned char *red=pixels+
                    y*(size_t)stride+x*3u;
                if(red[0]!=255u || red[1]!=0u || red[2]!=0u)
                    goto fail;
            }
            quantapdf_drop_bitmap(device_bitmap);device_bitmap=NULL;
        }
    }
    quantapdf_drop_page(page);page=NULL;
    quantapdf_close(document);document=NULL;
    quantapdf_drop_context(context);context=NULL;
    status=quantapdf_bitmap_data(bitmap,&pixels,&size);
    if(status!=QUANTAPDF_OK || pixels==NULL || size!=768u)goto fail;
    if(pixels[2u*48u+5u*3u+1u]!=0u)goto fail;

    /* Strictly independent installed C11 consumer: ORIGINAL quarter
     * Media/Crop endpoints on a true selected page index 2. Visible
     * extent is exact 16x16, but authored red PATH crosses the Crop
     * right at x=11.25: fixed 4x4 C11 source-over edge at (11,11)
     * is (255,191,191), never a rounded whole-pixel rectangle. */
    quantapdf_drop_bitmap(bitmap);bitmap=NULL;
    if(!write_multi_pdf(filename,3))goto done;
    status=quantapdf_context_create(&context);
    if(status!=QUANTAPDF_OK || context==NULL)goto fail;
    status=quantapdf_open_with_context(
        context,filename,NULL,&document);
    if(status!=QUANTAPDF_OK || document==NULL)goto fail;
    status=quantapdf_page_count(document,&pages);
    if(status!=QUANTAPDF_OK || pages!=3)goto fail;
    status=quantapdf_load_page(document,2,&page);
    if(status!=QUANTAPDF_OK || page==NULL)goto fail;
    {
        quantapdf_rect bounds={0};
        status=quantapdf_page_bounds(page,&bounds);
        if(status!=QUANTAPDF_OK || bounds.x0!=0.0f ||
           bounds.y0!=0.0f || bounds.x1!=16.0f ||
           bounds.y1!=16.0f)goto fail;
    }
    status=quantapdf_render_page(page,&bitmap);
    if(status!=QUANTAPDF_OK || bitmap==NULL)goto fail;
    status=quantapdf_bitmap_dimensions(
        bitmap,&w,&h,&stride,&channels);
    if(status!=QUANTAPDF_OK || w!=16 || h!=16 ||
       stride!=48 || channels!=3)goto fail;
    status=quantapdf_bitmap_data(bitmap,&pixels,&size);
    if(status!=QUANTAPDF_OK || pixels==NULL || size!=768u)goto fail;
    {
        const unsigned char *red=pixels+11u*48u+2u*3u;
        const unsigned char *edge=pixels+11u*48u+11u*3u;
        const unsigned char *white=pixels+11u*48u+12u*3u;
        if(red[0]!=255u || red[1]!=0u || red[2]!=0u ||
           edge[0]!=255u || edge[1]!=191u || edge[2]!=191u ||
           white[0]!=255u || white[1]!=255u || white[2]!=255u)
            goto fail;
    }
    {
        const int dpis[2]={96,144};
        for(size_t k=0u;k<2u;++k) {
            quantapdf_render_options opts={0};
            opts.struct_size=sizeof(opts);
            opts.dpi=(float)dpis[k];
            status=quantapdf_render_page_with_options(
                page,&opts,&device_bitmap);
            if(status!=QUANTAPDF_OK || device_bitmap==NULL)goto fail;
            status=quantapdf_bitmap_dimensions(
                device_bitmap,&w,&h,&stride,&channels);
            if(status!=QUANTAPDF_OK ||
               w!=(dpis[k]==96?22:32) || h!=w ||
               stride!=w*3 || channels!=3)goto fail;
            status=quantapdf_bitmap_data(
                device_bitmap,&pixels,&size);
            if(status!=QUANTAPDF_OK || pixels==NULL ||
               size!=(size_t)w*(size_t)h*3u)goto fail;
            {
                const size_t x=dpis[k]==96?14u:22u;
                const size_t y=dpis[k]==96?15u:23u;
                const unsigned char *sample=pixels+
                    y*(size_t)stride+x*3u;
                if(sample[0]!=255u ||
                   sample[1]!=(dpis[k]==96?0u:127u) ||
                   sample[2]!=(dpis[k]==96?0u:127u))
                    goto fail;
            }
            quantapdf_drop_bitmap(device_bitmap);device_bitmap=NULL;
        }
    }
    quantapdf_drop_page(page);page=NULL;
    quantapdf_close(document);document=NULL;
    quantapdf_drop_context(context);context=NULL;
    status=quantapdf_bitmap_data(bitmap,&pixels,&size);
    if(status!=QUANTAPDF_OK || pixels==NULL || size!=768u)goto fail;
    if(pixels[11u*48u+11u*3u+1u]!=191u)goto fail;

    /* True FRACTIONAL original visible width=16.5 and height=12.25,
     * authored as a REAL third PageTree leaf. The exported standalone
     * C11 library must ceil only its physical DEVICE storage, not
     * stretch ink or crop a previously rendered 72-DPI bitmap.
     * Mode 5 additionally applies ORIGINAL PDF /Rotate90. */
    quantapdf_drop_bitmap(bitmap);bitmap=NULL;
    for(int source_turn=0;source_turn<2;++source_turn) {
        if(!write_multi_pdf(filename,source_turn?5:4))goto done;
        status=quantapdf_context_create(&context);
        if(status!=QUANTAPDF_OK || context==NULL)goto fail;
        status=quantapdf_open_with_context(
            context,filename,NULL,&document);
        if(status!=QUANTAPDF_OK || document==NULL)goto fail;
        status=quantapdf_page_count(document,&pages);
        if(status!=QUANTAPDF_OK || pages!=3)goto fail;
        status=quantapdf_load_page(document,2,&page);
        if(status!=QUANTAPDF_OK || page==NULL)goto fail;
        {
            quantapdf_rect bounds={0};
            status=quantapdf_page_bounds(page,&bounds);
            if(status!=QUANTAPDF_OK || bounds.x0!=0.0f ||
               bounds.y0!=0.0f ||
               bounds.x1!=(source_turn?12.25f:16.5f) ||
               bounds.y1!=(source_turn?16.5f:12.25f))goto fail;
        }
        const int dpis[3]={72,96,144};
        for(size_t k=0u;k<3u;++k) {
            quantapdf_render_options opts={0};
            opts.struct_size=sizeof(opts);
            opts.dpi=(float)dpis[k];
            status=quantapdf_render_page_with_options(
                page,&opts,&device_bitmap);
            if(status!=QUANTAPDF_OK || device_bitmap==NULL)goto fail;
            const int wide=dpis[k]==72?17:dpis[k]==96?22:33;
            const int tall=dpis[k]==72?13:dpis[k]==96?17:25;
            status=quantapdf_bitmap_dimensions(
                device_bitmap,&w,&h,&stride,&channels);
            if(status!=QUANTAPDF_OK ||
               w!=(source_turn?tall:wide) ||
               h!=(source_turn?wide:tall) ||
               stride!=w*3 || channels!=3)goto fail;
            status=quantapdf_bitmap_data(
                device_bitmap,&pixels,&size);
            if(status!=QUANTAPDF_OK || pixels==NULL ||
               size!=(size_t)stride*(size_t)h)goto fail;
            const unsigned char *inside=pixels+4u*(size_t)stride+4u*3u;
            if(inside[0]!=255u || inside[1]!=0u || inside[2]!=0u)
                goto fail;
            if(dpis[k]==72) {
                const size_t rx=source_turn?12u:16u;
                const size_t ry=source_turn?16u:12u;
                const unsigned char *edge_x=pixels+
                    4u*(size_t)stride+rx*3u;
                const unsigned char *edge_y=pixels+
                    ry*(size_t)stride+4u*3u;
                const unsigned char *corner=pixels+
                    ry*(size_t)stride+rx*3u;
                if(edge_x[0]!=255u || edge_y[0]!=255u ||
                   corner[0]!=255u ||
                   edge_x[1]!=(source_turn?191u:127u) ||
                   edge_y[1]!=(source_turn?127u:191u) ||
                   corner[1]!=223u ||
                   edge_x[2]!=edge_x[1] ||
                   edge_y[2]!=edge_y[1] ||
                   corner[2]!=223u)goto fail;
            }
            if(dpis[k]==96) {
                const size_t rx=source_turn?16u:21u;
                const size_t ry=source_turn?21u:16u;
                const unsigned char *right=pixels+
                    4u*(size_t)stride+rx*3u;
                const unsigned char *bottom=pixels+
                    ry*(size_t)stride+4u*3u;
                if(right[1]!=(source_turn?191u:0u) ||
                   bottom[1]!=(source_turn?0u:191u))goto fail;
            }
            if(dpis[k]==144) {
                const size_t rx=source_turn?24u:32u;
                const size_t ry=source_turn?32u:24u;
                const unsigned char *right=pixels+
                    4u*(size_t)stride+rx*3u;
                const unsigned char *bottom=pixels+
                    ry*(size_t)stride+4u*3u;
                if(right[1]!=(source_turn?127u:0u) ||
                   bottom[1]!=(source_turn?0u:127u))goto fail;
            }
            quantapdf_drop_bitmap(device_bitmap);device_bitmap=NULL;
        }
        status=quantapdf_render_page(page,&bitmap);
        if(status!=QUANTAPDF_OK || bitmap==NULL)goto fail;
        quantapdf_drop_page(page);page=NULL;
        quantapdf_close(document);document=NULL;
        quantapdf_drop_context(context);context=NULL;
        status=quantapdf_bitmap_dimensions(
            bitmap,&w,&h,&stride,&channels);
        if(status!=QUANTAPDF_OK ||
           w!=(source_turn?13:17) || h!=(source_turn?17:13) ||
           stride!=w*3 || channels!=3)goto fail;
        status=quantapdf_bitmap_data(bitmap,&pixels,&size);
        if(status!=QUANTAPDF_OK || pixels==NULL ||
           size!=(size_t)stride*(size_t)h)goto fail;
        if(pixels[4u*(size_t)stride+4u*3u+1u]!=0u)goto fail;
        quantapdf_drop_bitmap(bitmap);bitmap=NULL;
    }
    result=EXIT_SUCCESS;
    goto done;
fail:
    fprintf(stderr,"installed C11 consumer: status %d (%s)\n",
        (int)status,quantapdf_status_string(status));
done:
    quantapdf_drop_bitmap(device_bitmap);
    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_page(page);
    quantapdf_close(document);
    quantapdf_drop_context(context);
    remove(filename);
    return result;
}
