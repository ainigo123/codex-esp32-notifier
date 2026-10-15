#include "audio_notify.h"

#include <math.h>
#include <stdint.h>

#include "driver/i2c.h"
#include "driver/i2s.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define AUDIO_I2C_PORT I2C_NUM_0
#define AUDIO_I2C_SDA GPIO_NUM_41
#define AUDIO_I2C_SCL GPIO_NUM_42
#define ES8388_ADDRESS 0x10

#define AUDIO_I2S_PORT I2S_NUM_0
#define AUDIO_MCLK GPIO_NUM_3
#define AUDIO_BCLK GPIO_NUM_46
#define AUDIO_LRCK GPIO_NUM_9
#define AUDIO_DOUT GPIO_NUM_10
#define AUDIO_DIN GPIO_NUM_14

#define AUDIO_SAMPLE_RATE 16000
#define AUDIO_AMPLITUDE 7000
#define AUDIO_CHUNK_FRAMES 256

static const char *TAG = "audio_notify";

static esp_err_t es8388_write(uint8_t reg, uint8_t value)
{
    const uint8_t data[2] = {reg, value};
    return i2c_master_write_to_device(
        AUDIO_I2C_PORT,
        ES8388_ADDRESS,
        data,
        sizeof(data),
        pdMS_TO_TICKS(100));
}

static esp_err_t es8388_init_playback(void)
{
    /* Register sequence from the DNESP32S3 IDF guide, Chapter 41. */
    static const struct {
        uint8_t reg;
        uint8_t value;
    } init_sequence[] = {
        {0x00, 0x80},
        {0x00, 0x00},
        {0x01, 0x58},
        {0x01, 0x50},
        {0x02, 0xF3},
        {0x02, 0xF0},
        {0x03, 0x09},
        {0x00, 0x06},
        {0x04, 0x00},
        {0x08, 0x00},
        {0x2B, 0x80},
        {0x09, 0x88},
        {0x0C, 0x4C},
        {0x0D, 0x02},
        {0x10, 0x00},
        {0x11, 0x00},
        {0x17, 0x18},
        {0x18, 0x02},
        {0x1A, 0x00},
        {0x1B, 0x00},
        {0x27, 0xB8},
        {0x2A, 0xB8},
        /* Playback-only power and output routing. */
        {0x02, 0x0A},
        {0x04, 0x30},
        /* Moderate onboard speaker volume (valid range 0..33). */
        {0x30, 24},
        {0x31, 24},
    };

    for (size_t i = 0; i < sizeof(init_sequence) / sizeof(init_sequence[0]); ++i) {
        ESP_RETURN_ON_ERROR(
            es8388_write(init_sequence[i].reg, init_sequence[i].value),
            TAG,
            "ES8388 write failed at register 0x%02x",
            init_sequence[i].reg);

        if (i == 1) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    return ESP_OK;
}

esp_err_t audio_notify_init(void)
{
    const i2c_config_t i2c_config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = AUDIO_I2C_SDA,
        .scl_io_num = AUDIO_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
        .clk_flags = 0,
    };

    ESP_RETURN_ON_ERROR(i2c_param_config(AUDIO_I2C_PORT, &i2c_config), TAG, "I2C config failed");
    ESP_RETURN_ON_ERROR(
        i2c_driver_install(AUDIO_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0),
        TAG,
        "I2C install failed");

    const i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX,
        .sample_rate = AUDIO_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = 0,
        .dma_buf_count = 8,
        .dma_buf_len = AUDIO_CHUNK_FRAMES,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0,
        .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        .bits_per_chan = I2S_BITS_PER_CHAN_16BIT,
    };

    const i2s_pin_config_t pin_config = {
        .mck_io_num = AUDIO_MCLK,
        .bck_io_num = AUDIO_BCLK,
        .ws_io_num = AUDIO_LRCK,
        .data_out_num = AUDIO_DOUT,
        .data_in_num = AUDIO_DIN,
    };

    ESP_RETURN_ON_ERROR(
        i2s_driver_install(AUDIO_I2S_PORT, &i2s_config, 0, NULL),
        TAG,
        "I2S install failed");
    ESP_RETURN_ON_ERROR(i2s_set_pin(AUDIO_I2S_PORT, &pin_config), TAG, "I2S pin config failed");
    ESP_RETURN_ON_ERROR(i2s_zero_dma_buffer(AUDIO_I2S_PORT), TAG, "I2S DMA clear failed");
    ESP_RETURN_ON_ERROR(es8388_init_playback(), TAG, "ES8388 initialization failed");

    ESP_LOGI(TAG, "ES8388 playback path ready");
    return ESP_OK;
}

static void play_tone(float frequency_hz, uint32_t duration_ms)
{
    int16_t samples[AUDIO_CHUNK_FRAMES * 2];
    const uint32_t total_frames = (AUDIO_SAMPLE_RATE * duration_ms) / 1000;
    uint32_t frame = 0;

    while (frame < total_frames) {
        uint32_t frames_this_chunk = total_frames - frame;
        if (frames_this_chunk > AUDIO_CHUNK_FRAMES) {
            frames_this_chunk = AUDIO_CHUNK_FRAMES;
        }

        for (uint32_t i = 0; i < frames_this_chunk; ++i) {
            const float phase = 2.0f * 3.14159265358979323846f * frequency_hz *
                                (float)(frame + i) / (float)AUDIO_SAMPLE_RATE;
            const int16_t value = (int16_t)(sinf(phase) * AUDIO_AMPLITUDE);
            samples[i * 2] = value;
            samples[i * 2 + 1] = value;
        }

        size_t bytes_written = 0;
        const size_t bytes_to_write = frames_this_chunk * 2 * sizeof(int16_t);
        if (i2s_write(AUDIO_I2S_PORT, samples, bytes_to_write, &bytes_written, portMAX_DELAY) != ESP_OK ||
            bytes_written != bytes_to_write) {
            ESP_LOGW(TAG, "Short I2S write");
            return;
        }

        frame += frames_this_chunk;
    }
}

void audio_notify_play(void)
{
    static const float notes[] = {659.25f, 783.99f, 1046.50f};

    for (size_t i = 0; i < sizeof(notes) / sizeof(notes[0]); ++i) {
        play_tone(notes[i], 130);
        i2s_zero_dma_buffer(AUDIO_I2S_PORT);
        vTaskDelay(pdMS_TO_TICKS(60));
    }
}

