/*
 * Copyright (c) 2024 Titouan Christophe
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file
 * @brief Sample application for USB MIDI 2.0 device class
 */

#include <sample_usbd.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/usb/class/usbd_midi2.h>

#include <ump_stream_responder.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sample_usb_midi, LOG_LEVEL_INF);

#define USB_MIDI_DT_NODE DT_NODELABEL(usb_midi)

static const struct device *const midi = DEVICE_DT_GET(USB_MIDI_DT_NODE);

static struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {0});

static void key_press(struct input_event *evt, void *user_data)
{
	/* Only handle key presses in the 7bit MIDI range */
	if (evt->type != INPUT_EV_KEY || evt->code > 0x7f) {
		return;
	}
	uint8_t command = evt->value ? UMP_MIDI_NOTE_ON : UMP_MIDI_NOTE_OFF;
	uint8_t channel = 0;
	uint8_t note = evt->code;
	uint8_t velocity = 100;

	struct midi_ump ump = UMP_MIDI1_CHANNEL_VOICE(0, command, channel,
						      note, velocity);
	usbd_midi_send(midi, ump);
}
INPUT_CALLBACK_DEFINE(NULL, key_press, NULL);

static const struct ump_endpoint_dt_spec ump_ep_dt =
	UMP_ENDPOINT_DT_SPEC_GET(USB_MIDI_DT_NODE);

const struct ump_stream_responder_cfg responder_cfg =
	UMP_STREAM_RESPONDER(midi, usbd_midi_send, &ump_ep_dt);

static void on_midi_packet(const struct device *dev, const struct midi_ump ump)
{
	LOG_INF("Received MIDI packet (MT=%X)", UMP_MT(ump));

	switch (UMP_MT(ump)) {
	case UMP_MT_MIDI1_CHANNEL_VOICE:
		/* Only send MIDI1 channel voice messages back to the host */
		LOG_INF("Send back MIDI1 message %02X %02X %02X",
			UMP_MIDI_STATUS(ump), UMP_MIDI1_P1(ump), UMP_MIDI1_P2(ump));
		usbd_midi_send(dev, ump);
		break;
	case UMP_MT_UMP_STREAM:
		ump_stream_respond(&responder_cfg, ump);
		break;
	}
}

static void on_device_ready(const struct device *dev, const bool ready)
{
	/* Light up the LED (if any) when USB-MIDI2.0 is enabled */
	if (led.port) {
		gpio_pin_set_dt(&led, ready);
	}
}

static const struct usbd_midi_ops ops = {
	.rx_packet_cb = on_midi_packet,
	.ready_cb = on_device_ready,
};

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/audio/codec.h>
#include <zephyr/toolchain.h>
#include <string.h>

#include "sine.h"

#define I2S_CODEC_TX DT_ALIAS(i2s_codec_tx)

#define SAMPLE_FREQUENCY CONFIG_SAMPLE_FREQ
#define SAMPLE_BIT_WIDTH CONFIG_SAMPLE_WIDTH
#define BYTES_PER_SAMPLE CONFIG_BYTES_PER_SAMPLE
#define NUMBER_OF_CHANNELS (2U)
/* Such block length provides an echo with the delay of 100 ms. */
#define SAMPLES_PER_BLOCK ((SAMPLE_FREQUENCY / 10) * NUMBER_OF_CHANNELS)
#define INITIAL_BLOCKS    CONFIG_I2S_INIT_BUFFERS
#define TIMEOUT           (2000U)

#define BLOCK_SIZE  (BYTES_PER_SAMPLE * SAMPLES_PER_BLOCK)
#define BLOCK_COUNT (INITIAL_BLOCKS + CONFIG_EXTRA_BLOCKS)

K_MEM_SLAB_DEFINE_IN_SECT_STATIC(mem_slab, __nocache, BLOCK_SIZE, BLOCK_COUNT, 4);

static bool configure_tx_streams(const struct device *i2s_dev, struct i2s_config *config)
{
	int ret;

	ret = i2s_configure(i2s_dev, I2S_DIR_TX, config);
	if (ret < 0) {
		printk("Failed to configure codec stream: %d\n", ret);
		return false;
	}

	return true;
}

static bool trigger_command(const struct device *i2s_dev_codec, enum i2s_trigger_cmd cmd)
{
	int ret;

	ret = i2s_trigger(i2s_dev_codec, I2S_DIR_TX, cmd);
	if (ret < 0) {
		printk("Failed to trigger command %d on TX: %d\n", cmd, ret);
		return false;
	}

	return true;
}

