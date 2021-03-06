#include "dither.hh"
#include "palette.hh"
#include "pixel.hh" // for verbose
#include "maptype.hh"

#include <algorithm>
#include <cstdio>
#include <map>

double DitherErrorFactor    = 1.0;
unsigned DitherMatrixWidth  = 8;
unsigned DitherMatrixHeight = 8;
unsigned TemporalDitherSize = 1; // 1 = no temporal dithering
bool     TemporalDitherMSB  = false;  // Use MSB rather than LSB for temporal dithering
unsigned DitherColorListSize = 0;
double   DitherCombinationContrast       = -1.0;
unsigned DitherCombinationRecursionLimit = 0;
unsigned DitherCombinationChangesLimit   = 0;
bool     DitherCombinationAllowSame      = false;
DitheringMethod Dithering = Dither_Yliluoma1Iterative;
DiffusionMethod Diffusion = Diffusion_None;

std::vector<unsigned> CreatePowerofTwoDitheringMatrix(unsigned Width, unsigned Height)
{
    // Find M=ceil(log2(x)) and L=ceil(log2(y))
    unsigned M=0; while( Width  > (1u << M)) ++M;
    unsigned L=0; while( Height > (1u << L)) ++L;
    unsigned RoundedWidth  = 1 << M;
    unsigned RoundedHeight = 1 << L;
    std::vector<unsigned> result;
    result.reserve(Width * Height);

    for(unsigned y=0; y<Height; ++y)
        for(unsigned x=0; x<Width; ++x)
        {
            unsigned v = 0, offset = 0, xmask = M, ymask = L;
            if(M==0 || (M > L && L != 0))
            {
                unsigned xc = x ^ (y<<M>>L), yc = y;
                for(unsigned bit=0; bit < M+L; )
                {
                    v |= ((yc >> --ymask)&1) << bit++;
                    for(offset += M; offset >= L; offset -= L)
                        v |= ((xc >> --xmask)&1) << bit++;
                }
            }
            else
            {
                unsigned xc = x, yc = y ^ (x<<L>>M);
                for(unsigned bit=0; bit < M+L; )
                {
                    v |= ((xc >> --xmask)&1) << bit++;
                    for(offset += L; offset >= M; offset -= M)
                        v |= ((yc >> --ymask)&1) << bit++;
                }
            }
            result.push_back(v);
        }

    if(Height != RoundedHeight
    || Width  != RoundedWidth)
    {
        // If the user requested e.g. 3x3 and we used 4x4 algorithm,
        // convert the numbers so that the resulting range is ungapped:
        //
        // We generated:  We saved:   We compress the number range:
        //  0 12  3 15     0 12  3     0 7 3
        //  8  4 11  7     8  4 11     5 4 6
        //  2 14  1 13     2 14  1     2 8 1
        // 10  6  9  5
        unsigned max_value   = RoundedWidth * RoundedHeight;
        unsigned matrix_size = Height * Width;
        unsigned n_missing = 0;
        for(unsigned v=0; v<max_value; ++v)
        {
            bool found = false;
            for(unsigned a=0; a<matrix_size; ++a)
            {
                if(result[a] == v)
                {
                    result[a] -= n_missing;
                    found = true;
                    break;
                }
            }
            if(!found)
                ++n_missing;
        }
    }
    return result;
}

std::vector<unsigned> CreateDispersedDitheringMatrix()
{
    return CreatePowerofTwoDitheringMatrix(DitherMatrixWidth, DitherMatrixHeight);
}

std::vector<unsigned> CreateTemporalDitheringMatrix()
{
    return CreatePowerofTwoDitheringMatrix(1, TemporalDitherSize);
}

/////////////////

struct ComparePaletteLuma
{
    const Palette& pal;

    ComparePaletteLuma(const Palette& p): pal(p) { }

    bool operator() (unsigned a, unsigned b) const
        { return pal.GetLuma(a) < pal.GetLuma(b); }
};

