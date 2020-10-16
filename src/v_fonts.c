// SONIC ROBO BLAST 2 KART
//-----------------------------------------------------------------------------
/// \file  v_fonts.c
/// \brief Unicode fonts and string drawing functions

#include "doomdef.h"
#include "doomstat.h"
#include "console.h"
#include "v_video.h"
#include "v_fonts.h"
#include "w_wad.h"
#include "z_zone.h"

static size_t num_fonts = 0;
static font_t** fonts = NULL;

// Read a single codepoint from a unicode string
// Advances the source ptr by the number of bytes read
UINT32 V_GetCodePoint(char** ptr)
{
	int    fcount = 0; // Number of flag bits
	UINT32 cpbits = 0; // Codepoint bits
	UINT32 olmask = 4; // Overlong detector mask

	char* str  = *ptr; // Obtain string
	UINT8 lchr = *str; // Copy leading byte
	*ptr = ++str;      // Advance one byte

	// No more than 4 flag bits (lchr > 11110xxx)
	if(lchr > 0xF7)
		return U_INVAL;

	// Count flag bits and generate mask
	while(lchr & 0x80)
	{
		fcount++;
		lchr <<= 1;
		olmask <<= (fcount+1);
	}

	// Just plain ascii, return
	if(!fcount)
		return lchr;

	// A valid leading byte must have 2-4 flag bits
	if(fcount > 1)
	{
		// Save remaining bits
		cpbits = (lchr >> fcount);

		// Read remaining bytes
		while(--fcount)
		{
			UINT8 b = *(str++);

			// No invalid/truncated sequences
			if((b & 0xC0) ^ 0x80)
				return U_INVAL;

			cpbits = (cpbits << 6) | (b & 0x3F);
		}

		// Check for overlong/invalid codepoint
		if(cpbits <= olmask || cpbits > U_MAX)
			return U_INVAL;

		*ptr = str; // Advance bytes
		return cpbits;
	}

	return U_INVAL;
}

// Free all data from fontinfo_t
static void free_font(font_t* font)
{
	unsigned int i;

	// Free planes LUT
	for(i = 0; i < 17; i++)
		free(font->planes[i]);

	// Free individual glyphs
	for(i = 0; i < font->glyph_count; i++)
		free(font->glyphs[i]);

	// TODO: are patches[n] supposed to be freed too?
	free(font->lumpname);
	free(font->fontname);
	free(font->patches);
	free(font->glyphs);
	free(font);
}

// ============================= FONTINFO PARSING =============================

enum fontinfo_keywords
{
	K_CHAR = 1,  // char
	K_LUMP,      // lump
	K_GFX,       // gfx
	K_TAG,       // tag
	K_DEFSCALE,  // defscale
	K_PXSIZE,    // pxsize
	K_FONTNAME,  // fontname
	K_FONTGROUP, // fontgroup
	K_COPYRIGHT  // copyright
};

// Decode fontinfo keywords into enum values
static int untok_keyword(char* key)
{
	if(!key) return 0;

	switch(*key)
	{
		case 'l':
			if(!strcmp(key, "lump")) return K_LUMP;
			break;
		case 'c':
			if(!strcmp(key, "char"))      return K_CHAR;
			if(!strcmp(key, "copyright")) return K_COPYRIGHT;
			break;
		case 'f':
			if(!strcmp(key, "fontname"))  return K_FONTNAME;
			if(!strcmp(key, "fontgroup")) return K_FONTGROUP;
			break;
		case 'd':
			if(!strcmp(key, "defscale")) return K_DEFSCALE;
			break;
		case 'g':
			if(!strcmp(key, "gfx")) return K_GFX;
			break;
		case 't':
			if(!strcmp(key, "tag")) return K_TAG;
			break;
		case 'p':
			if(!strcmp(key, "pxsize")) return K_PXSIZE;
			break;
	}
	return 0;
}

