//-------------------------------------------------------------------------
/*
Copyright (C) 2022 EDuke32 developers and contributors

This file is part of EDuke32.

EDuke32 is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
//-------------------------------------------------------------------------

// This file implements addons.h for mapster32
// exists solely to allow calling Addon_PrepareUserAddons() from G_LoadGroups() in common.cpp
// Could later be expanded for editor-specific user addon support

#include "duke3d.h"
#include "addons.h"

useraddon_t** g_useraddons_grpinfo = nullptr;
useraddon_t** g_useraddons_tcs = nullptr;
useraddon_t** g_useraddons_mods = nullptr;

int32_t g_addoncount_grpinfo = 0;
int32_t g_addoncount_tcs = 0;
int32_t g_addoncount_mods = 0;

bool g_addonstart_failed = false;

void Addon_FreePreviewHashTable(void){ /* stub */ };
void Addon_CachePreviewImages(void) { /* stub */ };
int32_t Addon_LoadPreviewTile(const useraddon_t* addon)
{
    // stub
    UNREFERENCED_PARAMETER(addon);
    return 0;
}

void Addon_FreeUserAddons(void) { /* stub */ };
void Addon_LoadDescriptors(void) { /* stub */ };
void Addon_PruneInvalidAddons(useraddon_t** & useraddons, int32_t & numuseraddons)
{
    UNREFERENCED_PARAMETER(useraddons);
    UNREFERENCED_PARAMETER(numuseraddons);
};

void Addon_InitializeLoadOrder(void) { /* stub */ };
void Addon_SwapLoadOrder(int32_t const indexA, int32_t const indexB)
{
    // stub
    (void) indexA;
    (void) indexB;
};

bool Addon_IsDependencyFulfilled(const addondependency_t* depPtr, const useraddon_t* otherAddonPtr)
{
    // stub
    UNREFERENCED_PARAMETER(depPtr);
    UNREFERENCED_PARAMETER(otherAddonPtr);
    return true;
};

int32_t Addon_PrepareGrpInfoAddon(void) { return 0; };
int32_t Addon_PrepareUserTCs(void) { return 0; };
int32_t Addon_PrepareUserMods(void) { return 0; };

int32_t Addon_StrncpyTextWrap(char* dst, const char *src, int32_t const nsize, int32_t const lblen)
{
    UNREFERENCED_PARAMETER(dst);
    (void) src;
    (void) nsize;
    (void) lblen;
    return 0;
}
