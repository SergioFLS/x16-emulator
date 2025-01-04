#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

#include <libretro.h>

#include "cpu/fake6502.h"
#include "timing.h"
#include "disasm.h"
#include "files.h"
#include "memory.h"
#include "video.h"
#include "via.h"
#include "serial.h"
#include "i2c.h"
#include "rtc.h"
#include "smc.h"
#include "vera_spi.h"
#include "sdcard.h"
#include "ieee.h"
#include "glue.h"
#include "debugger.h"
#include "iso_8859_15.h"
#include "joystick.h"
#include "rom_symbols.h"
#include "ymglue.h"
#include "audio.h"
#include "version.h"
#include "testbench.h"
#include "cartridge.h"
#include "midi.h"

#define lr_min(x,y) (((x) < (y)) ? (x) : (y))

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480

static uint32_t                 *frame_buf;
static struct retro_log_callback logging;
static retro_log_printf_t        log_cb;

// stuffs used in main.c, without them linker errors will occur
bool warp_mode = false;
bool log_video = false;
bool has_midi_card = false;
uint16_t midi_card_addr;
uint16_t num_ram_banks = 64; // 512 KB default
bool has_via2 = false;
bool debugger_enabled = false;
uint8_t keymap = 0; // KERNAL's default
bool disable_emu_cmd_keys = false;
bool save_on_exit = true;
echo_mode_t echo_mode;
bool log_keyboard = false;
uint16_t midi_card_addr = 0x9f60;
uint8_t MHZ = 8;
bool enable_midline = false;
bool log_speed = false;
bool testbench = false;
bool pwr_long_press=false;
uint8_t *fsroot_path = NULL;
uint8_t *startin_path = NULL;

void
main_shutdown()
{
	// stub
}

void
machine_nmi()
{
	nmi6502();
}

void
machine_paste(char *s)
{
	// stub
}

void
machine_dump(const char* reason)
{
	// stub
}

void
machine_reset()
{
	i2c_reset_state();
	ieee_init();
	memory_reset();
	vera_spi_init();
	via1_init();
	if (has_via2) {
		via2_init();
	}
	video_reset();
	reset6502(regs.is65c816);
	midi_serial_init();
}

void
machine_toggle_warp()
{
	// stub
}

static void
fallback_log(enum retro_log_level level, const char *fmt, ...)
{
	(void)level;
	va_list va;
	va_start(va, fmt);
	vfprintf(stderr, fmt, va);
	va_end(va);
}

void
retro_init(void)
{
	frame_buf = calloc(SCREEN_WIDTH * SCREEN_HEIGHT, sizeof(uint32_t));

	video_reset();
	memory_init();
	joystick_init();
	rtc_init(false);
	machine_reset();
}

void
retro_deinit(void)
{
	free(frame_buf);
	frame_buf = NULL;
}

unsigned
retro_api_version(void)
{
	return RETRO_API_VERSION;
}

void
retro_set_controller_port_device(unsigned port, unsigned device)
{
	log_cb(RETRO_LOG_INFO, "Plugging device %u into port %u.\n", device, port);
}

void
retro_get_system_info(struct retro_system_info *info)
{
	memset(info, 0, sizeof(*info));
	info->library_name     = "x16-emulator";
	info->library_version  = "v1";
	info->need_fullpath    = false;
	info->valid_extensions = NULL; // Anything is fine, we don't care.
}

static retro_video_refresh_t      video_cb;
static retro_audio_sample_t       audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t        environ_cb;
static retro_input_poll_t         input_poll_cb;
static retro_input_state_t        input_state_cb;

void
retro_get_system_av_info(struct retro_system_av_info *info)
{
	float aspect = 4.0f / 3.0f;

	info->timing = (struct retro_system_timing){
	    .fps         = 60.0,
	    .sample_rate = 0.0,
	};

	info->geometry = (struct retro_game_geometry){
	    .base_width   = SCREEN_WIDTH,
	    .base_height  = SCREEN_HEIGHT,
	    .max_width    = SCREEN_WIDTH,
	    .max_height   = SCREEN_HEIGHT,
	    .aspect_ratio = aspect,
	};
}

void
retro_set_environment(retro_environment_t cb)
{
	environ_cb = cb;

	bool no_content = true;
	cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_content);

	if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
		log_cb = logging.log;
	else
		log_cb = fallback_log;

	static const char* rom_system = "/x16-emu/rom.bin";
	char* system_dir = NULL;
	if (cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir) && system_dir)
	{
		printf("sysdir: %s\n", system_dir);
		char *rom_path = calloc(strlen(system_dir) + strlen(rom_system) + 1, sizeof(char));
		if (rom_path)
		{
			strcpy(rom_path, system_dir);
			strcat(rom_path, rom_system);
			FILE *romf = fopen(rom_path, "rb");
			if (!romf)
			{
				perror(rom_system);
			}
			else
			{
				printf("read %lu bytes of rom\n", fread(&ROM, 1, ROM_SIZE, romf));
				fclose(romf);
			}
			free(rom_path);
		}
	}
}

