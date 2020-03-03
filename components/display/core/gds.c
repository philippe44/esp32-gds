/**
 * Copyright (c) 2017-2018 Tara Keeling
 *				 2020 Philippe G. 
 * 
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "gds.h"
#include "gds_private.h"

static struct GDS_Device Display;

static char TAG[] = "gds";

struct GDS_Device* GDS_AutoDetect( char *Driver, GDS_DetectFunc* DetectFunc[] ) {
	for (int i = 0; DetectFunc[i]; i++) {
		if (DetectFunc[i](Driver, &Display)) {
			ESP_LOGD(TAG, "Detected driver %p", &Display);
			return &Display;
		}	
	}
	
	return NULL;
}

void GDS_ClearExt(struct GDS_Device* Device, bool full, ...) {
	bool commit = true;
	
	if (full) {
		GDS_Clear( Device, GDS_COLOR_BLACK ); 
	} else {
		va_list args;
		va_start(args, full);
		commit = va_arg(args, int);
		int x1 = va_arg(args, int), y1 = va_arg(args, int), x2 = va_arg(args, int), y2 = va_arg(args, int);
		if (x2 < 0) x2 = Device->Width - 1;
		if (y2 < 0) y2 = Device->Height - 1;
		GDS_ClearWindow( Device, x1, y1, x2, y2, GDS_COLOR_BLACK );
		va_end(args);
	}
	
	Device->Dirty = true;
	if (commit)	GDS_Update(Device);		
}	

void GDS_Clear( struct GDS_Device* Device, int Color ) {
	if (Device->Depth == 1) Color = Color == GDS_COLOR_BLACK ? 0 : 0xff;
	else if (Device->Depth == 4) Color = Color | (Color << 4);
    memset( Device->Framebuffer, Color, Device->FramebufferSize );
	Device->Dirty = true;
}

void GDS_ClearWindow( struct GDS_Device* Device, int x1, int y1, int x2, int y2, int Color ) {
	// driver can provide own optimized clear window
	if (Device->ClearWindow) {
		Device->ClearWindow( Device, x1, y1, x2, y2, Color );
	} else if (Device->Depth == 1) {
		// single shot if we erase all screen
		if (x2 - x1 == Device->Width - 1 && y2 - y1 == Device->Height - 1) {
			memset( Device->Framebuffer, Color == GDS_COLOR_BLACK ? 0 : 0xff, Device->FramebufferSize );
		} else {
			uint8_t _Color = Color == GDS_COLOR_BLACK ? 0: 0xff;
			uint8_t Width = Device->Width >> 3;
			uint8_t *optr = Device->Framebuffer;
			// try to do byte processing as much as possible
			for (int r = y1; r <= y2;) {
				int c = x1;
				// for a row that is not on a boundary, no optimization possible
				while (r & 0x07 && r <= y2) {
					for (c = x1; c <= x2; c++) GDS_DrawPixelFast( Device, c, r, Color );
					r++;
				}
				// go fast if we have more than 8 lines to write
				if (r + 8 <= y2) {
					memset(optr + Width * r + x1, _Color, x2 - x1 + 1);
					r += 8;
				} else while (r <= y2) {
					for (c = x1; c <= x2; c++) GDS_DrawPixelFast( Device, c, r, Color );
					r++;
				}
			}
		}
	} if (Device->Depth == 4) {
		if (x2 - x1 == Device->Width - 1 && y2 - y1 == Device->Height - 1) {
			// we assume color is 0..15
			memset( Device->Framebuffer, Color | (Color << 4), Device->FramebufferSize );
		} else {
			uint8_t _Color = Color | (Color << 4);
			int Width = Device->Width;
			uint8_t *optr = Device->Framebuffer;
			// try to do byte processing as much as possible
			for (int r = y1; r <= y2; r++) {
				int c = x1;
				if (c & 0x01) GDS_DrawPixelFast( Device, c++, r, Color);
				int chunk = (x2 - c + 1) >> 1;
				memset(optr + ((r * Width + c)  >> 1), _Color, chunk);
				if (c + chunk <= x2) GDS_DrawPixelFast( Device, x2, r, Color);
			}
		}	
	} else {
		for (int y = y1; y <= y2; y++) {
			for (int x = x1; x <= x2; x++) {
				GDS_DrawPixelFast( Device, x, y, Color);
			}
		}
	}
	
	// make sure diplay will do update
	Device->Dirty = true;
}

void GDS_Update( struct GDS_Device* Device ) {
	if (Device->Dirty) Device->Update( Device );
	Device->Dirty = false;
}

bool GDS_Reset( struct GDS_Device* Device ) {
	if ( Device->RSTPin >= 0 ) {
		gpio_set_level( Device->RSTPin, 0 );
		vTaskDelay( pdMS_TO_TICKS( 100 ) );
        gpio_set_level( Device->RSTPin, 1 );
    }
    return true;
}

bool GDS_Init( struct GDS_Device* Device ) {
	
	Device->FramebufferSize = (Device->Width * Device->Height) / (8 / Device->Depth);
	
	// allocate FB unless explicitely asked not to
	if (!(Device->Alloc & GDS_ALLOC_NONE)) {
		if ((Device->Alloc & GDS_ALLOC_IRAM) || ((Device->Alloc & GDS_ALLOC_IRAM_SPI) && Device->IF == GDS_IF_SPI)) {
			heap_caps_calloc( 1, Device->FramebufferSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA );
		} else {
			Device->Framebuffer = calloc( 1, Device->FramebufferSize );
		}	
		NullCheck( Device->Framebuffer, return false );
	}	
	
	bool Res = Device->Init( Device );
	if (!Res) free(Device->Framebuffer);
	return Res;
}

void GDS_SetContrast( struct GDS_Device* Device, uint8_t Contrast ) { if (Device->SetContrast) Device->SetContrast( Device, Contrast); }
void GDS_SetHFlip( struct GDS_Device* Device, bool On ) { if (Device->SetHFlip) Device->SetHFlip( Device, On ); }
void GDS_SetVFlip( struct GDS_Device* Device, bool On ) { if (Device->SetVFlip) Device->SetVFlip( Device, On ); }
int	GDS_GetWidth( struct GDS_Device* Device ) { return Device->Width; }
int	GDS_GetHeight( struct GDS_Device* Device ) { return Device->Height; }
void GDS_DisplayOn( struct GDS_Device* Device ) { if (Device->DisplayOn) Device->DisplayOn( Device ); }
void GDS_DisplayOff( struct GDS_Device* Device ) { if (Device->DisplayOff) Device->DisplayOff( Device ); }