#include <cassert>
#include "libretro.h"
#include <emulator/hdtoolkit.hpp>

static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll;
static retro_input_state_t input_state;
static retro_log_printf_t libretro_print;

#define SAMPLERATE 48000
#define AUDIOBUFSIZE (SAMPLERATE/50) * 2
static int16_t audio_buffer[AUDIOBUFSIZE];
static uint16_t audio_buffer_index = 0;
static uint16_t audio_buffer_max = AUDIOBUFSIZE;

static int run_ahead_frames = 0;

static void audio_queue(int16_t left, int16_t right)
{
	audio_buffer[audio_buffer_index++] = left;
	audio_buffer[audio_buffer_index++] = right;

	if (audio_buffer_index == audio_buffer_max)
	{
		audio_batch_cb(audio_buffer, audio_buffer_max/2);
		audio_buffer_index = 0;
	}
}

#include "program.cpp"

static string sgb_bios;
static vector<string> cheatList;

#define RETRO_DEVICE_JOYPAD_MULTITAP       RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 0)
#define RETRO_DEVICE_LIGHTGUN_SUPER_SCOPE  RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_LIGHTGUN, 0)
#define RETRO_DEVICE_LIGHTGUN_JUSTIFIER    RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_LIGHTGUN, 1)
#define RETRO_DEVICE_LIGHTGUN_JUSTIFIERS   RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_LIGHTGUN, 2)

#define RETRO_GAME_TYPE_SGB             0x101 | 0x1000
#define RETRO_GAME_TYPE_BSX             0x110 | 0x1000
#define RETRO_MEMORY_SGB_SRAM ((1 << 8) | RETRO_MEMORY_SAVE_RAM)
#define RETRO_MEMORY_GB_SRAM ((2 << 8) | RETRO_MEMORY_SAVE_RAM)
#define RETRO_MEMORY_BSX_SRAM ((3 << 8) | RETRO_MEMORY_SAVE_RAM)

