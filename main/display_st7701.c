/*
 * Первый эксперимент с панелью JC-ESP32P4-M3.
 *
 * Конфигурация основана на открытом BSP для близкой платы Guition
 * JC4880P443C_I_W: ST7701S, MIPI-DSI, 480x800, две линии DSI.
 * CST3530 намеренно не используется: это отдельный контроллер touch.
 */

#include "display_st7701.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "menu_font.h"

static const char *TAG = "display_st7701";

#define LCD_WIDTH 480
#define LCD_HEIGHT 800
#define LCD_RESET_GPIO 5
#define LCD_BACKLIGHT_GPIO 23
#define FONT_HEIGHT 24
#define UI_WIDTH LCD_HEIGHT
#define UI_HEIGHT LCD_WIDTH

typedef struct {
    uint8_t command;
    const uint8_t *data;
    size_t data_size;
    uint32_t delay_ms;
} panel_command_t;

/* Команды настройки ST7701S для панели 480x800. */
static const uint8_t cmd_ff_13[] = {0x77, 0x01, 0x00, 0x00, 0x13};
static const uint8_t cmd_ef_08[] = {0x08};
static const uint8_t cmd_ff_10[] = {0x77, 0x01, 0x00, 0x00, 0x10};
static const uint8_t cmd_c0[] = {0x63, 0x00};
static const uint8_t cmd_c1[] = {0x0D, 0x02};
static const uint8_t cmd_c2[] = {0x10, 0x08};
static const uint8_t cmd_cc[] = {0x10};
static const uint8_t cmd_b0_gamma[] = {0x80,0x09,0x53,0x0C,0xD0,0x07,0x0C,0x09,0x09,0x28,0x06,0xD4,0x13,0x69,0x2B,0x71};
static const uint8_t cmd_b1_gamma[] = {0x80,0x94,0x5A,0x10,0xD3,0x06,0x0A,0x08,0x08,0x25,0x03,0xD3,0x12,0x66,0x6A,0x0D};
static const uint8_t cmd_ff_11[] = {0x77, 0x01, 0x00, 0x00, 0x11};
static const uint8_t cmd_b0[] = {0x5D};
static const uint8_t cmd_b1[] = {0x58};
static const uint8_t cmd_b2[] = {0x87};
static const uint8_t cmd_b3[] = {0x80};
static const uint8_t cmd_b5[] = {0x4E};
static const uint8_t cmd_b7[] = {0x85};
static const uint8_t cmd_b8[] = {0x21};
static const uint8_t cmd_b9[] = {0x10, 0x1F};
static const uint8_t cmd_bb[] = {0x03};
static const uint8_t cmd_bc[] = {0x00};
static const uint8_t cmd_c1_78[] = {0x78};
static const uint8_t cmd_c2_78[] = {0x78};
static const uint8_t cmd_d0[] = {0x88};
static const uint8_t cmd_e0[] = {0x00, 0x3A, 0x02};
static const uint8_t cmd_e1[] = {0x04,0xA0,0x00,0xA0,0x05,0xA0,0x00,0xA0,0x00,0x40,0x40};
static const uint8_t cmd_e2[] = {0x30,0x00,0x40,0x40,0x32,0xA0,0x00,0xA0,0x00,0xA0,0x00,0xA0,0x00};
static const uint8_t cmd_e3[] = {0x00,0x00,0x33,0x33};
static const uint8_t cmd_e4[] = {0x44,0x44};
static const uint8_t cmd_e5[] = {0x09,0x2E,0xA0,0xA0,0x0B,0x30,0xA0,0xA0,0x05,0x2A,0xA0,0xA0,0x07,0x2C,0xA0,0xA0};
static const uint8_t cmd_e6[] = {0x00,0x00,0x33,0x33};
static const uint8_t cmd_e7[] = {0x44,0x44};
static const uint8_t cmd_e8[] = {0x08,0x2D,0xA0,0xA0,0x0A,0x2F,0xA0,0xA0,0x04,0x29,0xA0,0xA0,0x06,0x2B,0xA0,0xA0};
static const uint8_t cmd_eb[] = {0x00,0x00,0x4E,0x4E,0x00,0x00,0x00};
static const uint8_t cmd_ec[] = {0x08,0x01};
static const uint8_t cmd_ed[] = {0xB0,0x2B,0x98,0xA4,0x56,0x7F,0xFF,0xFF,0xFF,0xFF,0xF7,0x65,0x4A,0x89,0xB2,0x0B};
static const uint8_t cmd_ef[] = {0x08,0x08,0x08,0x45,0x3F,0x54};
static const uint8_t cmd_ff_00[] = {0x77, 0x01, 0x00, 0x00, 0x00};
static const uint8_t cmd_nop[] = {0x00};

