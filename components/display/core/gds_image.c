/* 
 * (c) Philippe G. 2019, philippe_44@outlook.com
 *
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 * 
 */
 
#include <string.h>
#include "math.h"
#include "esp32/rom/tjpgd.h"
#include "esp_log.h"

#include "gds.h"
#include "gds_private.h"
#include "gds_image.h"

const char TAG[] = "ImageDec";

#define SCRATCH_SIZE	3100

//Data that is passed from the decoder function to the infunc/outfunc functions.
typedef struct {
    const unsigned char *InData;	// Pointer to jpeg data
    int InPos;						// Current position in jpeg data
	int Width, Height;	
	uint8_t Mode;
	int (*Scaler)(uint8_t *Pixel);
	union {
		void *OutData;
		struct {						// DirectDraw
			struct GDS_Device * Device;
			int XOfs, YOfs;
			int XMin, YMin;
			int Depth;
		};	
	};	
} JpegCtx;

static inline int Scaler332(uint8_t *Pixels) {
	return (Pixels[0] & ~0x1f) | ((Pixels[1] & ~0x1f) >> 3) | (Pixels[2] >> 6);
}

static inline int Scaler444(uint8_t *Pixels) {
	return ((Pixels[0] & ~0x0f) << 4) | (Pixels[1] & ~0x0f) | (Pixels[2] >> 4);
}

static inline int Scaler555(uint8_t *Pixels) {
	return ((Pixels[0] & ~0x07) << 7) | ((Pixels[1] & ~0x07) << 2) | (Pixels[2] >> 3);
}

static inline int Scaler565(uint8_t *Pixels) {
	return ((Pixels[0] & ~0x07) << 8) | ((Pixels[1] & ~0x03) << 3) | (Pixels[2] >> 3);
}

static inline int Scaler666(uint8_t *Pixels) {
	return ((Pixels[0] & ~0x03) << 10) | ((Pixels[1] & ~0x03) << 4) | (Pixels[2] >> 2);
}

static inline int Scaler888(uint8_t *Pixels) {
	return (Pixels[0] << 16) | (Pixels[1] << 8) | Pixels[2];
}

static inline int ScalerGray(uint8_t *Pixels) {
	return (Pixels[0] * 14 + Pixels[1] * 76 + Pixels[2] * 38) >> 7;
}

static void *GetScaler(uint8_t Mode) {
	switch (Mode) {
		case GDS_RGB888: return Scaler888;
		case GDS_RGB666: return Scaler666;
		case GDS_RGB565: return Scaler565;
		case GDS_RGB555: return Scaler555;
		case GDS_RGB444: return Scaler444;
		case GDS_RGB332: return Scaler332;			
	}
	return NULL;	
}	

static unsigned InHandler(JDEC *Decoder, uint8_t *Buf, unsigned Len) {
    JpegCtx *Context = (JpegCtx*) Decoder->device;
    if (Buf) memcpy(Buf, Context->InData +  Context->InPos, Len);
    Context->InPos += Len;
    return Len;
}

#define OUTHANDLER(F)										\
	for (int y = Frame->top; y <= Frame->bottom; y++) {		\
		for (int x = Frame->left; x <= Frame->right; x++) {	\
			OutData[Context->Width * y + x] = F(Pixels);	\
			Pixels += 3;									\
		}													\
	}	

static unsigned OutHandler(JDEC *Decoder, void *Bitmap, JRECT *Frame) {
	JpegCtx *Context = (JpegCtx*) Decoder->device;
    uint8_t *Pixels = (uint8_t*) Bitmap;

	// decoded image is RGB888
	if (Context->Mode == GDS_RGB888) {
		uint32_t *OutData = (uint32_t*) Context->OutData;		
		OUTHANDLER(Scaler888);
	} else if (Context->Mode == GDS_RGB666) {
		uint32_t *OutData = (uint32_t*) Context->OutData;		
		OUTHANDLER(Scaler666);		
	} else if (Context->Mode == GDS_RGB565) {
		uint16_t *OutData = (uint16_t*) Context->OutData;
		OUTHANDLER(Scaler565);		
	} else if (Context->Mode == GDS_RGB555) {
		uint16_t *OutData = (uint16_t*) Context->OutData;
		OUTHANDLER(Scaler555);				
	} else if (Context->Mode == GDS_RGB444) {
		uint16_t *OutData = (uint16_t*) Context->OutData;
		OUTHANDLER(Scaler444);						
	} else if (Context->Mode == GDS_RGB332) {
		uint8_t *OutData = (uint8_t*) Context->OutData;
		OUTHANDLER(Scaler332);						
	} else if (Context->Mode <= GDS_GRAYSCALE) { 	 
		uint8_t *OutData = (uint8_t*) Context->OutData;		
		OUTHANDLER(ScalerGray);
	}
    
    return 1;
}

