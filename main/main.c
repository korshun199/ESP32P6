/* Точка входа приложения с главным экраном осциллографа. */

#include "esp_log.h"
#include "display_st7701.h"

/** Метка сообщений приложения в последовательном мониторе. */
static const char *TAG = "esp32p6_hello";

/**
 * Точка входа приложения ESP-IDF: пишет приветствие в UART и поднимает меню.
 */
void app_main(void)
{
    ESP_LOGI(TAG, "Привет, это я, Маша");
    esp_err_t display_result = display_st7701_show_menu();
    if (display_result != ESP_OK) {
        ESP_LOGE(TAG, "Инициализация дисплея не удалась: %s", esp_err_to_name(display_result));
        return;
    }
    ESP_LOGI(TAG, "ESP32-P4: главное меню показано; обработчик сенсора будет добавлен после проверки распиновки");
}