#define CMD(c, p) {c, p, sizeof(p), 0}
static const panel_command_t panel_commands[] = {
    CMD(0xFF, cmd_ff_13), CMD(0xEF, cmd_ef_08), CMD(0xFF, cmd_ff_10),
    CMD(0xC0, cmd_c0), CMD(0xC1, cmd_c1), CMD(0xC2, cmd_c2), CMD(0xCC, cmd_cc),
    CMD(0xB0, cmd_b0_gamma), CMD(0xB1, cmd_b1_gamma), CMD(0xFF, cmd_ff_11),
    CMD(0xB0, cmd_b0), CMD(0xB1, cmd_b1), CMD(0xB2, cmd_b2), CMD(0xB3, cmd_b3),
    CMD(0xB5, cmd_b5), CMD(0xB7, cmd_b7), CMD(0xB8, cmd_b8), CMD(0xB9, cmd_b9),
    CMD(0xBB, cmd_bb), CMD(0xBC, cmd_bc), CMD(0xC1, cmd_c1_78), CMD(0xC2, cmd_c2_78),
    CMD(0xD0, cmd_d0), CMD(0xE0, cmd_e0), CMD(0xE1, cmd_e1), CMD(0xE2, cmd_e2),
    CMD(0xE3, cmd_e3), CMD(0xE4, cmd_e4), CMD(0xE5, cmd_e5), CMD(0xE6, cmd_e6),
    CMD(0xE7, cmd_e7), CMD(0xE8, cmd_e8), CMD(0xEB, cmd_eb), CMD(0xEC, cmd_ec),
    CMD(0xED, cmd_ed), CMD(0xEF, cmd_ef), CMD(0xFF, cmd_ff_00),
    {0x11, cmd_nop, sizeof(cmd_nop), 120}, {0x29, cmd_nop, sizeof(cmd_nop), 20},
};

/** Читает один UTF-8 символ и передвигает указатель на следующий. */
static uint32_t utf8_next_codepoint(const char **cursor)
{
    const uint8_t *bytes = (const uint8_t *)*cursor;
    uint32_t codepoint = 0xFFFD;
    size_t byte_count = 1;

    if (bytes[0] < 0x80) {
        codepoint = bytes[0];
    } else if ((bytes[0] & 0xE0) == 0xC0 && bytes[1] != '\0') {
        codepoint = ((uint32_t)(bytes[0] & 0x1F) << 6) | (bytes[1] & 0x3F);
        byte_count = 2;
    } else if ((bytes[0] & 0xF0) == 0xE0 && bytes[1] != '\0' && bytes[2] != '\0') {
        codepoint = ((uint32_t)(bytes[0] & 0x0F) << 12) |
                    ((uint32_t)(bytes[1] & 0x3F) << 6) | (bytes[2] & 0x3F);
        byte_count = 3;
    }

    *cursor += byte_count;
    return codepoint;
}

/** Находит растровый глиф по его коду Unicode. */
static const menu_glyph_t *find_ui_glyph(uint32_t codepoint)
{
    for (size_t glyph_index = 0;
         glyph_index < sizeof(menu_glyphs) / sizeof(menu_glyphs[0]);
         ++glyph_index) {
        if (menu_glyphs[glyph_index].codepoint == codepoint) {
            return &menu_glyphs[glyph_index];
        }
    }
    return NULL;
}

/** Смешивает передний план с цветом плитки для сглаживания шрифта. */
static uint16_t blend_rgb565(uint16_t foreground, uint16_t background, uint8_t coverage)
{
    const uint32_t inverse = 3U - coverage;
    const uint32_t red = ((((foreground >> 11) & 0x1F) * coverage) +
                          (((background >> 11) & 0x1F) * inverse) + 1U) / 3U;
    const uint32_t green = ((((foreground >> 5) & 0x3F) * coverage) +
                            (((background >> 5) & 0x3F) * inverse) + 1U) / 3U;
    const uint32_t blue = (((foreground & 0x1F) * coverage) +
                           ((background & 0x1F) * inverse) + 1U) / 3U;
    return (uint16_t)((red << 11) | (green << 5) | blue);
}