namespace
{
    MixingPlan ResequenceMixingPlan(
        std::map<unsigned,unsigned, std::less<unsigned>, FSBAllocator<int>
                >& Solution,
        const Palette& pal)
    {
    #if 0 /* HACK for Mario Paint: Find largest common divisor and divide the sections as such. */
        for(unsigned divisor = DitherColorListSize; divisor > 0; --divisor)
        {
            bool even_divisor = true;
            for(std::map<unsigned,unsigned>::iterator
                i = Solution.begin(); i != Solution.end(); ++i)
            {
                if(i->second % divisor != 0) { even_divisor = false; break; }
            }
            if(even_divisor)
            {
                for(std::map<unsigned,unsigned>::iterator
                    i = Solution.begin(); i != Solution.end(); ++i)
                {
                    i->second /= divisor;
                }
                break;
            }
        }
    #endif

        // Sequence the solution.
        MixingPlan result;
        for(std::map<unsigned,unsigned>::iterator
            i = Solution.begin(); i != Solution.end(); ++i)
        {
            result.resize(result.size() + i->second, i->first);
        }
        // Sort the colors according to luminance
        std::sort(result.begin(), result.end(), ComparePaletteLuma(pal));
        std::reverse(result.begin(), result.end());
        return result;
    }

    ////////////
    /* Algorithm 1: Find best-matching combination.
     *              Single-shot, extremely fast, especially if KD-tree is used.
     * Algorithm 1b: Iteratively add combinations that counter the accumulated error.
     *               Fast, especially if KD-tree is used.
     * KD-Tree can be used. Because of that, this algorithm is fast.
     */
    template<bool Iterative>
    MixingPlan FindBestMixingPlan_Yliluoma1
        (const ColorInfo& input,
         const Palette& pal)
    {
        unsigned chosen = pal.FindClosestCombinationIndex(input).first;
        MixingPlan result(
            pal.Combinations[chosen].indexlist.begin(),
            pal.Combinations[chosen].indexlist.end() );

        if(Iterative)
        {
            ColorInfo goal( input );
            while(result.size() < DitherColorListSize)
            {
                // Adjust the goal according to error
                GammaColorVec refined =
                    goal.gammac +
                        (input.gammac - pal.Combinations[chosen].combination.gammac)
                       * DitherErrorFactor;
                refined.ClampTo0and1();
                goal = refined;

                chosen = pal.FindClosestCombinationIndex(goal).first;
                result.insert(result.end(),
                    pal.Combinations[chosen].indexlist.begin(),
                    pal.Combinations[chosen].indexlist.end() );
            }
            //std::sort(result.begin(), result.end(), ComparePaletteLuma(pal));
            std::map<unsigned,unsigned, std::less<unsigned>, FSBAllocator<int> > Solution;
            for(size_t a=0; a<result.size(); ++a)
                Solution[ result[a] ] += 1;
            return ResequenceMixingPlan(Solution, pal);
        }
        return result;
    }

    ////////////
    /* Algorithm 2: Find best-matching combination.
     *              Iteratively add combinations that improve the result.
     *              This algorithm has the best quality, but it is also slowest.
     *              It is not perfect, because the initial guess may be wrong.
     * KD-Tree cannot be used.
     *
     * Each each step, we have:
     *
     *    so_far           = sum of colors added so far
     *    proportion_total = so_far/proportion_total = average perceived color
     *
     * Find a combination to be added, such that
     *
     *    (so_far + combination.gammac * combination.size * repeatcount)
     *    --------------------------------------------------------------
     *         (proportion_total + combination.size * repeatcount)
     *
     *   best matches input.
     *   After decision is found, add
     *        combination.gammac * combination.size * repeatcount
     *   into so_far. Repeat until enough combinations have been added.
     */
    MixingPlan FindBestMixingPlan_Yliluoma2
        (const ColorInfo& input,
         const Palette& pal)
    {
        const unsigned NumCombinations = pal.NumCombinations();

        size_t GenerationLimit = DitherColorListSize;
        assert(GenerationLimit > 0);

        MixingPlan result;
        GammaColorVec so_far(0.0);

        while(result.size() < GenerationLimit)
        {
            unsigned chosen_amount = 1;
            unsigned chosen        = 0;

            double least_penalty = -1;

            unsigned proportion_total = result.size();
            for(unsigned i=0; i<NumCombinations; ++i)
            {
                unsigned count = pal.Combinations[i].indexlist.size();
                unsigned max_test_count_comb =
                    count==1
                        ? (proportion_total ? proportion_total : 1)
                        : std::min(proportion_total,
                                   (unsigned)GenerationLimit-proportion_total-count);

                if(proportion_total + max_test_count_comb*count > GenerationLimit)
                    max_test_count_comb = (GenerationLimit - proportion_total) / count;

                GammaColorVec add = pal.Combinations[i].combination.gammac * double(count);
                for(unsigned p=1; p<=max_test_count_comb; p+=p, add+=add)
                {
                    double t = 1.0 / (proportion_total + p*count);
                    double penalty = ColorCompare(input, ColorInfo( (so_far+add) * t) );
                    if(penalty < least_penalty || least_penalty < 0)
                        { least_penalty = penalty;
                          chosen = i;
                          chosen_amount = p; }
                }
            }

            unsigned count = pal.Combinations[chosen].indexlist.size();
            for(unsigned i=0; i<count; ++i)
                result.resize(result.size() + chosen_amount,
                              pal.Combinations[chosen].indexlist[i]);

            so_far += pal.Combinations[chosen].combination.gammac * double(chosen_amount*count);
        }

        std::map<unsigned,unsigned, std::less<unsigned>, FSBAllocator<int> > Solution;
        for(size_t a=0; a<result.size(); ++a)
            Solution[ result[a] ] += 1;

        return ResequenceMixingPlan(Solution, pal);
        /*std::sort(result.begin(), result.end(), ComparePaletteLuma(pal));
        return result;*/
    }

