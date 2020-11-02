// SONIC ROBO BLAST 2 KART
//-----------------------------------------------------------------------------
/// \file  f_fonts.c
/// \brief Unicode fonts and string drawing functions

#include "doomdef.h"
#include "doomstat.h"
#include "console.h"
#include "v_video.h"
#include "f_fonts.h"
#include "w_wad.h"
#include "z_zone.h"

// TODO: split functionality between f_fonts and v_video

static size_t num_fonts = 0;
static font_t** fonts = NULL;

// Read a single codepoint from a UTF-8 string
// Advances the source ptr by the number of bytes read
int F_GetCodepoint(const char** ptr)
{
	int    fcount = 0; // Number of flag bits
	UINT32 cpbits = 0; // Codepoint bits
	UINT32 olmask = 4; // Overlong detector mask

	const char* str = *ptr; // Obtain string
	UINT8 lbyte = *str;     // Copy leading byte
	*ptr = ++str;           // Advance one byte

	// No more than 4 flag bits (lbyte > 11110xxx)
	if(lbyte > 0xF7) return -1;

	// Count flag bits and generate mask
	while(lbyte & 0x80)
	{
		fcount++;
		lbyte <<= 1;
		olmask <<= (fcount+1);
	}

	// Just plain ascii, return
	if(!fcount) return lbyte;

	// A valid leading byte must have 2-4 flag bits
	if(fcount > 1)
	{
		// Save remaining bits
		cpbits = (lbyte >> fcount);

		// Read remaining bytes
		while(--fcount)
		{
			UINT8 b = *(str++);

			// No invalid/truncated sequences
			if((b & 0xC0) ^ 0x80) return -1;

			cpbits = (cpbits << 6) | (b & 0x3F);
		}

		// Check for overlong/invalid codepoint
		if(cpbits <= olmask || cpbits > U_MAX)
			return -1;

		*ptr = str; // Advance bytes
		return cpbits;
	}

	return -1;
}

// Free all data from font_t struct
static void free_fontdata(font_t* font)
{
	size_t i;

	// Free planes LUT
	for(i = 0; i < 17; i++)
		free(font->planes[i]);

	// Free individual glyphs
	for(i = 0; i < font->glyph_count; i++)
		free(font->glyphs[i]);

	// TODO: are patches[n] supposed to be freed too?
	free(font->fontid);
	free(font->fontname);
	free(font->patches);
	free(font->glyphs);
}

static size_t strtolower(char* dst, const char* src)
{
	const char* s = src;
	while(*s) *(dst++) = tolower(*(s++));
	*(dst) = '\0';
	return (s - src);
}

// ============================= FONTINFO PARSING =============================

enum fontinfo_keywords
{
	K_CHAR = 1,  // char
	K_TAG,       // tag
	K_SET,       // set
	K_SCALE,     // scale
	K_GFXLUMP,   // gfxlump
	K_FONTNAME,  // fontname
	K_FONTID,    // fontid
	K_COPYRIGHT, // copyright
	K_SPACING    // spacing
};

