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
#include "esp_lcd_gc9d01.h"
#include "lv_examples.h"

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
#define EXAMPLE_LCD_DRAW_BUFF_HEIGHT (EXAMPLE_LCD_V_RES)  // 全刷缓冲区必须比分辨率高，局部刷新可以小于分辨率
// #define EXAMPLE_LCD_BL_ON_LEVEL     (1)

/* LCD pins */
#define EXAMPLE_LCD_GPIO_SCLK       (GPIO_NUM_39)
#define EXAMPLE_LCD_GPIO_MOSI       (GPIO_NUM_38)
#define EXAMPLE_LCD_GPIO_RST        (GPIO_NUM_45)
#define EXAMPLE_LCD_GPIO_DC         (GPIO_NUM_40)
#define EXAMPLE_LCD_GPIO_CS0         (GPIO_NUM_47)
#define EXAMPLE_LCD_GPIO_CS1         (GPIO_NUM_48)
// #define EXAMPLE_LCD_GPIO_BL         (GPIO_NUM_NC)

typedef enum {
    LCD_SCREEN_0 = 0,   // 左眼屏幕
    LCD_SCREEN_1,       // 右眼屏幕
    LCD_SCREEN_NUM,     // 屏幕数量
}lcd_screen_num_t;

typedef struct {
    /* 初始化等都是相同的 */
    esp_lcd_panel_io_handle_t io;   // LCD面板IO句柄
    esp_lcd_panel_handle_t panel;   // LCD面板句柄
    lv_display_t *disp;             // LVGL显示句柄
    /* CS脚这些是不同的 */
    int cs_gpio_num;                // 片选GPIO编号
    const char *name;               // 屏幕名称
    lcd_screen_num_t num;           // 屏幕编号
    /* 屏幕方向相关 */
    bool mirror_x;                  // X方向镜像
    bool mirror_y;                  // Y方向镜像
    bool swap_xy;                   // 交换XY
} lcd_screen_t;

// 配置每块屏幕的方向
static lcd_screen_t lcd_screens[LCD_SCREEN_NUM] = {
    { .cs_gpio_num = EXAMPLE_LCD_GPIO_CS0, .name = "LEFT EYE",  .num = LCD_SCREEN_0, .mirror_x = false, .mirror_y = false, .swap_xy = false},
    { .cs_gpio_num = EXAMPLE_LCD_GPIO_CS1, .name = "RIGHT EYE", .num = LCD_SCREEN_1, .mirror_x = false, .mirror_y = false, .swap_xy = false},
};

static const char *TAG = "EXAMPLE";

static esp_err_t InitializeSpi()
{
    esp_err_t ret = ESP_OK;
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
    return ret;
}

static esp_err_t lcd_screen_init(lcd_screen_t *screen)
{
    esp_err_t ret = ESP_OK;

    /* LCD backlight */
    // gpio_config_t bk_gpio_config = {
    //     .mode = GPIO_MODE_OUTPUT,
    //     .pin_bit_mask = 1ULL << EXAMPLE_LCD_GPIO_BL
    // };
    // ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));

    /* LCD initialization */
    ESP_LOGI(screen->name, "Install panel IO");
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = EXAMPLE_LCD_GPIO_DC,
        .cs_gpio_num = screen->cs_gpio_num,
        .pclk_hz = EXAMPLE_LCD_PIXEL_CLK_HZ,
        .lcd_cmd_bits = EXAMPLE_LCD_CMD_BITS,
        .lcd_param_bits = EXAMPLE_LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)EXAMPLE_LCD_SPI_NUM, &io_config, &screen->io), err, screen->name, "New panel IO failed");

    ESP_LOGI(TAG, "Install LCD driver");
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_LCD_GPIO_RST,
        .color_space = EXAMPLE_LCD_COLOR_SPACE,
        .bits_per_pixel = EXAMPLE_LCD_BITS_PER_PIXEL,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_gc9d01(screen->io, &panel_config, &screen->panel), err, screen->name, "New panel failed");

    esp_lcd_panel_reset(screen->panel);
    esp_lcd_panel_init(screen->panel);
    esp_lcd_panel_invert_color(screen->panel, false);
    esp_lcd_panel_mirror(screen->panel, screen->mirror_x, screen->mirror_y);
    esp_lcd_panel_swap_xy(screen->panel, screen->swap_xy);
    esp_lcd_panel_disp_on_off(screen->panel, false);
    if (screen->num == LCD_SCREEN_NUM - 1) {    // 最后一个屏幕初始化结束后再全部开启，避免屏幕反复闪烁多次
        esp_lcd_panel_disp_on_off(screen->panel, true);
        /* LCD backlight on */
        // ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_LCD_GPIO_BL, EXAMPLE_LCD_BL_ON_LEVEL));
    }

    return ret;

