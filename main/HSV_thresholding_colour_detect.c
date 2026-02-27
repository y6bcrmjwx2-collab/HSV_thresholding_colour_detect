#include <stdio.h>
#include <math.h>
#include "esp_log.h"
#include "esp_heap_caps.h" //use of PSRAM for large arrays

extern const uint16_t rgb565_start[] asm("_binary_lutino_brightlight_rgb565_raw_start");
extern const size_t RGB565_size asm("_binary_lutino_brightlight_rgb565_raw_size");

size_t pixel_count = RGB565_size / 2;
const uint16_t *pixels = rgb565_start;

const char *TAG = "image_processing";

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

void find_blobs(bool *pixel_mask, blob_t *blobs, int *blob_count)
{
    bool visited[pixel_count] = {false};
    *blob_count = 0;

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
            int stack[pixel_count];
            int stack_ptr = 0;
            stack[stack_ptr++] = i;
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

                // Check neighbors (4-connected)
                int neighbors[4] = {
                    idx - 1,           // left
                    idx + 1,           // right
                    idx - IMAGE_WIDTH, // up
                    idx + IMAGE_WIDTH  // down
                };

                for (int n = 0; n < 4; n++)
                {
                    int nidx = neighbors[n];
                    if (nidx >= 0 && nidx < pixel_count && !visited[nidx] && pixel_mask[nidx])
                    {
                        // Check if neighbors are close enough (gap condition)
                        int nx = nidx % IMAGE_WIDTH;
                        int ny = nidx / IMAGE_WIDTH;

                        if (abs(nx - x) <= MAX_GAP && abs(ny - y) <= MAX_GAP)
                        {
                            visited[nidx] = true;
                            stack[stack_ptr++] = nidx;
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
                    blobs[(*blob_count)++] = current_blob;
                }
            }
        }
    }
}

void app_main(void)
{

    printf("Image loaded \n");
    printf("Size: %d, %d pixels\n", RGB565_size, pixel_count);
    // Initialize thresholds
    color_threshold_t lutino_thresh = lutino_thresh;
    color_threshold_t green_thresh = green_thresh;

    // Arrays to store pixel classifications
    bool lutino_pixels[pixel_count];
    bool green_pixels[pixel_count];
    memset(lutino_pixels, 0, pixel_count * sizeof(bool));
    memset(green_pixels, 0, pixel_count * sizeof(bool));

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

    // Calculate percentage of matching pixels
    float lutino_percent = (float)lutino_count / pixel_count * 100;
    float green_percent = (float)green_count / pixel_count * 100;

    printf("Lutino pixels: %d (%.2f%%)\n", lutino_count, lutino_percent);
    printf("Green pixels: %d (%.2f%%)\n", green_count, green_percent);

    // Find blobs for lutino and green
    blob_t lutino_blobs[MAX_BLOBS];
    blob_t green_blobs[MAX_BLOBS];
    int lutino_blob_count = 0;
    int green_blob_count = 0;

    find_blobs(lutino_pixels, lutino_blobs, &lutino_blob_count);
    find_blobs(green_pixels, green_blobs, &green_blob_count);

    // Detection logic with 30% threshold and blob analysis
    bool lutino_detected = false;
    bool green_detected = false;

    // Check lutino detection
    if (lutino_percent >= 30.0f)
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
    if (green_percent >= 30.0f)
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

    // Final result
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
}
