/* GDS Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"

#include "gds.h"
#include "gds_default_if.h"
#include "gds_draw.h"
#include "gds_text.h"
#include "gds_font.h"
#include "gds_image.h"

//#define I2C_MODE

#define I2C_ADDRESS	0x3C

static char TAG[] = "display";

//Reference the binary-included jpeg file
extern const uint8_t image_jpg_start[]   asm("_binary_image_jpg_start");
extern const uint8_t image_jpg_end[]     asm("_binary_image_jpg_end");
extern const uint8_t image2_jpg_start[]   asm("_binary_image2_jpg_start");
extern const uint8_t image2_jpg_end[]     asm("_binary_image2_jpg_end");

int i2c_system_port = 0;
int i2c_system_speed = 400000;
int spi_system_host = SPI2_HOST;
int spi_system_dc_gpio = 5;

struct GDS_Device *display;
extern GDS_DetectFunc SSD1306_Detect, SSD132x_Detect, SH1106_Detect;
GDS_DetectFunc* drivers[] = { SH1106_Detect, SSD1306_Detect, SSD132x_Detect, NULL };

bool init_display (char *config, char *welcome) {
	int width = -1, height = -1;
	char *p, driver[32] = "";
	
	ESP_LOGI(TAG, "Initializing display with config: %s", config);
	if ((p = strcasestr(config, "driver")) != NULL) sscanf(p, "%*[^=]=%31[^,]", driver);
	ESP_LOGI(TAG, "Extracted drivername %s", driver);
		
	display = GDS_AutoDetect(driver, drivers);
	
	if (!display) {
		ESP_LOGW(TAG, "Unknown display type or no serial interface configured");
		return false;
	}	
	
	// no time for smart parsing - this is for tinkerers
	if ((p = strcasestr(config, "width")) != NULL) width = atoi(strchr(p, '=') + 1);
	if ((p = strcasestr(config, "height")) != NULL) height = atoi(strchr(p, '=') + 1);

	if (width == -1 || height == -1) {
		ESP_LOGW(TAG, "No display configured %s [%d x %d]", config, width, height);
		return NULL;
	}	
	
	if (strstr(config, "I2C") && i2c_system_port != -1) {
		int address = I2C_ADDRESS;
				
		if ((p = strcasestr(config, "address")) != NULL) address = atoi(strchr(p, '=') + 1);
		
		GDS_I2CInit( i2c_system_port, -1, -1, i2c_system_speed ) ;
		GDS_I2CAttachDevice( display, width, height, address, -1 );
		
		ESP_LOGI(TAG, "Display is I2C on port %u", address);
	} else if (strstr(config, "SPI") && spi_system_host != -1) {
		int CS_pin = -1, speed = 0, RST_pin = -1;
		
		if ((p = strcasestr(config, "cs")) != NULL) CS_pin = atoi(strchr(p, '=') + 1);
		if ((p = strcasestr(config, "speed")) != NULL) speed = atoi(strchr(p, '=') + 1);
		if ((p = strcasestr(config, "rst")) != NULL) RST_pin = atoi(strchr(p, '=') + 1);
		
		GDS_SPIInit( spi_system_host, spi_system_dc_gpio );
        GDS_SPIAttachDevice( display, width, height, CS_pin, RST_pin, speed );
				
		ESP_LOGI(TAG, "Display is SPI host %u with cs:%d", spi_system_host, CS_pin);
		
	}
	
	GDS_SetHFlip( display, strcasestr(config, "HFlip") ? true : false);
	GDS_SetVFlip( display, strcasestr(config, "VFlip") ? true : false);
	GDS_SetFont( display, &Font_droid_sans_fallback_15x17 );
	GDS_TextPos( display, GDS_FONT_MEDIUM, GDS_TEXT_CENTERED, GDS_TEXT_CLEAR | GDS_TEXT_UPDATE, welcome);
	
	// set lines for "fixed" text mode
	GDS_TextSetFontAuto(display, 1, GDS_FONT_LINE_1, -3);
	GDS_TextSetFontAuto(display, 2, GDS_FONT_LINE_2, -3);

	return true;
}

void app_main()
{
#ifdef I2C_MODE	
	i2c_config_t i2c_config = {
		.mode = I2C_MODE_MASTER,
		.sda_io_num = 25,
		.sda_pullup_en = GPIO_PULLUP_ENABLE,
		.scl_io_num = 26,
		.scl_pullup_en = GPIO_PULLUP_ENABLE,
		.master.clk_speed = i2c_system_speed,
	};
#else	
	spi_bus_config_t BusConfig = {
		.mosi_io_num = 22,
        .sclk_io_num = 23,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1
    };
#endif	

#ifdef I2C_MODE
	// can also be done by GDS when SDA & SCL are passed to GDS_I2CInit()
	i2c_param_config(i2c_system_port, &i2c_config);
	i2c_driver_install(i2c_system_port, i2c_config.mode, 0, 0, 0 );
	init_display("I2C,driver=SSD1306,width=128,height=64,HFlip,VFlip", "hello I2C");
#else
	gpio_pad_select_gpio(spi_system_dc_gpio);
	gpio_set_direction( spi_system_dc_gpio, GPIO_MODE_OUTPUT );
	gpio_set_level( spi_system_dc_gpio, 0 );
	
	spi_bus_initialize( spi_system_host, &BusConfig, 1 );
	
	init_display("SPI,driver=SSD1327,width=128,height=128,cs=18,speed=16000000,rst=25", "Hello SPI");
#endif

	if (!display) {
		ESP_LOGE(TAG, "No driver found, stopping ...");
		vTaskSuspend(NULL);
	}
			
	#define NB_BARS	10
	struct {
		int current, max;
	} bars[NB_BARS] = { 0 };
    
	int bar_gap = 1;
	int width = GDS_GetWidth(display);
	int height = GDS_GetHeight(display);
	int bar_width = (width - bar_gap * (NB_BARS - 1)) / NB_BARS;
	int border = (width - (bar_width + bar_gap) * NB_BARS + bar_gap) / 2;
	
	GDS_SetContrast(display, 100);
	ESP_LOGI(TAG, "displaying %u bars of %u pixels with space %u and borders %u", NB_BARS, bar_width, bar_gap, border);
	GDS_ClearExt(display, true);
	
	GDS_TextPos( display, GDS_FONT_DEFAULT, GDS_TEXT_CENTERED, GDS_TEXT_CLEAR | GDS_TEXT_UPDATE, "Starting in 2.5s");
	vTaskDelay(2500 / portTICK_RATE_MS);
	GDS_Clear(display, GDS_COLOR_BLACK);
	GDS_TextLine(display, 1, GDS_TEXT_LEFT, GDS_TEXT_CLEAR, "This is LINE1");
	
	int count = 0;
	long long int avg = 0;
	static uint16_t *image;
	int show = 0;
	int image_width, image_height;
	
	// actual scaling factor is closest ^2
	image = GDS_DecodeJPEG((uint8_t*) image_jpg_start, &image_width, &image_height, 1);
	ESP_LOGI(TAG, "Image size %dx%d", image_width, image_height);
	
	while(1) {
		char String[128];
		int bar_height = GDS_GetHeight(display) / 2;

		int start_b = xthal_get_ccount();
		GDS_ClearExt( display, false, false, 0, 32, -1, -1);

		if (show == 1 && image) {
			GDS_DrawRGB16(display, image, 16, 32, image_width, image_height, GDS_RGB565 );
		} else if (show == 2) {
			GDS_DrawJPEG(display, (uint8_t*) image2_jpg_start, 0, 32, GDS_IMAGE_FIT | GDS_IMAGE_CENTER_X);		
		} else {
			for (int i = 0; i < NB_BARS; i++) {
				int x1 = border + i*(bar_width + bar_gap);
				int y1 = height - 1;
			
				bars[i].current = rand() % bar_height;
			
				if (bars[i].current > bars[i].max) bars[i].max = bars[i].current;
				else if (bars[i].max) bars[i].max--;
			
				for (int j = 0; j < bars[i].current; j += 2) 
					GDS_DrawLine(display, x1, y1 - j, x1 + bar_width - 1, y1 - j, GDS_COLOR_WHITE);
			
				if (bars[i].max > 1) {
					GDS_DrawLine(display, x1, y1 - bars[i].max, x1 + bar_width - 1, y1 - bars[i].max, GDS_COLOR_WHITE);			
					GDS_DrawLine(display, x1, y1 - bars[i].max + 1, x1 + bar_width - 1, y1 - bars[i].max + 1, GDS_COLOR_WHITE);			
				}	
			}
		}	
		
		GDS_Update(display);
		
		int end_b = xthal_get_ccount();
		avg += end_b - start_b;
		
		if (count++ == 10) {
			sprintf(String, "CPU %d", (int) (avg / count));
			ESP_LOGI(TAG, "Average is %d", (int) (avg / count));
			GDS_TextLine(display, 2, GDS_TEXT_LEFT, GDS_TEXT_CLEAR | GDS_TEXT_UPDATE, String);
			avg = 0;
			count = 0;
			show = (show + 1) % 3;
		}	
		
		vTaskDelay(100 / portTICK_RATE_MS);
    }
	
	// just to complete example
	if (image) free(image);
}