static unsigned OutHandlerDirectGray(JDEC *Decoder, void *Bitmap, JRECT *Frame) {
	JpegCtx *Context = (JpegCtx*) Decoder->device;
    uint8_t *Pixels = (uint8_t*) Bitmap;
	int Shift = 8 - Context->Depth;
	
	for (int y = Frame->top; y <= Frame->bottom; y++) {
		if (y < Context->YMin) continue;
		for (int x = Frame->left; x <= Frame->right; x++) {
			if (x < Context->XMin) continue;
			// Convert the 888 to grayscale
			int Value = ((Pixels[0]*14 + Pixels[1]*76 + Pixels[2]*38) >> 7) >> Shift;
			Pixels += 3;
			// used DrawPixel and not "fast" version as X,Y may be beyond screen
			GDS_DrawPixel( Context->Device, x + Context->XOfs, y + Context->YOfs, Value);
		}
	}
	
    return 1;
}

static unsigned OutHandlerDirectColor(JDEC *Decoder, void *Bitmap, JRECT *Frame) {
	JpegCtx *Context = (JpegCtx*) Decoder->device;
    uint8_t *Pixels = (uint8_t*) Bitmap;

	// used DrawPixel and not "fast" version as X,Y may be beyond screen
	for (int y = Frame->top; y <= Frame->bottom; y++) {
		if (y < Context->YMin) continue;
		for (int x = Frame->left; x <= Frame->right; x++) {
			if (x < Context->XMin) continue;
			GDS_DrawPixel( Context->Device, x + Context->XOfs, y + Context->YOfs, Context->Scaler(Pixels));
			Pixels += 3;
		}
	}

    return 1;
}

//Decode the embedded image into pixel lines that can be used with the rest of the logic.
static void* DecodeJPEG(uint8_t *Source, int *Width, int *Height, float Scale, bool SizeOnly, int RGB_Mode) {
    JDEC Decoder;
    JpegCtx Context;
	char *Scratch = calloc(SCRATCH_SIZE, 1);
	
    if (!Scratch) {
        ESP_LOGE(TAG, "Cannot allocate workspace");
        return NULL;
    }

	Context.OutData = NULL;
    Context.InData = Source;
    Context.InPos = 0;
	        
    //Prepare and decode the jpeg.
    int Res = jd_prepare(&Decoder, InHandler, Scratch, SCRATCH_SIZE, (void*) &Context);
	if (Width) *Width = Decoder.width;
	if (Height) *Height = Decoder.height;
	Decoder.scale = Scale;

    if (Res == JDR_OK && !SizeOnly) {
		if (RGB_Mode <= GDS_RGB332) Context.OutData = malloc(Decoder.width * Decoder.height);
		else if (RGB_Mode < GDS_RGB666) Context.OutData = malloc(Decoder.width * Decoder.height * 2);
		else if (RGB_Mode < GDS_RGB888) Context.OutData = malloc(Decoder.width * Decoder.height * 4);
		
		// find the scaling factor
		uint8_t N = 0, ScaleInt =  ceil(1.0 / Scale);
		ScaleInt--; ScaleInt |= ScaleInt >> 1; ScaleInt |= ScaleInt >> 2; ScaleInt++;
		while (ScaleInt >>= 1) N++;
		if (N > 3) {
			ESP_LOGW(TAG, "Image will not fit %dx%d", Decoder.width, Decoder.height);
			N = 3;
		}	
		
		// ready to decode		
		if (Context.OutData) {
			Context.Width = Decoder.width / (1 << N);
			Context.Height = Decoder.height / (1 << N);
			Context.Mode = RGB_Mode;
			if (Width) *Width = Context.Width;
			if (Height) *Height = Context.Height;
			Res = jd_decomp(&Decoder, OutHandler, N);
			if (Res != JDR_OK) {
				ESP_LOGE(TAG, "Image decoder: jd_decode failed (%d)", Res);
			}	
		} else {
			ESP_LOGE(TAG, "Can't allocate bitmap %dx%d or invalid mode %d", Decoder.width, Decoder.height, RGB_Mode);			
		}	
	} else if (!SizeOnly) {
        ESP_LOGE(TAG, "Image decoder: jd_prepare failed (%d)", Res);
    }    

	// free scratch area
    if (Scratch) free(Scratch);
    return Context.OutData;
}

