// SONIC ROBO BLAST 2 KART
//-----------------------------------------------------------------------------
/// \file  v_fonts.h
/// \brief Unicode fonts and string drawing functions

#ifndef __V_FONTS__
#define __V_FONTS__

#define MAX_FONTS 16

// Codepoint defs
#define U_NULL  0x000000
#define U_MAX   0x10FFFF
#define U_INVAL 0x1FFFFF

#define PLANEOF(n) ((n >> 16) & 0xFF)
#define CODEOF(n)  (n & 0xFFFF)

typedef struct cliprect
{
	int16_t  x;
	int16_t  y;
	uint16_t w;
	uint16_t h;
} cliprect_t;

typedef struct glyph
{
	uint32_t   code; // Codepoint
	patch_t*   pgfx; // Patch gfx ptr
	cliprect_t rect; // Patch clip rect
	int16_t    gx;   // Glyph X offset
	int16_t    gy;   // Glyph Y offset
} glyph_t;

typedef struct font
{
	char*   lumpname; // Lump name, 8 chars
	char*   fontname; // Font name, 32 chars
	size_t  pxsize;   // Glyph size in pixels
	fixed_t defscale; // Default scale factor

	size_t tag_count;   // Number of emoji tags
	size_t patch_count; // Number of patch lumps
	size_t glyph_count; // Number of glyphs

	// Lookup table for all planes/codepoints
	glyph_t** planes[17];  // 17 x glyphinfo[65536]

	// Backup list of patches/glyphs
	patch_t** patches;
	glyph_t** glyphs;
} font_t;

// Functions
UINT32 V_GetCodePoint(char**);

void V_DrawGlyph(int, int, glyph_t*);
void V_DrawCharF(int, int, int);
void V_DrawStringF(int, int, char*);

int  V_LoadFont(const char*);
void V_InitFonts(void);

#endif // __V_FONTS__
