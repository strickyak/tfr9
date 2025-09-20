#ifndef _SSD1306_H_
#define _SSD1306_H_

#include <hardware/i2c.h>
#include "pico2/pico-ssd1306/ssd1306.h"
#include "pico2/pico-ssd1306/textRenderer/8x8_font.h"
#include "pico2/pico-ssd1306/textRenderer/TextRenderer.h"

#define I2C_PIN_SDA 20
#define I2C_PIN_SCL 21
#define I2C_PORT i2c0

enum {
    DISPLAY_X_PORT = 8,
    DISPLAY_Y_PORT = 9,
    DISPLAY_COMMAND_PORT = 10,
};
enum {
    DISPLAY_CLEAR_BUFFER = 0,
    DISPLAY_SET_POINT = 1,
    DISPLAY_SEND_BUFFER = 2,
};

pico_ssd1306::SSD1306 *display;

template <typename T>
struct DontSsd1306 {
    static bool DoesSsd1306() { return false; }
    static void Ssd1306_Init(byte i2c_addr) {}
};

bool Display_hardware_initted;
byte Display_x;
byte Display_y;

template <typename T>
struct DoSsd1306 {
    static bool DoesSsd1306() { return true; }

    static void Ssd1306_Init(uint base) {
        base &= 0xFF;
        IOReaders[base + DISPLAY_X_PORT] = [](uint addr, byte data) {
            return Display_x;
        };
        IOReaders[base + DISPLAY_Y_PORT] = [](uint addr, byte data) {
            return Display_y;
        };
        IOWriters[base + DISPLAY_X_PORT] = [](uint addr, byte data) {
            Display_x = data;
printf(" display x=%d\n", data);
        };
        IOWriters[base + DISPLAY_Y_PORT] = [](uint addr, byte data) {
            Display_y = data;
printf(" display y=%d\n", data);
        };
        IOWriters[base + DISPLAY_COMMAND_PORT] = [](uint addr, byte data) {
            // The first time we get a command, init the port.
            if (!Display_hardware_initted) {
                Ssd1306_HardwareInit();
                Display_hardware_initted = true;
            }
            assert(display);

            switch (data) {
                case DISPLAY_CLEAR_BUFFER:
                    display->clear();
printf(" [clear] \n");
                    break;
                case DISPLAY_SEND_BUFFER:
                    display->sendBuffer();
printf(" [send] \n");
                    break;
                case DISPLAY_SET_POINT:
                    display->setPixel(Display_x, Display_y);
printf(" [point] %d,%d\n", Display_x, Display_y);
                    break;
                default:
                    if (' ' <= data && data <= '~') {
                        drawChar(display, font_8x8, data, Display_x, Display_y);
printf(" [char:%c] %d,%d\n", data, Display_x, Display_y);
                    } else {
printf(" [?default %d] \n", data);
                    }
                    break;
            }
        };
    }
 private:
    static void Ssd1306_HardwareInit() {
        i2c_init(I2C_PORT, 1000000); // Use i2c port with baud rate of 1Mhz

        // Set pins for I2C operation
        gpio_set_function(I2C_PIN_SDA, GPIO_FUNC_I2C);
        gpio_set_function(I2C_PIN_SCL, GPIO_FUNC_I2C);
        gpio_pull_up(I2C_PIN_SDA);
        gpio_pull_up(I2C_PIN_SCL);

        // Create a new global display object
        if (display) delete display;
        display = new pico_ssd1306::SSD1306(i2c0, 0x3C, pico_ssd1306::Size::W128xH64);

    }

};

#endif // _SSD1306_H_
