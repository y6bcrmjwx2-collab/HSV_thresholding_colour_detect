#include <stdio.h>
#include <math.h>
#include "esp_log.h"

extern const uint16_t rgb565_start[] asm("_binary_lutino_brightlight_rgb565_raw_start");
extern const size_t RGB565_size asm("_binary_lutino_brightlight_rgb565_raw_size");

size_t pixel_count = RGB565_size / 2;
const uint16_t *pixels = rgb565_start;

const char *TAG = "image_processing";
void app_main(void)
{

    printf("Image loaded \n");
    printf("Size: %d, %d pixels\n", RGB565_size, pixel_count);
}
