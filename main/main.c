#include <stdio.h>
#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h" //use of PSRAM for large arrays
#include "esp_psram.h"

const char *TAG = "image_processing";

// HSV color structure
typedef struct
{
    uint8_t h; // Hue (0-180)
    uint8_t s; // Saturation (0-255)
    uint8_t v; // Value (0-255)
} hsv_t;

// Color threshold structure
typedef struct
{
    uint8_t h_min;
    uint8_t h_max;
    uint8_t s_min;
    uint8_t s_max;
    uint8_t v_min;
    uint8_t v_max;
} color_threshold_t;

// Structure for detected regions
typedef struct
{
    int x_center;
    int y_center;
    int pixel_count;
    int min_x;
    int max_x;
    int min_y;
    int max_y;
} blob_t;

#define MAX_BLOBS 10
#define IMAGE_WIDTH 1134 // my custom image dimensions
#define IMAGE_HEIGHT 805
#define MIN_PIXEL_THRESHOLD 50 // Minimum pixels to consider a blob
#define MAX_GAP 3              // Maximum gap between pixels in same blob

// extern const uint16_t rgb565_start[] asm("_binary_lutino_brightlight_rgb565_raw_start");
// extern const size_t RGB565_size asm("_binary_lutino_brightlight_rgb565_raw_size");
extern const uint16_t _binary_lutino_brightlight_rgb565_raw_start[];
extern const size_t _binary_lutino_brightlight_rgb565_raw_size;

// const size_t pixel_count = RGB565_size / 2;
// const uint16_t *pixels = rgb565_start;

size_t pixel_count;
const uint16_t *pixels;
// adjusted values with some tolerance
color_threshold_t lutino_thresh = {
    .h_min = 18,
    .h_max = 32,
    .s_min = 190,
    .s_max = 255,
    .v_min = 90,
    .v_max = 255};

color_threshold_t green_thresh = {
    .h_min = 28,
    .h_max = 48,
    .s_min = 130,
    .s_max = 200,
    .v_min = 45,
    .v_max = 255};

hsv_t rgb565_to_hsv(uint16_t rgb565);
bool matches_threshold(hsv_t hsv, color_threshold_t thresh);
void find_blobs(bool *pixel_mask, blob_t *blobs, int *blob_count);

// HSV conversion function
hsv_t rgb565_to_hsv(uint16_t rgb565)
{
    hsv_t hsv;

    // Extract RGB components (5-bit R, 6-bit G, 5-bit B)
    uint8_t r = (rgb565 >> 11) & 0x1F;
    uint8_t g = (rgb565 >> 5) & 0x3F;
    uint8_t b = rgb565 & 0x1F;

    // Scale to 8-bit
    r = (r * 255) / 31;
    g = (g * 255) / 63;
    b = (b * 255) / 31;

    // Find min and max
    uint8_t min = r;
    uint8_t max = r;
    if (g < min)
        min = g;
    if (b < min)
        min = b;
    if (g > max)
        max = g;
    if (b > max)
        max = b;

    uint8_t delta = max - min;

    // Value
    hsv.v = max;

    // Saturation
    if (max == 0)
    {
        hsv.s = 0;
    }
    else
    {
        hsv.s = (delta * 255) / max;
    }

    // Hue
    if (delta == 0)
    {
        hsv.h = 0;
    }
    else
    {
        int hue_temp;
        if (max == r)
        {
            hue_temp = 30 * ((g - b) / delta);
            if (g < b)
                hue_temp += 180;
        }
        else if (max == g)
        {
            hue_temp = 30 * (2 + ((b - r) / delta));
        }
        else
        {
            hue_temp = 30 * (4 + ((r - g) / delta));
        }
        hsv.h = (hue_temp * 180) / 360;
    }

    return hsv;
}

// Threshold checking function
bool matches_threshold(hsv_t hsv, color_threshold_t thresh)
{
    return (hsv.h >= thresh.h_min && hsv.h <= thresh.h_max &&
            hsv.s >= thresh.s_min && hsv.s <= thresh.s_max &&
            hsv.v >= thresh.v_min && hsv.v <= thresh.v_max);
}

