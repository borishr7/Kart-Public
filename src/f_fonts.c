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

// Just like strcpy but converts to lowercase
static size_t strtolower(char* dst, const char* src)
{
	const char* s = src;
	while(*s) *(dst++) = tolower(*(s++));
	*(dst) = '\0';
	return (s - src);
}

// ============================= FONTINFO PARSING =============================

// Parsing state
typedef struct fp_state
{
	font_t*   font;
	lumpnum_t lumpnum;

	int    errset;
	size_t line_num;

	char*  line;
	char*  nextline;

	// Argument list
	size_t numargs;
	char*  arglist[16];

	// Checklist
	int has_defchar;
	int has_spacing;
	int has_spwidth;
	int has_lnheight;

	// Modifiers
	size_t bbox_w; // Monospace bbox width
	size_t bbox_h; // Monospace bbox height
	int yoffs_ena; // baseline offset enablec
	int defchar;   // Default replacement char
} fp_state_t;

// Free all data from font_t struct
static void free_fontdata(font_t* font)
{
	size_t i;

	// Free planes LUT
	for(i = 0; i < 17; i++)
		free(font->planes[i]);

	// Free individual glyphs
	for(i = 0; i < font->num_glyphs; i++)
		free(font->glyphs[i]);

	// TODO: are patches[n] supposed to be freed too?
	free(font->patches);
	free(font->glyphs);
}

static void parser_error(fp_state_t* ps, const char* msg)
{
	const char* lumpname = W_CheckNameForNum(ps->lumpnum);
	CONS_Printf("ERROR: '%s', Line %lu: %s\n", lumpname, ps->line_num, msg);

	ps->errset = 1;
} 

static int untok_keyword(char* key)
{
	switch(*key)
	{
		case 'a':
			if(!strcmp(key, "addchar")) return K_CHAR;
			break;
		case 'b':
			if(!strcmp(key, "bbox"))        return K_BBOX;
			if(!strcmp(key, "boundingbox")) return K_BBOX;
			break;
		case 'c':
			if(!strcmp(key, "char")) return K_CHAR;
			break;
		case 'd':
			if(!strcmp(key, "defchar"))     return K_DEFCHR;
			if(!strcmp(key, "defaultchar")) return K_DEFCHR;
			if(!strcmp(key, "defspacing"))  return K_DEFSP;
			break;
		case 'l':
			if(!strcmp(key, "loadgfx"))   return K_LOAD;
			if(!strcmp(key, "lineheight")) return K_LINE;
			break;
		case 's':
			if(!strcmp(key, "set"))        return K_SET;
			if(!strcmp(key, "spacewidth")) return K_SPACE;
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
			if(*s == '\0') break;	 // if trailing quote
			if(*s != c) return NULL; // if bad escape sequence

			s++;
		}
		*(t++) = c;
	}

	*t = '\0';
	return str;
}

static long int untok_int(char* str)
{
	long int n;
	char* s;

	n = strtol(str, &s, 10);

//	if(*s) return 0;

	return n;
}

// Simple whitespace-delimited tokenizer.
// Returns the ptr to the first individual token found.
// Advances the source ptr to the start of the next token.
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
			// Just in case...
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

// Fill a list with tokens from a single line
// Returns the number of tokens read (< list_size)
static size_t get_token_list(char** line, char** list, size_t list_size)
{
	size_t n;
	size_t count;
	for(n = 0, count = 0; n < list_size; n++)
	{
		char* tok = get_token(line);
		list[n] = tok;

		if(tok != NULL) count++;
	}

	return count;
}

static void parser_loadgfx(fp_state_t* ps)
{
	char* gfxname = untok_string(ps->arglist[0]);
	if(ps->numargs != 1 || !gfxname)
	{
		parser_error(ps, "Expected a valid lump name");
		return;
	}

	lumpnum_t gfxnum = W_CheckNumForName(gfxname);
	if(gfxnum == LUMPERROR)
	{
		parser_error(ps, va("Lump '%s' not found", gfxname));
		return;
	}

	font_t* font = ps->font;
	patch_t* gfx = W_CachePatchNum(gfxnum, PU_HUDGFX);

	// Resize lump list
	size_t new_count = (font->num_patches + 1);
	size_t new_size  = (new_count * sizeof(patch_t*));
	font->patches = realloc(font->patches, new_size);

	// Write to list
	font->num_patches = new_count;
	font->patches[new_count-1] = gfx;
}

