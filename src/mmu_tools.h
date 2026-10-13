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

bool mmu_has_filament(const MmuSlot &slot);
// the slot whose map holds T<tool>, -1 if none
int mmu_slot_for_tool(const std::vector<MmuSlot> &slots, int tool);

// Tools a file uses (filament weight > 0) from its moonraker metadata response.
std::vector<MmuFileTool> mmu_file_tools(const json &metadata);

#endif // __MMU_TOOLS_H__
