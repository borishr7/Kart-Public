// SONIC ROBO BLAST 2 KART
//-----------------------------------------------------------------------------
/// \file  hu_fonts.c
/// \brief Unicode fonts and string drawing functions

#include "doomdef.h"
#include "v_video.h"
#include "hu_fonts.h"

// Return a single codepoint given a pointer to a unicode string
// TODO: maybe return U+FFFF for U_INVAL?
UINT32 HU_GetCodePoint(char** ptr)
{
	int    fcount = 0; // Number of flag bits
	UINT32 cpbits = 0; // Codepoint bits
	UINT32 cp_min = 4; // Overlong detector

	char* str = *ptr; // Obtain string
	UINT8 lc  = *str; // Copy leading byte
	*ptr = ++str;     // Skip one byte

	// No more than 4 flag bits
	if(lc > 0xF7)
		return U_INVAL;

	// Count flag bits
	while(lc & 0x80)
	{
		fcount++;
		lc <<= 1;
		cp_min <<= (fcount+1);
	}

	// Plain ascii
	if(!fcount)
		return lc;

	if(fcount > 1)
	{
		// Save remaining bits
		// TODO: multiarch: check for undefined behavior
		cpbits = (lc >> fcount);

		// Read remaining bytes
		while(--fcount)
		{
			uint8_t b = *(str++);

			// No invalid/truncated sequences
			if((b & 0xC0) ^ 0x80)
				return U_INVAL;

			cpbits = (cpbits << 6) | (b & 0x3F);
		}

		// No overlongs/invalid codepoints
		if(cpbits <= cp_min || cpbits > U_MAX)
			return U_INVAL;

		*ptr = str; // Skip read bytes
		return cpbits;
	}

	return U_INVAL;
}

// Load from font definition lump
void HU_LoadFontLump(const char* lumpname)
{
}

// Initialization stuff
void HU_InitFonts()
{
}
