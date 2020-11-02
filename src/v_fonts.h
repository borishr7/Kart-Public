// SONIC ROBO BLAST 2 KART
//-----------------------------------------------------------------------------
/// \file  v_fonts.h
/// \brief Unicode fonts and string drawing functions

#ifndef __V_FONTS__
#define __V_FONTS__

#define MAX_FONTS 16

// Codepoint defs
#define U_NULL 0x000000
#define U_MAX  0x10FFFF
#define U_MASK 0x1FFFFF // 21 bits

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
	char* fontid;    // 8 chars lowercase
	char* fontname;  // 32 chars ascii
	char* copyright; // 32 chars

	int16_t sp_x; // Global x spacing
	int16_t sp_y; // Global y spacing

	fixed_t  scale;  // Default scale factor
	glyph_t* repchr; // Default replacement char

	size_t patch_count; // Number of patch lumps
	size_t glyph_count; // Number of glyphs

	// Lookup table for all planes/codepoints
	glyph_t** planes[17];  // 17 x glyphinfo[65536]

	// Backup list of patches/glyphs
	patch_t** patches;
	glyph_t** glyphs;
} font_t;

// Functions
int V_GetCodepoint(const char** ptr);

font_t*  V_GetFont(const char* id);
glyph_t* V_GetGlyph(font_t* font, int cp);

int  V_DrawGlyph(int sx, int sy, int scale, int colormap, glyph_t* glyph);
void V_DrawStringF(int sx, int sy, int scale, font_t* font, const char* str);

int  V_LoadFont(const char* lmpname);
void V_InitFonts(void);

#endif // __V_FONTS__