int main(void)
{
	struct usbd_context *sample_usbd;

	if (!device_is_ready(midi)) {
		LOG_ERR("MIDI device not ready");
		return -1;
	}

	if (led.port) {
		if (gpio_pin_configure_dt(&led, GPIO_OUTPUT)) {
			LOG_ERR("Unable to setup LED, not using it");
			memset(&led, 0, sizeof(led));
		}
	}

	usbd_midi_set_ops(midi, &ops);

	sample_usbd = sample_usbd_init_device(NULL);
	if (sample_usbd == NULL) {
		LOG_ERR("Failed to initialize USB device");
		return -1;
	}

	if (usbd_enable(sample_usbd)) {
		LOG_ERR("Failed to enable device support");
		return -1;
	}

	LOG_INF("USB device support enabled");
	//return 0;

	const struct device *const i2s_dev_codec = DEVICE_DT_GET(I2S_CODEC_TX);
	const struct device *const codec_dev = DEVICE_DT_GET(DT_NODELABEL(audio_codec));
	struct i2s_config config;
	struct audio_codec_cfg audio_cfg;
	int ret = 0;

	printk("codec sample\n");

	if (!device_is_ready(i2s_dev_codec)) {
		printk("%s is not ready\n", i2s_dev_codec->name);
		return 0;
	}

	if (!device_is_ready(codec_dev)) {
		printk("%s is not ready", codec_dev->name);
		return 0;
	}

	audio_cfg.dai_route = AUDIO_ROUTE_PLAYBACK;
	audio_cfg.dai_type = AUDIO_DAI_TYPE_I2S;
	audio_cfg.dai_cfg.i2s.word_size = SAMPLE_BIT_WIDTH;
	audio_cfg.dai_cfg.i2s.channels = 2;
	audio_cfg.dai_cfg.i2s.format = I2S_FMT_DATA_FORMAT_I2S;
	#ifdef CONFIG_USE_CODEC_CLOCK
	audio_cfg.dai_cfg.i2s.options = I2S_OPT_FRAME_CLK_MASTER | I2S_OPT_BIT_CLK_MASTER;
#else
	audio_cfg.dai_cfg.i2s.options = I2S_OPT_FRAME_CLK_SLAVE | I2S_OPT_BIT_CLK_SLAVE;
#endif
	audio_cfg.dai_cfg.i2s.frame_clk_freq = SAMPLE_FREQUENCY;
	audio_cfg.dai_cfg.i2s.mem_slab = &mem_slab;
	audio_cfg.dai_cfg.i2s.block_size = BLOCK_SIZE;
	audio_codec_configure(codec_dev, &audio_cfg);
	k_msleep(1000);

	config.word_size = SAMPLE_BIT_WIDTH;
	config.channels = NUMBER_OF_CHANNELS;
	config.format = I2S_FMT_DATA_FORMAT_I2S;
#ifdef CONFIG_USE_CODEC_CLOCK
	config.options = I2S_OPT_BIT_CLK_SLAVE | I2S_OPT_FRAME_CLK_SLAVE;
#else
	config.options = I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER;
#endif
	config.frame_clk_freq = SAMPLE_FREQUENCY;
	config.mem_slab = &mem_slab;
	config.block_size = BLOCK_SIZE;
	config.timeout = TIMEOUT;
	if (!configure_tx_streams(i2s_dev_codec, &config)) {
		printk("failure to config streams\n");
		return 0;
	}

	printk("start streams\n");

	for (;;)
	{
		bool started = false;

		while (1)
		{
			void *mem_block;
			uint32_t block_size = BLOCK_SIZE;
			int i;

			for (i = 0; i < CONFIG_I2S_INIT_BUFFERS; i++)
			{
				BUILD_ASSERT(
					BLOCK_SIZE <= __16kHz16bit_stereo_sine_pcm_len,
					"BLOCK_SIZE is bigger than test sine wave buffer size."
				);
				mem_block = (void *)&__16kHz16bit_stereo_sine_pcm;

				ret = i2s_buf_write(i2s_dev_codec, mem_block, block_size);

				if (ret < 0)
				{
					printk("Failed to write data: %d\n", ret);
					break;
				}
			}

			if (ret < 0)
			{
				printk("error %d\n", ret);
				break;
			}

			if (!started)
			{
				i2s_trigger(i2s_dev_codec, I2S_DIR_TX, I2S_TRIGGER_START);
				started = true;
			}
		}

		if (!trigger_command(i2s_dev_codec, I2S_TRIGGER_DROP))
		{
			printk("Send I2S trigger DRAIN failed: %d", ret);
			return 0;
		}

		printk("Streams stopped\n");
		return 0;
	}
}
