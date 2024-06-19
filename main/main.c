#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "bsp/esp-box-3.h"
#include "bsp_board.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "file_iterator.h"
#include <ctype.h>
#include "audio_player.h"
#include "bsp_board.h"


#define MOUNT_MAX_FILES 100

static const char *TAG = "main";
static const char *SUFFIX = ".png";
static const char *PREFIX = ".";
static lv_obj_t *img = NULL;
static lv_obj_t *label = NULL;

file_iterator_instance_t *file_iterator;
static lv_group_t *g_btn_op_group = NULL;

const char* get_next_image();
static void play_audio();

static esp_err_t mount_sdcard()
{
    gpio_config_t power_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << BSP_SD_POWER
    };
    ESP_ERROR_CHECK(gpio_config(&power_gpio_config));

    /* SD card power on first */
    ESP_ERROR_CHECK(gpio_set_level(BSP_SD_POWER, 0));

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = MOUNT_MAX_FILES,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
    slot_config.cmd = BSP_SD_CMD;
    slot_config.clk = BSP_SD_CLK;
    slot_config.d0 = BSP_SD_D0;
    slot_config.d1 = BSP_SD_D1;
    slot_config.d2 = BSP_SD_D2;
    slot_config.d3 = BSP_SD_D3;

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(BSP_SD_MOUNT_POINT, &host, &slot_config, &mount_config, &bsp_sdcard);

    sdmmc_card_print_info(stdout, bsp_sdcard);

    return ret;
}

static void init_indev()
{
    lv_indev_t *indev = lv_indev_get_next(NULL);
    if ((lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD) || \
            lv_indev_get_type(indev) == LV_INDEV_TYPE_ENCODER) {
        ESP_LOGI(TAG, "Input device type is keypad");
        g_btn_op_group = lv_group_create();
        lv_indev_set_group(indev, g_btn_op_group);
    }
    img = lv_img_create(lv_scr_act());
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_size(img, 320, 240);

    label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
}

static void image_show()
{
    while(1) 
    {
        const char *filename = get_next_image();
        if(filename == NULL) {
            lv_label_set_text(label, "No image found");
            vTaskDelay(10000 / portTICK_PERIOD_MS);
            continue;
        }

        char filename_with_path[256];
        strcpy(filename_with_path, "S:/sdcard/");
        strncat(filename_with_path, filename, strlen(filename) - strlen(SUFFIX));
        strcat(filename_with_path, SUFFIX);

        ESP_LOGI(TAG, "filename: %s", filename_with_path);

        lv_img_set_src(img, filename_with_path);

        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }

}

static bool starts_with(const char *str, const char *prefix) 
{
    if (!str || !prefix) {
        return false;
    }
    size_t str_len = strlen(str);
    size_t prefix_len = strlen(prefix);

    if (prefix_len > str_len) {
        return false;
    }

    for (size_t i = 0; i < prefix_len; ++i) {
        if (tolower((unsigned char)str[i]) != tolower((unsigned char)prefix[i])) {
            return false;
        }
    }

    return true;
}

static bool ends_with(const char *str, const char *suffix) 
{
    if (!str || !suffix) {
        return false;
    }
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);

    if (suffix_len > str_len) {
        return false;
    }

    const char *str_end = str + str_len - suffix_len;
    for (size_t i = 0; i < suffix_len; ++i) {
        if (tolower((unsigned char)str_end[i]) != tolower((unsigned char)suffix[i])) {
            return false;
        }
    }

    return true;
}

const char* get_next_image()
{
    const char* filename = NULL;
    do {
        filename = file_iterator_get_name_from_index(file_iterator, file_iterator_get_index(file_iterator));
        file_iterator_next(file_iterator);
    }while(filename == NULL || !ends_with(filename, SUFFIX) || starts_with(filename, PREFIX));

    return filename;
}

static esp_err_t audio_mute_function(AUDIO_PLAYER_MUTE_SETTING setting)
{
    bsp_codec_mute_set(setting == AUDIO_PLAYER_MUTE ? true : false);

    // restore the voice volume upon unmuting
    if (setting == AUDIO_PLAYER_UNMUTE) {
        bsp_codec_volume_set(CONFIG_VOLUME_LEVEL, NULL);
    }

    return ESP_OK;
}

static void audio_callback(audio_player_cb_ctx_t *ctx)
{
    ESP_LOGI(TAG, "Audio callback: %d", ctx->audio_event);
    if(ctx->audio_event == AUDIO_PLAYER_CALLBACK_EVENT_IDLE) {
        play_audio();
    }
}

void mute_btn_handler(void *handle, void *arg)
{
    button_event_t event = (button_event_t)arg;

    if (BUTTON_PRESS_DOWN == event) {
        bsp_codec_mute_set(true);
    } else {
        bsp_codec_mute_set(false);
    }
}

static void init_audio()
{
    audio_player_config_t config = { .mute_fn = audio_mute_function,
                                     .write_fn = bsp_i2s_write,
                                     .clk_set_fn = bsp_codec_set_fs,
                                     .priority = 1,
                                     .coreID = 1
                                   };
    ESP_ERROR_CHECK(audio_player_new(config));

    audio_player_callback_register(audio_callback, NULL);
}

static void play_audio()
{
    FILE *fp = fopen("/spiffs/Canon.mp3", "rb");
    if(fp) {
        audio_player_play(fp);
    }else {
        ESP_LOGE(TAG, "File not found");
    }
}

void app_main(void)
{
    /* Initialize I2C (for touch and audio) */
    ESP_ERROR_CHECK(bsp_i2c_init());

    /* Initialize display and LVGL */
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES,
        .double_buffer = 0,
        .flags = {
            .buff_dma = true,
        }
    };
    bsp_display_start_with_config(&cfg);

    /* Set display brightness to 100% */
    bsp_display_backlight_on();

    ESP_ERROR_CHECK(mount_sdcard());

    file_iterator = file_iterator_new("/sdcard");
    assert(file_iterator != NULL);

    init_indev();
    
    xTaskCreatePinnedToCore(image_show, "image_show task", 1024 * 4, NULL, 1, NULL, 0);

    bsp_spiffs_mount();

    bsp_board_init();

    init_audio();

    play_audio();
}
