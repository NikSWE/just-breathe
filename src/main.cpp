#include <mbed.h>
#include "drivers/LCD_DISCO_F429ZI.h"

#define BACKGROUND 1
#define FOREGROUND 0

#define GRAPH_PADDING 20
#define Y_MIN 0.05
#define Y_MAX 3
#define BUFFER_SIZE 240

// ================================================= GLOBAL =================================================

// Use to manipulate the lcd on the board
LCD_DISCO_F429ZI lcd;

// Pin to read the sensor data from
AnalogIn ain(PA_6);

// Buffer for holding displayed text strings
char display_buf[4][20];

// For drawing the graph and showing the important information to the user
unsigned char draw_thread_stack[4096];
Thread draw_thread(osPriorityBelowNormal1, 4096, draw_thread_stack);

// Graph window configuration
uint32_t graph_width = lcd.GetXSize() - 1 * GRAPH_PADDING;
uint32_t max_graph_width = lcd.GetXSize() - 1;

uint32_t graph_height = graph_width;

// circular buffer is used like a queue. The main thread pushes
// new data into the buffer, and the draw thread pops them out
// and updates the graph
CircularBuffer<float, BUFFER_SIZE> new_values;

// semaphore used to protect the new_values buffer
Semaphore new_values_semaphore(0, BUFFER_SIZE);

// Contains the values to be displayed on the lcd
uint32_t graph_pixels[BUFFER_SIZE];

// The index of the value to be graphed on the lcd
uint32_t next_write_index = 0;

// ================================================= GLOBAL =================================================

// ================================================= FUNCTIONS =================================================

// Sets the background layer to be visible, transparent, and
// resets its colors to all black
void setup_background_layer()
{
    lcd.SelectLayer(BACKGROUND);
    lcd.Clear(LCD_COLOR_BLACK);
    lcd.SetBackColor(LCD_COLOR_BLACK);
    lcd.SetTextColor(LCD_COLOR_WHITE);
    lcd.SetLayerVisible(BACKGROUND, ENABLE);
    lcd.SetTransparency(BACKGROUND, 0x7Fu);
}

// resets the foreground layer to
// all black
void setup_foreground_layer()
{
    lcd.SelectLayer(FOREGROUND);
    lcd.Clear(LCD_COLOR_BLACK);
    lcd.SetBackColor(LCD_COLOR_BLACK);
    lcd.SetTextColor(LCD_COLOR_GREEN);
}

// draws a rectangle with horizontal tick marks
// on the background layer. The spacing between tick
// marks in pixels is taken as a parameter
void draw_graph_window(uint32_t horiz_tick_spacing)
{
    lcd.SelectLayer(BACKGROUND);
    lcd.DrawRect(0, 0, lcd.GetXSize(), lcd.GetXSize());
    for (int32_t i = 0; i < lcd.GetXSize() - 1; i += horiz_tick_spacing)
        lcd.DrawVLine(i, lcd.GetXSize() - 30, 30);
}

// maps inputY in the range minVal to maxVal, to a y-axis value pixel in the range
// minPixelY to MaxPixelY
uint16_t mapPixelY(float inputY, float minVal, float maxVal, int32_t minPixelY, int32_t maxPixelY)
{
    const float mapped_pixel_y = (float)maxPixelY - (inputY) / (maxVal - minVal) * ((float)maxPixelY - (float)minPixelY);
    return mapped_pixel_y;
}

void draw_thread_proc()
{
    static float next_value = 0.0;
    setup_background_layer();
    setup_foreground_layer();
    draw_graph_window(4);
    
    lcd.SelectLayer(FOREGROUND);
   
    for (int i = 0; i < lcd.GetXSize(); i++)
        graph_pixels[i] = lcd.GetXSize();
    
    while (1)
    {
        // wait for main thread to release a semaphore,
        // to indicate a new sample is ready to be graphed
        new_values_semaphore.acquire();
        new_values.pop(next_value);
        for (int i = 0; i < lcd.GetXSize(); i++)
        {
            // the x coordinate of the graph value being updated.
            // think about it like a vertical line
            // that sweeps across the graph from left to right,
            // updating each point in the graph as it travels across.
            const uint32_t target_x_coord = i;
            // y coordinate of the previous function value at the target x coordinate
            const uint32_t old_pixelY = graph_pixels[(i + next_write_index) % (lcd.GetXSize() - 1)];
            // y coordinate of the current function value at the target x coordinate
            const uint32_t new_pixelY = graph_pixels[(i + next_write_index + 1) % (lcd.GetXSize() - 1)];
            // remove (set to black) the old pixel for the current x coordinate
            // from the screen
            lcd.DrawPixel(target_x_coord, old_pixelY, LCD_COLOR_BLACK);
            // draw the new pixel for the current x coordinate on the screen
            lcd.DrawPixel(target_x_coord, new_pixelY, LCD_COLOR_GREEN);
        }
        // retrieve and erase the right most(last) pixel in the graph
        const uint32_t last_old_pixelY = graph_pixels[((lcd.GetXSize() - 1) - 1 + next_write_index) % (lcd.GetXSize() - 1)];
        lcd.DrawPixel((lcd.GetXSize() - 1) - 1, last_old_pixelY, LCD_COLOR_BLACK);
        // map, draw and store the newest value
        graph_pixels[next_write_index] = mapPixelY(next_value, 0, 0.2, 0, graph_height);
        lcd.DrawPixel((lcd.GetXSize() - 1) - 1, graph_pixels[next_write_index], LCD_COLOR_GREEN);
        next_write_index = (next_write_index + 1) % (lcd.GetXSize() - 1);
    }
}

// ================================================= FUNCTIONS =================================================

int main()
{
    draw_thread.start(draw_thread_proc);
    thread_sleep_for(1000);

    while (true)
    {
        if (!new_values.full())
        {
            float current_val = ain.read();
            printf("%f\n", current_val);
            snprintf(display_buf[0], 60, "Cur: %f", current_val);
            lcd.DisplayStringAtLine(16, (uint8_t *)display_buf[0]);

            // push the next value into the circular buffer
            new_values.push(current_val);

            if (new_values_semaphore.release() != osOK)
                MBED_ERROR(MBED_MAKE_ERROR(MBED_MODULE_APPLICATION, MBED_ERROR_CODE_OUT_OF_MEMORY), "semaphore overflow\r\n");
        }
        else
            MBED_ERROR(MBED_MAKE_ERROR(MBED_MODULE_APPLICATION, MBED_ERROR_CODE_OUT_OF_MEMORY), "circular buffer is full\r\n");

        thread_sleep_for(10);
    }
}