static bool flush_variables() // returns whether video dimensions have changed (overscan, aspectcorrection scale or widescreen AR)
{
	retro_variable variable = { "bsnes_blur_emulation", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		if (strcmp(variable.value, "ON") == 0)
			emulator->configure("Video/BlurEmulation", true);
		else if (strcmp(variable.value, "OFF") == 0)
			emulator->configure("Video/BlurEmulation", false);
	}

	variable = { "bsnes_hotfixes", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		if (strcmp(variable.value, "ON") == 0)
			emulator->configure("Hacks/Hotfixes", true);
		else if (strcmp(variable.value, "OFF") == 0)
			emulator->configure("Hacks/Hotfixes", false);
	}

	variable = { "bsnes_entropy", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		if (strcmp(variable.value, "None") == 0)
			emulator->configure("Hacks/Entropy", "None");
		else if (strcmp(variable.value, "Low") == 0)
			emulator->configure("Hacks/Entropy", "Low");
		else if (strcmp(variable.value, "High") == 0)
			emulator->configure("Hacks/Entropy", "High");
	}

	variable = { "bsnes_cpu_overclock", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		int val = atoi(variable.value);
		emulator->configure("Hacks/CPU/Overclock", val);
	}

	variable = { "bsnes_cpu_fastmath", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		if (strcmp(variable.value, "ON") == 0)
			emulator->configure("Hacks/CPU/FastMath", true);
		else if (strcmp(variable.value, "OFF") == 0)
			emulator->configure("Hacks/CPU/FastMath", false);
	}

	variable = { "bsnes_cpu_sa1_overclock", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		int val = atoi(variable.value);
		emulator->configure("Hacks/SA1/Overclock", val);
	}

	variable = { "bsnes_cpu_sfx_overclock", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		int val = atoi(variable.value);
		emulator->configure("Hacks/SuperFX/Overclock", val);
	}

	variable = { "bsnes_ppu_fast", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		if (strcmp(variable.value, "ON") == 0)
			emulator->configure("Hacks/PPU/Fast", true);
		else if (strcmp(variable.value, "OFF") == 0)
			emulator->configure("Hacks/PPU/Fast", false);
	}

	variable = { "bsnes_ppu_deinterlace", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		if (strcmp(variable.value, "ON") == 0)
			emulator->configure("Hacks/PPU/Deinterlace", true);
		else if (strcmp(variable.value, "OFF") == 0)
			emulator->configure("Hacks/PPU/Deinterlace", false);
	}

	variable = { "bsnes_ppu_no_sprite_limit", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		if (strcmp(variable.value, "ON") == 0)
			emulator->configure("Hacks/PPU/NoSpriteLimit", true);
		else if (strcmp(variable.value, "OFF") == 0)
			emulator->configure("Hacks/PPU/NoSpriteLimit", false);
	}

	variable = { "bsnes_ppu_no_vram_blocking", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		if (strcmp(variable.value, "ON") == 0)
			emulator->configure("Hacks/PPU/NoVRAMBlocking", true);
		else if (strcmp(variable.value, "OFF") == 0)
			emulator->configure("Hacks/PPU/NoVRAMBlocking", false);
	}

	bool overscan = program->overscan;
	variable = { "bsnes_ppu_show_overscan", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		if (strcmp(variable.value, "ON") == 0)
			overscan = true;
		else if (strcmp(variable.value, "OFF") == 0)
			overscan = false;
	}
	emulator->configure("Video/Overscan", overscan);

	variable = { "bsnes_dsp_fast", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		if (strcmp(variable.value, "ON") == 0)
			emulator->configure("Hacks/DSP/Fast", true);
		else if (strcmp(variable.value, "OFF") == 0)
			emulator->configure("Hacks/DSP/Fast", false);
	}

	variable = { "bsnes_dsp_cubic", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		if (strcmp(variable.value, "ON") == 0)
			emulator->configure("Hacks/DSP/Cubic", true);
		else if (strcmp(variable.value, "OFF") == 0)
			emulator->configure("Hacks/DSP/Cubic", false);
	}

	variable = { "bsnes_dsp_echo_shadow", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		if (strcmp(variable.value, "ON") == 0)
			emulator->configure("Hacks/DSP/EchoShadow", true);
		else if (strcmp(variable.value, "OFF") == 0)
			emulator->configure("Hacks/DSP/EchoShadow", false);
	}

	variable = { "bsnes_coprocessor_delayed_sync", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		if (strcmp(variable.value, "ON") == 0)
			emulator->configure("Hacks/Coprocessor/DelayedSync", true);
		else if (strcmp(variable.value, "OFF") == 0)
			emulator->configure("Hacks/Coprocessor/DelayedSync", false);
	}

	variable = { "bsnes_coprocessor_prefer_hle", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		if (strcmp(variable.value, "ON") == 0)
			emulator->configure("Hacks/Coprocessor/PreferHLE", true);
		else if (strcmp(variable.value, "OFF") == 0)
			emulator->configure("Hacks/Coprocessor/PreferHLE", false);
	}

	variable = { "bsnes_sgb_bios", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		sgb_bios = variable.value;
	}

	variable = { "bsnes_run_ahead_frames", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		if (strcmp(variable.value, "OFF") == 0)
			run_ahead_frames = 0;
		else
			run_ahead_frames = atoi(variable.value);
	}



	// scale: 2x|3x|4x|5x|6x|7x|8x|9x|10x|disable|1x
	variable = { "bsnes_mode7_scale", nullptr };
	int scale = 0;
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
	  if (strlen(variable.value)      == 7) scale =  0; //"disable"
      else if (strlen(variable.value) == 3) scale = 10; //"10x"		
      else //"1x"-"9x"
	  {
		  int val = variable.value[0] - '0';
		  if (val >= 1 && val <= 9) scale = val;
	  }
	}
	emulator->configure("Hacks/PPU/Mode7/Scale", scale);

	// widescreen: 16:9|2:1|21:9|16:10|4:3|none
	variable = { "bsnes_mode7_widescreen", nullptr };
	int ws = 0;
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		if (strcmp(variable.value,       "none") == 0) ws =    0;
		else if (strcmp(variable.value,   "4:3") == 0) ws =  403;
		else if (strcmp(variable.value, "16:10") == 0) ws = 1610;
		else if (strcmp(variable.value,  "16:9") == 0) ws = 1609;
		else if (strcmp(variable.value,   "2:1") == 0) ws =  201;
		else if (strcmp(variable.value,  "21:9") == 0) ws = 2109;
	}
	if (scale == 0) ws = 0;
	emulator->configure("Hacks/PPU/Mode7/Widescreen", ws);

	// perspective: auto (wide)|auto (medium)|auto (narrow)|on (wide)|on (medium)|on (narrow)|off
	variable = { "bsnes_mode7_perspective", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		int val = 1;
		if (variable.value[1] == 'u') // auto
		{
			if (variable.value[6] == 'w') val = 1; // wide
			else if (variable.value[6] == 'm') val = 2; // medium
			else if (variable.value[6] == 'n') val = 3;// narrow
		}
		else if (variable.value[1] == 'n') // on
		{
			if (variable.value[4] == 'w') val = 4; // wide
			else if (variable.value[4] == 'm') val = 5; // medium
			else if (variable.value[4] == 'n') val = 6; // narrow
		}
		else if (variable.value[1] == 'f') val = 0; // off
		emulator->configure("Hacks/PPU/Mode7/Perspective", val);
	}

	// supersample: none|2x|3x|4x|5x|6x|7x|8x|9x|10x
	variable = { "bsnes_mode7_supersample", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		int val = 1;
	  	if (strlen(variable.value)      == 4) val =  1; //"none"
      	else if (strlen(variable.value) == 3) val = 10; //"10x"		
      	else //"2x"-"9x"
	  	{
		  	int v = variable.value[0] - '0';
		  	if (v >= 2 && v <= 9) val = v;
	  	}
		emulator->configure("Hacks/PPU/Mode7/Supersample", val);
	}

	// mosaic: 1x scale|non-HD|ignore
	variable = { "bsnes_mode7_mosaic", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		int val = 1;
		if (strcmp(variable.value,    "1x scale") == 0) val = 1;
		else if (strcmp(variable.value, "non-HD") == 0) val = 0;
		else if (strcmp(variable.value, "ignore") == 0) val = 2;
		emulator->configure("Hacks/PPU/Mode7/Mosaic", val);
	}

	// wsMode: Mode 7|all|none
	variable = { "bsnes_mode7_wsMode", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		int val = 1;
		if (strcmp(variable.value,    "Mode 7") == 0) val = 1;
		else if (strcmp(variable.value,  "all") == 0) val = 2;
		else if (strcmp(variable.value, "none") == 0) val = 0;
		emulator->configure("Hacks/PPU/Mode7/WsMode", val);
		if (val == 0) ws = 0;
	}

	// wsbg1: auto horz and vert|off|on|above line 40|below line 40|above line 80|below line 80|above line 120|below line 120|above line 160|below line 160|above line 200|below line 200|crop edges|auto crop edges|disable entirely|auto horizontal
	variable = { "bsnes_mode7_wsbg1", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		int val = 16;
		if (strcmp(variable.value, "auto horz and vert") == 0) val = 16;
		else if (strcmp(variable.value, "off") == 0) val = 0;
		else if (strcmp(variable.value, "on") == 0) val = 1;
		else if (strcmp(variable.value, "above line 40") == 0) val = 2;
		else if (strcmp(variable.value, "below line 40") == 0) val = 3;
		else if (strcmp(variable.value, "above line 80") == 0) val = 4;
		else if (strcmp(variable.value, "below line 80") == 0) val = 5;
		else if (strcmp(variable.value, "above line 120") == 0) val = 6;
		else if (strcmp(variable.value, "below line 120") == 0) val = 7;
		else if (strcmp(variable.value, "above line 160") == 0) val = 8;
		else if (strcmp(variable.value, "below line 160") == 0) val = 9;
		else if (strcmp(variable.value, "above line 200") == 0) val = 10;
		else if (strcmp(variable.value, "below line 200") == 0) val = 11;
		else if (strcmp(variable.value, "crop edges") == 0) val = 12;
		else if (strcmp(variable.value, "auto crop edges") == 0) val = 13;
		else if (strcmp(variable.value, "disable entirely") == 0) val = 14;
		else if (strcmp(variable.value, "auto horizontal") == 0) val = 15;
		emulator->configure("Hacks/PPU/Mode7/Wsbg1", val);
	}

	// wsbg2: auto horz and vert|off|on|above line 40|below line 40|above line 80|below line 80|above line 120|below line 120|above line 160|below line 160|above line 200|below line 200|crop edges|auto crop edges|disable entirely|auto horizontal
	variable = { "bsnes_mode7_wsbg2", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		int val = 16;
		if (strcmp(variable.value, "auto horz and vert") == 0) val = 16;
		else if (strcmp(variable.value, "off") == 0) val = 0;
		else if (strcmp(variable.value, "on") == 0) val = 1;
		else if (strcmp(variable.value, "above line 40") == 0) val = 2;
		else if (strcmp(variable.value, "below line 40") == 0) val = 3;
		else if (strcmp(variable.value, "above line 80") == 0) val = 4;
		else if (strcmp(variable.value, "below line 80") == 0) val = 5;
		else if (strcmp(variable.value, "above line 120") == 0) val = 6;
		else if (strcmp(variable.value, "below line 120") == 0) val = 7;
		else if (strcmp(variable.value, "above line 160") == 0) val = 8;
		else if (strcmp(variable.value, "below line 160") == 0) val = 9;
		else if (strcmp(variable.value, "above line 200") == 0) val = 10;
		else if (strcmp(variable.value, "below line 200") == 0) val = 11;
		else if (strcmp(variable.value, "crop edges") == 0) val = 12;
		else if (strcmp(variable.value, "auto crop edges") == 0) val = 13;
		else if (strcmp(variable.value, "disable entirely") == 0) val = 14;
		else if (strcmp(variable.value, "auto horizontal") == 0) val = 15;
		emulator->configure("Hacks/PPU/Mode7/Wsbg2", val);
	}

	// wsbg3: auto horz and vert|off|on|above line 40|below line 40|above line 80|below line 80|above line 120|below line 120|above line 160|below line 160|above line 200|below line 200|crop edges|auto crop edges|disable entirely|auto horizontal
	variable = { "bsnes_mode7_wsbg3", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		int val = 16;
		if (strcmp(variable.value, "auto horz and vert") == 0) val = 16;
		else if (strcmp(variable.value, "off") == 0) val = 0;
		else if (strcmp(variable.value, "on") == 0) val = 1;
		else if (strcmp(variable.value, "above line 40") == 0) val = 2;
		else if (strcmp(variable.value, "below line 40") == 0) val = 3;
		else if (strcmp(variable.value, "above line 80") == 0) val = 4;
		else if (strcmp(variable.value, "below line 80") == 0) val = 5;
		else if (strcmp(variable.value, "above line 120") == 0) val = 6;
		else if (strcmp(variable.value, "below line 120") == 0) val = 7;
		else if (strcmp(variable.value, "above line 160") == 0) val = 8;
		else if (strcmp(variable.value, "below line 160") == 0) val = 9;
		else if (strcmp(variable.value, "above line 200") == 0) val = 10;
		else if (strcmp(variable.value, "below line 200") == 0) val = 11;
		else if (strcmp(variable.value, "crop edges") == 0) val = 12;
		else if (strcmp(variable.value, "auto crop edges") == 0) val = 13;
		else if (strcmp(variable.value, "disable entirely") == 0) val = 14;
		else if (strcmp(variable.value, "auto horizontal") == 0) val = 15;
		emulator->configure("Hacks/PPU/Mode7/Wsbg3", val);
	}

	// wsbg4: auto horz and vert|off|on|above line 40|below line 40|above line 80|below line 80|above line 120|below line 120|above line 160|below line 160|above line 200|below line 200|crop edges|auto crop edges|disable entirely|auto horizontal
	variable = { "bsnes_mode7_wsbg4", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		int val = 16;
		if (strcmp(variable.value, "auto horz and vert") == 0) val = 16;
		else if (strcmp(variable.value, "off") == 0) val = 0;
		else if (strcmp(variable.value, "on") == 0) val = 1;
		else if (strcmp(variable.value, "above line 40") == 0) val = 2;
		else if (strcmp(variable.value, "below line 40") == 0) val = 3;
		else if (strcmp(variable.value, "above line 80") == 0) val = 4;
		else if (strcmp(variable.value, "below line 80") == 0) val = 5;
		else if (strcmp(variable.value, "above line 120") == 0) val = 6;
		else if (strcmp(variable.value, "below line 120") == 0) val = 7;
		else if (strcmp(variable.value, "above line 160") == 0) val = 8;
		else if (strcmp(variable.value, "below line 160") == 0) val = 9;
		else if (strcmp(variable.value, "above line 200") == 0) val = 10;
		else if (strcmp(variable.value, "below line 200") == 0) val = 11;
		else if (strcmp(variable.value, "crop edges") == 0) val = 12;
		else if (strcmp(variable.value, "auto crop edges") == 0) val = 13;
		else if (strcmp(variable.value, "disable entirely") == 0) val = 14;
		else if (strcmp(variable.value, "auto horizontal") == 0) val = 15;
		emulator->configure("Hacks/PPU/Mode7/Wsbg4", val);
	}

	// wsobj: safe|unsafe|disable entirely|clip
	variable = { "bsnes_mode7_wsobj", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		int val = 0;
		if (strcmp(variable.value,      "safe") == 0) val = 0;
		else if (strcmp(variable.value, "unsafe") == 0) val = 1;
		else if (strcmp(variable.value, "disable entirely") == 0) val = 2;
		else if (strcmp(variable.value, "clip") == 0) val = 3;
		emulator->configure("Hacks/PPU/Mode7/Wsobj", val);
	}

	// wsBgCol: auto|color|black
	variable = { "bsnes_mode7_wsBgCol", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		int val = 1;
		if (strcmp(variable.value,      "auto") == 0) val = 1;
		else if (strcmp(variable.value, "color") == 0) val = 0;
		else if (strcmp(variable.value, "black") == 0) val = 2;
		emulator->configure("Hacks/PPU/Mode7/WsBgCol", val);
	}

	// igwin: outside|outside and always|all|none
	variable = { "bsnes_mode7_igwin", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		int val = 1;
		if (strcmp(variable.value,      "outside") == 0) val = 1;
		else if (strcmp(variable.value, "outside and always") == 0) val = 2;
		else if (strcmp(variable.value, "all") == 0) val = 3;
		else if (strcmp(variable.value, "none") == 0) val = 0;
		emulator->configure("Hacks/PPU/Mode7/Igwin", val);
	}

	// igwinx: 128|168|216|40|88
	variable = { "bsnes_mode7_igwinx", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		int val = 128;
		if (strcmp(variable.value,      "128") == 0) val = 128;
		else if (strcmp(variable.value, "168") == 0) val = 168;
		else if (strcmp(variable.value, "216") == 0) val = 216;
		else if (strcmp(variable.value,  "40") == 0) val =  40;
		else if (strcmp(variable.value,  "88") == 0) val =  88;
		emulator->configure("Hacks/PPU/Mode7/Igwinx", val);
	}

	// wsMarker: none|lines|darken
	variable = { "bsnes_mode7_wsMarker", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		int val = 0;
		if (strcmp(variable.value,      "none") == 0) val = 0;
		else if (strcmp(variable.value, "lines") == 0) val = 1;
		else if (strcmp(variable.value, "darken") == 0) val = 2;
		emulator->configure("Hacks/PPU/Mode7/WsMarker", val);
	}

	// wsMarkerAlpha: 1/1|1/2|1/3|1/4|1/5|1/6|1/7|1/8|1/9|1/10
	variable = { "bsnes_mode7_wsMarkerAlpha", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		int val = 0;
	  	if (strlen(variable.value) == 4) val = 10; //"1/10"
      	else //"1/1"-"1/9"
	  	{
		  	int v = variable.value[2] - '0';
		  	if (v >= 1 && v <= 9) val = v;
	  	}
		emulator->configure("Hacks/PPU/Mode7/WsMarkerAlpha", val);
	}

	// bgGrad: 4|5|6|7|8|0|1|2|3
	variable = { "bsnes_mode7_bgGrad", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		int val = 4;
	  	if (strlen(variable.value) == 1)
	  	{
		  	int v = variable.value[0] - '0';
		  	if (v >= 0 && v <= 8) val = v;
	  	}
		emulator->configure("Hacks/PPU/Mode7/BgGrad", val);
	}

	// windRad: 0|1|2|3|4|5|6|7|8
	variable = { "bsnes_mode7_windRad", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		int val = 0;
	  	if (strlen(variable.value) == 1)
	  	{
		  	int v = variable.value[0] - '0';
		  	if (v >= 0 && v <= 8) val = v;
	  	}
		emulator->configure("Hacks/PPU/Mode7/WindRad", val);
	}

	bool aspectcorrection = program->aspectcorrection;
	variable = { "bsnes_video_aspectcorrection", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		if (strcmp(variable.value, "ON") == 0)
			aspectcorrection = true;
		else if (strcmp(variable.value, "OFF") == 0)
			aspectcorrection = false;
	}
	emulator->configure("Video/AspectCorrection", aspectcorrection);

	variable = { "bsnes_video_luminance", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		int val = atoi(variable.value);
		emulator->configure("Video/Luminance", val);
	}

	variable = { "bsnes_video_saturation", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		int val = atoi(variable.value);
		emulator->configure("Video/Saturation", val);
	}

	variable = { "bsnes_video_gamma", nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value)
	{
		int val = atoi(variable.value);
		emulator->configure("Video/Gamma", val);
	}

	bool vc = program->overscan != overscan; // overscan changed
	program->overscan = overscan;	
	vc = vc | program->aspectcorrection != aspectcorrection; // aspectcorrection changed
	program->aspectcorrection = aspectcorrection;
	vc = vc | program->scale != scale; // scale changed
	program->scale = scale;
	ws = HdToolkit::determineWsExt(ws, overscan, aspectcorrection);
	vc = vc | program->ws != ws; // widescreen AR changed
	program->ws = ws;
	return vc; // returns whether video dimensions have changed (overscan, aspectcorrection scale or widescreen AR)
}