    ////////////
    /* Algorithm 3: Find best-matching combination.
     *              Recursively split one color into
     *              two equal portions (of different colors),
     *              if said split improves the result.
     * KD-Tree cannot be used, except for the initial combination.
     */
    MixingPlan FindBestMixingPlan_Yliluoma3
        (const ColorInfo& input,
         const Palette& pal)
    {
        const unsigned NumCombinations = pal.NumCombinations();

        std::map<
            unsigned, unsigned,
            std::less<unsigned>, FSBAllocator<int>
        > Solution;

        // First, find the closest color to the input color.
        if(1)
        {
            unsigned chosen = pal.FindClosestCombinationIndex(input).first;
            unsigned count = pal.Combinations[chosen].indexlist.size();
            unsigned proportion = 0;
            for(unsigned i=0; i<count; ++i)
            {
                unsigned index = pal.Combinations[chosen].indexlist[i];
                proportion += DitherColorListSize;
                unsigned ThisProportion = proportion / count;
                proportion %= count;
                if(ThisProportion > 0)
                    Solution[index] = ThisProportion;
            }
        }

        // The penalty of our currently "best" solution.
        double current_penalty = -1;
        double dbllimit = 1.0 / DitherColorListSize;
        while(current_penalty > 0.0)
        {
            // Find out if there is a region in Solution that
            // can be split in two for benefit.
            double   best_penalty      = current_penalty;
            unsigned best_splitfrom    = ~0u;
            unsigned best_split_to[2]  = { 0,0};

            //printf("pen=%g, cur=%g\n", best_penalty, current_penalty);

            GammaColorVec current_sum(0.0);
            for(std::map<unsigned,unsigned>::iterator
                j = Solution.begin();
                j != Solution.end();
                ++j)
            {
                current_sum += pal.Data[ j->first ].gammac * double(j->second * dbllimit);
            }

            for(std::map<unsigned,unsigned>::iterator
                i = Solution.begin();
                i != Solution.end();
                ++i)
            {
                //if(i->second <= 1) continue;
                unsigned split_color = i->first;
                unsigned split_count = i->second;
                double portion_total = split_count * dbllimit;

                GammaColorVec sum = current_sum - pal.Data[split_color].gammac * portion_total;

                if(true)
                {
                    double portion1 = (split_count / 2            ) * dbllimit;
                    double portion2 = (split_count - split_count/2) * dbllimit;
                    for(unsigned c=0; c<NumCombinations; ++c)
                    {
                        if(pal.Combinations[c].indexlist.size() == 1
                        || (split_count > 1
                         && pal.Combinations[c].indexlist.size() == 2)
                          )
                        {
                            unsigned a = pal.Combinations[c].indexlist.front();
                            unsigned b = pal.Combinations[c].indexlist.back();
                            if(a == split_color || b == split_color) continue;
                            if(portion1 == portion2 || a == b)
                            {
                                GammaColorVec V = pal.Combinations[c].combination.gammac * portion_total;

                                // Figure out if this split is better than what we had
                                double penalty = ColorCompare(input, ColorInfo(sum+V) );

                                if(penalty < best_penalty)
                                {
                                    best_penalty   = penalty;
                                    best_splitfrom = split_color;
                                    best_split_to[0] = a;
                                    best_split_to[1] = b;
                                }
                            }
                            else // portion1 != portion2 (split_count was uneven).
                            {
                              if(1)
                              {
                                GammaColorVec V = pal.Data[a].gammac*portion1 + pal.Data[b].gammac*portion2;

                                // Figure out if this split is better than what we had
                                double penalty = ColorCompare(input, ColorInfo(sum+V) );

                                if(penalty < best_penalty)
                                {
                                    best_penalty   = penalty;
                                    best_splitfrom = split_color;
                                    best_split_to[0] = a;
                                    best_split_to[1] = b;
                                }
                              }
                              if(1)
                              {
                                GammaColorVec V = pal.Data[b].gammac*portion1 + pal.Data[a].gammac*portion2;

                                // Figure out if this split is better than what we had
                                double penalty = ColorCompare(input, ColorInfo(sum+V) );

                                if(penalty < best_penalty)
                                {
                                    best_penalty   = penalty;
                                    best_splitfrom = split_color;
                                    best_split_to[0] = b;
                                    best_split_to[1] = a;
                                }
                              }
                            }
                        } // == 2
                    } // for NumCombinations
                }
            }
            if(best_penalty == current_penalty) break; // No better solution was found.

            std::map<unsigned,unsigned>::iterator i = Solution.find(best_splitfrom);
            unsigned split_count = i->second;
            unsigned split1 = split_count/2;
            unsigned split2 = split_count - split1;
            Solution.erase(i);
            if(split1 > 0) Solution[best_split_to[0]] += split1;
            if(split2 > 0) Solution[best_split_to[1]] += split2;
            current_penalty = best_penalty;
        }

        return ResequenceMixingPlan(Solution, pal);
    }

