#pragma once

/** Which positions the user may change in the editor. Saved with the plugin state. */
enum class EditMode
{
    Sources = 1,  // default: move sources, speakers are locked
    Speakers = 2, // move speakers, sources are locked
    Both = 3,     // move both
    View = 4      // nothing can be moved
};

inline bool canEditSources (EditMode m) noexcept  { return m == EditMode::Sources  || m == EditMode::Both; }
inline bool canEditSpeakers (EditMode m) noexcept { return m == EditMode::Speakers || m == EditMode::Both; }
