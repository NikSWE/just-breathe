#include <mbed.h>
#include "drivers/LCD_DISCO_F429ZI.h"
#include <chrono>

#define BACKGROUND 1
#define FOREGROUND 0

#define GRAPH_PADDING 20
#define Y_MIN -0.1
#define Y_MAX 0.27
#define BUFFER_SIZE 239

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
// uint32_t graph_width = lcd.GetXSize() - 1 * GRAPH_PADDING;
uint32_t max_graph_width = 239;

// uint32_t graph_height = graph_width;
uint32_t max_graph_height = 239 - 50;

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

// Timer for notifying when breathing stops for more than 10 secs
Timer timer;

// Blink LED when the baby is not breathing
DigitalOut led(LED3);
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

void reset_lcd()
{
    lcd.SelectLayer(BACKGROUND);
    lcd.Clear(LCD_COLOR_BLACK);
    lcd.SetBackColor(LCD_COLOR_BLACK);
    lcd.SetTextColor(LCD_COLOR_WHITE);
    lcd.SetLayerVisible(BACKGROUND, DISABLE);
    lcd.SetTransparency(BACKGROUND, 0x7Fu);
    lcd.SelectLayer(FOREGROUND);
    lcd.Clear(LCD_COLOR_BLACK);
    lcd.SetBackColor(LCD_COLOR_BLACK);
    lcd.SetTextColor(LCD_COLOR_RED);
}

// draws a rectangle with horizontal tick marks
// on the background layer. The spacing between tick
// marks in pixels is taken as a parameter
void draw_graph_window(uint32_t horiz_tick_spacing)
{
    lcd.SelectLayer(BACKGROUND);
    lcd.DrawRect(0, 0, max_graph_width, max_graph_height);
    for (int32_t i = 0; i < max_graph_width; i += horiz_tick_spacing)
        lcd.DrawVLine(i, max_graph_height - 20, 20);
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

    for (int i = 0; i < max_graph_width; i++)
        graph_pixels[i] = max_graph_height;

    while (1)
    {
        // wait for main thread to release a semaphore,
        // to indicate a new sample is ready to be graphed
        new_values_semaphore.acquire();
        new_values.pop(next_value);
        for (int i = 0; i < max_graph_width; i++)
        {
            // the x coordinate of the graph value being updated.
            // think about it like a vertical line
            // that sweeps across the graph from left to right,
            // updating each point in the graph as it travels across.
            const uint32_t target_x_coord = i;
            // y coordinate of the previous function value at the target x coordinate
            const uint32_t old_pixelY = graph_pixels[(i + next_write_index) % max_graph_width];
            // y coordinate of the current function value at the target x coordinate
            const uint32_t new_pixelY = graph_pixels[(i + next_write_index + 1) % max_graph_width];
            // remove (set to black) the old pixel for the current x coordinate
            // from the screen
            lcd.DrawPixel(target_x_coord, old_pixelY, LCD_COLOR_BLACK);
            // draw the new pixel for the current x coordinate on the screen
            lcd.DrawPixel(target_x_coord, new_pixelY, LCD_COLOR_GREEN);
        }
        // retrieve and erase the right most(last) pixel in the graph
        const uint32_t last_old_pixelY = graph_pixels[(max_graph_width - 1 + next_write_index) % max_graph_width];
        lcd.DrawPixel(max_graph_width - 1, last_old_pixelY, LCD_COLOR_BLACK);
        // map, draw and store the newest value
        graph_pixels[next_write_index] = mapPixelY(next_value, Y_MIN, Y_MAX, 0, max_graph_height - 20);
        lcd.DrawPixel(max_graph_width - 1, graph_pixels[next_write_index], LCD_COLOR_GREEN);
        next_write_index = (next_write_index + 1) % max_graph_width;
    }
}

// ================================================= FUNCTIONS =================================================