void* GDS_DecodeJPEG(uint8_t *Source, int *Width, int *Height, float Scale, int RGB_Mode) {
	return DecodeJPEG(Source, Width, Height, Scale, false, RGB_Mode);
}	

void GDS_GetJPEGSize(uint8_t *Source, int *Width, int *Height) {
	DecodeJPEG(Source, Width, Height, 1, true, -1);
}	

/****************************************************************************************
 * RGB conversion 
 * monochrome (0.2125 * color.r) + (0.7154 * color.g) + (0.0721 * color.b)
 * grayscale (0.3 * R) + (0.59 * G) + (0.11 * B) )
 */
 
inline int ToGray888(uint32_t Pixel) {
	return (((Pixel & 0xff) * 14) + ((Pixel >> 8) & 0xff) * 76 + ((Pixel >> 16) * 38) + 1) >> 7;
} 
 
inline int ToGray666(uint32_t Pixel) {
	return (((Pixel & 0x3f) * 14) + ((Pixel >> 6) & 0x3f) * 76 + ((Pixel >> 12) * 38) + 1) >> 7;
}

inline int ToGray565(uint16_t Pixel) {
	return ((((Pixel & 0x1f) * 14) << 1) + ((Pixel >> 5) & 0x3f) * 76 + (((Pixel >> 11) * 38) << 1) + 1) >> 7;
}

inline int ToGray555(uint16_t Pixel) {
	return ((Pixel & 0x1f) * 14 + ((Pixel >> 5) & 0x1f) * 76 + (Pixel >> 10) * 38) >> 7;
}

inline int ToGray444(uint16_t Pixel) {
	return ((Pixel & 0x0f) * 14 + ((Pixel >> 4) & 0x0f) * 76 + (Pixel >> 8) * 38) >> 7;
}

inline int ToGray332(uint8_t Pixel) {
	return ((((Pixel & 0x3) * 14) << 1) + ((Pixel >> 2) & 0x7) * 76 + (Pixel >> 5) * 38 + 1) >> 7;
}

#define TOSELF(X) (X)
	
#define DRAW_RGB(S,F)														\
	if (Scale > 0) {														\
		for (int r = 0; r < Height; r++) {									\
			for (int c = 0; c < Width; c++) {								\
				GDS_DrawPixel( Device, c + x, r + y, F(*S++) >> Scale);		\
			}																\
		}																	\
	} else {																\
		for (int r = 0; r < Height; r++) {									\
			for (int c = 0; c < Width; c++) {								\
				GDS_DrawPixel( Device, c + x, r + y, F(*S++) << -Scale);	\
			}																\
		}																	\
	}								

/****************************************************************************************
 *  Decode the embedded image into pixel lines that can be used with the rest of the logic.
 */
