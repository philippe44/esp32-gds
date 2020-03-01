#ifndef _GDS_PRIVATE_H_
#define _GDS_PRIVATE_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_attr.h"
#include "gds.h"
#include "gds_err.h"

#define GDS_ALLOC_NONE		0x80
#define GDS_ALLOC_IRAM		0x01
#define GDS_ALLOC_IRAM_SPI	0x02

#define GDS_CLIPDEBUG_NONE 0
#define GDS_CLIPDEBUG_WARNING 1
#define GDS_CLIPDEBUG_ERROR 2

#if CONFIG_GDS_CLIPDEBUG == GDS_CLIPDEBUG_NONE
    /*
     * Clip silently with no console output.
     */
    #define ClipDebug( x, y )
#elif CONFIG_GDS_CLIPDEBUG == GDS_CLIPDEBUG_WARNING
    /*
     * Log clipping to the console as a warning.
     */
    #define ClipDebug( x, y ) { \
        ESP_LOGW( __FUNCTION__, "Line %d: Pixel at %d, %d CLIPPED", __LINE__, x, y ); \
    }
#elif CONFIG_GDS_CLIPDEBUG == GDS_CLIPDEBUG_ERROR
    /*
     * Log clipping as an error to the console.
     * Also invokes an abort with stack trace.
     */
    #define ClipDebug( x, y ) { \
        ESP_LOGE( __FUNCTION__, "Line %d: Pixel at %d, %d CLIPPED, ABORT", __LINE__, x, y ); \
        abort( ); \
    }
#endif


#define GDS_ALWAYS_INLINE __attribute__( ( always_inline ) )

#define MAX_LINES	8

#if ! defined BIT
#define BIT( n ) ( 1 << ( n ) )
#endif

struct GDS_Device;
struct GDS_FontDef;

/*
 * These can optionally return a succeed/fail but are as of yet unused in the driver.
 */
typedef bool ( *WriteCommandProc ) ( struct GDS_Device* Device, uint8_t Command );
typedef bool ( *WriteDataProc ) ( struct GDS_Device* Device, const uint8_t* Data, size_t DataLength );

struct spi_device_t;
typedef struct spi_device_t* spi_device_handle_t;

#define GDS_IF_SPI	0
#define GDS_IF_I2C	1

struct GDS_Device {
	uint8_t IF;
	union {
		// I2C Specific
		struct {
			uint8_t Address;
		};
		// SPI specific
		struct {
			spi_device_handle_t SPIHandle;
			int8_t RSTPin;
			int8_t CSPin;
		};
	};	

    // cooked text mode
	struct {
		int16_t Y, Space;
		const struct GDS_FontDef* Font;
	} Lines[MAX_LINES];
	
	uint16_t Width;
    uint16_t Height;
	uint8_t Depth;
	
	uint8_t	Alloc;	
	uint8_t* Framebuffer;
    uint16_t FramebufferSize;
	bool Dirty;

	// default fonts when using direct draw	
	const struct GDS_FontDef* Font;
    bool FontForceProportional;
    bool FontForceMonospace;

	// various driver-specific method
	// must always provide 
	bool (*Init)( struct GDS_Device* Device);
	void (*Update)( struct GDS_Device* Device );
	// may provide if supported
	void (*SetContrast)( struct GDS_Device* Device, uint8_t Contrast );
	void (*DisplayOn)( struct GDS_Device* Device );
	void (*DisplayOff)( struct GDS_Device* Device );
	void (*SetHFlip)( struct GDS_Device* Device, bool On );
	void (*SetVFlip)( struct GDS_Device* Device, bool On );
	// must provide for depth other than 1 (vertical) and 4 (may provide for optimization)
	void (*DrawPixelFast)( struct GDS_Device* Device, int X, int Y, int Color );
	void (*DrawBitmapCBR)(struct GDS_Device* Device, uint8_t *Data, int Width, int Height, int Color );
	// may provide for optimization
	void (*DrawRGB16)( struct GDS_Device* Device, int x, int y, int Width, int Height, int RGB_Mode, uint16_t *Image );
	void (*ClearWindow)( struct GDS_Device* Device, int x1, int y1, int x2, int y2, int Color );
		    
	// interface-specific methods	
    WriteCommandProc WriteCommand;
    WriteDataProc WriteData;

	// 16 bytes for whatever the driver wants (should be aligned as it's 32 bits)	
	uint32_t Private[4];
};

bool GDS_Reset( struct GDS_Device* Device );
bool GDS_Init( struct GDS_Device* Device );

inline bool IsPixelVisible( struct GDS_Device* Device, int x, int y )  {
    bool Result = (
        ( x >= 0 ) &&
        ( x < Device->Width ) &&
        ( y >= 0 ) &&
        ( y < Device->Height )
    ) ? true : false;

#if CONFIG_GDS_CLIPDEBUG > 0
    if ( Result == false ) {
        ClipDebug( x, y );
    }
#endif

    return Result;
}

inline void IRAM_ATTR GDS_DrawPixel1Fast( struct GDS_Device* Device, int X, int Y, int Color ) {
    uint32_t YBit = ( Y & 0x07 );
    uint8_t* FBOffset = NULL;

    /* 
     * We only need to modify the Y coordinate since the pitch
     * of the screen is the same as the width.
     * Dividing Y by 8 gives us which row the pixel is in but not
     * the bit position.
     */
    Y>>= 3;

    FBOffset = Device->Framebuffer + ( ( Y * Device->Width ) + X );

    if ( Color == GDS_COLOR_XOR ) {
        *FBOffset ^= BIT( YBit );
    } else {
        *FBOffset = ( Color == GDS_COLOR_BLACK ) ? *FBOffset & ~BIT( YBit ) : *FBOffset | BIT( YBit );
    }
}

inline void IRAM_ATTR GDS_DrawPixel4Fast( struct GDS_Device* Device, int X, int Y, int Color ) {
	uint8_t* FBOffset;

    FBOffset = Device->Framebuffer + ( (Y * Device->Width >> 1) + (X >> 1));
	*FBOffset = X & 0x01 ? (*FBOffset & 0x0f) | ((Color  & 0x0f) << 4) : ((*FBOffset & 0xf0) | (Color & 0x0f));
}

inline void IRAM_ATTR GDS_DrawPixelFast( struct GDS_Device* Device, int X, int Y, int Color ) {
    if (Device->DrawPixelFast) Device->DrawPixelFast( Device, X, Y, Color );
	else if (Device->Depth == 4) GDS_DrawPixel4Fast( Device, X, Y, Color);
	else if (Device->Depth == 1) GDS_DrawPixel1Fast( Device, X, Y, Color);
}	

inline void IRAM_ATTR GDS_DrawPixel( struct GDS_Device* Device, int x, int y, int Color ) {
    if ( IsPixelVisible( Device, x, y ) == true ) {
        GDS_DrawPixelFast( Device, x, y, Color );
    }
}

#endif