// Unicode notation token to codepoint (UPPERCASE)
// Returns a 21-bit codepoint promoted to int or -1 if invalid
static int untok_codepoint(char* s)
{
	const char* hex = "0123456789ABCDEF";

	if(!s) return -1;

	// Check leading chars
	if(*(s++) != 'U') return -1;
	if(*(s++) != '+') return -1;

	// Check empty hex notation
	if(*s == '\0') return -1;

	// Parse hex code
	int cp = 0;
	char c;
	while((c = *(s++)))
	{
		// Pointer displacement trick
		size_t d = (strchr(hex, c) - hex);
		cp = (cp << 4) | (d & 15);

		// Exceedingly large tokens or invalid chars
		if(cp > U_MAX || d > 15) return -1;
	}

	return cp;
}

// Unquote and unescape a string block token.
// String tokens must have at least one leading quote to be valid.
// A trailing quote is not strictly necessary.
static char* untok_string(char* str)
{
	if(!str) return NULL;

	if(*(str++) != '\"') return NULL;

	char* s = str; // read ptr
	char* t = s;   // write ptr

	char c;
	while((c = *(s++)))
	{
		if(c == '\"')
		{
			if(*s == '\0') break;	 // Trailing quote
			if(*s != c) return NULL; // Invalid escape sequence

			s++;
		}
		*(t++) = c;
	}

	*t = '\0';
	return str;
}

// Simple whitespace-delimited tokenizer
static char* get_token(char** str_ptr)
{
	char* s = *str_ptr;

	// Skip leading whitespaces
	while(isspace(*s)) s++;

	// Empty/Comment-only lines
	if(*s == '#' || *s == '\0') return NULL;

	char* token = s;
	int isstring = 0;

	while(*s)
	{
		if(*s == '\"') isstring = ~isstring;

		// If not inside string block
		if(!isstring)
		{
			// Just in case
			if(*s == '#')
			{
				*s = '\0';
				break;
			}

			// Stop on first whitespace
			if(isspace(*s))
			{
				*(s++) = '\0';
				break;
			}
		}
		s++;
	}

	*str_ptr = s; // Advance bytes
	return token;
}

static size_t get_token_list(char** line, char** list, size_t list_size)
{
	size_t count = 0;

	while(count < list_size)
	{
		char* tok = get_token(line);
		list[count] = tok;

		if(tok == NULL) break;

		count++;
	}

	return count;
}

// This is just to hide the clutter of repeated error checking
#define err_break(...) {errset = 1; snprintf(errmsg, 256, __VA_ARGS__); break;}

