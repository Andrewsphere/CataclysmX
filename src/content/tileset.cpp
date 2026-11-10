#include "content/tileset.h"
#include <cstdlib>

namespace cataclysmx {

AnimationSample SampleAnimation(const AnimationClip& clip, std::uint64_t elapsedMilliseconds, AnimationPlayback playback) {
    if (clip.frames.empty() || !clip.totalMilliseconds) std::abort();
    if (playback == AnimationPlayback::Once && elapsedMilliseconds >= clip.totalMilliseconds) {
        return {clip.frames.back().spriteIndex, {}, true};
    }
    if (playback == AnimationPlayback::Loop && clip.frames.size() == 1) { return {clip.frames[0].spriteIndex,{}}; }
    const auto phase = playback == AnimationPlayback::Loop ? elapsedMilliseconds % clip.totalMilliseconds : elapsedMilliseconds;
    for (const auto& frame : clip.frames) if (phase < frame.endMilliseconds) { return {frame.spriteIndex,frame.endMilliseconds-phase}; }
    std::abort();
}
}
