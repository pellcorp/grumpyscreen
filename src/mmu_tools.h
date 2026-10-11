#ifndef __MMU_TOOLS_H__
#define __MMU_TOOLS_H__

#include "mmu_backend.h"

#include <string>
#include <vector>

// Vendor-neutral reading of slots and filaments, shared by the panels and by
// any backend that opts into automap. Nothing here knows a vendor.

// "" when there is no colour, uppercase "RRGGBB" otherwise. Vendors and
// slicers spell colours "RRGGBB", "#rrggbb", "" or "NONE".
std::string mmu_normalise_hex(const std::string &colour);

// Vendors normalise material names differently -- one upper-cases what it is
// given, another stores it verbatim -- so "PETG" and "petg" are one material.
bool mmu_same_material(const std::string &a, const std::string &b);

// How far apart two colours look: CIE76 delta E, the distance between them in
// CIE Lab (sRGB, D65). -1 when either is not a colour.
//
// Not Fluidd's luma-weighted RGB rule: weighting red at 0.3 left red against
// near-black (14087) and purple against red (4423) under its 16000 threshold
// while yellow against red (16691) went over it.
double mmu_colour_distance(const std::string &a, const std::string &b);
// Above this the colours are not the same filament colour: shades of one
// colour stay under ~13 (two reds 6.1, black and near-black 12.7), the next
// colour along starts at ~32 (blue and indigo 32.3, white and light grey
// 34.9). The screen warns past it. Ours, measured on the common presets.
static const double MMU_COLOUR_MISMATCH = 30;
// Up to this a colour is close enough for automap to pick it, with that
// warning: neighbours on the colour wheel stay under ~74 (blue and purple
// 65.4, green and cyan 65.7, brown and orange 73.5), unrelated colours start
// at ~87 (white and black 87.3; blue and yellow 140.3). Ours, measured.
static const double MMU_COLOUR_CLOSE = 80;

// filament physically in the slot, in the unit or in the tool
bool mmu_has_filament(const MmuSlot &slot);

// the slot whose `map` names tool `tool` ("T3"), -1 when none does
int mmu_slot_for_tool(const std::vector<MmuSlot> &slots, int tool);

// Automap: the slot each file tool should come from, -1 to leave it where it
// is, MMU_AUTOMAP_CLEAR to leave it unassigned for the user to pick.
// `current` is the slot each tool is on now (-1 none), indexed like tools.
//
// It never forces a match. A slot is a candidate only when it is loaded, holds
// the same material, its colour is within MMU_COLOUR_CLOSE and -- with
// `weights` (spoolman) -- it has the grams left. A tool already on a candidate
// of the same colour stays; the rest take free candidates, nearest colour
// first; where the vendor lets tools share a slot (`share`), a candidate
// already in use if it has the grams for every tool on it. A tool with no
// candidate is left unassigned for the user, and the screen holds Print until
// they pick. A tool already on a candidate is never left without one.
static const int MMU_AUTOMAP_CLEAR = -2;
std::vector<int> mmu_automap(const std::vector<MmuFileTool> &tools,
                             const std::vector<MmuSlot> &slots,
                             const std::vector<int> &current,
                             bool share, bool weights);

// The tools a print file uses, from its moonraker metadata response: those
// with filament weight, as Fluidd and Mainsail both decide it. Empty when the
// slicer wrote no per-tool weights.
std::vector<MmuFileTool> mmu_file_tools(const json &metadata);

#endif // __MMU_TOOLS_H__
