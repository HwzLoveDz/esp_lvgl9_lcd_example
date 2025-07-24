/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_gc9d01.h"
#include "lvgl.h"
#include "esp_timer.h"
#include "img_bulb_gif.h"

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
#define EXAMPLE_LCD_DRAW_BUFF_HEIGHT (160) // 局部刷新缓冲区高度

/* LCD pins */
#define EXAMPLE_LCD_GPIO_SCLK       (GPIO_NUM_39)
#define EXAMPLE_LCD_GPIO_MOSI       (GPIO_NUM_38)
#define EXAMPLE_LCD_GPIO_RST        (GPIO_NUM_45)
#define EXAMPLE_LCD_GPIO_DC         (GPIO_NUM_40)
#define EXAMPLE_LCD_GPIO_CS0        (GPIO_NUM_47)
#define EXAMPLE_LCD_GPIO_CS1        (GPIO_NUM_48)

#define LCD_SCREEN_NUM 2

static const char *TAG = "EXAMPLE";

// 屏幕描述结构体
typedef struct {
    int cs_gpio_num;
    const char *name;
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
    lv_disp_drv_t *disp_drv; // 新增
} lcd_screen_t;

static lcd_screen_t lcd_screens[LCD_SCREEN_NUM] = {
    { .cs_gpio_num = EXAMPLE_LCD_GPIO_CS0, .name = "LEFT EYE" },
    { .cs_gpio_num = EXAMPLE_LCD_GPIO_CS1, .name = "RIGHT EYE" },
};

// LVGL缓冲区
// static lv_color_t buf1[LCD_SCREEN_NUM][EXAMPLE_LCD_H_RES * EXAMPLE_LCD_DRAW_BUFF_HEIGHT];
// static lv_color_t buf2[LCD_SCREEN_NUM][EXAMPLE_LCD_H_RES * EXAMPLE_LCD_DRAW_BUFF_HEIGHT];
static lv_disp_t *disp[LCD_SCREEN_NUM] = {NULL};

// SPI初始化
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

static bool lv_port_flush_ready(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_disp_drv_t *disp_drv = (lv_disp_drv_t *)user_ctx;
    lv_disp_flush_ready(disp_drv);
    return false;
}

// 屏幕底层初始化
static esp_err_t lcd_screen_init(lcd_screen_t *screen)
{
    esp_err_t ret = ESP_OK;
    ESP_LOGI(screen->name, "Install panel IO");
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = EXAMPLE_LCD_GPIO_DC,
        .cs_gpio_num = screen->cs_gpio_num,
        .pclk_hz = EXAMPLE_LCD_PIXEL_CLK_HZ,
        .lcd_cmd_bits = EXAMPLE_LCD_CMD_BITS,
        .lcd_param_bits = EXAMPLE_LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = lv_port_flush_ready,
        .user_ctx = screen->disp_drv, // 关键
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)EXAMPLE_LCD_SPI_NUM, &io_config, &screen->io), err, screen->name, "New panel IO failed");

    ESP_LOGI(screen->name, "Install LCD driver");
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_LCD_GPIO_RST,
        .color_space = EXAMPLE_LCD_COLOR_SPACE,
        .bits_per_pixel = EXAMPLE_LCD_BITS_PER_PIXEL,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_gc9d01(screen->io, &panel_config, &screen->panel), err, screen->name, "New panel failed");

    esp_lcd_panel_reset(screen->panel);
    esp_lcd_panel_init(screen->panel);
    esp_lcd_panel_invert_color(screen->panel, false);
    esp_lcd_panel_mirror(screen->panel, false, false);
    esp_lcd_panel_swap_xy(screen->panel, false);
    esp_lcd_panel_disp_on_off(screen->panel, true);
    return ret;
err:
    if (screen->panel) esp_lcd_panel_del(screen->panel);
    if (screen->io) esp_lcd_panel_io_del(screen->io);
    return ret;
}

// LVGL flush_cb，每个屏幕一个
static void my_flush_cb_0(lv_disp_drv_t * disp_drv, const lv_area_t *area, lv_color_t *px_map)
{
    lcd_screen_t *screen = &lcd_screens[0];
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    esp_lcd_panel_draw_bitmap(screen->panel, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, px_map);
    lv_disp_flush_ready(disp_drv);
}
static void my_flush_cb_1(lv_disp_drv_t * disp_drv, const lv_area_t *area, lv_color_t *px_map)
{
    lcd_screen_t *screen = &lcd_screens[1];
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    esp_lcd_panel_draw_bitmap(screen->panel, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, px_map);
    lv_disp_flush_ready(disp_drv);
}

void lv_tick_task(void* arg)
{
    (void) arg;
    lv_tick_inc(1);
}

