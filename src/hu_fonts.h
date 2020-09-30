// SONIC ROBO BLAST 2 KART
//-----------------------------------------------------------------------------
/// \file  hu_fonts.h
/// \brief Unicode fonts and string drawing functions

#ifndef __HU_FONTS__
#define __HU_FONTS__

// Codepoint defs
#define U_NULL  0x000000
#define U_MAX   0x10FFFF
#define U_INVAL 0x1FFFFF

// Functions
UINT32 HU_GetCodePoint(char** ptr);
void   HU_LoadFontLump(const char* lumpname);
void   HU_InitFonts();

#endif // __HU_FONTS__