void
retro_set_audio_sample(retro_audio_sample_t cb)
{
	audio_cb = cb;
}

void
retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
	audio_batch_cb = cb;
}

void
retro_set_input_poll(retro_input_poll_t cb)
{
	input_poll_cb = cb;
}

void
retro_set_input_state(retro_input_state_t cb)
{
	input_state_cb = cb;
}

void
retro_set_video_refresh(retro_video_refresh_t cb)
{
	video_cb = cb;
}

static unsigned x_coord;
static unsigned y_coord;

void
retro_reset(void)
{
	x_coord = 0;
	y_coord = 0;
	machine_reset();
}

static void
update_input(void)
{
	input_poll_cb();
	if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP)) {
		/* stub */
	}
}

/*static*/ void
render_checkered(void)
{
	uint32_t *buf     = frame_buf;
	unsigned  stride  = SCREEN_WIDTH;

	memcpy(buf, video_get_framebuffer(), sizeof(buf[0])*SCREEN_WIDTH*SCREEN_HEIGHT);

	video_cb(buf, SCREEN_WIDTH, SCREEN_HEIGHT, stride << 2);
}

static void
check_variables(void)
{
}

static void
audio_callback(void)
{
	audio_cb(0, 0);
}

bool
lr_emulator_loop(void *param)
{
	uint32_t old_clockticks6502 = clockticks6502;
	for (;;) {
		if (smc_requested_reset) machine_reset();

		//instruction_counter += waiting ^ 0x1;

		step6502();
		uint32_t clocks = clockticks6502 - old_clockticks6502;
		old_clockticks6502 = clockticks6502;
		bool new_frame = false;
		via1_step(clocks);
		vera_spi_step(MHZ, clocks);
		#if 0
		if (has_serial) {
			serial_step(clocks);
		}
		if (has_via2) {
			via2_step(clocks);
		}
		#endif
		new_frame |= video_step(MHZ, clocks, false);

		for (uint32_t i = 0; i < clocks; i++) {
			i2c_step();
		}
		rtc_step(clocks);

		audio_step(clocks);

		midi_serial_step(clocks);

		if (new_frame) {
			if (!video_update()) {
				return false;
			}

			// After completing a frame we yield back control to the browser to stay responsive
			return true;
		}

		// The optimization from the opportunistic batching of audio rendering
		// is lost if we need to track the YM2151 IRQ, so it has been made a
		// command-line switch that's disabled by default.
		//if (ym2151_irq_support) {
		//	audio_render();
		//}

		if (video_get_irq_out() || via1_irq() /*|| (has_via2 && via2_irq()) || (ym2151_irq_support && YM_irq()) || (has_midi_card && midi_serial_irq())*/) {
//			printf("IRQ!\n");
			irq6502();
		}

		if (regs.pc == 0xffff) {
			break;
		}
	}

	return true;
}

void
retro_run(void)
{
	update_input();
	render_checkered();
	audio_callback();
	lr_emulator_loop(NULL);

	bool updated = false;
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
		check_variables();
}

bool
retro_load_game(const struct retro_game_info *info)
{
	enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
	if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
		log_cb(RETRO_LOG_INFO, "XRGB8888 is not supported.\n");
		return false;
	}

	check_variables();
	machine_reset();

	(void)info;
	return true;
}

void
retro_unload_game(void)
{
}

unsigned
retro_get_region(void)
{
	return RETRO_REGION_NTSC;
}

bool
retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{
	if (type != 0x200)
		return false;
	if (num != 2)
		return false;
	return retro_load_game(NULL);
}

size_t
retro_serialize_size(void)
{
	return 2;
}

bool
retro_serialize(void *data_, size_t size)
{
	if (size < 2)
		return false;

	uint8_t *data = data_;
	data[0]       = x_coord;
	data[1]       = y_coord;
	return true;
}

bool
retro_unserialize(const void *data_, size_t size)
{
	if (size < 2)
		return false;

	const uint8_t *data = data_;
	x_coord             = data[0] & 31;
	y_coord             = data[1] & 31;
	return true;
}

void *
retro_get_memory_data(unsigned id)
{
	(void)id;
	return NULL;
}

size_t
retro_get_memory_size(unsigned id)
{
	(void)id;
	return 0;
}

void
retro_cheat_reset(void)
{
}

void
retro_cheat_set(unsigned index, bool enabled, const char *code)
{
	(void)index;
	(void)enabled;
	(void)code;
}
