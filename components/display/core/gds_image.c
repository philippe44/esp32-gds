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
	union {
		uint16_t *OutData;				// Decompress
		struct {						// DirectDraw
			struct GDS_Device * Device;
			int XOfs, YOfs;
			int Depth;
		};	
	};	
} JpegCtx;

static unsigned InHandler(JDEC *Decoder, uint8_t *Buf, unsigned Len) {
    JpegCtx *Context = (JpegCtx*) Decoder->device;
    if (Buf) memcpy(Buf, Context->InData +  Context->InPos, Len);
    Context->InPos += Len;
    return Len;
}

static unsigned OutHandler(JDEC *Decoder, void *Bitmap, JRECT *Frame) {
	JpegCtx *Context = (JpegCtx*) Decoder->device;
    uint8_t *Pixels = (uint8_t*) Bitmap;
	
    for (int y = Frame->top; y <= Frame->bottom; y++) {
		if (y < Context->YOfs) continue;
        for (int x = Frame->left; x <= Frame->right; x++) {
			if (x < Context->XOfs) continue;
            // Convert the 888 to RGB565
            uint16_t Value = (*Pixels++ & ~0x07) << 8;
            Value |= (*Pixels++ & ~0x03) << 3;
            Value |= *Pixels++ >> 3;
            Context->OutData[Context->Width * y + x] = Value;
        }
    }
    return 1;
}

static unsigned OutHandlerDirect(JDEC *Decoder, void *Bitmap, JRECT *Frame) {
	JpegCtx *Context = (JpegCtx*) Decoder->device;
    uint8_t *Pixels = (uint8_t*) Bitmap;
	int Shift = 8 - Context->Depth;
	
    for (int y = Frame->top; y <= Frame->bottom; y++) {
        for (int x = Frame->left; x <= Frame->right; x++) {
            // Convert the 888 to RGB565
            int Value = ((Pixels[0]*11 + Pixels[1]*59 + Pixels[2]*30) / 100) >> Shift;
			Pixels += 3;
			// used DrawPixel and not "fast" version as X,Y may be beyond screen
			GDS_DrawPixel( Context->Device, Context->XOfs + x, Context->YOfs + y, Value);
        }
    }
    return 1;
}

//Decode the embedded image into pixel lines that can be used with the rest of the logic.
static uint16_t* DecodeJPEG(uint8_t *Source, int *Width, int *Height, float Scale, bool SizeOnly) {
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
		// ready to decode		
		Context.OutData = malloc(Decoder.width * Decoder.height * sizeof(uint16_t));
		uint8_t N = 0, iScale =  1.0 / Scale;
		while (iScale >>= 1) N++;
		if (Context.OutData) {
			Context.Width = Decoder.width / (1 << N);
			Context.Height = Decoder.height / (1 << N);
			if (Width) *Width = Context.Width;
			if (Height) *Height = Context.Height;
			Res = jd_decomp(&Decoder, OutHandler, N > 3 ? 3 : N);
			if (Res != JDR_OK) {
				ESP_LOGE(TAG, "Image decoder: jd_decode failed (%d)", Res);
			}	
		} else {
			ESP_LOGE(TAG, "Can't allocate bitmap %dx%d", Decoder.width, Decoder.height);			
		}	
	} else if (!SizeOnly) {
        ESP_LOGE(TAG, "Image decoder: jd_prepare failed (%d)", Res);
    }    
      
	// free scratch area
    if (Scratch) free(Scratch);
    return Context.OutData;
}

uint16_t* GDS_DecodeJPEG(uint8_t *Source, int *Width, int *Height, float Scale) {
	return DecodeJPEG(Source, Width, Height, Scale, false);
}	

void GDS_GetJPEGSize(uint8_t *Source, int *Width, int *Height) {
	DecodeJPEG(Source, Width, Height, 1, true);
}	

/****************************************************************************************
 * Simply draw a RGB565 image
 * monoschrome (0.2125 * color.r) + (0.7154 * color.g) + (0.0721 * color.b)
 * grayscale (0.3 * R) + (0.59 * G) + (0.11 * B) )
 */
void GDS_DrawRGB16( struct GDS_Device* Device, uint16_t *Image, int x, int y, int Width, int Height, int RGB_Mode ) {
	if (Device->DrawRGB16) {
		Device->DrawRGB16( Device, x, y, Width, Height, RGB_Mode, Image );
	} else {
		int Scale = Device->Depth < 5 ? 5 - Device->Depth : 0;
		switch(RGB_Mode) {
		case GDS_RGB565:
			for (int c = 0; c < Width; c++) {
				for (int r = 0; r < Height; r++) {
					int pixel = Image[Width*r + c];
					pixel = ((pixel & 0x1f) * 11 + ((((pixel >> 5) & 0x3f)  * 59) >> 1) + (pixel >> 11) * 30) / 100;
					GDS_DrawPixel( Device, c + x, r + y, pixel >> Scale);
				}	
			}	
			break;
		case GDS_RGB555:
			for (int c = 0; c < Width; c++) {
				for (int r = 0; r < Height; r++) {
					int pixel = Image[Width*r + c];
					pixel = ((pixel & 0x1f) * 11 + ((pixel >> 5) & 0x1f)  * 59 + (pixel >> 10) * 30) / 100;
					GDS_DrawPixel( Device, c + x, r + y, pixel >> Scale);
				}	
			}	
			break;
		case GDS_RGB444:
			for (int c = 0; c < Width; c++) {
				for (int r = 0; r < Height; r++) {
					int pixel = Image[Width*r + c];
					pixel = (pixel & 0x0f) * 11 + ((pixel >> 4) & 0x0f)  * 59 + (pixel >> 8) * 30;
					GDS_DrawPixel( Device, c + x, r + y, pixel >> (Scale - 1));
				}	
			}	
			break;				
		}
	}	 
}	

//Decode the embedded image into pixel lines that can be used with the rest of the logic.
bool GDS_DrawJPEG( struct GDS_Device* Device, uint8_t *Source, int x, int y, int Fit) {
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
			Context.Width /= 1 << N;
			Context.Height /= 1 << N;
		} 
		
		// then place it
		if (Fit & GDS_IMAGE_CENTER_X) Context.XOfs = x + ((Device->Width - x) - Context.Width) / 2;
		if (Fit & GDS_IMAGE_CENTER_Y) Context.YOfs = y + ((Device->Height - y) - Context.Height) / 2;
		
		// do decompress & draw
		Res = jd_decomp(&Decoder, OutHandlerDirect, N > 3 ? 3 : N);
		if (Res == JDR_OK) {
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

