/* #269 test-owned binary-mask boundary proximity analysis only.
 * This never changes PDFium/original source or the native Raster algorithm.
 * A 3x3/5x5 neighborhood finds transitions in the ORIGINAL PDFium Fill-only
 * and W-only coverages. It does NOT invent anti-aliasing pixels.
 */
static bool qp269_near_primitive_mask_transition(
    const std::vector<unsigned char>& mask,
    size_t width,size_t height,size_t x,size_t y,int radius)
{
    CHECK(radius==1 || radius==2);
    CHECK(width>0u && height>0u && x<width && y<height);
    CHECK(mask.size()==width*height);
    const unsigned char center=mask[y*width+x];
    for(int dy=-radius;dy<=radius;++dy)
        for(int dx=-radius;dx<=radius;++dx) {
            if(dx==0 && dy==0) continue;
            const long long xx=(long long)x+(long long)dx;
            const long long yy=(long long)y+(long long)dy;
            if(xx<0 || yy<0 || xx>=(long long)width ||
               yy>=(long long)height)
                continue;
            if(mask[(size_t)yy*width+(size_t)xx]!=center)
                return true;
        }
    return false;
}