static void parser_addchar(fp_state_t* ps)
{
	font_t* font  = ps->font;
	int codepoint = untok_codepoint(ps->arglist[0]);

	if(codepoint < 0x21)
	{
		parser_error(ps, va("Invalid codepoint \"%s\"", ps->arglist[0]));
		return;
	}

	int lmpindex = untok_int(ps->arglist[1]);

	if(lmpindex >= (int)font->num_patches || lmpindex < 0)
	{
		parser_error(ps, va("Invalid lump index '%d'", lmpindex));
		return;
	}

	// Allocate glyph and fill data
	glyph_t* glyph = malloc(sizeof(glyph_t));
	memset(glyph, 0, sizeof(glyph_t));

	glyph->code   = codepoint;
	glyph->pgfx   = font->patches[lmpindex];
	glyph->rect.x = untok_int(ps->arglist[2]);
	glyph->rect.y = untok_int(ps->arglist[3]);
	glyph->rect.w = untok_int(ps->arglist[4]);
	glyph->rect.h = untok_int(ps->arglist[5]);
	glyph->yoffs  = untok_int(ps->arglist[6]);
	glyph->bbox.w = ps->bbox_w;
	glyph->bbox.h = ps->bbox_h;

	// Resize glyph list and write to it
	size_t new_count = (font->num_glyphs + 1);
	size_t new_size  = (new_count * sizeof(glyph_t*));

	font->glyphs = realloc(font->glyphs, new_size);
	font->num_glyphs = new_count;
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
	// TODO: if outside gfx rect, ignore glyph and gen a warning
//	else {}

	font->planes[plane_num][code_num] = glyph;
}

static void parse_line(fp_state_t* ps)
{
	char* cmd = get_token(&ps->line);
	if(!cmd) return;

	ps->numargs = get_token_list(&ps->line, ps->arglist, 16);

	switch(untok_keyword(cmd))
	{
		case 0:
		{
			parser_error(ps, va("Invalid keyword \"%s\"", cmd));
			break;
		}
		case K_BBOX:
		{
			if(ps->numargs != 2)
			{
				parser_error(ps, "Syntax error!");
				break;
			}

			long int w = untok_int(ps->arglist[0]);
			long int h = untok_int(ps->arglist[1]);

			if(w < 0 || h < 0)
			{
				parser_error(ps, "bbox values can't be negative");
			}

			ps->bbox_w = (size_t)w;
			ps->bbox_h = (size_t)h;
			break;
		}
		case K_DEFCHR:
		{
			if(ps->numargs != 1)
			{
				parser_error(ps, "Syntax error!");
				break;
			}

			int cp = untok_codepoint(ps->arglist[0]);
			if(cp < 0)
			{
				parser_error(ps, "Invalid codepoint");
				break;
			}

			printf("defchar: U+%04lX\n", (size_t)cp);
			break;
		}
		case K_SPACE:
		{
			if(ps->numargs != 1)
			{
				parser_error(ps, "Syntax error!");
				break;
			}
			ps->font->sp_width = untok_int(ps->arglist[0]);
			ps->has_spwidth = 1;
			break;
		}
		case K_LINE:
		{
			if(ps->numargs != 1)
			{
				parser_error(ps, "Syntax error!");
				break;
			}
			ps->font->ln_height = untok_int(ps->arglist[0]);
			ps->has_lnheight = 1;
			break;
		}
		case K_LOAD:
		{
			parser_loadgfx(ps);
			break;
		}
		case K_CHAR:
		{
			parser_addchar(ps);
			break;
		}
	}
}

static int parse_fontinfo(font_t* font, lumpnum_t lumpnum)
{
	// Read and allocate data
	size_t lumpsize = W_LumpLength(lumpnum);
	char*  lumpdata = malloc(lumpsize+1);
	W_ReadLump(lumpnum, lumpdata);
	*(lumpdata+lumpsize) = '\0';

	// Local parsing state
	fp_state_t ps;
	memset(&ps, 0, sizeof(ps));

	ps.font     = font;
	ps.lumpnum  = lumpnum;
	ps.nextline = lumpdata;

	while(!ps.errset && (ps.line = ps.nextline))
	{
		ps.line_num++;

		// Split line (remove LF)
		ps.nextline = strchr(ps.line, '\n');
		if(ps.nextline) *(ps.nextline++) = '\0';

		parse_line(&ps);
	}

	free(lumpdata);
	return ps.errset;
}

// ============================================================================

// TODO: dynamically allocate new slots
int F_LoadFont(const char* lumpname)
{
	lumpnum_t lumpnum = W_CheckNumForName(lumpname);

	if(lumpnum == LUMPERROR)
	{
		CONS_Printf("F_LoadFont(): \"%s\" not found\n", lumpname);
		return -1;
	}

	// Find first free slot
	int slot;
	for(slot = 0;slot < 16;slot++)
		if(fonts[slot] == NULL) break;

	if(slot == 15)
	{
		CONS_Printf("F_LoadFont(): No free slots!\n");
		return -1;
	}

	// Allocate font and set ID
	font_t* font = malloc(sizeof(font_t));
	memset(font, 0, sizeof(font_t));
	strtolower(font->fontid, lumpname);

	if(parse_fontinfo(font, lumpnum))
	{
		free_fontdata(font);
		free(font);

		if(WADFILENUM(lumpnum) <= mainwads)
			I_Error("Failed to load system font '%s'", lumpname);
		else
			CONS_Printf("F_LoadFont(): Failed to load \"%s\"\n", lumpname);

		return -1;
	}

	fonts[slot] = font;
	num_fonts++;

	CONS_Printf("F_LoadFont(): Loaded '%s' on slot %d\n", lumpname, slot);
	return 0;
}

