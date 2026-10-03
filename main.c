#include <stdio.h>
#include <tusb.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/vreg.h"
#include "hardware/gpio.h"
#include "cam.h"
#include "ImageData.h"
#include "LCD_1in14_V2.h" 
#include "GUI_Paint.h"
#include "hardware/clocks.h"

uint8_t image_buf[324 * 324]; // 8 bit color (monochrome?)
uint16_t display_buf[240 * 135]; // RGB565 format

#define FLAG_VALUE 1234 // ack flag
uint8_t image_ready = 0;

void core1_entry() {
    DEV_Module_Init(); // initializes peripherals.
    LCD_1IN14_V2_Init(HORIZONTAL); // initializes screen with horizontal scanning.
    LCD_1IN14_V2_Clear(BLACK); // clears screen to black.

    // question: is UDOUBLE named like that since the board we are working with uses 16 bit words/registers?
    // also isn't this a bad name because of double precision floats.
    UDOUBLE ImageSize = LCD_1IN14_V2_HEIGHT * LCD_1IN14_V2_WIDTH;
    UWORD* BlackImage;
    // question: why do we need to explicitly cast here?
    if ((BlackImage = (UWORD*)malloc(ImageSize)) == NULL) {
        printf("Failed to allocate D: ...\r\n");
        exit(0);
    }

    multicore_fifo_push_blocking(FLAG_VALUE); // push ack flag to other core.

    uint32_t ack = multicore_fifo_pop_blocking(); // wait for ack from other core.
    if (ack != FLAG_VALUE)
        printf("Error: Core 1 failed to receive acknowledgement from core 0!\n");
    else
        printf("Success: Core 1 received acknowledgement from core 0!\n");

    // again, why not cast earlier, also why does this only take a byte for the addressing.
    Paint_NewImage((UBYTE*) BlackImage, LCD_1IN14_V2_WIDTH, LCD_1IN14_V2_HEIGHT, 0, WHITE);
    Paint_SetScale(65);
    Paint_SetRotate(ROTATE_0);
    Paint_DrawImage(realityLabsLogo, 0, 0, 240, 135);
    LCD_1IN14_V2_Display(BlackImage);

    // initalize camera
    struct cam_config config;
    cam_config_struct(&config);
    cam_init(&config);

    while (true) {
        cam_capture_frame(&config);
        uint16_t index = 0;

        for (int y = 134; y > 0; --y) {
            for (int x = 0; x < 240; ++x) {
                uint16_t c = image_buf[y * 324 + x];
                uint16_t imageRGB = (((c & 0xF8) << 8) | ((c & 0xFC) << 3) | ((c & 0xF8) >> 3));
                display_buf[index++] = (imageRGB >> 8) | (imageRGB << 8); // why are we swapping?
            }
        }
        image_ready = 1;
    }
}

int main() {
    stdio_init_all();
    vreg_set_voltage(VREG_VOLTAGE_1_10); // setting voltage to 1.1V
    set_sys_clock_khz(250000, true); // 250MHz

    // trying something here.
    for (int loops = 20; loops >= 0 && !tud_cdc_connected(); --loops) {
        sleep_ms(100);
    }
    printf("tud_cdc_connected(%d)\n", tud_cdc_connected() ? 1 : 0);

    multicore_launch_core1(core1_entry);

    uint32_t ack = multicore_fifo_pop_blocking(); // block here until there is data in the queue from the other core, and pop some data.
    if (ack != FLAG_VALUE)
        printf("Error: Core 0 failed to receive acknowledgment from core 1!\n");
    else {
        multicore_fifo_push_blocking(FLAG_VALUE); // block here (most likely not) until there is space in the queue to the other core, and push the ack flag.
        printf("Success: Core 0 Received acknowledgment from core 1!\n");
    }

    while (true) {
        if (image_ready == 1) {
            LCD_1IN14_V2_Display(display_buf); // will display the image stored in the buffer.
            image_ready = 0;
        }
        sleep_ms(1); // we do need to have some sort of delay.
    }

    tight_loop_contents(); // question: does this just denote that there is a "tight" loop around here?
}
