#include <quantapdf/quantapdf.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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
    long offsets[6]={0,0,0,0,0,0};
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
        const char *body=(shifted_origin && i==5) ?
            "<< /Type /Page /Parent 2 0 R /MediaBox [-9 5 -4 12]"
            " /Resources << >> >>" : object_text[i];
        if(offsets[i]<0 ||
           fprintf(file,"%d 0 obj\n%s\nendobj\n",
                   i,body)<0)goto done;
    }
    xref=ftell(file);
    if(xref<0 || fputs(
        "xref\n0 6\n0000000000 65535 f \n",file)==EOF)goto done;
    for(int i=1;i<=5;++i)
        if(fprintf(file,"%010ld 00000 n \n",offsets[i])<0)
            goto done;
    if(fprintf(file,
        "trailer\n<< /Size 6 /Root 1 0 R >>\n"
        "startxref\n%ld\n%%%%EOF\n",xref)<0)goto done;
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
