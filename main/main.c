/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lvgl_port.h"
// #include "esp_lcd_gc9a01.h"
#include "esp_lcd_gc9d01.h"
#include "lv_examples.h"

// #include "esp_lcd_touch_tt21100.h"

/* LCD size */
#define EXAMPLE_LCD_H_RES   (160)
#define EXAMPLE_LCD_V_RES   (160)

/* LCD settings */
#define EXAMPLE_LCD_SPI_NUM         (SPI2_HOST)
#define EXAMPLE_LCD_PIXEL_CLK_HZ    (SPI_MASTER_FREQ_80M)
#define EXAMPLE_LCD_CMD_BITS        (8)
#define EXAMPLE_LCD_PARAM_BITS      (8)
#define EXAMPLE_LCD_COLOR_SPACE     (ESP_LCD_COLOR_SPACE_BGR)
#define EXAMPLE_LCD_BITS_PER_PIXEL  (16)
#define EXAMPLE_LCD_DRAW_BUFF_DOUBLE (1)
#define EXAMPLE_LCD_DRAW_BUFF_HEIGHT (160)  // 全刷缓冲区必须比分辨率高，局部刷新可以小于分辨率
// #define EXAMPLE_LCD_BL_ON_LEVEL     (1)

/* LCD pins */
#define EXAMPLE_LCD_GPIO_SCLK       (GPIO_NUM_39)
#define EXAMPLE_LCD_GPIO_MOSI       (GPIO_NUM_38)
#define EXAMPLE_LCD_GPIO_RST        (GPIO_NUM_45)
#define EXAMPLE_LCD_GPIO_DC         (GPIO_NUM_40)
#define EXAMPLE_LCD_GPIO_CS0         (GPIO_NUM_47)
#define EXAMPLE_LCD_GPIO_CS1         (GPIO_NUM_48)
// #define EXAMPLE_LCD_GPIO_BL         (GPIO_NUM_NC)

/* Touch settings */
// #define EXAMPLE_TOUCH_I2C_NUM       (0)
// #define EXAMPLE_TOUCH_I2C_CLK_HZ    (400000)

/* LCD touch pins */
// #define EXAMPLE_TOUCH_I2C_SCL       (GPIO_NUM_18)
// #define EXAMPLE_TOUCH_I2C_SDA       (GPIO_NUM_8)
// #define EXAMPLE_TOUCH_GPIO_INT      (GPIO_NUM_3)

static const char *TAG = "EXAMPLE";

// LVGL image declare
// LV_IMG_DECLARE(esp_logo)

/* LCD IO and panel */
static esp_lcd_panel_io_handle_t lcd_io = NULL;
static esp_lcd_panel_handle_t lcd_panel = NULL;
// static esp_lcd_touch_handle_t touch_handle = NULL;

/* LVGL display and touch */
static lv_display_t *lvgl_disp = NULL;
// static lv_indev_t *lvgl_touch_indev = NULL;

static esp_err_t app_lcd_init(void)
{
    esp_err_t ret = ESP_OK;

    /* LCD backlight */
    // gpio_config_t bk_gpio_config = {
    //     .mode = GPIO_MODE_OUTPUT,
    //     .pin_bit_mask = 1ULL << EXAMPLE_LCD_GPIO_BL
    // };
    // ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));

    /* LCD initialization */
    ESP_LOGI(TAG, "Initialize SPI bus");
    const spi_bus_config_t buscfg = {
        .sclk_io_num = EXAMPLE_LCD_GPIO_SCLK,
        .mosi_io_num = EXAMPLE_LCD_GPIO_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = EXAMPLE_LCD_H_RES * EXAMPLE_LCD_DRAW_BUFF_HEIGHT * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(EXAMPLE_LCD_SPI_NUM, &buscfg, SPI_DMA_CH_AUTO), TAG, "SPI init failed");

    ESP_LOGI(TAG, "Install panel IO");
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = EXAMPLE_LCD_GPIO_DC,
        .cs_gpio_num = EXAMPLE_LCD_GPIO_CS0,
        .pclk_hz = EXAMPLE_LCD_PIXEL_CLK_HZ,
        .lcd_cmd_bits = EXAMPLE_LCD_CMD_BITS,
        .lcd_param_bits = EXAMPLE_LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)EXAMPLE_LCD_SPI_NUM, &io_config, &lcd_io), err, TAG, "New panel IO failed");

    ESP_LOGI(TAG, "Install LCD driver");
    printf(" _______      ______       ______       ______       ______       ____        \r\n");
    printf("/______/\\    /_____/\\     /_____/\\     /_____/\\     /_____/\\     /___/\\       \r\n");
    printf("\\::::__\\/__  \\:::__\\/     \\:::_:\\ \\    \\:::_ \\ \\    \\:::_ \\ \\    \\_::\\ \\      \r\n");
    printf(" \\:\\ /____/\\  \\:\\ \\  __    \\:\\_\\:\\ \\    \\:\\ \\ \\ \\    \\:\\ \\ \\ \\     \\::\\ \\     \r\n");
    printf("  \\:\\\\_  _\\/   \\:\\ \\/_/\\    \\::__:\\ \\    \\:\\ \\ \\ \\    \\:\\ \\ \\ \\    _\\: \\ \\__  \r\n");
    printf("   \\:\\_\\ \\ \\    \\:\\_\\ \\ \\        \\ \\ \\    \\:\\/.:| |    \\:\\_\\ \\ \\  /__\\: \\__/\\ \r\n");
    printf("    \\_____\\/     \\_____\\/         \\_\\/     \\____/_/     \\_____\\/  \\________\\/ \r\n");
    printf("                                                                              \r\n");
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_LCD_GPIO_RST,
        .color_space = EXAMPLE_LCD_COLOR_SPACE,
        .bits_per_pixel = EXAMPLE_LCD_BITS_PER_PIXEL,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_gc9d01(lcd_io, &panel_config, &lcd_panel), err, TAG, "New panel failed");

    esp_lcd_panel_reset(lcd_panel);
    esp_lcd_panel_init(lcd_panel);
    esp_lcd_panel_invert_color(lcd_panel, false);
    esp_lcd_panel_mirror(lcd_panel, true, true);
    esp_lcd_panel_disp_on_off(lcd_panel, true);

    /* LCD backlight on */
    // ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_LCD_GPIO_BL, EXAMPLE_LCD_BL_ON_LEVEL));

    return ret;