void find_blobs(bool *pixel_mask, blob_t *blobs, int *blob_count)
{
    bool *visited = (bool *)heap_caps_malloc(pixel_count * sizeof(bool), MALLOC_CAP_SPIRAM);
    if (visited == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate visited array in PSRAM!");
        *blob_count = 0;
        return;
    }

    memset(visited, 0, pixel_count * sizeof(bool));
    *blob_count = 0;
    printf("starting blob detection \n");

    int *stack = (int *)heap_caps_malloc(pixel_count * sizeof(int), MALLOC_CAP_SPIRAM);
    if (stack == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate stack in PSRAM!");
        heap_caps_free(visited);
        *blob_count = 0;
        return;
    }

    for (int i = 0; i < pixel_count; i++)
    {
        if (pixel_mask[i] && !visited[i])
        {
            // Start new blob
            blob_t current_blob = {0};
            current_blob.min_x = IMAGE_WIDTH;
            current_blob.min_y = IMAGE_HEIGHT;
            current_blob.max_x = 0;
            current_blob.max_y = 0;

            // Simple flood fill (4-connected)
            // a DFS approach in this case
            // int stack[pixel_count];
            int stack_ptr = 0;
            stack[stack_ptr++] = i; // stack push operation
            visited[i] = true;

            while (stack_ptr > 0)
            {
                int idx = stack[--stack_ptr];
                int x = idx % IMAGE_WIDTH;
                int y = idx / IMAGE_WIDTH;

                // Update blob bounds
                if (x < current_blob.min_x)
                    current_blob.min_x = x;
                if (x > current_blob.max_x)
                    current_blob.max_x = x;
                if (y < current_blob.min_y)
                    current_blob.min_y = y;
                if (y > current_blob.max_y)
                    current_blob.max_y = y;

                current_blob.pixel_count++;
                current_blob.x_center += x;
                current_blob.y_center += y;

                // Check ALL pixels within MAX_GAP distance
                for (int dy = -MAX_GAP; dy <= MAX_GAP; dy++)
                {
                    for (int dx = -MAX_GAP; dx <= MAX_GAP; dx++)
                    {
                        if (dx == 0 && dy == 0)
                            continue; // Skip current pixel

                        int nx = x + dx;
                        int ny = y + dy;

                        // Check bounds
                        if (nx >= 0 && nx < IMAGE_WIDTH && ny >= 0 && ny < IMAGE_HEIGHT)
                        {
                            int nidx = ny * IMAGE_WIDTH + nx;

                            if (!visited[nidx] && pixel_mask[nidx])
                            {
                                visited[nidx] = true;
                                stack[stack_ptr++] = nidx;
                            }
                        }
                    }
                }
            }

            // Calculate center
            if (current_blob.pixel_count > 0)
            {
                current_blob.x_center /= current_blob.pixel_count;
                current_blob.y_center /= current_blob.pixel_count;

                // Only keep blobs above minimum size
                if (current_blob.pixel_count >= MIN_PIXEL_THRESHOLD)
                {
                    // blobs[(*blob_count)++] = current_blob;
                    blobs[*blob_count] = current_blob;

                    // Blob storage tracking
                    printf("Blob %d is added to blobs array", *blob_count);
                    printf(" (size: %d pixels at [%d,%d])",
                           current_blob.pixel_count,
                           current_blob.x_center,
                           current_blob.y_center);
                    (*blob_count)++;

                    printf(" → Total blobs now: %d\n", *blob_count);
                }
                else
                {
                    printf("  Ignored small blob of %d pixels (below threshold %d)\n",
                           current_blob.pixel_count, MIN_PIXEL_THRESHOLD);
                }
            }
        }
    }
    printf(" Blob Detection Complete: %d blobs found\n", *blob_count);
    heap_caps_free(visited);
    heap_caps_free(stack);
}

