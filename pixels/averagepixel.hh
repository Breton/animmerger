class AveragePixel
{
    unsigned r,g,b;
    unsigned n;
    unsigned pix;
public:
    AveragePixel() : r(0),g(0),b(0),n(0), pix(DefaultPixel)
    {
    }
    void set(unsigned R,unsigned G,unsigned B)
    {
        r+=R; g+=G; b+=B;
        ++n;
        
        pix = (((r/n) << 16) + ((g/n) << 8) + (b/n));
    }
    operator uint32() const { return pix; }
    void Compress()
    {
    }
};