static int untok_keyword(char* key)
{
	if(!key) return 0;

	switch(*key)
	{
		case 'c':
			if(!strcmp(key, "char"))      return K_CHAR;
			if(!strcmp(key, "copyright")) return K_COPYRIGHT;
			break;
		case 'f':
			if(!strcmp(key, "fontid"))    return K_FONTID;
			if(!strcmp(key, "fontname"))  return K_FONTNAME;
			break;
		case 'g':
			if(!strcmp(key, "gfxlump")) return K_GFXLUMP;
			break;
		case 's':
			if(!strcmp(key, "set"))     return K_SET;
			if(!strcmp(key, "spacing")) return K_SPACING;
			if(!strcmp(key, "scale"))   return K_SCALE;
			break;
		case 't':
			if(!strcmp(key, "tag")) return K_TAG;
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

	// Check for empty hex notation
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

//static long int untok_int(char* str)
//{
//	long int n;
//	char* s;
//
//	n = strtol(str, s, 10);
//
//	return n;
//}

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

	//for(count = 0; count < list_size; count++)
	while(count < list_size)
	{
		char* tok = get_token(line);
		list[count] = tok;

		if(tok == NULL) break;

		count++;
	}

	return count;
}

#define err_break(...) {errset = 1; snprintf(errmsg, 256, __VA_ARGS__); break;}

static int parse_fontinfo(font_t* font, lumpnum_t lmpnum)
{
	// Read and allocate data
	size_t lmpsize = W_LumpLength(lmpnum);
	char*  lmpdata = malloc(lmpsize+1);
	W_ReadLump(lmpnum, lmpdata);
	*(lmpdata+lmpsize) = '\0';

	// Token list
	char* toklist[16];
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

		// args
		int ntokens = get_token_list(&line, toklist, 16);

		switch(kcode)
		{
//			case K_SET:
//			{
//				break;
//			}
//			case K_FONTID:
//			{
//				break;
//			}
			case K_FONTNAME:
			{
				if(ntokens != 1) err_break("Syntax error! Expected a valid string");

				char* name = untok_string(toklist[0]);
				if(!name) err_break("Expected a valid string argument");

				font->fontname = malloc(strlen(name) + 1);
				strcpy(font->fontname, name);
				break;
			}
			case K_SPACING:
			{
				if(ntokens != 2)
					err_break("Syntax error! Expected %i args, got %d", 2, ntokens);

				int sp_x = strtol(toklist[0], NULL, 10);
				int sp_y = strtol(toklist[1], NULL, 10);

				// TODO: validate
				font->sp_x = sp_x;
				font->sp_y = sp_y;
				break;
			}
			case K_GFXLUMP:
			{
				if(ntokens != 1)
					err_break("Syntax error! Expected a valid lump name");

				char* gfxname = untok_string(toklist[0]);
				if(!gfxname) err_break("Expected a valid lump name");

				lumpnum_t gfxnum = W_CheckNumForName(gfxname);
				if(gfxnum == LUMPERROR)
					err_break("Lump \"%s\" not found", gfxname);

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
				if(ntokens != 8)
					err_break("Syntax error! Expected %i args, got %d", 8, ntokens);

				int codepoint = untok_codepoint(toklist[0]);
				if(codepoint < 0) err_break("Invalid codepoint \"%s\"", toklist[0]);

				int lmpindex = strtol(toklist[1], NULL, 10);
				if(lmpindex >= (int)font->patch_count)
					err_break("No lump matching index '%d'", lmpindex);

				// Allocate glyph and fill data
				glyph_t* glyph = malloc(sizeof(glyph_t));
				memset(glyph, 0, sizeof(glyph_t));

				glyph->code   = codepoint;
				glyph->pgfx   = (lmpindex < 0) ? NULL : font->patches[lmpindex];
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
				// TODO: if outside range, set gfx to null and gen a warn msg
//				else { }

				font->planes[plane_num][code_num] = glyph;
				break;
			}
		}
	}

	free(lmpdata);

	if(errset)
	{
		// TODO: get lump name from num
		CONS_Printf("ERROR: \"%s\", Line %lu: %s\n", "----", line_num, errmsg);
		free_fontdata(font);
		free(errmsg);
		return 0;
	}

	free(errmsg);
	return 1;
}

// ============================================================================

// TODO: reloading fonts: "F_LoadFont(): Reloaded '%s'\n"
int F_LoadFont(const char* lmpname)
{
	if(!fonts) return -1;

	lumpnum_t lmpnum = W_CheckNumForName(lmpname);

	if(lmpnum == LUMPERROR)
	{
		CONS_Printf("F_LoadFont(): \"%s\" not found\n", lmpname);
		return -1;
	}

	// Find first free slot
	// TODO: dynamically allocate new slots
	int slot;
	for(slot = 0;slot < MAX_FONTS;slot++)
		if(fonts[slot] == NULL) break;

	if(slot == (MAX_FONTS-1))
	{
		CONS_Printf("F_LoadFont(): No free slots!\n");
		return -1;
	}

	// Allocate font struct
	font_t* font = malloc(sizeof(font_t));
	memset(font, 0, sizeof(font_t));

	// Parse font data
	if(!parse_fontinfo(font, lmpnum))
	{
		free(font);

		if(WADFILENUM(lmpnum) <= mainwads)
			I_Error("Failed to load system font '%s'", lmpname);
		else
			CONS_Printf("F_LoadFont(): Failed to load \"%s\"\n", lmpname);

		return -1;
	}

	// Set lmpname as ID (if not set previously)
	if(!font->fontid)
	{
		font->fontid = malloc(10);
		strtolower(font->fontid, lmpname);
	}

	fonts[slot] = font;
	num_fonts++;

	CONS_Printf("F_LoadFont(): Loaded '%s' on slot %d\n", lmpname, slot);
	return 0;
}