void app_main(void)
{
    pixel_count = _binary_lutino_brightlight_rgb565_raw_size / 2;
    pixels = _binary_lutino_brightlight_rgb565_raw_start;

    // size_t pixel_count = RGB565_size / 2;
    // const uint16_t *pixels = rgb565_start;
    printf("Image loaded \n");
    printf("Size: %d, %d pixels\n", _binary_lutino_brightlight_rgb565_raw_size, pixel_count);

    // color_threshold_t lutino_thresh = lutino_thresh;
    // color_threshold_t green_thresh = green_thresh;

    printf("initializing PSRAM and checking availability...\n");
    if (!esp_psram_is_initialized())
    {
        ESP_LOGE(TAG, "PSRAM not initialized!");
        return;
    }

    size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t psram_free_start = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    printf("Total PSRAM: %d bytes (%.2f MB)\n", psram_total, psram_total / 1024.0 / 1024.0);
    printf("Free PSRAM at start: %d bytes (%.2f MB)\n", psram_free_start, psram_free_start / 1024.0 / 1024.0);

    // Arrays to store pixel classifications, allocate in PSRAM
    bool *lutino_pixels = (bool *)heap_caps_malloc(pixel_count * sizeof(bool), MALLOC_CAP_SPIRAM);
    bool *green_pixels = (bool *)heap_caps_malloc(pixel_count * sizeof(bool), MALLOC_CAP_SPIRAM);

    if (lutino_pixels == NULL || green_pixels == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate PSRAM!");
        return;
    }

    memset(lutino_pixels, 0, pixel_count * sizeof(bool));
    memset(green_pixels, 0, pixel_count * sizeof(bool));
    size_t psram_after_alloc = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    printf("\nPSRAM after allocating arrays: %d bytes free (used %d bytes)\n",
           psram_after_alloc, psram_free_start - psram_after_alloc);

    int lutino_count = 0;
    int green_count = 0;

    // Process all pixels
    for (int i = 0; i < pixel_count; i++)
    {
        uint16_t prc_pixel = pixels[i];
        hsv_t hsv = rgb565_to_hsv(prc_pixel);

        if (matches_threshold(hsv, lutino_thresh))
        {
            lutino_pixels[i] = true;
            lutino_count++;
        }
        else if (matches_threshold(hsv, green_thresh))
        {
            green_pixels[i] = true;
            green_count++;
        }
    }
    printf("Pixel classification complete\n");

    // Calculate percentage of matching pixels
    float lutino_percent = (float)lutino_count / pixel_count * 100;
    float green_percent = (float)green_count / pixel_count * 100;

    printf("Lutino pixels: %d (%.2f%%)\n", lutino_count, lutino_percent);
    printf("Green pixels: %d (%.2f%%)\n", green_count, green_percent);

    // Check PSRAM before blob detection
    size_t psram_before_blobs = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    printf("\nPSRAM before blob detection: %d bytes free\n", psram_before_blobs);

    // Find blobs for lutino and green
    blob_t lutino_blobs[MAX_BLOBS];
    blob_t green_blobs[MAX_BLOBS];
    int lutino_blob_count = 0;
    int green_blob_count = 0;

    printf("Lutino Blob Detection Starting...\n");
    find_blobs(lutino_pixels, lutino_blobs, &lutino_blob_count);
    printf("Green Blob Detection Starting...\n");
    find_blobs(green_pixels, green_blobs, &green_blob_count);
    // Check PSRAM after blob detection
    size_t psram_after_blobs = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    printf("\nPSRAM after blob detection: %d bytes free\n", psram_after_blobs);

    // Detection logic with 30% threshold and blob analysis
    bool lutino_detected = false;
    bool green_detected = false;

    // Check lutino detection
    if (lutino_percent >= 10.0f)
    {
        // Check if there's a significant blob
        for (int i = 0; i < lutino_blob_count; i++)
        {
            if (lutino_blobs[i].pixel_count >= MIN_PIXEL_THRESHOLD)
            {
                // Calculate blob density or aspect ratio if needed
                int blob_width = lutino_blobs[i].max_x - lutino_blobs[i].min_x;
                int blob_height = lutino_blobs[i].max_y - lutino_blobs[i].min_y;
                float aspect_ratio = (float)blob_width / blob_height;

                // Bird-like shape typically has aspect ratio between 0.5 and 2.0
                if (aspect_ratio > 0.3f && aspect_ratio < 3.0f)
                {
                    lutino_detected = true;
                    ESP_LOGI(TAG, "Lutino detected! Center at (%d, %d), size: %d pixels",
                             lutino_blobs[i].x_center, lutino_blobs[i].y_center,
                             lutino_blobs[i].pixel_count);
                    break;
                }
            }
        }
    }

    // Check green detection similarly
    if (green_percent >= 10.0f)
    {
        for (int i = 0; i < green_blob_count; i++)
        {
            if (green_blobs[i].pixel_count >= MIN_PIXEL_THRESHOLD)
            {
                green_detected = true;
                ESP_LOGI(TAG, "Green detected! Center at (%d, %d), size: %d pixels",
                         green_blobs[i].x_center, green_blobs[i].y_center,
                         green_blobs[i].pixel_count);
                break;
            }
        }
    }

    if (lutino_detected && green_detected)
    {
        printf("Both birds detected!\n");
    }
    else if (lutino_detected)
    {
        printf("Lutino bird detected\n");
    }
    else if (green_detected)
    {
        printf("Green bird detected\n");
    }
    else
    {
        printf("No bird detected\n");
    }

    // Free allocated PSRAM memory
    heap_caps_free(lutino_pixels);
    heap_caps_free(green_pixels);

    // Final PSRAM check
    size_t psram_final = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    printf("Final PSRAM free: %d bytes (should equal start: %d)\n",
           psram_final, psram_free_start);

    if (psram_final == psram_free_start)
    {
        printf("All PSRAM properly freed\n");
    }
    else
    {
        printf("Memory leak detected! %d bytes not freed\n",
               psram_free_start - psram_final);
    }
}
