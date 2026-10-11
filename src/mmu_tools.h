#ifndef __MMU_TOOLS_H__
#define __MMU_TOOLS_H__

#include "mmu_backend.h"

#include <string>
#include <vector>

// Vendor-neutral helpers for slots, filament colours and print file tools.

// "" for no colour, else uppercase "RRGGBB"; accepts "#rrggbb" and "NONE"
std::string mmu_normalise_hex(const std::string &colour);
// case-insensitive: vendors store "PETG" and "petg" alike
bool mmu_same_material(const std::string &a, const std::string &b);

// CIE76 delta E between two colours, -1 if either is not one.
// Thresholds measured on the common filament presets:
//   shades of one colour stay under ~13, the next colour starts at ~32;
//   neighbours (blue/purple 65, brown/orange 74) stay under ~74,
//   unrelated colours (white/black 87, blue/yellow 140) start at ~87.
double mmu_colour_distance(const std::string &a, const std::string &b);
static const double MMU_COLOUR_MISMATCH = 30;  // not the same colour: warn
static const double MMU_COLOUR_CLOSE = 80;     // close enough for automap

bool mmu_has_filament(const MmuSlot &slot);
// the slot whose map holds T<tool>, -1 if none
int mmu_slot_for_tool(const std::vector<MmuSlot> &slots, int tool);

// Automap: the slot each tool should use, -1 to leave it, MMU_AUTOMAP_CLEAR to
// leave it unassigned for the user. `current` is each tool's slot now.
// Only picks a slot that is loaded, holds the same material, is within
// MMU_COLOUR_CLOSE and, with `weights`, has the grams; nearest colour first,
// sharing a slot only where `share` allows. Never forces a match.
static const int MMU_AUTOMAP_CLEAR = -2;
std::vector<int> mmu_automap(const std::vector<MmuFileTool> &tools,
                             const std::vector<MmuSlot> &slots,
                             const std::vector<int> &current,
                             bool share, bool weights);

// Tools a file uses (filament weight > 0) from its moonraker metadata response.
std::vector<MmuFileTool> mmu_file_tools(const json &metadata);

#endif // __MMU_TOOLS_H__