err:
    if (screen->panel) esp_lcd_panel_del(screen->panel);
    if (screen->io) esp_lcd_panel_io_del(screen->io);
    return ret;
}

static esp_err_t lcd_screen_lvgl_register(lcd_screen_t *screen)
{
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = screen->io,
        .panel_handle = screen->panel,
        .buffer_size = EXAMPLE_LCD_H_RES * EXAMPLE_LCD_DRAW_BUFF_HEIGHT,
        .double_buffer = EXAMPLE_LCD_DRAW_BUFF_DOUBLE,
        .hres = EXAMPLE_LCD_H_RES,
        .vres = EXAMPLE_LCD_V_RES,
        .monochrome = false,
#if LVGL_VERSION_MAJOR >= 9
        .color_format = LV_COLOR_FORMAT_RGB565,
#endif
        .rotation = {
            .swap_xy = screen->swap_xy,
            .mirror_x = screen->mirror_x,
            .mirror_y = screen->mirror_y,
        },
        .flags = {
            .buff_dma = true,
            .buff_spiram = true,
#if LVGL_VERSION_MAJOR >= 9
            .swap_bytes = true,
#endif
            .full_refresh = true,
        }
    };
    screen->disp = lvgl_port_add_disp(&disp_cfg);
    return (screen->disp != NULL) ? ESP_OK : ESP_FAIL;
}

void app_main(void)
{
    printf(" _______      ______       ______       ______       ______       ____        \r\n");
    printf("/______/\\    /_____/\\     /_____/\\     /_____/\\     /_____/\\     /___/\\       \r\n");
    printf("\\::::__\\/__  \\:::__\\/     \\:::_:\\ \\    \\:::_ \\ \\    \\:::_ \\ \\    \\_::\\ \\      \r\n");
    printf(" \\:\\ /____/\\  \\:\\ \\  __    \\:\\_\\:\\ \\    \\:\\ \\ \\ \\    \\:\\ \\ \\ \\     \\::\\ \\     \r\n");
    printf("  \\:\\\\_  _\\/   \\:\\ \\/_/\\    \\::__:\\ \\    \\:\\ \\ \\ \\    \\:\\ \\ \\ \\    _\\: \\ \\__  \r\n");
    printf("   \\:\\_\\ \\ \\    \\:\\_\\ \\ \\        \\ \\ \\    \\:\\/.:| |    \\:\\_\\ \\ \\  /__\\: \\__/\\ \r\n");
    printf("    \\_____\\/     \\_____\\/         \\_\\/     \\____/_/     \\_____\\/  \\________\\/ \r\n");
    printf("                                                                              \r\n");

    /* LVGL初始化（只需一次） */
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,
        .task_stack = 4096,
        .task_affinity = -1,
        .task_max_sleep_ms = 500,
        .timer_period_ms = 5
    };
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    /* 初始化SPI总线（只需一次） */
    ESP_ERROR_CHECK(InitializeSpi());

    /* 初始化所有屏幕并注册到LVGL */
    for (int i = 0; i < LCD_SCREEN_NUM; ++i) {
        ESP_ERROR_CHECK(lcd_screen_init(&lcd_screens[i]));
        ESP_ERROR_CHECK(lcd_screen_lvgl_register(&lcd_screens[i]));
    }

    /* 分别显示不同内容 */
    for (int i = 0; i < LCD_SCREEN_NUM; ++i) {
        lvgl_port_lock(0);
        /* 关键修改：设置当前操作的显示设备 */
        lv_disp_set_default(lcd_screens[i].disp);

        /* 为当前显示设备创建独立的屏幕对象 */
        lv_obj_t *scr = lv_obj_create(NULL);
        lv_disp_load_scr(scr);

        if (i == 0) {
            lv_obj_t *label = lv_label_create(scr);
            lv_label_set_text(label, "Hello LEFT Eye!");
            lv_obj_center(label);
        } else {
            lv_obj_t *label = lv_label_create(scr);
            lv_label_set_text(label, "Hello RIGHT Eye!");
            lv_obj_center(label);
            // 若需要显示GIF，使用以下代码替换：
            // lv_example_gif_1(); // 确保该函数使用当前默认显示设备
        }
        lvgl_port_unlock();
    }
}