// Parse fontinfo lump
static font_t* parse_fontinfo(lumpnum_t lmpnum, const char* lmpname)
{
	// Read and allocate data
	size_t lmpsize = W_LumpLength(lmpnum);
	char*  lmpdata = malloc(lmpsize+1);
	W_ReadLump(lmpnum, lmpdata);
	*(lmpdata+lmpsize) = '\0';

	// Allocate font struct
	font_t* font = malloc(sizeof(font_t));
	memset(font, 0, sizeof(font_t));

	// Allocate token list, 16 entries
	char** toklist = malloc(16 * sizeof(char*));
	memset(toklist, 0, (16 * sizeof(char*)));

	// Allocate error message string
	int   errset = 0;
	char* errmsg = malloc(256);

	char*  line;
	char*  nextline = lmpdata;
	size_t line_num = 0;

	while(!errset && (line = nextline))
	{
		line_num++;

		// Split line (remove LF)
		nextline = strchr(line, '\n');
		if(nextline) *(nextline++) = '\0';

		// Get first valid token
		char* tok = get_token(&line);
		if(!tok) continue;

		// First token must also be a valid keyword
		int kcode = untok_keyword(tok);
		if(!kcode) err_break("Invalid keyword \"%s\"", tok);

		switch(kcode)
		{
			case K_FONTNAME:
			{
				int n = get_token_list(&line, toklist, 16);
				if(n != 1) err_break("Expected a single argument, got %d\n", n);

				char* name = untok_string(toklist[0]);
				if(!name) err_break("Expected a valid string argument");

				font->fontname = malloc(strlen(name) + 1);
				strcpy(font->fontname, name);
				break;
			}
//			case K_PXSIZE:
//			{
//				int n = get_token_list(&line, toklist, 16);
//				if(n != 1) err_break("Expected a single argument, got %d\n", n);

//				int pxsize = strtol(toklist[0], NULL, 10);
//				if(pxsize < 0)

//				font->pxsize = pxsize;
//				break;
//			}
			case K_LUMP:
			{
				int n = get_token_list(&line, toklist, 16);
				if(n != 3) err_break("Syntax error!");

				// Get gfxname
				char* gfxname = untok_string(toklist[2]);
				if(!gfxname) err_break("Expected a valid lump name");

				// Check if it exists
				lumpnum_t gfxnum = W_CheckNumForName(gfxname);
				if(gfxnum == LUMPERROR)
					err_break("Lump \"%s\" not found", gfxname);

				// Load the lump itself
				patch_t* gfx = W_CachePatchNum(gfxnum, PU_HUDGFX);

				// Resize lump list
				size_t new_count = (font->patch_count + 1);
				size_t new_size  = (new_count * sizeof(patch_t*));
				font->patches = realloc(font->patches, new_size);

				// Write to list
				font->patch_count = new_count;
				font->patches[new_count-1] = gfx;
				break;
			}
			case K_CHAR:
			{
				int n = get_token_list(&line, toklist, 16);
				if(n != 8)
					err_break("Syntax error! Expected 8 args, got %d\n", n);

				// Validate codepoint
				int codepoint = untok_codepoint(toklist[0]);
				if(codepoint < 0) err_break("Invalid codepoint \"%s\"", toklist[0]);

				// Get lump index
				int lump_index = strtol(toklist[1], NULL, 10);
				if(lump_index < 0 || lump_index >= font->patch_count)
					err_break("No lump matching index '%d'", lump_index);

				// Allocate glyph and fill data
				glyph_t* glyph = malloc(sizeof(glyph_t));
				memset(glyph, 0, sizeof(glyph_t));

				glyph->code   = codepoint;
				glyph->pgfx   = font->patches[lump_index];
				glyph->rect.x = strtol(toklist[2], NULL, 10);
				glyph->rect.y = strtol(toklist[3], NULL, 10);
				glyph->rect.w = strtol(toklist[4], NULL, 10);
				glyph->rect.h = strtol(toklist[5], NULL, 10);
				glyph->gx     = strtol(toklist[6], NULL, 10);
				glyph->gy     = strtol(toklist[7], NULL, 10);

				// Resize glyph list and write to it
				size_t new_count = (font->glyph_count + 1);
				size_t new_size  = (new_count * sizeof(glyph_t*));

				font->glyphs = realloc(font->glyphs, new_size);

				font->glyph_count = new_count;
				font->glyphs[new_count-1] = glyph;

				// Split codepoint
				uint16_t code_num  = CODEOF(codepoint);
				uint16_t plane_num = PLANEOF(codepoint);

				// If plane entry doesn't exist yet, allocate one
				if(font->planes[plane_num] == NULL)
				{
					glyph_t** plane = malloc(65536 * sizeof(glyph_t*));
					memset(plane, 0, (65536 * sizeof(glyph_t*)));

					font->planes[plane_num] = plane;
				}

				// TODO: Check if glyph is already set
//				else { }

				font->planes[plane_num][code_num] = glyph;
				break;
			}
		}
	}

	if(errset)
	{
		CONS_Printf("ERROR: \"%s\", Line %lu: %s\n", lmpname, line_num, errmsg);
		free_font(font);
		font = NULL;
	}

	free(lmpdata);
	free(toklist);
	free(errmsg);
	return font;
}

// ============================================================================