err:
    if (lcd_panel) {
        esp_lcd_panel_del(lcd_panel);
    }
    if (lcd_io) {
        esp_lcd_panel_io_del(lcd_io);
    }
    spi_bus_free(EXAMPLE_LCD_SPI_NUM);
    return ret;
}

// static esp_err_t app_touch_init(void)
// {
//     /* Initilize I2C */
//     const i2c_config_t i2c_conf = {
//         .mode = I2C_MODE_MASTER,
//         .sda_io_num = EXAMPLE_TOUCH_I2C_SDA,
//         .sda_pullup_en = GPIO_PULLUP_DISABLE,
//         .scl_io_num = EXAMPLE_TOUCH_I2C_SCL,
//         .scl_pullup_en = GPIO_PULLUP_DISABLE,
//         .master.clk_speed = EXAMPLE_TOUCH_I2C_CLK_HZ
//     };
//     ESP_RETURN_ON_ERROR(i2c_param_config(EXAMPLE_TOUCH_I2C_NUM, &i2c_conf), TAG, "I2C configuration failed");
//     ESP_RETURN_ON_ERROR(i2c_driver_install(EXAMPLE_TOUCH_I2C_NUM, i2c_conf.mode, 0, 0, 0), TAG, "I2C initialization failed");

//     /* Initialize touch HW */
//     const esp_lcd_touch_config_t tp_cfg = {
//         .x_max = EXAMPLE_LCD_H_RES,
//         .y_max = EXAMPLE_LCD_V_RES,
//         .rst_gpio_num = GPIO_NUM_NC, // Shared with LCD reset
//         .int_gpio_num = EXAMPLE_TOUCH_GPIO_INT,
//         .levels = {
//             .reset = 0,
//             .interrupt = 0,
//         },
//         .flags = {
//             .swap_xy = 0,
//             .mirror_x = 1,
//             .mirror_y = 0,
//         },
//     };
//     esp_lcd_panel_io_handle_t tp_io_handle = NULL;
//     const esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_TT21100_CONFIG();
//     ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)EXAMPLE_TOUCH_I2C_NUM, &tp_io_config, &tp_io_handle), TAG, "");
//     return esp_lcd_touch_new_i2c_tt21100(tp_io_handle, &tp_cfg, &touch_handle);
// }

static esp_err_t app_lvgl_init(void)
{ 
    /* Initialize LVGL */
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,         /* LVGL task priority */
        .task_stack = 4096,         /* LVGL task stack size */
        .task_affinity = -1,        /* LVGL task pinned to core (-1 is no affinity) */
        .task_max_sleep_ms = 500,   /* Maximum sleep in LVGL task */
        .timer_period_ms = 5        /* LVGL timer tick period in ms */
    };
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL port initialization failed");

    /* Add LCD screen */
    ESP_LOGD(TAG, "Add LCD screen");
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = lcd_io,
        .panel_handle = lcd_panel,
        .buffer_size = EXAMPLE_LCD_H_RES * EXAMPLE_LCD_DRAW_BUFF_HEIGHT,
        .double_buffer = EXAMPLE_LCD_DRAW_BUFF_DOUBLE,
        .hres = EXAMPLE_LCD_H_RES,
        .vres = EXAMPLE_LCD_V_RES,
        .monochrome = false,
#if LVGL_VERSION_MAJOR >= 9
        .color_format = LV_COLOR_FORMAT_RGB565,
#endif
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,
            .buff_spiram = true,    // 双缓冲+DMA使用外部PSRAM
#if LVGL_VERSION_MAJOR >= 9
            .swap_bytes = true,
#endif
            .full_refresh = true,   // 这个屌屏幕驱动局部刷新会有乱点
        }
    };
    lvgl_disp = lvgl_port_add_disp(&disp_cfg);

    // /* Add touch input (for selected screen) */
    // const lvgl_port_touch_cfg_t touch_cfg = {
    //     .disp = lvgl_disp,
    //     .handle = touch_handle,
    // };
    // lvgl_touch_indev = lvgl_port_add_touch(&touch_cfg);

    return ESP_OK;
}

static void app_main_display(void)
{
    lv_obj_t *scr = lv_scr_act();

    /* Task lock */
    lvgl_port_lock(0);

    /* LCD HW rotation */
    // lv_disp_set_rotation(lvgl_disp, LV_DISPLAY_ROTATION_0);

    /* Your LVGL objects code here .... */

    lv_example_gif_1();

    /* Task unlock */
    lvgl_port_unlock();
}

void app_main(void)
{
    /* LCD HW initialization */
    ESP_ERROR_CHECK(app_lcd_init());

    /* Touch initialization */
    // ESP_ERROR_CHECK(app_touch_init());

    /* LVGL initialization */
    ESP_ERROR_CHECK(app_lvgl_init());

    /* Show LVGL objects */
    app_main_display();
}