static void check_variables()
{
	bool updated = false;
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated) {
		if (flush_variables()) { // returns whether video dimensions have changed (overscan, aspectcorrection scale or widescreen AR)
			struct retro_system_av_info info;
			retro_get_system_av_info(&info);
			environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &info);
		}
	}
}

static uint retro_device_to_snes(unsigned device)
{
	switch (device)
	{
		default:
		case RETRO_DEVICE_NONE:
			return SuperFamicom::ID::Device::None;
		case RETRO_DEVICE_JOYPAD:
			return SuperFamicom::ID::Device::Gamepad;
		case RETRO_DEVICE_ANALOG:
			return SuperFamicom::ID::Device::Gamepad;
		case RETRO_DEVICE_JOYPAD_MULTITAP:
			return SuperFamicom::ID::Device::SuperMultitap;
		case RETRO_DEVICE_MOUSE:
			return SuperFamicom::ID::Device::Mouse;
		case RETRO_DEVICE_LIGHTGUN_SUPER_SCOPE:
			return SuperFamicom::ID::Device::SuperScope;
		case RETRO_DEVICE_LIGHTGUN_JUSTIFIER:
			return SuperFamicom::ID::Device::Justifier;
		case RETRO_DEVICE_LIGHTGUN_JUSTIFIERS:
			return SuperFamicom::ID::Device::Justifiers;
	}
}