// Load fontinfo lump
// Returns -1 on error
// TODO: call I_Error if the lump fails to load from IWAD
int V_LoadFont(const char* lmpname)
{
	if(!fonts) return -1;

	lumpnum_t lmpnum = W_CheckNumForName(lmpname);
//	char* wadname = wadfiles[WADFILENUM(lmpnum)]->filename;

	if(lmpnum == LUMPERROR)
	{
		CONS_Printf("V_LoadFont(): \"%s\" not found\n", lmpname);
		return -1;
	}

	// Find first free slot
	int slotnum = 0;
	while(slotnum < MAX_FONTS)
		if(fonts[slotnum] == NULL) break;

	if(slotnum == (MAX_FONTS-1))
	{
		CONS_Printf("V_LoadFont(): No free slots!\n");
		return -1;
	}

	// Parse font data
	font_t* font = parse_fontinfo(lmpnum, lmpname);
	if(!font)
	{
		CONS_Printf("V_LoadFont(): Failed to load \"%s\"\n", lmpname);

		if(WADFILENUM(lmpnum) <= mainwads)
			I_Error("Failed to load system font '%s'", lmpname);

		return -1;
	}

	fonts[slotnum] = font;

	CONS_Printf("V_LoadFont(): Loaded '%s' on slot %d\n", lmpname, slotnum);
//	CONS_Printf("mainwads = %u, this_wad = %u\n", mainwads, WADFILENUM(lmpnum));
	return 0;
}

void V_DrawGlyph(int sx, int sy, glyph_t* glyph)
{
	patch_t* patch = glyph->pgfx;
	int cx = glyph->rect.x;
	int cy = glyph->rect.y;
	int cw = glyph->rect.w;
	int ch = glyph->rect.h;
	int gx = glyph->gx;
	int gy = glyph->gy;

	int xpos = (sx << FRACBITS);
	int ypos = ((sy+gy) << FRACBITS);

	uint32_t scale = (1 << FRACBITS);
	uint32_t vflags = (V_NOSCALEPATCH | V_NOSCALESTART);

	V_DrawCroppedPatch(xpos, ypos, scale, vflags, patch, cx, cy, (cw+cx), (ch+cy));
}

void V_DrawCharF(int cp, int sx, int sy)
{
	if(cp < 0 || cp > U_MAX) return;

	glyph_t** plane;
	glyph_t*  glyph;

	// Placeholder
	font_t* font = fonts[0];
	if(!font) return;

	plane = font->planes[PLANEOF(cp)];
	if(!plane) return;
	
	glyph = plane[CODEOF(cp)];
	if(!glyph) return;

	V_DrawGlyph(sx, sy, glyph);
}

void V_DrawStringF(int sx, int sy, char* str)
{
	char* s = str;
	uint32_t cp;

	int x_offs = 0;
	int y_offs = 0;

	// Placeholder
	font_t* font = fonts[0];
	if(!font) return;

	// TODO: replace invalid glyphs here
	while((cp = V_GetCodePoint(&s)))
	{
		if(cp == 0x20)
		{
			x_offs += 8;
			continue;
		}
		if(cp == 0x0A)
		{
			y_offs += 12;
			continue;
		}

		glyph_t** plane = font->planes[PLANEOF(cp)];
		if(!plane) continue;

		glyph_t* glyph = plane[CODEOF(cp)];
		if(!glyph) continue;

		V_DrawGlyph((sx + x_offs), (sy + y_offs), glyph);

		x_offs += (glyph->rect.w + 1);
	}
}

// Initialization stuff
void V_InitFonts()
{
	if(fonts) return;

	num_fonts = 0;

	// Allocate font slots
	fonts = malloc(sizeof(font_t*) * MAX_FONTS);
	memset(fonts, 0, sizeof(font_t*) * MAX_FONTS);

	CONS_Printf("V_InitFonts(): %u free slots\n", MAX_FONTS);

	// Load system fonts
	V_LoadFont("MANIAFNT"); // Mania font
//	V_LoadFont("STCFNT");   // Console
//	V_LoadFont("MKFNT");    // SRB2Kart
//	V_LoadFont("LTFNT");    // Level title
//	V_LoadFont("TNYFNT");   // Thin font
//	V_LoadFont("CREDFNT");  // Credits
}

