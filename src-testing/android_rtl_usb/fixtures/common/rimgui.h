#pragma once
namespace RImGui {
inline void beginDisabled() {}
inline void endDisabled() {}
inline bool SteppedSliderFloat(const char *, float *, float, float, float, const char *) { return false; }
inline bool IsItemDeactivatedAfterEdit() { return false; }
inline bool Checkbox(const char *, bool *) { return false; }
}
