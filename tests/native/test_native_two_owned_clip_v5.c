/* Private two-owned-Clip C11 V5 Image + PATH admission (#530).
 * No qpdf, PDFium, second raster, Source route or public ABI. */
#include "render/ir_raster_scoped_mixed_v5.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { \
  fprintf(stderr,"two owned V5 Clip:%d: %s\n",__LINE__,#x); \
  return EXIT_FAILURE; } } while(0)
#define OK(x) CHECK((x)==QUANTAPDF_OK)

typedef struct fixture {
  quantapdf_ir ir;
  quantapdf_composer_page_state page;
  quantapdf_composer_operation ops[3];
  quantapdf_composer_image_state images[1];
  quantapdf_composer_graphics_state gs;
  quantapdf_composer_clip_state clips[3];
  quantapdf_composer_clip_id members[2];
  quantapdf_composer_path_command shapes[2][5],blue[5],green[5];
  unsigned char red[3];
} fixture;
static const quantapdf_ir_raster_scoped_mixed_v5_limits budget={
  64u,4u,512u,8000000u,4u,128u
};
static void point(quantapdf_composer_path_command *p,
                  quantapdf_composer_path_command_kind k,float x,float y)
{
  memset(p,0,sizeof(*p)); p->kind=k; p->point1=(quantapdf_point){x,y};
}
static void rectangle(quantapdf_composer_path_command p[5],
                      float a,float b,float c,float d)
{
  point(&p[0],QUANTAPDF_COMPOSER_PATH_MOVE_TO,a,b);
  point(&p[1],QUANTAPDF_COMPOSER_PATH_LINE_TO,c,b);
  point(&p[2],QUANTAPDF_COMPOSER_PATH_LINE_TO,c,d);
  point(&p[3],QUANTAPDF_COMPOSER_PATH_LINE_TO,a,d);
  point(&p[4],QUANTAPDF_COMPOSER_PATH_CLOSE,0,0);
}
static void curve(quantapdf_composer_path_command p[5],float x0,float x1)
{
  point(&p[0],QUANTAPDF_COMPOSER_PATH_MOVE_TO,x0,5);
  point(&p[1],QUANTAPDF_COMPOSER_PATH_CUBIC_TO,x0,1);
  p[1].point2=(quantapdf_point){x1,1};
  p[1].point3=(quantapdf_point){x1,5};
  point(&p[2],QUANTAPDF_COMPOSER_PATH_LINE_TO,x1,7);
  point(&p[3],QUANTAPDF_COMPOSER_PATH_LINE_TO,x0,7);
  point(&p[4],QUANTAPDF_COMPOSER_PATH_CLOSE,0,0);
}
static void setup(fixture *f)
{
  memset(f,0,sizeof(*f));
  f->page.width_points=f->page.height_points=8.0f;
  f->page.background_argb=UINT32_C(0xffffffff);
  f->ir.pages=&f->page; f->ir.page_count=1u;
  f->ir.operations=f->ops; f->ir.operation_count=3u;
  f->ir.resources.images=f->images;
  f->ir.resources.image_count=f->ir.resources.image_capacity=1u;
  f->ir.resources.graphics_states=&f->gs;
  f->ir.resources.graphics_state_count=
      f->ir.resources.graphics_state_capacity=1u;
  f->ir.resources.clips=f->clips;
  f->ir.resources.clip_count=f->ir.resources.clip_capacity=3u;
  f->red[0]=255u;
  f->images[0].format=QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW;
  f->images[0].data=f->red; f->images[0].size=3u;
  f->images[0].width=f->images[0].height=1u;
  f->images[0].components=3u;
  f->gs.fill_alpha=f->gs.stroke_alpha=1.0f;
  f->gs.blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;
  f->gs.clip_id=3u;
  curve(f->shapes[0],1,7); curve(f->shapes[1],3,8);
  for(size_t i=0;i<2u;++i) {
    f->clips[i].kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
    f->clips[i].fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    f->clips[i].commands=f->shapes[i];
    f->clips[i].command_count=5u;
    f->clips[i].transform=(quantapdf_affine_transform){1,0,0,1,0,0};
  }
  f->clips[2].kind=QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;
  f->clips[2].members=f->members; f->clips[2].member_count=2u;
  f->members[0]=1u;f->members[1]=2u;
  f->ops[0].kind=QUANTAPDF_COMPOSER_OPERATION_IMAGE;
  f->ops[0].graphics_state_id=1u;
  f->ops[0].value.image.image_id=1u;
  f->ops[0].value.image.options.struct_size=
      QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_SIZE;
  f->ops[0].value.image.options.fit=QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
  f->ops[0].value.image.options.graphics_state_id=1u;
  f->ops[0].bounds=(quantapdf_rect){0,0,8,8};
  f->ops[1].kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
  f->ops[1].graphics_state_id=1u;
  f->ops[1].value.path.commands=f->blue;
  f->ops[1].value.path.command_count=5u;
  f->ops[1].value.path.options.struct_size=
      QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
  f->ops[1].value.path.options.fill=1;
  f->ops[1].value.path.options.fill_argb=UINT32_C(0xff0000ff);
  f->ops[1].value.path.options.graphics_state_id=1u;
  rectangle(f->blue,0,0,5,8);
  f->ops[2].kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
  f->ops[2].value.path.commands=f->green;
  f->ops[2].value.path.command_count=5u;
  f->ops[2].value.path.options.struct_size=
      QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
  f->ops[2].value.path.options.fill=1;
  f->ops[2].value.path.options.fill_argb=UINT32_C(0xff00ff00);
  rectangle(f->green,0,0,8,1); /* Q-restored GS #0 */
}
static uint32_t rgba(const quantapdf_ir_raster_image *im,size_t x,size_t y)
{
  const unsigned char *p=im->pixels+y*im->stride+x*4u;
  return ((uint32_t)p[0]<<24u)|((uint32_t)p[1]<<16u)|
         ((uint32_t)p[2]<<8u)|p[3];
}
static int reset(const quantapdf_ir_raster_image *im)
{
  return im->pixels==NULL && im->size==0u &&
      im->width==0u && im->height==0u && im->stride==0u;
}
static int equal(const quantapdf_ir_raster_image *a,
                 const quantapdf_ir_raster_image *b)
{
  return a->pixels!=NULL && b->pixels!=NULL &&
      a->size==b->size && a->width==b->width &&
      a->height==b->height && a->stride==b->stride &&
      memcmp(a->pixels,b->pixels,a->size)==0;
}
static quantapdf_status draw(const fixture *f,
  const quantapdf_ir_raster_scoped_mixed_v5_limits *limit,
  double w,double h,quantapdf_ir_raster_image *out)
{
  return quantapdf_ir_raster_scoped_mixed_v5_two_clip_device_viewport(
      &f->ir,0u,limit,w,h,out);
}
static int ordered_mixed_and_borrow(void)
{
  fixture f,saved;
  quantapdf_ir_raster_image a={0},b={0};
  setup(&f);saved=f;
  OK(draw(&f,&budget,8,8,&a));
  CHECK(a.width==8u && a.height==8u && a.stride==32u && a.size==256u);
  CHECK(rgba(&a,4,6)==UINT32_C(0x0000ffff)); /* clipped blue */
  CHECK(rgba(&a,5,6)==UINT32_C(0xff0000ff)); /* clipped RAW red */
  CHECK(rgba(&a,0,6)==UINT32_C(0xffffffff));
  CHECK(rgba(&a,7,6)==UINT32_C(0xffffffff));
  CHECK(rgba(&a,0,0)==UINT32_C(0x00ff00ff)); /* Q restores visible green */
  for(int rule0=0;rule0<2;++rule0)
  for(int rule1=0;rule1<2;++rule1) {
    f.clips[0].fill_rule=rule0?
        QUANTAPDF_COMPOSER_FILL_EVEN_ODD:
        QUANTAPDF_COMPOSER_FILL_NONZERO;
    f.clips[1].fill_rule=rule1?
        QUANTAPDF_COMPOSER_FILL_EVEN_ODD:
        QUANTAPDF_COMPOSER_FILL_NONZERO;
    OK(draw(&f,&budget,8,8,&b));
    CHECK(equal(&a,&b)); quantapdf_ir_raster_image_dispose(&b);
    f.members[0]=2u;f.members[1]=1u;
    OK(draw(&f,&budget,8,8,&b));
    CHECK(equal(&a,&b)); quantapdf_ir_raster_image_dispose(&b);
    f.members[0]=1u;f.members[1]=2u;
  }
  f.clips[0].fill_rule=f.clips[1].fill_rule=
      QUANTAPDF_COMPOSER_FILL_NONZERO;
  CHECK(memcmp(&f,&saved,sizeof(f))==0); /* IR borrowed read-only */
  f.red[0]=0u; f.shapes[1][1].point2.x=1000.0f;
  CHECK(rgba(&a,5,6)==UINT32_C(0xff0000ff)); /* detached output */
  quantapdf_ir_raster_image_dispose(&a);
  CHECK(reset(&a));
  return EXIT_SUCCESS;
}
static int image_only_empty_and_viewport(void)
{
  fixture f;
  quantapdf_ir_raster_image a={0},b={0};
  setup(&f);f.ir.operation_count=1u;
  OK(draw(&f,&budget,8,8,&a));
  CHECK(rgba(&a,4,6)==UINT32_C(0xff0000ff));
  CHECK(rgba(&a,0,6)==UINT32_C(0xffffffff));
  quantapdf_ir_raster_image_dispose(&a);
  f.clips[1].command_count=1u; /* a valid zero-edge MOVE-only clip */
  OK(draw(&f,&budget,8,8,&a));
  for(size_t y=0;y<8u;++y)
    for(size_t x=0;x<8u;++x)
      CHECK(rgba(&a,x,y)==UINT32_C(0xffffffff));
  quantapdf_ir_raster_image_dispose(&a);
  f.clips[1].command_count=5u;f.clips[1].transform.e=20.0f;
  OK(draw(&f,&budget,8,8,&a)); /* valid disjoint Clip */
  for(size_t y=0;y<8u;++y)
    for(size_t x=0;x<8u;++x)
      CHECK(rgba(&a,x,y)==UINT32_C(0xffffffff));
  quantapdf_ir_raster_image_dispose(&a);
  setup(&f);
  f.ops[0].bounds=(quantapdf_rect){0.5f,0.5f,7.5f,7.5f};
  OK(draw(&f,&budget,7.5,8.0,&a));
  OK(draw(&f,&budget,7.5,8.0,&b));
  CHECK(equal(&a,&b));
  CHECK(rgba(&a,0,0)==UINT32_C(0x00ff00ff));
  CHECK(rgba(&a,0,6)==UINT32_C(0xffffffff));
  quantapdf_ir_raster_image_dispose(&a);
  quantapdf_ir_raster_image_dispose(&b);
  return EXIT_SUCCESS;
}
static int refused(fixture *f,
  const quantapdf_ir_raster_scoped_mixed_v5_limits *limit,int legacy)
{
  quantapdf_ir_raster_image out={0};
  out.pixels=(unsigned char *)(uintptr_t)1u;
  out.size=out.width=out.height=out.stride=SIZE_MAX;
  const quantapdf_status status=legacy?
      quantapdf_ir_raster_scoped_mixed_v5_device_viewport(
          &f->ir,0u,limit,8,8,&out) :
      draw(f,limit,8,8,&out);
  CHECK(status!=QUANTAPDF_OK && reset(&out));
  return EXIT_SUCCESS;
}
static int malformed_and_budgets(void)
{
  fixture f;
  quantapdf_ir_raster_scoped_mixed_v5_limits limit=budget;
  setup(&f); CHECK(refused(&f,&budget,1)==EXIT_SUCCESS);
  limit.max_edges=3u;
  CHECK(refused(&f,&limit,0)==EXIT_SUCCESS);
  limit=budget;limit.max_sample_tests=100u;
  CHECK(refused(&f,&limit,0)==EXIT_SUCCESS);
  limit=budget;limit.max_drawn_pixels=3u;
  CHECK(refused(&f,&limit,0)==EXIT_SUCCESS);
  limit=budget;limit.max_source_pixels=0u;
  CHECK(refused(&f,&limit,0)==EXIT_SUCCESS);
  setup(&f);f.gs.clip_id=1u;
  CHECK(refused(&f,&budget,0)==EXIT_SUCCESS);
  setup(&f);f.members[1]=1u;
  CHECK(refused(&f,&budget,0)==EXIT_SUCCESS);
  setup(&f);f.members[1]=3u;
  CHECK(refused(&f,&budget,0)==EXIT_SUCCESS);
  setup(&f);f.clips[2].members=NULL;
  CHECK(refused(&f,&budget,0)==EXIT_SUCCESS);
  setup(&f);f.clips[2].member_count=1u;
  CHECK(refused(&f,&budget,0)==EXIT_SUCCESS);
  setup(&f);f.clips[1].commands=NULL;
  CHECK(refused(&f,&budget,0)==EXIT_SUCCESS);
  setup(&f);f.clips[0].command_count=10u;
  CHECK(refused(&f,&budget,0)==EXIT_SUCCESS);
  setup(&f);f.clips[1].transform.a=NAN;
  CHECK(refused(&f,&budget,0)==EXIT_SUCCESS);
  setup(&f);f.ir.resources.clip_capacity=4u;
  CHECK(refused(&f,&budget,0)==EXIT_SUCCESS);
  setup(&f);f.ir.resources.graphics_state_capacity=2u;
  CHECK(refused(&f,&budget,0)==EXIT_SUCCESS);
  setup(&f);f.ops[2].kind=QUANTAPDF_COMPOSER_OPERATION_TEXT;
  CHECK(refused(&f,&budget,0)==EXIT_SUCCESS);
  setup(&f);f.ops[1].graphics_state_id=0u;
  CHECK(refused(&f,&budget,0)==EXIT_SUCCESS);
  return EXIT_SUCCESS;
}
int main(void)
{
  CHECK(ordered_mixed_and_borrow()==EXIT_SUCCESS);
  CHECK(image_only_empty_and_viewport()==EXIT_SUCCESS);
  CHECK(malformed_and_budgets()==EXIT_SUCCESS);
  fprintf(stderr,"[native.two_owned_clip_v5] completed\n");
  return EXIT_SUCCESS;
}