/** Переводит логическую альбомную точку в родной портретный framebuffer панели. */
static void set_landscape_pixel(uint16_t *pixels, int x, int y, uint16_t color)
{
    if (x < 0 || x >= UI_WIDTH || y < 0 || y >= UI_HEIGHT) {
        return;
    }
    const int native_x = y;
    const int native_y = LCD_HEIGHT - 1 - x;
    pixels[native_y * LCD_WIDTH + native_x] = color;
}

/** Заполняет прямоугольник цветом в альбомных координатах. */
static void fill_landscape_rect(uint16_t *pixels, int left, int top, int width, int height,
                                uint16_t color)
{
    for (int y = top; y < top + height; ++y) {
        for (int x = left; x < left + width; ++x) {
            set_landscape_pixel(pixels, x, y, color);
        }
    }
}

/** Рисует сглаженную надпись по центру области альбомного экрана. */
static esp_err_t draw_ui_text(uint16_t *pixels, const char *text, int center_x, int top,
                              uint16_t foreground, uint16_t background)
{
    ESP_RETURN_ON_FALSE(pixels != NULL && text != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "буфер и строка меню обязательны");
    if (*text == '\0') {
        return ESP_OK;
    }

    int line_width = 0;
    const char *counter = text;
    while (*counter != '\0') {
        const menu_glyph_t *glyph = find_ui_glyph(utf8_next_codepoint(&counter));
        ESP_RETURN_ON_FALSE(glyph != NULL, ESP_ERR_NOT_FOUND, TAG,
                            "в шрифте меню отсутствует символ строки");
        line_width += glyph->advance;
    }
    ESP_RETURN_ON_FALSE(line_width <= UI_WIDTH && top >= 0 && top + FONT_HEIGHT <= UI_HEIGHT,
                        ESP_ERR_INVALID_SIZE, TAG, "строка интерфейса не помещается на экран");

    int cursor_x = center_x - line_width / 2;
    const char *character = text;
    while (*character != '\0') {
        const uint32_t codepoint = utf8_next_codepoint(&character);
        const menu_glyph_t *glyph = find_ui_glyph(codepoint);
        const uint8_t *bitmap = glyph->bitmap;
        for (int row = 0; row < glyph->height; ++row) {
            for (int column = 0; column < glyph->width; ++column) {
                const size_t bit_index = (size_t)row * glyph->width + column;
                const uint8_t packed = bitmap[bit_index / 4U];
                const uint8_t coverage = (packed >> (6U - 2U * (bit_index % 4U))) & 0x03U;
                if (coverage != 0) {
                    const int pixel_x = cursor_x + glyph->bearing_x + column;
                    const int pixel_y = top + FONT_HEIGHT - 3 + glyph->bearing_y + row;
                    set_landscape_pixel(pixels, pixel_x, pixel_y,
                                        blend_rgb565(foreground, background, coverage));
                }
            }
        }
        cursor_x += glyph->advance;
    }
    return ESP_OK;
}

/** Рисует альбомное меню с шестью цветными плитками на чёрно-синем фоне. */
static esp_err_t draw_main_menu(uint16_t *pixels)
{
    const uint16_t background = 0x0863;
    const uint16_t title_color = 0xFFFF;
    const uint16_t tile_colors[6] = {0x249F, 0x04EF, 0xB220, 0x79F3, 0x43A8, 0xA31F};
    const char *tile_titles[6] = {
        "ОСЦИЛЛОГРАФ", "НАСТРОЙКИ", "РЕЗЕРВ", "РЕЗЕРВ", "ПОМОЩЬ", "О ПРОГРАММЕ",
    };
    const int tile_left[6] = {20, 410, 20, 410, 20, 410};
    const int tile_top[6] = {78, 78, 210, 210, 342, 342};
    const int tile_width = 370;
    const int tile_height = 112;

    for (int y = 0; y < UI_HEIGHT; ++y) {
        for (int x = 0; x < UI_WIDTH; ++x) {
            set_landscape_pixel(pixels, x, y, background);
        }
    }
    fill_landscape_rect(pixels, 0, 0, UI_WIDTH, 66, 0x10A5);
    fill_landscape_rect(pixels, 20, 65, UI_WIDTH - 40, 2, 0x39E7);
    ESP_RETURN_ON_ERROR(draw_ui_text(pixels, "ОСЦИЛЛОГРАФ", UI_WIDTH / 2, 20,
                                     title_color, 0x10A5),
                        TAG, "не удалось нарисовать заголовок меню");

    for (int tile_index = 0; tile_index < 6; ++tile_index) {
        const int tile_center_x = tile_left[tile_index] + tile_width / 2;
        const int text_top = tile_top[tile_index] + (tile_height - FONT_HEIGHT) / 2;
        fill_landscape_rect(pixels, tile_left[tile_index], tile_top[tile_index],
                            tile_width, tile_height, tile_colors[tile_index]);
        ESP_RETURN_ON_ERROR(draw_ui_text(pixels, tile_titles[tile_index], tile_center_x,
                                         text_top, title_color, tile_colors[tile_index]),
                            TAG, "не удалось нарисовать пункт меню %d", tile_index + 1);
    }
    return ESP_OK;
}