uint64_t F_GetScreenRect()
{
	rect16_t s = {{0, 0, (uint16_t)vid.width, (uint16_t)vid.height}};
	return s.rect;
}

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
	char lstr[16];
	strtolower(lstr, str);

	int i;
	for(i = 0;i < (int)num_fonts;i++)
	{
		font_t* font = fonts[i];
		if(!font) continue;
		if(!strcmp(lstr, font->fontid)) return font;
	}
	return NULL;
}

void F_DrawGlyph(fixed_t x, fixed_t y, fixed_t scale, uint32_t flags, glyph_t* glyph)
{
	if(!glyph) return;

	if(flags & F_DEBUG)
		V_DrawFill((x >> FRACBITS), (y >> FRACBITS), (glyph->rect.w), (glyph->rect.h), 184);

	patch_t* patch = glyph->pgfx;
	if(patch)
	{
		int cx = glyph->rect.x;
		int cy = glyph->rect.y;
		int cw = glyph->rect.w;
		int ch = glyph->rect.h;
		uint8_t* color = V_GetStringColormap(flags & V_CHARCOLORMASK);

		// TODO: fix w/h issues on V_DrawCroppedPatch
		V_DrawCroppedPatch(x, y, scale, flags, patch, color, cx, cy, (cw+cx), (ch+cy));
	}
}

void F_DrawString(int x, int y, float scale, uint32_t flags, font_t* font, const char* str)
{
	if(!font) return;

	// Clamp scale value
	if(scale < 0) scale = 0;
	else if(scale > 32768) scale = 32768;

	fixed_t fx = (x << FRACBITS); // Fixed point X start (top left edge)
	fixed_t fy = (y << FRACBITS); // Fixed point Y start (top left edge)
	fixed_t fs = FLOAT_TO_FIXED(scale); // Fixed point scale factor (0 to 32768)

	fixed_t sp = (font->sp_width  << FRACBITS);
	fixed_t ln = (font->ln_height << FRACBITS);

	uint32_t allowedflags = (
		F_FONTMASK |
		V_CHARCOLORMASK |
		V_ALPHAMASK |
		V_SNAPTOTOP |
		V_SNAPTOBOTTOM |
		V_SNAPTOLEFT |
		V_SNAPTORIGHT |
		V_NOSCALESTART );

	// Filter off unsupported flags
	flags = (flags & allowedflags);

	if(!(flags & F_DOOMSCALE))
		flags |= (V_NOSCALEPATCH|V_NOSCALESTART);

	uint32_t cp;
	const char* s = str;

	while((cp = F_GetCodepoint(&s)))
	{
		// Control chars (not ansi compliant, lol)
		// do not change the order of each check
		if(cp < 0x21)
		{
			// Space
			if(cp == 0x20)
			{
				fx += FixedMul(sp, fs);
				continue;
			}

			// Colormap range: 0x10 -> 0x1F
			if(cp >= 0x10)
			{
				flags |= (cp & 0x0F) << V_CHARCOLORSHIFT;
				continue;
			}

			// Linefeed
			if(cp == 0x0A)
			{
				fy += FixedMul(ln, fs);
				fx = (x << FRACBITS);
				continue;
			}

			// Horizontal tab
//			if(cp == 0x09) {}

			// Scale increment/decrement
//			if(cp == 0x0E) {}

			// Arbitrary colormap
//			if(cp == 0x0F) {}

			continue;
		}

		glyph_t* glyph = F_GetGlyph(font, cp);

		if(!glyph) continue;

		fixed_t gx = fx;
		fixed_t gy = fy;
		fixed_t gw = (glyph->rect.w << FRACBITS); // Glyph width
		fixed_t bw = (glyph->bbox.w << FRACBITS); // Bbox width
		fixed_t bo = (glyph->yoffs  << FRACBITS); // Baseline offset

		// Debug boundingbox
		if(flags & F_DEBUG)
			V_DrawFill((fx >> FRACBITS), (fy >> FRACBITS), (glyph->bbox.w), (glyph->bbox.h), 193);

		if(flags & F_MONOSPACE)
		{
			// Align rect to center of bbox
			gx += FixedDiv((bw - gw), (2 << FRACBITS)); // abs(bw-gw)
			gy += FixedMul(bo, fs);
			fx += FixedMul(bw, fs);
		}
		else
		{
			gy += FixedMul(bo, fs);
			fx += FixedMul(gw, fs);
		}

		F_DrawGlyph(gx, gy, fs, flags, glyph);
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
	fonts = malloc(sizeof(font_t*) * 16);
	memset(fonts, 0, sizeof(font_t*) * 16);

	// Load system fonts
	F_LoadFont("MANFNT"); // Mania font
	F_LoadFont("MKFNT");  // SRB2Kart
	F_LoadFont("LTFNT");  // Level title
	F_LoadFont("TNYFNT"); // Thin font
//	F_LoadFont("STCFNT"); // Console
	F_LoadFont("CRFNT");  // Credits
}