//glyph_t* F_GetGlyph(font_t* font, int cp, int flags)
glyph_t* F_GetGlyph(font_t* font, int cp)
{
	if(!font) return NULL;

	if(cp < 0 || cp > U_MAX) return NULL;

	glyph_t** plane = font->planes[PLANEOF(cp)];
	if(!plane) return NULL;

	glyph_t* glyph = plane[CODEOF(cp)];
	if(!glyph) return NULL;

	return glyph;
}

font_t* F_GetFont(const char* str)
{
	// Lowercase ID
	char lstr[10];
	strtolower(lstr, str);

	int i;
	for(i = 0;i < (int)num_fonts;i++)
	{
		font_t* font = fonts[i];
		if(!font) continue;

		char* fontid = font->fontid;
		if(!fontid) continue;

		if(!strcmp(lstr, fontid)) return font;
	}
	return NULL;
}

//int F_DrawGlyph(int sx, int sy, fixed_t scale, int flags, glyph_t* glyph)
int F_DrawGlyph(int sx, int sy, int scale, int colormap, glyph_t* glyph)
{
	if(!glyph) return 0;

	int cx = glyph->rect.x;
	int cy = glyph->rect.y;
	int cw = glyph->rect.w;
	int ch = glyph->rect.h;
	int gx = glyph->gx;
	int gy = glyph->gy;

	// Clamp scale factor value
	if(scale < 1) scale = 1;

	patch_t* patch = glyph->pgfx;
	if(patch)
	{
		// TODO: fix this issue on V_DrawCroppedPatch
		cw += cx;
		ch += cy;

		int xpos = ((sx + gx) << FRACBITS);
		int ypos = ((sy + gy) << FRACBITS);
		uint32_t scf = (scale << FRACBITS);
		uint32_t vflags = (V_NOSCALEPATCH | V_NOSCALESTART);
		uint8_t* cmap = V_GetStringColormap(colormap);

		V_DrawCroppedPatch(xpos, ypos, scf, vflags, patch, cmap, cx, cy, cw, ch);
	}

//	return (cw * scale);
	return ((glyph->rect.w + 1) * scale);
}

void F_DrawString(int sx, int sy, int scale, font_t* font, const char* str)
{
	if(!font) return;

	int cmap = 0;
	int x_offs = 0;
	int y_offs = 0;

	uint32_t cp;
	const char* s = str;

	// Clamp scale value
	if(scale < 1) scale = 1;

	while((cp = F_GetCodepoint(&s)))
	{
		// TODO: move this to after the ctrl char check
		glyph_t* glyph = F_GetGlyph(font, cp);
		if(!glyph) glyph = F_GetGlyph(font, 0x20);

		// TODO: replace invalid chars here

		if(cp < 0x20)
		{
			// Colormap range: 0x10 -> 0x1F
			if(cp >= 0x10)
			{
				cmap = (cp & 0x0F) << V_CHARCOLORSHIFT;
			}

			// Linefeed
			else if(cp == 0x0A)
			{
				// TODO: replace rect.h by bbox.h
				y_offs += ((glyph->rect.h + 1) * scale);
				x_offs = 0;
			}

			continue;
		}

		x_offs += F_DrawGlyph((sx + x_offs), (sy + y_offs), scale, cmap, glyph);
	}
}

//void F_DebugCharMap(font_t* font)
//{
//}

// Initialization stuff
void F_InitFonts()
{
	if(fonts) return;

	CONS_Printf("F_InitFonts()...\n");

	num_fonts = 0;

	// Allocate font slots
	fonts = malloc(sizeof(font_t*) * MAX_FONTS);
	memset(fonts, 0, sizeof(font_t*) * MAX_FONTS);

	// Load system fonts
	F_LoadFont("MANFNT"); // Mania font
	F_LoadFont("MKFNT");  // SRB2Kart
	F_LoadFont("LTFNT");  // Level title
	F_LoadFont("TNYFNT"); // Thin font
//	F_LoadFont("STCFNT"); // Console
	F_LoadFont("CRFNT");  // Credits
}