void GDS_DrawRGB( struct GDS_Device* Device, uint8_t *Image, int x, int y, int Width, int Height, int RGB_Mode ) {
	// don't do anything if driver supplies a draw function
	if (Device->DrawRGB) {
		Device->DrawRGB( Device, Image, x, y, Width, Height, RGB_Mode );
		Device->Dirty = true;	
		return;
	}
	
	// set the right scaler
	if (RGB_Mode <= GDS_GRAYSCALE) {
		int Scale = 8 - Device->Depth;
		DRAW_RGB(Image,TOSELF);
	} else if (RGB_Mode == GDS_RGB332) {
		int Scale = 3 - Device->Depth;		
		DRAW_RGB(Image,ToGray332);
	} else if (RGB_Mode < GDS_RGB666)	{
		uint16_t *Source = (uint16_t*) Image;
		
		if (RGB_Mode == GDS_RGB565) {
			int Scale = 6 - Device->Depth;
			DRAW_RGB(Source,ToGray565);
		} else if (RGB_Mode == GDS_RGB555) {
			int Scale = 5 - Device->Depth;
			DRAW_RGB(Source,ToGray555);
		} else if (RGB_Mode == GDS_RGB444) {
			int Scale = 4 - Device->Depth; 
			DRAW_RGB(Source,ToGray444)
		}	
		
	} else {
		uint32_t *Source = (uint32_t*) Image;
		
		if (RGB_Mode == GDS_RGB666) {
			int Scale = 6 - Device->Depth;
			DRAW_RGB(Source,ToGray666);
		} else if (RGB_Mode == GDS_RGB888) {
			int Scale = 8 - Device->Depth;
			DRAW_RGB(Source,ToGray888);
		}	
	} 
	
	Device->Dirty = true;	
}

/****************************************************************************************
 *  Decode the embedded image into pixel lines that can be used with the rest of the logic.
 */
bool GDS_DrawJPEG(struct GDS_Device* Device, uint8_t *Source, int x, int y, int Fit) {
    JDEC Decoder;
    JpegCtx Context;
	bool Ret = false;
	char *Scratch = calloc(SCRATCH_SIZE, 1);
	
    if (!Scratch) {
        ESP_LOGE(TAG, "Cannot allocate workspace");
        return NULL;
    }

    // Populate fields of the JpegCtx struct.
    Context.InData = Source;
    Context.InPos = 0;
	Context.XOfs = x;
	Context.YOfs = y;
	Context.Device = Device;
	Context.Depth = Device->Depth;
        
    //Prepare and decode the jpeg.
    int Res = jd_prepare(&Decoder, InHandler, Scratch, SCRATCH_SIZE, (void*) &Context);
	Context.Width = Decoder.width;
	Context.Height = Decoder.height;
	
    if (Res == JDR_OK) {
		uint8_t N = 0;
		
		// do we need to fit the image
		if (Fit & GDS_IMAGE_FIT) {
			float XRatio = (Device->Width - x) / (float) Decoder.width, YRatio = (Device->Height - y) / (float) Decoder.height;
			uint8_t Ratio = XRatio < YRatio ? ceil(1/XRatio) : ceil(1/YRatio);
			Ratio--; Ratio |= Ratio >> 1; Ratio |= Ratio >> 2; Ratio++;
			while (Ratio >>= 1) N++;
			if (N > 3) {
				ESP_LOGW(TAG, "Image will not fit %dx%d", Decoder.width, Decoder.height);
				N = 3;
			}	
			Context.Width /= 1 << N;
			Context.Height /= 1 << N;
		} 
		
		// then place it
		if (Fit & GDS_IMAGE_CENTER_X) Context.XOfs = (Device->Width + x - Context.Width) / 2;
		else if (Fit & GDS_IMAGE_RIGHT) Context.XOfs = Device->Width - Context.Width;
		if (Fit & GDS_IMAGE_CENTER_Y) Context.YOfs = (Device->Height + y - Context.Height) / 2;
		else if (Fit & GDS_IMAGE_BOTTOM) Context.YOfs = Device->Height - Context.Height;

		Context.XMin = x - Context.XOfs;
		Context.YMin = y - Context.YOfs;
		Context.Scaler = GetScaler(Device->Mode);
					
		// do decompress & draw
		Res = jd_decomp(&Decoder, Device->Mode > GDS_GRAYSCALE ? OutHandlerDirectColor : OutHandlerDirectGray, N);
		if (Res == JDR_OK) {
			Device->Dirty = true;
			Ret = true;
		} else {	
			ESP_LOGE(TAG, "Image decoder: jd_decode failed (%d)", Res);
		}	
	} else {	
        ESP_LOGE(TAG, "Image decoder: jd_prepare failed (%d)", Res);
    }    
      
	// free scratch area
    if (Scratch) free(Scratch);
	return Ret;
}