static void set_controller_ports(unsigned port, unsigned device)
{
	if (port < 2)
		emulator->connect(port, retro_device_to_snes(device));
}

static void set_environment_info(retro_environment_t cb)
{

    static const struct retro_subsystem_memory_info sgb_memory[] = {
        { "srm", RETRO_MEMORY_SGB_SRAM },
    };

    static const struct retro_subsystem_memory_info gb_memory[] = {
        { "srm", RETRO_MEMORY_GB_SRAM },
    };
	
	static const struct retro_subsystem_memory_info bsx_memory[] = {
		{ "srm", RETRO_MEMORY_BSX_SRAM },
	};

    static const struct retro_subsystem_rom_info sgb_roms[] = {
        { "Game Boy ROM", "gb|gbc", true, false, true, gb_memory, 1 },
        { "Super Game Boy ROM", "smc|sfc|swc|fig", true, false, true, sgb_memory, 1 },
    };
	
	static const struct retro_subsystem_rom_info bsx_roms[] = {
		{ "BS-X ROM", "bs", true, false, true, bsx_memory, 1 },
		{ "BS-X BIOS ROM", "smc|sfc|swc|fig", true, false, true, bsx_memory, 1 },
	};

    static const struct retro_subsystem_info subsystems[] = {
        { "Super Game Boy", "sgb", sgb_roms, 2, RETRO_GAME_TYPE_SGB },
		{ "BS-X Satellaview", "bsx", bsx_roms, 2, RETRO_GAME_TYPE_BSX },
        {}
    };

	cb(RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO,  (void*)subsystems);

	static const retro_controller_description port_1[] = {
		{ "SNES Joypad", RETRO_DEVICE_JOYPAD },
		{ "SNES Mouse", RETRO_DEVICE_MOUSE },
	};

	static const retro_controller_description port_2[] = {
		{ "SNES Joypad", RETRO_DEVICE_JOYPAD },
		{ "SNES Mouse", RETRO_DEVICE_MOUSE },
		{ "Multitap", RETRO_DEVICE_JOYPAD_MULTITAP },
		{ "SuperScope", RETRO_DEVICE_LIGHTGUN_SUPER_SCOPE },
		{ "Justifier", RETRO_DEVICE_LIGHTGUN_JUSTIFIER },
		{ "Justifiers", RETRO_DEVICE_LIGHTGUN_JUSTIFIERS },
	};

	static const retro_controller_info ports[] = {
		{ port_1, 2 },
		{ port_2, 6 },
		{ 0 },
	};

	cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, const_cast<retro_controller_info *>(ports));

	static const retro_input_descriptor desc[] = {
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "B" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "A" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "X" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Y" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,   "Select" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },

		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "B" },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "A" },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "X" },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Y" },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L" },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R" },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,   "Select" },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },

		{ 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
		{ 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
		{ 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
		{ 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
		{ 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "B" },
		{ 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "A" },
		{ 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "X" },
		{ 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Y" },
		{ 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L" },
		{ 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R" },
		{ 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,   "Select" },
		{ 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },

		{ 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
		{ 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
		{ 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
		{ 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
		{ 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "B" },
		{ 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "A" },
		{ 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "X" },
		{ 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Y" },
		{ 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L" },
		{ 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R" },
		{ 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,   "Select" },
		{ 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },

		{ 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
		{ 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
		{ 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
		{ 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
		{ 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "B" },
		{ 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "A" },
		{ 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "X" },
		{ 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Y" },
		{ 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L" },
		{ 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R" },
		{ 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,   "Select" },
		{ 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },

		{ 0 },
	};

	cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, const_cast<retro_input_descriptor *>(desc));

	static const retro_variable vars[] = {
		{ "bsnes_mode7_scale",         "HD Mode 7 Scale; 2x|3x|4x|5x|6x|7x|8x|9x|10x|disable|1x" },
		{ "bsnes_mode7_perspective",   "HD Mode 7 Perspective Correction; auto (wide)|auto (medium)|auto (narrow)|on (wide)|on (medium)|on (narrow)|off" },
		{ "bsnes_mode7_supersample",   "HD Mode 7 Supersampling; none|2x|3x|4x|5x|6x|7x|8x|9x|10x" },
		{ "bsnes_mode7_mosaic",        "HD Mode 7 HD->SD Mosaic; 1x scale|non-HD|ignore" },
		{ "bsnes_mode7_wsMode",        "WideScreen Mode; Mode 7|all|none" },
		{ "bsnes_mode7_widescreen",    "WideScreen Aspect Ratio; 16:9|2:1|21:9|16:10|4:3|none" },
		{ "bsnes_mode7_wsbg1",         "WideScreen Background 1; auto horz and vert|off|on|above line 40|below line 40|above line 80|below line 80|above line 120|below line 120|above line 160|below line 160|above line 200|below line 200|crop edges|auto crop edges|disable entirely|auto horizontal" },
		{ "bsnes_mode7_wsbg2",         "WideScreen Background 2; auto horz and vert|off|on|above line 40|below line 40|above line 80|below line 80|above line 120|below line 120|above line 160|below line 160|above line 200|below line 200|crop edges|auto crop edges|disable entirely|auto horizontal" },
		{ "bsnes_mode7_wsbg3",         "WideScreen Background 3; auto horz and vert|off|on|above line 40|below line 40|above line 80|below line 80|above line 120|below line 120|above line 160|below line 160|above line 200|below line 200|crop edges|auto crop edges|disable entirely|auto horizontal" },
		{ "bsnes_mode7_wsbg4",         "WideScreen Background 4; auto horz and vert|off|on|above line 40|below line 40|above line 80|below line 80|above line 120|below line 120|above line 160|below line 160|above line 200|below line 200|crop edges|auto crop edges|disable entirely|auto horizontal" },
		{ "bsnes_mode7_wsobj",         "WideScreen Sprites; safe|unsafe|disable entirely|clip" },
		{ "bsnes_mode7_wsBgCol",       "WideScreen Area Background Color; auto|color|black" },
		{ "bsnes_mode7_igwin",         "WideScreen Ignore Window; outside|outside and always|all|none" },
		{ "bsnes_mode7_igwinx",        "WideScreen Ig Win Coordinate; 128|168|216|40|88" },
		{ "bsnes_mode7_wsMarker",      "WideScreen Marker; none|lines|darken" },
		{ "bsnes_mode7_wsMarkerAlpha", "WideScreen Marker Alpha; 1/1|1/2|1/3|1/4|1/5|1/6|1/7|1/8|1/9|1/10" },
		{ "bsnes_mode7_bgGrad",        "HD Background Color Radius; 4|5|6|7|8|0|1|2|3" },
		{ "bsnes_mode7_windRad",       "HD Windowing (experimental); 0|1|2|3|4|5|6|7|8" },	
		{ "bsnes_ppu_show_overscan", "Show Overscan; OFF|ON" },
		{ "bsnes_video_aspectcorrection", "Pixel Aspect Correction; OFF|ON" },
		{ "bsnes_blur_emulation", "Blur emulation; OFF|ON" },
		{ "bsnes_entropy", "Entropy (randomization); Low|High|None" },
		{ "bsnes_hotfixes", "Hotfixes; OFF|ON" },
		{ "bsnes_cpu_fastmath", "CPU Fast Math; OFF|ON" },
		{ "bsnes_cpu_overclock", "CPU Overclocking; 100|110|120|130|140|150|160|170|180|190|200|210|220|230|240|250|260|270|280|290|300|310|320|330|340|350|360|370|380|390|400" },
		{ "bsnes_sa1_overclock", "SA1 Coprocessor Overclocking; 100|110|120|130|140|150|160|170|180|190|200|210|220|230|240|250|260|270|280|290|300|310|320|330|340|350|360|370|380|390|400" },
		{ "bsnes_sfx_overclock", "SuperFX Coprocessor Overclocking; 100|110|120|130|140|150|160|170|180|190|200|210|220|230|240|250|260|270|280|290|300|310|320|330|340|350|360|370|380|390|400|410|420|430|440|450|460|470|480|490|500|510|520|530|540|550|560|570|580|590|600|610|620|630|640|650|660|670|680|690|700|710|720|730|740|750|760|770|780|790|800" },
		{ "bsnes_ppu_fast", "PPU Fast mode; ON|OFF" },
		{ "bsnes_ppu_deinterlace", "PPU Deinterlace; ON|OFF" },
		{ "bsnes_ppu_no_sprite_limit", "PPU No sprite limit; ON|OFF" },
		{ "bsnes_ppu_no_vram_blocking", "PPU No VRAM blocking; OFF|ON" },
		{ "bsnes_dsp_fast", "DSP Fast mode; ON|OFF" },
		{ "bsnes_dsp_cubic", "DSP Cubic interpolation; OFF|ON" },
		{ "bsnes_dsp_echo_shadow", "DSP Echo shadow RAM; OFF|ON" },
		{ "bsnes_coprocessor_delayed_sync", "Coprocessor Delayed Sync; ON|OFF" },
		{ "bsnes_coprocessor_prefer_hle", "Coprocessor Prefer HLE; ON|OFF" },
		{ "bsnes_sgb_bios", "Preferred Super GameBoy BIOS (restart); SGB1.sfc|SGB2.sfc" },
		{ "bsnes_run_ahead_frames", "Amount of frames for run-ahead; OFF|1|2|3|4" },
		{ "bsnes_video_luminance", "Luminance; 100|90|80|70|60|50|40|30|20|10|0" },
		{ "bsnes_video_saturation", "Saturation; 100|90|80|70|60|50|40|30|20|10|0|200|190|180|170|160|150|140|130|120|110" },
		{ "bsnes_video_gamma", "Gamma; 100|110|120|130|140|150|160|170|180|190|200" },
		{ nullptr },
	};
	cb(RETRO_ENVIRONMENT_SET_VARIABLES, const_cast<retro_variable *>(vars));
}

RETRO_API void retro_set_environment(retro_environment_t cb)
{
	environ_cb = cb;

	retro_log_callback log = {};
	if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log) && log.log)
		libretro_print = log.log;

	set_environment_info(cb);
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb)
{
	video_cb = cb;
}

RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb)
{
	audio_cb = cb;
}

RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
	audio_batch_cb = cb;
}

RETRO_API void retro_set_input_poll(retro_input_poll_t cb)
{
	input_poll = cb;
}

RETRO_API void retro_set_input_state(retro_input_state_t cb)
{
	input_state = cb;
}

RETRO_API void retro_init()
{
	emulator = new SuperFamicom::Interface;
	program = new Program;
}

RETRO_API void retro_deinit()
{
	delete program;
}

RETRO_API unsigned retro_api_version()
{
	return RETRO_API_VERSION;
}

RETRO_API void retro_get_system_info(retro_system_info *info)
{
	info->library_name     = Emulator::Name;
	info->library_version  = Emulator::Version;
	info->need_fullpath    = true;
	info->valid_extensions = "smc|sfc|gb|gbc|bs";
	info->block_extract = false;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info)
{
	if (program->scale < 1) {
		info->geometry.base_width = 256;
		info->geometry.base_height = program->overscan ? 224 : 216;
		info->geometry.max_width = 256 * 2;
		info->geometry.max_height = (program->overscan ? 224 : 216) * 2;
	} else {
		uint w = (256 + program->ws * 2) * program->scale; 
		uint h = (program->overscan ? 224 : 216) * program->scale;
		info->geometry.base_width = w;
		info->geometry.base_height = h;
		info->geometry.max_width = w;
		info->geometry.max_height = h;
	}
	info->geometry.aspect_ratio = !program->aspectcorrection ?  -1.0
			: 8.0 / 7.0 * info->geometry.base_width / info->geometry.base_height;
	info->timing.sample_rate   = SAMPLERATE;
	if (retro_get_region() == RETRO_REGION_NTSC) {
		info->timing.fps = 21477272.0 / 357366.0;
		audio_buffer_max = (SAMPLERATE/60) * 2;
	}
	else
	{
		info->timing.fps = 21281370.0 / 425568.0;
	}
}

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device)
{
	set_controller_ports(port, device);
}

RETRO_API void retro_reset()
{
	emulator->reset();
}

static void run_with_runahead(const int frames)
{
	assert(frames > 0);

	emulator->setRunAhead(true);
	emulator->run();
	auto state = emulator->serialize(0);
	for (int i = 0; i < frames - 1; ++i) {
		emulator->run();
	}
	emulator->setRunAhead(false);
	emulator->run();
	state.setMode(serializer::Mode::Load);
	emulator->unserialize(state);
}

RETRO_API void retro_run()
{
	check_variables();
	input_poll();

	bool is_fast_forwarding = false;
	environ_cb(RETRO_ENVIRONMENT_GET_FASTFORWARDING, &is_fast_forwarding);
	if (is_fast_forwarding || run_ahead_frames == 0)
		emulator->run();
	else
		run_with_runahead(run_ahead_frames);
}

RETRO_API size_t retro_serialize_size()
{
	return emulator->serialize().size();
}

RETRO_API bool retro_serialize(void *data, size_t size)
{
	memcpy(data, emulator->serialize().data(), size);
	return true;
}

RETRO_API bool retro_unserialize(const void *data, size_t size)
{
	serializer s(static_cast<const uint8_t *>(data), size);
	return emulator->unserialize(s);
}

RETRO_API void retro_cheat_reset()
{
	cheatList.reset();
	emulator->cheats(cheatList);
}

RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
	string cheat = string(code);
	bool decoded = false;

	if (program->gameBoy.program)
	{
		decoded = decodeGB(cheat);
	}
	else
	{
		decoded = decodeSNES(cheat);
	}

	if (enabled && decoded)
	{
		cheatList.append(cheat);
		emulator->cheats(cheatList);
	}
}

RETRO_API bool retro_load_game(const retro_game_info *game)
{
	retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
	if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
		return false;

	emulator->configure("Audio/Frequency", SAMPLERATE);

	flush_variables();

	if (string(game->path).endsWith(".gb"))
	{
		const char *system_dir;
		environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir);
		string sgb_full_path = string(game->path).transform("\\", "/");
		string sgb_full_path2 = string(sgb_full_path).replace(".gb", ".sfc");
		if (!file::exists(sgb_full_path2)) {
			string sgb_full_path = string(system_dir, "/", sgb_bios).transform("\\", "/");
			program->superFamicom.location = sgb_full_path;
		}
        else {
			program->superFamicom.location = sgb_full_path2;
		}
		program->gameBoy.location = string(game->path);
		if (!file::exists(program->superFamicom.location)) {
			return false;
		}
	}
	else if (string(game->path).endsWith(".gbc"))
	{
		const char *system_dir;
		environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir);
		string sgb_full_path = string(game->path).transform("\\", "/");
		string sgb_full_path2 = string(sgb_full_path).replace(".gbc", ".sfc");
		if (!file::exists(sgb_full_path2)) {
			string sgb_full_path = string(system_dir, "/", sgb_bios).transform("\\", "/");
			program->superFamicom.location = sgb_full_path;
		}
        else {
			program->superFamicom.location = sgb_full_path2;
		}
		program->gameBoy.location = string(game->path);
		if (!file::exists(program->superFamicom.location)) {
			return false;
		}

	}
	else if (string(game->path).endsWith(".bs"))
	{
		const char *system_dir;
		environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir);
		string bs_full_path = string(system_dir, "/", "BS-X.bin").transform("\\", "/");
		if (!file::exists(bs_full_path)) {
			return false;
		}

		program->superFamicom.location = bs_full_path;
		program->bsMemory.location = string(game->path);
	}
	else
	{
		program->superFamicom.location = string(game->path);
	}
	program->base_name = string(game->path);

	program->load();

	emulator->connect(SuperFamicom::ID::Port::Controller1, SuperFamicom::ID::Device::Gamepad);
	emulator->connect(SuperFamicom::ID::Port::Controller2, SuperFamicom::ID::Device::Gamepad);
	return true;
}

RETRO_API bool retro_load_game_special(unsigned game_type,
		const struct retro_game_info *info, size_t num_info)
{
	retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
	if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
		return false;

	emulator->configure("Audio/Frequency", SAMPLERATE);

	flush_variables();

	switch(game_type)
	{
		case RETRO_GAME_TYPE_SGB:
		{
			libretro_print(RETRO_LOG_INFO, "GB ROM: %s\n", info[0].path);
			libretro_print(RETRO_LOG_INFO, "SGB ROM: %s\n", info[1].path);
			program->gameBoy.location = info[0].path;
			program->superFamicom.location = info[1].path;
		}
		break;
		case RETRO_GAME_TYPE_BSX:
		{
			libretro_print(RETRO_LOG_INFO, "BS-X ROM: %s\n", info[0].path);
			libretro_print(RETRO_LOG_INFO, "BS-X BIOS ROM: %s\n", info[1].path);
			program->bsMemory.location = info[0].path;
			program->superFamicom.location = info[1].path;
		}
		break;
		default:
			return false;
	}

	program->load();

	emulator->connect(SuperFamicom::ID::Port::Controller1, SuperFamicom::ID::Device::Gamepad);
	emulator->connect(SuperFamicom::ID::Port::Controller2, SuperFamicom::ID::Device::Gamepad);
	return true;
}

RETRO_API void retro_unload_game()
{
	program->save();
	emulator->unload();
}

RETRO_API unsigned retro_get_region()
{
	return program->superFamicom.region == "NTSC" ? RETRO_REGION_NTSC : RETRO_REGION_PAL;
}

// Currently, there is no safe/sensible way to use the memory interface without severe hackery.
// Rely on higan to load and save SRAM until there is really compelling reason not to.
RETRO_API void *retro_get_memory_data(unsigned id)
{
	return nullptr;
}

RETRO_API size_t retro_get_memory_size(unsigned id)
{
	return 0;
}