    ////////////
    struct CombinationMaker
    {
        const std::vector<Palette::PaletteItem>& PaletteData;
        const std::vector<unsigned>& luma_order;
        const int MaxDifference;
    private:
        std::vector<int> PalDifferences;
        std::vector<unsigned> indices;
    public:
        CombinationMaker
            (const std::vector<Palette::PaletteItem>& p,
             const std::vector<unsigned>& lo,
             int md)
            : PaletteData(p),
              luma_order(lo),
              MaxDifference(md),
              PalDifferences( p.size()*p.size(), 0 )
        {
            const unsigned PaletteSize = p.size();
            // Note: a+1 is correct.
            // a==b gives a difference of 0, which is default.
            for(unsigned a=0; a<PaletteSize; ++a)
            for(unsigned b=a+1; b<PaletteSize; ++b)
            {
                PalDifferences[a*PaletteSize+b] =
                PalDifferences[b*PaletteSize+a] =
                    std::abs( p[a].luma - p[b].luma );
            }
        }

        void Make(
            std::vector<Palette::Combination>& Combinations,
            unsigned begin,
            const GammaColorVec& insum,
            size_t RecursionLimit,
            size_t ChangesLimit)
        {
            if(RecursionLimit <= 0) return;

            double mul = 1.0 / (indices.size() + 1);

            const unsigned RecursionLevel = indices.size();
            indices.push_back(0);
            const unsigned PaletteSize = PaletteData.size();

            for(unsigned bb = begin; bb < PaletteSize; ++bb)
            {
                const unsigned b = luma_order[bb];

                bool too_big = false;
                for(unsigned a=0; a<RecursionLevel; ++a)
                    if( PalDifferences[ b*PaletteSize + indices[a] ] > MaxDifference)
                    {
                        too_big = true;
                        break;
                    }
                if(too_big) break; //continue;

                const GammaColorVec nvec = PaletteData[b].gammac; // Gamma corrected R,G,B
                GammaColorVec our_sum = insum + nvec;
                indices.back() = b;

                if(true)
                {
                    GammaColorVec combined = our_sum * mul;
                    uint32 uncorrected_rgb = combined.GetGammaUncorrectedRGB();
                    //^  Gamma uncorrected R,G,B

                    // Do not insert combinations that yield the lone component color
                    if(RecursionLevel == 0
                    || uncorrected_rgb != PaletteData[b].rgb)
                    {
                        if(verbose >= 3)
                        {
                            printf("Candidate %u, %06X(%.4f,%.4f,%.4f): ",
                                unsigned(Combinations.size()),
                                uncorrected_rgb,
                                combined.r,combined.g,combined.b);
                            for(size_t a=0; a<indices.size(); ++a)
                                printf(" %06X", PaletteData[ indices[a] ].rgb);
                            printf("\n");
                        }
                        Combinations.push_back(
                            Palette::Combination(indices, uncorrected_rgb, combined) );
                    }
                }

                size_t WasSame = RecursionLevel == 0 ? 0
                               : b == indices[indices.size()-2];

                unsigned next_begin = bb;
                if(!DitherCombinationAllowSame || ChangesLimit == 0)
                    ++next_begin;

                Make(Combinations, next_begin, our_sum,
                    RecursionLimit - 1,
                    ChangesLimit - WasSame);
            }
            indices.pop_back();
        }
    };
} /* namespace */

