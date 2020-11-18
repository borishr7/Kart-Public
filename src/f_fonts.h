// SONIC ROBO BLAST 2 KART
//-----------------------------------------------------------------------------
/// \file  f_fonts.h
/// \brief Unicode fonts and string drawing functions

#ifndef __F_FONTS__
#define __F_FONTS__

// Codepoint defs
#define U_MAX  0x10FFFF
#define U_MASK 0x1FFFFF // 21 bits

#define PLANEOF(n) (n & 0xFF0000) >> 16
#define CODEOF(n)  (n & 0x00FFFF)

// String flags
#define F_FONTMASK   0x0FFF
#define F_WORDWRAP   0x0001 // Wordwrap
#define F_CJKWWRAP   0x0002 // CJK Wordwrap
#define F_MONOSPACE  0x0004 // Monospace
#define F_RTOL       0x0008 // Right-to-left text
#define F_DEBUG      0x0010 // Draw bounding boxes
#define F_DOOMSCALE  0x0020 // Disable both V_NOSCALESTART and V_NOSCALEPATCH
#define F_ALIGNRIGHT 0x0040 // Right aligned text

typedef union point16
{
	struct
	{
		int16_t x;
		int16_t y;
	};
	uint32_t point;
} point16_t;

typedef union box16
{
	struct
	{
		uint16_t w;
		uint16_t h;
	};
	uint32_t box;
} box16_t;

typedef union rect16
{
	struct
	{
		int16_t  x;
		int16_t  y;
		uint16_t w;
		uint16_t h;
	};
	struct
	{
		point16_t pt;
		box16_t  box;
	};
	uint64_t rect;
} rect16_t;

// Monospace: rect is bottom-center aligned with bbox
// Dynamic: bbox is ignored and rect is used instead
typedef struct glyph
{
	uint32_t code;  // Codepoint
	patch_t* pgfx;  // Patch gfx
	rect16_t rect;  // Patch clip rect
	box16_t  bbox;  // Virtual bounding box
	int16_t  yoffs; // Baseline alignment offset (Y offset)
} glyph_t;

typedef struct font
{
	char  fontid[16]; // lowercase only

	size_t sp_width;
	size_t ln_height;

	size_t num_patches;
	size_t num_glyphs;

	patch_t** patches;
	glyph_t** glyphs;
	glyph_t** planes[17];  // 17 x glyphinfo[65536]
} font_t;

enum fontinfo_keywords
{
	K_CHAR=1, // char
	K_SET,    // set
	K_LOAD,   // loadgfx
	K_SPACE,  // spacewidth
	K_LINE,   // lineheight
	K_DEFCHR, // defchar, defaultchar
	K_DEFSP,  // defspacing
	K_BBOX    // bbox, boundingbox
};

// Functions
int F_GetCodepoint(const char** ptr);

font_t*  F_GetFont(const char* id);
glyph_t* F_GetGlyph(font_t* font, int cp);
uint64_t F_GetScreenRect(void);

void F_DrawGlyph(fixed_t x, fixed_t y, fixed_t scale, uint32_t flags, glyph_t* glyph);
void F_DrawString(int x, int y, float scale, uint32_t flags, font_t* font, const char* str);

int  F_LoadFont(const char* lmpname);
void F_InitFonts(void);

#endif // __F_FONTS__
