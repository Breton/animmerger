#ifndef bqtTileTrackerSpritesHH
#define bqtTileTrackerSpritesHH

#include "types.hh"
#include "vectype.hh"

struct DifferingSection
{
    unsigned x1,y1, wid,hei;

    bool operator< (const DifferingSection& b) const;
    bool operator== (const DifferingSection& b) const;
    bool Overlaps(const DifferingSection& b) const;
};

typedef VecType<DifferingSection> DifferencesOnFrame;

/* Generate a set of rectangles that expresses all
 * differing pixels between the two images.
 */
DifferencesOnFrame FindDifferences
    (const VecType<uint32>& background,
     const VecType<uint32>& screen,
     unsigned width);

/* In any average animation, sometimes actors collide.
 * They shoot projectiles, in which case one actor splits into two.
 * They pass each others, in which case two actors become one, or
 * one becomes two, or both.
 * The job of this function is to find out which DifferingSections
 * refer to the same actor, and to ignore those DifferingSections
 * that are ambiguous.
 *
 * TODO: Later phase:
 * There is also the possibility that an actor is partially visible
 * (emerging from screen edges or from behind an object); one should
 * reconstruct the actor in those cases.
 *
 * TODO: Later phase:
 * An actor may also sometimes become partially transparent. This
 * may happen if it shares background colors. One should attempt
 * to merge the individual frames by checking if one pose
 * can be overlayed over another pose without conflicts.
 */
struct SpriteLore
{
    VecType<DifferencesOnFrame> DifferencesEachFrame;

    void FindDistinctActors();
};

#endif