/* FindBestMixingPlan:
 *   Task: Find the combination of palette colors that,
 *   when mixed together, the average of them (gamma-corrected)
 *   best resembles the input color (using ColorCompare).
 */
MixingPlan FindBestMixingPlan(const ColorInfo& input, const Palette& pal)
{
    switch(Dithering)
    {
        case Dither_Yliluoma1:
            return FindBestMixingPlan_Yliluoma1<false>(input, pal);
        case Dither_Yliluoma1Iterative:
        default:
            return FindBestMixingPlan_Yliluoma1<true>(input, pal);
        case Dither_Yliluoma2:
            return FindBestMixingPlan_Yliluoma2(input, pal);
        case Dither_Yliluoma3:
            return FindBestMixingPlan_Yliluoma3(input, pal);
    }
}

void Palette::Analyze()
{
    const unsigned PaletteSize = Data.size();
    Combinations.clear();

    if(!PaletteSize) return;

    // Sort palette by luma.
    // Create combinations for all pairs where the difference
    // between the luma of the two is <= the average difference
    // between the luma of two successive palette elements.
    std::vector<unsigned> luma_order(PaletteSize);;
    for(unsigned a=0; a<PaletteSize; ++a) luma_order[a] = a;
    std::sort(luma_order.begin(), luma_order.end(), ComparePaletteLuma(*this));

    int    average_luma_difference = 0;
    int    maximum_luma_difference = 0;
    for(unsigned a=1; a<PaletteSize; ++a)
    {
        int    diff = GetLuma(luma_order[a]) - GetLuma(luma_order[a-1]);
        average_luma_difference += diff;
        if(diff > maximum_luma_difference)
            maximum_luma_difference = diff;
    }
    average_luma_difference /= (PaletteSize - 1);

    int    total_luma = GetLuma(luma_order.back()) - GetLuma(luma_order.front());

    int    luma_threshold = 0;

    double p = DitherCombinationContrast;
    if(p < 0.0 || p > 3.0)
    {
        switch(Dithering)
        {
            case Dither_Yliluoma1:
                p = 2.0;
                break;
            case Dither_Yliluoma2:
                p = 1.0;
                break;
            case Dither_Yliluoma3:
                p = 1.0;
                break;
            case Dither_Yliluoma1Iterative:
                p = 1.0; // "Reasonable default"
        }
    }

    if(p >= 0.0
    && p < 1.0)
        luma_threshold = average_luma_difference * p;
    else if(p < 2.0)
        luma_threshold = average_luma_difference +
            (p-1.0) * (maximum_luma_difference-average_luma_difference);
    else if(p <= 3.0)
        luma_threshold = maximum_luma_difference +
            (p-2.0) * (total_luma-maximum_luma_difference);

    if(verbose >= 2)
    {
        for(unsigned a=0; a<PaletteSize; ++a)
            std::printf("Luma[%3u]: %6d (%3u = #%06X)\n", a, GetLuma(luma_order[a]),
                luma_order[a], Data[luma_order[a]].rgb);
        std::printf("Average: %d, max: %d, total: %d, threshold: %d\n",
            average_luma_difference,
            maximum_luma_difference,
            total_luma,
            luma_threshold);
    }

    // Create some combinations
    switch(Dithering)
    {
          /*if(DitherCombinationRecursionLimit == 0)
                DitherCombinationRecursionLimit = DitherCombinationAllowSame ? 4 : 2;
            if(DitherCombinationChangesLimit == 0)
                DitherCombinationChangesLimit = DitherCombinationRecursionLimit;
            break;*/
        case Dither_Yliluoma1:
            if(DitherCombinationRecursionLimit == 0)
                DitherCombinationRecursionLimit = DitherColorListSize;
            if(DitherCombinationChangesLimit == 0)
            {
                DitherCombinationChangesLimit = DitherCombinationRecursionLimit;
                if(DitherCombinationChangesLimit > 4)
                    DitherCombinationChangesLimit = 4;
            }
            break;
        case Dither_Yliluoma1Iterative:
        case Dither_Yliluoma2:
            if(DitherCombinationRecursionLimit == 0)
            {
                DitherCombinationRecursionLimit
                    = DitherColorListSize > 2
                            ? (DitherColorListSize+1)/2
                            : DitherColorListSize;
                if(DitherCombinationRecursionLimit > 8)
                    DitherCombinationRecursionLimit = 8;
            }
            if(DitherCombinationChangesLimit == 0)
            {
                DitherCombinationChangesLimit = DitherCombinationRecursionLimit;
                if(DitherCombinationChangesLimit > 3)
                    DitherCombinationChangesLimit = 3;
            }
            break;
        case Dither_Yliluoma3:
            if(DitherCombinationRecursionLimit == 0)
                DitherCombinationRecursionLimit = 2;
            if(DitherCombinationChangesLimit == 0)
                DitherCombinationChangesLimit = 2;
    }

    if(verbose >= 1)
    {
        std::printf("Calculating palette-color combinations... %s",
            verbose >= 3 ? "\n" : "");
        std::fflush(stdout);
    }

  { CombinationMaker maker(Data, luma_order, luma_threshold);
    maker.Make(Combinations,
        0, GammaColorVec(0.0),
        DitherCombinationRecursionLimit,
        DitherCombinationChangesLimit); }

    if(Dithering == Dither_Yliluoma1
    || Dithering == Dither_Yliluoma1Iterative)
    {
      #pragma omp parallel for schedule(static)
        for(size_t a=0; a<Combinations.size(); ++a)
        {
            std::vector<unsigned>& indices = Combinations[a].indexlist;
            std::sort(indices.begin(), indices.end(), ComparePaletteLuma(*this));
        }
    }

    if(verbose >= 1)
    {
        std::printf("Done.\n");
        std::fflush(stdout);
    }

    if(verbose >= 2)
    {
        typedef MapType<size_t,size_t> CountMapType;
        CountMapType Counts;

        for(size_t a=0; a<Combinations.size(); ++a)
            Counts[ Combinations[a].indexlist.size() ] += 1;

        std::printf("Palette-color combinations chosen for dithering:");

        for(CountMapType::const_iterator i = Counts.begin(); i != Counts.end(); ++i)
            std::printf(" %u:%u", (unsigned) i->first, (unsigned) i->second );

        std::printf(" -- total: %u\n", (unsigned) Combinations.size());
        std::fflush(stdout);
    }

    if((ColorComparing == Compare_RGB
     || ColorComparing == Compare_CIE76_DeltaE)
     && (Dithering == Dither_Yliluoma1
      || Dithering == Dither_Yliluoma1Iterative
      || Dithering == Dither_Yliluoma3)
      )
    {
        for(size_t a=0; a<Combinations.size(); ++a)
        {
            if(ColorComparing == Compare_RGB)
            {
                const ColorInfo& meta = Combinations[a].combination;
                KDTree<unsigned,4>::KDPoint p( meta.R, meta.G, meta.B, meta.A );
                CombinationTree.insert(p, a);
            }
            else if(ColorComparing == Compare_CIE76_DeltaE)
            {
                const ColorInfo& meta = Combinations[a].combination;
                KDTree<unsigned,4>::KDPoint p( meta.lab.L, meta.lab.a, meta.lab.b, meta.A );
                CombinationTree.insert(p, a);
            }
        }

        if(verbose >= 2)
        {
            std::printf("Palette search trees created.\n");
            std::fflush(stdout);
        }
    }
}
