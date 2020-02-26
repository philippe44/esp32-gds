#ifndef _GDS_FONT_H_
#define _GDS_FONT_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct GDS_Device;

/* 
 * X-GLCD Font format:
 *
 * First byte of glyph is it's width in pixels.
 * Each data byte represents 8 pixels going down from top to bottom.
 * 
 * Example glyph layout for a 16x16 font
 * 'a': [Glyph width][Pixel column 0][Pixel column 1] where the number of pixel columns is the font height divided by 8
 * 'b': [Glyph width][Pixel column 0][Pixel column 1]...
 * 'c': And so on...
 */
 
struct GDS_FontDef {
    const uint8_t* FontData;

    int Width;
    int Height;

    int StartChar;
    int EndChar;

    bool Monospace;
};

typedef enum {
    TextAnchor_East = 0,
    TextAnchor_West,
    TextAnchor_North,
    TextAnchor_South,
    TextAnchor_NorthEast,
    TextAnchor_NorthWest,
    TextAnchor_SouthEast,
    TextAnchor_SouthWest,
    TextAnchor_Center
} TextAnchor;

bool GDS_SetFont( struct GDS_Device* Display, const struct GDS_FontDef* Font );

void GDS_FontForceProportional( struct GDS_Device* Display, bool Force );
void GDS_FontForceMonospace( struct GDS_Device* Display, bool Force );

int GDS_FontGetWidth( struct GDS_Device* Display );
int GDS_FontGetHeight( struct GDS_Device* Display );

int GDS_FontGetMaxCharsPerRow( struct GDS_Device* Display );
int GDS_FontGetMaxCharsPerColumn( struct GDS_Device* Display );

int GDS_FontGetCharWidth( struct GDS_Device* Display, char Character );
int GDS_FontGetCharHeight( struct GDS_Device* Display );
int GDS_FontMeasureString( struct GDS_Device* Display, const char* Text );\

void GDS_FontDrawChar( struct GDS_Device* Display, char Character, int x, int y, int Color );
void GDS_FontDrawString( struct GDS_Device* Display, int x, int y, const char* Text, int Color );
void GDS_FontDrawAnchoredString( struct GDS_Device* Display, TextAnchor Anchor, const char* Text, int Color );
void GDS_FontGetAnchoredStringCoords( struct GDS_Device* Display, int* OutX, int* OutY, TextAnchor Anchor, const char* Text );

extern const struct GDS_FontDef Font_droid_sans_fallback_11x13;
extern const struct GDS_FontDef Font_droid_sans_fallback_15x17;
extern const struct GDS_FontDef Font_droid_sans_fallback_24x28;

extern const struct GDS_FontDef Font_droid_sans_mono_7x13;
extern const struct GDS_FontDef Font_droid_sans_mono_13x24;
extern const struct GDS_FontDef Font_droid_sans_mono_16x31;

extern const struct GDS_FontDef Font_liberation_mono_9x15;
extern const struct GDS_FontDef Font_liberation_mono_13x21;
extern const struct GDS_FontDef Font_liberation_mono_17x30;

extern const struct GDS_FontDef Font_Tarable7Seg_16x32;
extern const struct GDS_FontDef Font_Tarable7Seg_32x64;

extern const struct GDS_FontDef Font_line_1;
extern const struct GDS_FontDef Font_line_2;

#ifdef __cplusplus
}
#endif

#endif