int main()
{
    led = 0;
    draw_thread.start(draw_thread_proc);
    thread_sleep_for(1000);

    float total = 0;
    float average;
    const int numReadings = 20;
    float readings[numReadings];
    float max_reading = -1;
    float min_reading = 5;

    for (int i = 0; i < numReadings; i++)
        readings[i] = 0;

    int readIndex = 0;
    int start_timestamp = -1;
    int st = -1;
    float average_temp;
    timer.start();
    bool is_breathing = true;
    while (is_breathing)
    {
        // printf("is breathing = %d\n", is_breathing);
        if (!new_values.full())
        {
            if (start_timestamp == -1)
                start_timestamp = std::chrono::duration_cast<std::chrono::seconds>(timer.elapsed_time()).count();
            float current_val = ain.read();
            if (current_val > max_reading)
                max_reading = current_val;
            if (current_val < min_reading)
                min_reading = current_val;
            total -= readings[readIndex];
            readings[readIndex] = current_val;
            total += readings[readIndex];
            readIndex++;
            if (readIndex >= numReadings)
                readIndex = 0;
            average = total / numReadings;
            if (st == -1)
            {
                st = std::chrono::duration_cast<std::chrono::seconds>(timer.elapsed_time()).count();
                average_temp = average;
            }

            int et = std::chrono::duration_cast<std::chrono::seconds>(timer.elapsed_time()).count();

            if (et - start_timestamp >= 10)
            {
                is_breathing = false;
            }

            if (et - st == 1)
            {
                printf("Second has elasped\n");
                printf("a: %f, at: %f, d: %f\n", average, average_temp, average - average_temp);
                if (abs(average - average_temp) > 0.01)
                {
                    start_timestamp = -1;
                }
                st = -1;
            }

            snprintf(display_buf[0], 60, "Monitoring");
            lcd.DisplayStringAt(0, max_graph_height + 10, (uint8_t *)display_buf[0], CENTER_MODE);
            snprintf(display_buf[0], 60, "Timer: %d s", et - start_timestamp);
            lcd.DisplayStringAt(0, max_graph_height + 30, (uint8_t *)display_buf[0], CENTER_MODE);
            snprintf(display_buf[0], 60, "Cur: %f", current_val);
            lcd.DisplayStringAt(0, max_graph_height + 60, (uint8_t *)display_buf[0], CENTER_MODE);
            snprintf(display_buf[0], 60, "Avg: %f", average);
            lcd.DisplayStringAt(0, max_graph_height + 75, (uint8_t *)display_buf[0], CENTER_MODE);
            snprintf(display_buf[0], 60, "Max: %f", max_reading);
            lcd.DisplayStringAt(0, max_graph_height + 90, (uint8_t *)display_buf[0], CENTER_MODE);
            snprintf(display_buf[0], 60, "Min: %f", min_reading);
            lcd.DisplayStringAt(0, max_graph_height + 105, (uint8_t *)display_buf[0], CENTER_MODE);

            // push the next value into the circular buffer
            new_values.push(average);

            if (new_values_semaphore.release() != osOK)
                MBED_ERROR(MBED_MAKE_ERROR(MBED_MODULE_APPLICATION, MBED_ERROR_CODE_OUT_OF_MEMORY), "semaphore overflow\r\n");
        }
        else
            MBED_ERROR(MBED_MAKE_ERROR(MBED_MODULE_APPLICATION, MBED_ERROR_CODE_OUT_OF_MEMORY), "circular buffer is full\r\n");

        thread_sleep_for(10);
    }

    reset_lcd();
    while (!is_breathing)
    {
        lcd.DisplayStringAt(0, lcd.GetYSize() / 2 - 20, (uint8_t *)"!!! Alert !!!", CENTER_MODE);
        lcd.DisplayStringAt(0, lcd.GetYSize() / 2 + 20, (uint8_t *)"Not Breathing", CENTER_MODE);
        led = !led;
        thread_sleep_for(100);
    }
}