/**
 * Copyright (c) 2017-2018 Tara Keeling
 *				 2020 Philippe G.
 * 
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <esp_heap_caps.h>
#include <esp_log.h>

#include "gds.h"
#include "gds_private.h"

#define SHADOW_BUFFER
#define USE_IRAM

static char TAG[] = "SSD1306";

struct SSD1306_Private {
	uint8_t *Shadowbuffer;
};

// Functions are not declared to minimize # of lines

static void SetColumnAddress( struct GDS_Device* Device, uint8_t Start, uint8_t End ) {
	Device->WriteCommand( Device, 0x21 );
	Device->WriteCommand( Device, Start );
	Device->WriteCommand( Device, End );
}
static void SetPageAddress( struct GDS_Device* Device, uint8_t Start, uint8_t End ) {
	Device->WriteCommand( Device, 0x22 );	
	Device->WriteCommand( Device, Start );
	Device->WriteCommand( Device, End );
}

static void Update( struct GDS_Device* Device ) {
#ifdef SHADOW_BUFFER
	struct SSD1306_Private *Private = (struct SSD1306_Private*) Device->Private;
	// not sure the compiler does not have to redo all calculation in for loops, so local it is
	int width = Device->Width, rows = Device->Height / 8;
	uint8_t *optr = Private->Shadowbuffer, *iptr = Device->Framebuffer;
	int CurrentRow = -1, FirstCol = -1, LastCol = -1;
	
	// by row, find first and last columns that have been updated
	for (int r = 0; r < rows; r++) {
		uint8_t first = 0, last;	
		for (int c = 0; c < width; c++) {
			if (*iptr != *optr) {
				if (!first) first = c + 1;
				last = c ;
			}	
			*optr++ = *iptr++;
		}
		
		// now update the display by "byte rows"
		if (first--) {
			// only set column when useful, saves a fair bit of CPU
			if (first > FirstCol && first <= FirstCol + 4 && last < LastCol && last >= LastCol - 4) {
				first = FirstCol;
				last = LastCol;
			} else {	
				SetColumnAddress( Device, first, last );
				FirstCol = first;
				LastCol = last;
			}
			
			// Set row only when needed, otherwise let auto-increment work
			if (r != CurrentRow) SetPageAddress( Device, r, Device->Height / 8 - 1 );
			CurrentRow = r + 1;
			
			// actual write
			Device->WriteData( Device, Private->Shadowbuffer + r*width + first, last - first + 1);
		}
	}	
#else	
	// automatic counter and end Page/Column
	SetColumnAddress( Device, 0, Device->Width - 1);
	SetPageAddress( Device, 0, Device->Height / 8 - 1);
	Device->WriteData( Device, Device->Framebuffer, Device->FramebufferSize );
#endif	
}

static void SetHFlip( struct GDS_Device* Device, bool On ) { Device->WriteCommand( Device, On ? 0xA1 : 0xA0 ); }
static void SetVFlip( struct GDS_Device *Device, bool On ) { Device->WriteCommand( Device, On ? 0xC8 : 0xC0 ); }
static void DisplayOn( struct GDS_Device* Device ) { Device->WriteCommand( Device, 0xAF ); }
static void DisplayOff( struct GDS_Device* Device ) { Device->WriteCommand( Device, 0xAE ); }

static void SetContrast( struct GDS_Device* Device, uint8_t Contrast ) {
    Device->WriteCommand( Device, 0x81 );
    Device->WriteCommand( Device, Contrast );
}

static bool Init( struct GDS_Device* Device ) {
	Device->FramebufferSize = ( Device->Width * Device->Height ) / 8;	
	
	// benchmarks showed little gain to have SPI memory already in IRAL vs letting driver copy		
#ifdef SHADOW_BUFFER	
	struct SSD1306_Private *Private = (struct SSD1306_Private*) Device->Private;
	Device->Framebuffer = calloc( 1, Device->FramebufferSize );
    NullCheck( Device->Framebuffer, return false );
#ifdef USE_IRAM
	if (Device->IF == IF_SPI) Private->Shadowbuffer = heap_caps_malloc( Device->FramebufferSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA );
	else 
#endif
	Private->Shadowbuffer = malloc( Device->FramebufferSize );	
	NullCheck( Private->Shadowbuffer, return false );
	memset(Private->Shadowbuffer, 0xFF, Device->FramebufferSize);
#else	// not SHADOW_BUFFER
#ifdef USE_IRAM
	// benchmarks showed little gain to have SPI memory already in IRAL vs letting driver copy
	if (Device->IF == IF_SPI) Device->Framebuffer = heap_caps_calloc( 1, Device->FramebufferSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA );
	else 
#endif
	Device->Framebuffer = calloc( 1, Device->FramebufferSize );
#endif	
		
	// need to be off and disable display RAM
	Device->DisplayOff( Device );
    Device->WriteCommand( Device, 0xA5 );
	
	// charge pump regulator, do direct init
	Device->WriteCommand( Device, 0x8D );
	Device->WriteCommand( Device, 0x14 ); 
			
	// COM pins HW config (alternative:EN if 64, DIS if 32, remap:DIS) - some display might need something different
	Device->WriteCommand( Device, 0xDA );
	Device->WriteCommand( Device, ((Device->Height == 64 ? 1 : 0) << 4) | (0 < 5) );
		
	// MUX Ratio
    Device->WriteCommand( Device, 0xA8 );
    Device->WriteCommand( Device, Device->Height - 1);
	// Display Offset
    Device->WriteCommand( Device, 0xD3 );
    Device->WriteCommand( Device, 0 );
	// Display Start Line
    Device->WriteCommand( Device, 0x40 + 0x00 );
	Device->SetContrast( Device, 0x7F );
	// set flip modes
	Device->SetVFlip( Device, false );
	Device->SetHFlip( Device, false );
	// no Display Inversion
    Device->WriteCommand( Device, 0xA6 );
	// set Clocks
    Device->WriteCommand( Device, 0xD5 );
    Device->WriteCommand( Device, ( 0x08 << 4 ) | 0x00 );
	// set Adressing Mode Horizontal
	Device->WriteCommand( Device, 0x20 );
	Device->WriteCommand( Device, 0 );
	
	// gone with the wind
	Device->WriteCommand( Device, 0xA4 );
	Device->DisplayOn( Device );
	Device->Update( Device );
	
	return true;
}	

static const struct GDS_Device SSD1306 = {
	.DisplayOn = DisplayOn, .DisplayOff = DisplayOff, .SetContrast = SetContrast,
	.SetVFlip = SetVFlip, .SetHFlip = SetHFlip,
	.Update = Update, .Init = Init,
	//.DrawPixelFast = GDS_DrawPixelFast,
	//.ClearWindow = ClearWindow,
};	

struct GDS_Device* SSD1306_Detect(char *Driver, struct GDS_Device* Device) {
	if (!strcasestr(Driver, "SSD1306")) return NULL;
	
	if (!Device) Device = calloc(1, sizeof(struct GDS_Device));
	*Device = SSD1306;	
	Device->Depth = 1;
	ESP_LOGI(TAG, "SSD1306 driver");
	
	return Device;
}