/** Включает подсветку панели через подтверждённый для этой платы GPIO23. */
static esp_err_t enable_backlight(void)
{
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << LCD_BACKLIGHT_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "не удалось настроить GPIO подсветки");
    ESP_RETURN_ON_ERROR(gpio_set_level(LCD_BACKLIGHT_GPIO, 1), TAG, "не удалось включить подсветку");
    return ESP_OK;
}

/** Инициализирует панель и показывает шесть пунктов главного меню. */
esp_err_t display_st7701_show_menu(void)
{
    esp_ldo_channel_handle_t ldo = NULL;
    esp_lcd_dsi_bus_handle_t dsi_bus = NULL;
    esp_lcd_panel_io_handle_t dbi_io = NULL;
    esp_lcd_panel_handle_t panel = NULL;
    void *framebuffer = NULL;

    const esp_ldo_channel_config_t ldo_config = {.chan_id = 3, .voltage_mv = 2500};
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_config, &ldo), TAG, "не удалось включить DSI LDO");

    const esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0, .num_data_lanes = 2,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = 500,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_config, &dsi_bus), TAG, "не удалось создать DSI bus");

    const esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0, .lcd_cmd_bits = 8, .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_config, &dbi_io), TAG, "не удалось создать DBI IO");

    const esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 34,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .out_color_format = LCD_COLOR_FMT_RGB565,
        .num_fbs = 1,
        .video_timing = {
            .h_size = LCD_WIDTH, .v_size = LCD_HEIGHT,
            .hsync_pulse_width = 12, .hsync_back_porch = 42, .hsync_front_porch = 42,
            .vsync_pulse_width = 2, .vsync_back_porch = 8, .vsync_front_porch = 166,
        },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_dpi(dsi_bus, &dpi_config, &panel), TAG, "не удалось создать DPI panel");

    gpio_config_t reset_config = {
        .pin_bit_mask = 1ULL << LCD_RESET_GPIO, .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&reset_config), TAG, "не удалось настроить reset панели");
    ESP_RETURN_ON_ERROR(gpio_set_level(LCD_RESET_GPIO, 0), TAG, "не удалось опустить reset панели");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(gpio_set_level(LCD_RESET_GPIO, 1), TAG, "не удалось отпустить reset панели");
    vTaskDelay(pdMS_TO_TICKS(120));

    for (size_t i = 0; i < sizeof(panel_commands) / sizeof(panel_commands[0]); ++i) {
        const panel_command_t *item = &panel_commands[i];
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(dbi_io, item->command, item->data, item->data_size), TAG,
                            "ошибка команды ST7701S 0x%02X", item->command);
        if (item->delay_ms != 0) {
            vTaskDelay(pdMS_TO_TICKS(item->delay_ms));
        }
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "не удалось завершить init панели");
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_get_frame_buffer(panel, 1, &framebuffer), TAG, "нет framebuffer панели");

    /* Рисует пункты меню в альбомных координатах с поворотом в родной буфер. */
    uint16_t *pixels = framebuffer;
    memset(pixels, 0x00, LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t));
    ESP_RETURN_ON_ERROR(draw_main_menu(pixels), TAG, "не удалось нарисовать главное меню");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(panel, 0, 0, LCD_WIDTH, LCD_HEIGHT, pixels), TAG,
                        "не удалось показать framebuffer");
    ESP_RETURN_ON_ERROR(enable_backlight(), TAG, "не удалось включить подсветку");
    ESP_LOGI(TAG, "панель инициализирована; показано меню 800x480, сенсор пока не подключён к обработчику");
    return ESP_OK;
}
