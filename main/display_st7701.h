#pragma once

#include "esp_err.h"

/** Инициализирует MIPI-DSI-панель и выводит альбомное главное меню. */
esp_err_t display_st7701_show_menu(void);
