#ifndef _GDS_H_
#define _GDS_H_

#include <stdint.h>
#include <stdbool.h>

/* NOTE for drivers:
 The build-in DrawPixel(Fast), DrawCBR and ClearWindow are optimized for 1 bit 
 and 4 bits screen depth. For any other type of screen, DrawCBR and ClearWindow
 default to use DrawPixel, which is very sub-optimal. For such other depth, you 
 must supply the DrawPixelFast. The built-in 1 bit depth function are only for 
 screen with vertical framing (1 byte = 8 lines). For example SSD1326 in 
 monochrome mode is not such type of screen, SH1106 and SSD1306 are
*/ 

enum { 	GDS_COLOR_L0 = 0, GDS_COLOR_L1 = 1, GDS_COLOR_L2, GDS_COLOR_L3, GDS_COLOR_L4, GDS_COLOR_L5, GDS_COLOR_L6, GDS_COLOR_L7, 
		GDS_COLOR_L8, GDS_COLOR_L9, GDS_COLOR_L10, GDS_COLOR_L11, GDS_COLOR_L12, GDS_COLOR_L13, GDS_COLOR_L14, GDS_COLOR_L15,
		GDS_COLOR_MAX
};
		
#define GDS_COLOR_BLACK GDS_COLOR_L0
#define GDS_COLOR_WHITE GDS_COLOR_L1
#define GDS_COLOR_XOR 	(GDS_COLOR_MAX + 1)

struct GDS_Device;
struct GDS_FontDef;

typedef struct GDS_Device* GDS_DetectFunc(char *Driver, struct GDS_Device *Device);

struct GDS_Device*	GDS_AutoDetect( char *Driver, GDS_DetectFunc* DetectFunc[] );

void 	GDS_SetContrast( struct GDS_Device* Device, uint8_t Contrast );
void 	GDS_DisplayOn( struct GDS_Device* Device );
void 	GDS_DisplayOff( struct GDS_Device* Device ); 
void 	GDS_Update( struct GDS_Device* Device );
void 	GDS_SetHFlip( struct GDS_Device* Device, bool On );
void 	GDS_SetVFlip( struct GDS_Device* Device, bool On );
int 	GDS_GetWidth( struct GDS_Device* Device );
int 	GDS_GetHeight( struct GDS_Device* Device );
void 	GDS_ClearExt( struct GDS_Device* Device, bool full, ...);
void 	GDS_Clear( struct GDS_Device* Device, int Color );
void 	GDS_ClearWindow( struct GDS_Device* Device, int x1, int y1, int x2, int y2, int Color );

#endif
