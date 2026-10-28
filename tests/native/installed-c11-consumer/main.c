#include <quantapdf/quantapdf.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int write_blank_pdf(const char *path)
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
    if(offsets[2]<0 || fputs(
        "2 0 obj\n"
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>\n"
        "endobj\n",file)==EOF)goto done;
    offsets[3]=ftell(file);
    if(offsets[3]<0 || fputs(
        "3 0 obj\n"
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 8 8]"
        " /Resources << >> >>\n"
        "endobj\n",file)==EOF)goto done;
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

    if(!write_blank_pdf(filename)) {
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
