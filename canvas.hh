#ifndef bqtTileTrackerCanvasHH
#define bqtTileTrackerCanvasHH

#include <map>
#include <vector>
#include <cstring> // std::memcmp
#include <string>
#include <cstdio>

#include "pixel.hh"
#include "vectype.hh"
#include "alloc/FSBAllocator.hh"
#include "palette.hh"

extern "C" {
//#include <gd.h>
typedef struct gdImageStruct* gdImagePtr;
}

extern int SaveGif;
extern bool UseDitherCache;
extern std::string OutputNameTemplate;

struct AlignResult;

class dither_cache_t;
class transform_cache_t;
class transform_caches_t;

class TILE_Tracker
{
    int org_x, org_y;

    int xmin,ymin;
    int xmax,ymax;

    typedef UncertainPixelVector256x256 vectype;

    struct cubetype
    {
        mutable bool changed;
        vectype pixels;
    };

    typedef std::map<int,cubetype, std::less<int>, FSBAllocator<int> > xmaptype;
    typedef std::map<int,xmaptype, std::less<int>, FSBAllocator<int> > ymaptype;
    ymaptype screens;

    VecType<uint32> LastScreen;  // For ChangeLog
    std::string LastFilename;    // For ChangeLog
    unsigned SequenceBegin;
    unsigned CurrentTimer;

    Palette CurrentPalette;
    std::vector<unsigned> DitheringMatrix;
    std::vector<unsigned> TemporalMatrix;

public:
    TILE_Tracker() : LastFilename(), SequenceBegin(0), CurrentTimer(0)
    {
        Reset();
    }

    ~TILE_Tracker()
    {
    }

    void Save(unsigned method = ~0u);

    void SaveFrame(PixelMethod method, unsigned timer, unsigned imgcounter);

    typedef std::pair<void*,int> ImgResult;

    template<bool TransformColors>
    gdImagePtr CreateFrame_TrueColor(
        const VecType<uint32>& screen,
        unsigned frameno, unsigned wid, unsigned hei);

    template<bool TransformColors>
    gdImagePtr CreateFrame_Palette_Auto(
        const VecType<uint32>& screen,
        unsigned frameno, unsigned wid, unsigned hei);

    template<bool TransformColors, bool UseErrorDiffusion>
    gdImagePtr CreateFrame_Palette_Dither(
        const VecType<uint32>& screen,
        unsigned frameno, unsigned wid, unsigned hei);

    template<bool TransformColors, bool UseErrorDiffusion>
    gdImagePtr CreateFrame_Palette_Dither_CGA16(
        const VecType<uint32>& screen,
        unsigned frameno, unsigned wid, unsigned hei);

    template<bool TransformColors, bool UseErrorDiffusion>
    gdImagePtr CreateFrame_Palette_Dither_With(
        const VecType<uint32>& screen,
        unsigned frameno, unsigned wid, unsigned hei,
        const Palette& pal);

    void CreatePalette(PixelMethod method, unsigned nframes);

    template<bool TransformColors>
    struct HistogramType CountColors(PixelMethod method, unsigned nframes);

    void Reset();

    const VecType<uint32> LoadScreen(int ox,int oy, unsigned sx,unsigned sy,
                                     unsigned timer,
                                     PixelMethod method) const;
    const VecType<uint32> LoadBackground(int ox,int oy, unsigned sx,unsigned sy) const;

    void PutScreen(const uint32*const input, int ox,int oy, unsigned sx,unsigned sy,
                   unsigned timer);

    void FitScreenAutomatic(const uint32* input, unsigned sx,unsigned sy);

    AlignResult TryAlignWithHotspots(
        const uint32* input, unsigned sx,unsigned sy) const;
    AlignResult TryAlignWithPrevFrame(
        const uint32* prev_input,
        const uint32* input, unsigned sx,unsigned sy) const;
    AlignResult TryAlignWithBackground(
        const uint32* input, unsigned sx,unsigned sy) const;

    void FitScreen(const uint32* input, unsigned sx,unsigned sy,
                   const AlignResult& alignment,
                   int extra_offs_x=0,
                   int extra_offs_y=0
                  );

    void NextFrame();

    bool IsHeavyDithering(bool animated) const;

    template<bool TransformColors>
    inline unsigned GetMixColor(dither_cache_t& cache,
                                transform_caches_t& transform_cache,
                                unsigned wid,unsigned hei, unsigned frameno,
                                unsigned x,unsigned y, uint32 pix,
                                const Palette& pal);
};

extern std::string transform_common;
extern std::string transform_r;
extern std::string transform_g;
extern std::string transform_b;
void SetColorTransformations();
extern int pad_top, pad_bottom, pad_left, pad_right;

#endif