static esp_err_t lv_port_tick_init(void)
{
    // 启动LVGL tick定时器（esp_timer，精度高于FreeRTOS软件定时器）
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &lv_tick_task,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 1000)); // 1ms

    return ESP_OK;
}

LV_IMG_DECLARE(img_bulb_gif0);
LV_IMG_DECLARE(img_bulb_gif1);

void create_demo_app(void)
{

    // 屏幕0（左眼）
    lv_disp_set_default(disp[0]);
    lv_obj_t *scr0 = lv_obj_create(NULL);
    lv_scr_load(scr0);

    lv_obj_t *gif_left = lv_gif_create(scr0);
    lv_gif_set_src(gif_left, &img_bulb_gif1);
    lv_obj_align(gif_left, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *label_left = lv_label_create(scr0);
    lv_label_set_text(label_left, "L");
    lv_obj_align_to(label_left, gif_left, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    // 屏幕1（右眼）
    lv_disp_set_default(disp[1]);
    lv_obj_t *scr1 = lv_obj_create(NULL);
    lv_scr_load(scr1);

    lv_obj_t *gif_right = lv_gif_create(scr1);
    lv_gif_set_src(gif_right, &img_bulb_gif0);
    lv_obj_align(gif_right, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *label_right = lv_label_create(scr1);
    lv_label_set_text(label_right, "R");
    lv_obj_align_to(label_right, gif_right, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    lv_disp_set_default(disp[0]);
}

/* Creates a semaphore to handle concurrent call to lvgl stuff
 * If you wish to call *any* lvgl function from other threads/tasks
 * you should lock on the very same semaphore! */
SemaphoreHandle_t xGuiSemaphore;

void uiTask(void *pvParameters)
{
    (void) pvParameters;
    xGuiSemaphore = xSemaphoreCreateMutex();

    lv_init();

    ESP_ERROR_CHECK(InitializeSpi());

    // 分配LVGL缓冲区
    static lv_color_t* buf1[LCD_SCREEN_NUM];
    static lv_color_t* buf2[LCD_SCREEN_NUM];
    static lv_disp_draw_buf_t draw_buf[LCD_SCREEN_NUM];
    static lv_disp_drv_t disp_drv[LCD_SCREEN_NUM];

    for (int i = 0; i < LCD_SCREEN_NUM; ++i) {
        buf1[i] = heap_caps_malloc(EXAMPLE_LCD_H_RES * EXAMPLE_LCD_DRAW_BUFF_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_DMA);
        assert(buf1[i]);
#ifndef CONFIG_LV_TFT_DISPLAY_MONOCHROME
        buf2[i] = heap_caps_malloc(EXAMPLE_LCD_H_RES * EXAMPLE_LCD_DRAW_BUFF_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_DMA);
        assert(buf2[i]);
#else
        buf2[i] = NULL;
#endif
        lv_disp_draw_buf_init(&draw_buf[i], buf1[i], buf2[i], EXAMPLE_LCD_H_RES * EXAMPLE_LCD_DRAW_BUFF_HEIGHT);

        lv_disp_drv_init(&disp_drv[i]);
        disp_drv[i].hor_res = EXAMPLE_LCD_H_RES;
        disp_drv[i].ver_res = EXAMPLE_LCD_V_RES;
        disp_drv[i].draw_buf = &draw_buf[i];
        disp_drv[i].user_data = &lcd_screens[i];
        disp_drv[i].flush_cb = (i == 0) ? my_flush_cb_0 : my_flush_cb_1;
        disp[i] = lv_disp_drv_register(&disp_drv[i]);
        lcd_screens[i].disp_drv = &disp_drv[i]; // 关键：赋值
    }

    // 初始化所有屏幕（此时 screen->disp_drv 已经有值）
    for (int i = 0; i < LCD_SCREEN_NUM; ++i) {
        ESP_ERROR_CHECK(lcd_screen_init(&lcd_screens[i]));
    }

    lv_port_tick_init();
    create_demo_app();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        if (pdTRUE == xSemaphoreTake(xGuiSemaphore, portMAX_DELAY)) {
            lv_task_handler();
            xSemaphoreGive(xGuiSemaphore);
        }
    }

    // 资源释放（理论上不会执行到）
    for (int i = 0; i < LCD_SCREEN_NUM; ++i) {
        free(buf1[i]);
#ifndef CONFIG_LV_TFT_DISPLAY_MONOCHROME
        free(buf2[i]);
#endif
    }
    vTaskDelete(NULL);
}

void app_main(void)
{

    xTaskCreatePinnedToCore(uiTask, "lv_ui_Task", 1024 * 8, NULL, 5, NULL, 1);
}
