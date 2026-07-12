/**********************************************************************************
                NES RA Adapter - Raspberry Pi Pico Firmware

   This project is the Raspberry Pi Pico firmware for the NES RA Adapter, a device
   that connects to an NES console and enables users to connect to the RetroAchievements
   server to unlock achievements in games played on original hardware.

   The Pico firmware has two main responsibilities. The first is to read a portion of
   the cartridge to calculate the CRC and identify the game; the second is to monitor
   the bus for memory writes and process them using the rcheevos library.

    * Core 0 handles CRC calculation, executes the rcheevos routines, and manages serial
      communication with the ESP32 (primarily for Internet connectivity).

    * Core 1 monitors the bus using three PIO state machines and no DMA:
      - SM0 captures exactly one bus snapshot per CPU write cycle (sampled inside the
        data-valid window of M2 high) and core 1 drains the FIFO directly, updating
        static mirrors of the NES RAM and cartridge SRAM.
      - SM1 watches for reads of the NMI vector ($FFFA/$FFFB): a match marks the exact
        start of vblank, when core 1 takes an atomic snapshot of the mirrors and
        signals core 0 to run a rcheevos frame.
      - SM2 watches for reads of the RESET vector ($FFFC/$FFFD) to detect a console
        reset and reset the rcheevos runtime state.
      Writes to $4014 (OAM DMA) are kept as a vblank fallback for games that run with
      NMI disabled, and a timer-based fallback covers forced-blank periods.

    Inter-core communication is managed via a circular buffer. Please note that the available
    space for serial communication is limited to around 32KB, which restricts the size of
    the achievement list response.

   Date:             2026-07-12
   Version:          1.4
   By odelot

   Libs used:
   rcheevos: https://github.com/RetroAchievements/rcheevos

   Compiled with Pico SDK 1.5.1

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.

**********************************************************************************/

#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <limits.h>
#include <math.h>

#include "pico/stdlib.h"
#include "pico/malloc.h"
#include "pico/multicore.h"
#include "pico/mutex.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/structs/systick.h"
#include "hardware/timer.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "hardware/watchdog.h"

#include "memory-bus.pio.h"

#include "rc_runtime_types.h"
#include "rc_client.h"
#include "rc_client_internal.h"
#include "rc_api_info.h"
#include "rc_api_runtime.h"
#include "rc_api_user.h"
#include "rc_consoles.h"
#include "rc_hash.h"
#include "rc_version.h"
#include "rc_internal.h"

#define FIRMWARE_VERSION "1.4"

// run at 200mhz can save energy and need to be tested if it is stable - it saves ~0.010A
#define RUN_AT_200MHZ

#define BUS_PIO pio0
#define BUS_SM_WRITE 0 // one snapshot per CPU write cycle
#define BUS_SM_NMI 1   // NMI vector fetch watcher ($FFFA/$FFFB)
#define BUS_SM_RESET 2 // RESET vector fetch watcher ($FFFC/$FFFD)
#define BUS_SM_PPU 3   // PPU /RD quiescence watcher (ENABLE_PPU_RD_VBLANK)

#define UART_ID uart0
#define BAUD_RATE 115200

#define NES_D0 0
#define NES_D1 1
#define NES_D2 2
#define NES_D3 3
#define NES_D4 4
#define NES_D5 5
#define NES_D6 6
#define NES_D7 7

#define NES_A00 8
#define NES_A01 9
#define NES_A02 10
#define NES_A03 11
#define NES_A04 12
#define NES_A05 13
#define NES_A06 14
#define NES_A07 15

#define NES_A08 16
#define NES_A09 17
#define NES_A10 18
#define NES_A11 19
#define NES_A12 20
#define NES_A13 21
#define NES_A14 22

#define NES_M2 23     // F0
#define NES_ROMSEL 24 // F1
#define NES_RW 25     // F2

#define UART_TX_PIN 28 // GPIO pin for TX
#define UART_RX_PIN 29 // GPIO pin for RX

#define REFRESH_RATE_60_HZ // comment this line to compile to 50hz

#ifdef REFRESH_RATE_60_HZ
#define FRAME_TIME_US 16667 // 1000000 / 60 = 16666.6667
#else
#define FRAME_TIME_US 20000 // 1000000 / 50 = 20000
#endif

/**
 * enable internal web app support
 */

#define ENABLE_INTERNAL_WEB_APP_SUPPORT

/**
 * OPTIONAL: universal vblank detection via PPU /RD quiescence.
 *
 * Requires a HARDWARE WIRE from the cartridge slot PPU /RD signal to
 * PPU_RD_PIN (through a ~1k series resistor). The PPU fetches from the
 * CHR bus every ~186ns while rendering, so a sustained quiet period on
 * /RD marks the start of blanking - a vblank signal that works even for
 * games that run with NMI disabled and never write $4014.
 *
 * Disabled by default: without the wire the pin floats (an internal
 * pull-up keeps it inert, producing at most one spurious frame signal).
 * Enable only on boards that have the /RD wire installed.
 */
// #define ENABLE_PPU_RD_VBLANK
#define PPU_RD_PIN 26

/**
 * Vblank detection instrumentation: core 1 collects timing statistics and
 * core 0 prints a VBSTAT line on the USB serial every 10s. Interpretation:
 *   - avg NMI-to-NMI period must be ~16639us (NTSC) / ~19997us (PAL) with
 *     min/max within a few tens of us; gaps/spurious must stay 0 in gameplay
 *     (gaps appear legitimately in menus/loads that disable NMI).
 *   - every $4014 write should land inside the vblank window right after our
 *     NMI marker (in_vb == oam, lat_max < ~2300us) - physical ground truth.
 * Comment this define out for release builds.
 */
#define ENABLE_VBLANK_INSTRUMENTATION

const uint16_t NES_D[8] = {NES_D0, NES_D1, NES_D2, NES_D3, NES_D4, NES_D5, NES_D6, NES_D7};
const uint16_t NES_A[15] = {NES_A00, NES_A01, NES_A02, NES_A03, NES_A04, NES_A05, NES_A06, NES_A07, NES_A08, NES_A09, NES_A10, NES_A11, NES_A12, NES_A13, NES_A14};
const uint16_t NES_F[3] = {NES_ROMSEL, NES_M2, NES_RW};

// CRC32 global variables
uint32_t crc_begin = 0xFFFFFFFF;
uint32_t crc_end = 0xFFFFFFFF;

static const uint32_t crc_32_tab[] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba,
    0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
    0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
    0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de,
    0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec,
    0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
    0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
    0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940,
    0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116,
    0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
    0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
    0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a,
    0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818,
    0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
    0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
    0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c,
    0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2,
    0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
    0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
    0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086,
    0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4,
    0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
    0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
    0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8,
    0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe,
    0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
    0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
    0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252,
    0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60,
    0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
    0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
    0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04,
    0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a,
    0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
    0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
    0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e,
    0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c,
    0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
    0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
    0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0,
    0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6,
    0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
    0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d};

/*
 * PIO Bus Watcher global variables
 */

uint PIO_offset_write;
uint PIO_offset_vector;
#ifdef ENABLE_PPU_RD_VBLANK
uint PIO_offset_ppu;
#endif
mutex_t cpu_bus_mutex;

/*
 * Serial buffer to handle commands from the ESP32
 *
 * Allocated dynamically so we can hold the large achievement patch response
 * (up to ~100KB from the ESP32) during load, then shrink to a small buffer
 * for the in-game phase where messages are tiny (award acks, pings, etc).
 * The +64 slack covers the "RESP=XX;" prefix and the "\r\n" terminator.
 */

#define SERIAL_BUFFER_INITIAL_SIZE (100 * 1024 + 64)
#define SERIAL_BUFFER_RUNTIME_SIZE (16 * 1024)
u_char *serial_buffer = NULL;
u_char *serial_buffer_head = NULL;
uint32_t serial_buffer_size = 0;

/*
 * Dynamic arrays for NES RAM and PRG-RAM/SRAM mirroring.
 * Allocated only when the game starts (right before core 1 is launched),
 * so the heap is free for the big serial buffer during patch download.
 */
#define NES_RAM_SIZE  2048
#define NES_SRAM_SIZE 8192

volatile uint8_t *nes_ram = NULL;
volatile uint8_t *nes_sram = NULL;

// Snapshot arrays to capture atomic state at vblank (NMI vector fetch)
uint8_t *nes_ram_snapshot = NULL;
uint8_t *nes_sram_snapshot = NULL;

// flags for synchronization between cores
volatile bool flag_internal_ram_written = false;
volatile bool flag_vblank_detected = false; // set by core 1 on NMI vector fetch (or $4014 fallback)
volatile bool flag_reset_detected = false;  // set by core 1 on RESET vector fetch

/*
 * Staging area used while taking an atomic snapshot of the RAM/SRAM mirrors.
 * The snapshot memcpy takes ~50-70us; bus writes captured meanwhile are parked
 * here (instead of being applied) so the snapshot is a true point-in-time copy,
 * then replayed onto the live mirrors afterwards. 256 entries cover the
 * theoretical worst case of one write per CPU cycle for the whole snapshot.
 */
#define WRITE_STAGE_SIZE 256
static uint32_t write_stage[WRITE_STAGE_SIZE];

#ifdef ENABLE_VBLANK_INSTRUMENTATION
/*
 * Vblank instrumentation counters. Written by core 1 inside the bus loop,
 * sampled and reported by core 0. Debug-grade: cross-core races only risk a
 * stale sample in one report, never corruption of the detection itself.
 */
volatile uint32_t vbstat_nmi_periods = 0;     // completed NMI-to-NMI intervals
volatile uint32_t vbstat_period_sum_us = 0;   // period accumulator (wraps; deltas used)
volatile uint32_t vbstat_period_min_us = 0xFFFFFFFF; // windowed (reset each report)
volatile uint32_t vbstat_period_max_us = 0;          // windowed (reset each report)
volatile uint32_t vbstat_gaps = 0;            // periods > 25ms (NMI off or missed)
volatile uint32_t vbstat_spurious = 0;        // periods < 15ms (double detection)
volatile uint32_t vbstat_oam_writes = 0;      // every $4014 write seen on the bus
volatile uint32_t vbstat_oam_in_vblank = 0;   // ...landing <=2.5ms after our NMI marker
volatile uint32_t vbstat_oam_lat_max_us = 0;  // worst NMI->$4014 latency (windowed)
volatile uint32_t vbstat_stage_peak = 0;      // most writes staged during one snapshot
volatile uint32_t vbstat_stage_overflows = 0; // staging overflows (must stay 0)
volatile uint32_t vbstat_resets = 0;          // RESET vector detections
static uint32_t vbstat_timer_frames = 0;      // core 0 only: timer-fallback frames
#endif

/**
 * RetroAchievements (rcheevos) related global variables
 */

typedef struct
{
    rc_client_server_callback_t callback;
    void *callback_data;
} async_callback_data;

rc_client_t *g_client = NULL;
static void *g_callback_userdata = &g_client; /* dummy data */
char rcheevos_userdata[16];

async_callback_data async_data;

// struct to keep track of a async request using its request id
typedef struct
{
    uint8_t id;
    async_callback_data async_data;
} async_callback_data_id;

#define MAX_ASYNC_CALLBACKS 5
async_callback_data_id async_handlers[MAX_ASYNC_CALLBACKS];
uint8_t async_handlers_index = 0;

uint8_t request_id = 0; // last request id used - used to identify the response of async requests

/*
 * FIFO for achievements the user won - he can earn more than one achievement at a time,
 * but we will send them one by one to the ESP32 to be shown on the screen
 */

#define FIFO_SIZE 15

typedef struct
{
    uint32_t id;
    u_int8_t event;
    char measured_progress[24];
} achievement_t;

typedef struct
{
    achievement_t buffer[FIFO_SIZE];
    int head;
    int tail;
    int count;
} FIFO_t;

FIFO_t achievements_fifo;

/*
 * states and general variables
 */

// state for the state machine
uint8_t state = 0;

// do we detected a NES reset after connect the BUS between NES and Cartridge?
int nes_reseted = 0;

// how many requests are in flight
uint8_t request_ongoing = 0;

// timestamp of the last request to control timeout
uint32_t last_request = 0;

// timestamp of the last frame processed
uint64_t last_frame_processed = 0;

// keeps the MD5 of the game (or RA Hash)
char md5[33];

// keeps the user token to authenticate on RetroAchievements
char ra_token[32];

// keeps the user name to authenticate on RetroAchievements
char ra_user[256];

/*
 * GPIO configuration functions
 */

// reset GPIO to default state
void reset_GPIO()
{
    for (int i = 0; i < 8; i += 1)
    {
        gpio_init(NES_D[i]);
        gpio_set_dir(NES_D[i], GPIO_IN);
        gpio_disable_pulls(NES_D[i]);
    }
    for (int i = 0; i < 15; i += 1)
    {
        gpio_init(NES_A[i]);
        gpio_set_dir(NES_A[i], GPIO_IN);
        gpio_disable_pulls(NES_A[i]);
    }
    for (int i = 0; i < 3; i += 1)
    {
        gpio_init(NES_F[i]);
        gpio_set_dir(NES_F[i], GPIO_IN);
        gpio_disable_pulls(NES_F[i]);
    }
}

// configure GPIO to read the cartridge and calculate the CRCs
void init_GPIO_for_CRC32()
{
    for (int i = 0; i < 8; i += 1)
    {
        gpio_init(NES_D[i]);
        gpio_set_dir(NES_D[i], GPIO_IN);
        gpio_pull_down(NES_D[i]);
    }
    for (int i = 0; i < 15; i += 1)
    {
        gpio_init(NES_A[i]);
        gpio_set_dir(NES_A[i], GPIO_OUT);
        gpio_set_drive_strength(NES_A[i], GPIO_DRIVE_STRENGTH_12MA);
    }
    for (int i = 0; i < 3; i += 1)
    {
        gpio_init(NES_F[i]);
        gpio_set_dir(NES_F[i], GPIO_OUT);
        gpio_set_drive_strength(NES_F[i], GPIO_DRIVE_STRENGTH_12MA);
    }
}

// reset GPIO to default state after calculate the CRCs
void end_GPIO_for_CRC32()
{
    for (int i = 0; i < 8; i += 1)
    {
        gpio_init(NES_D[i]);
        gpio_set_dir(NES_D[i], GPIO_IN);
        gpio_disable_pulls(NES_D[i]);
    }
    for (int i = 0; i < 15; i += 1)
    {
        gpio_init(NES_A[i]);
        gpio_set_dir(NES_A[i], GPIO_IN);
        gpio_disable_pulls(NES_A[i]);
    }
    for (int i = 0; i < 3; i += 1)
    {
        gpio_init(NES_F[i]);
        gpio_set_dir(NES_F[i], GPIO_IN);
        gpio_disable_pulls(NES_F[i]);
    }
}

// configure pull-ups to dominate the bus and prevent console from interacting
// with the cartridge while the analog switch is off. RW pulled high prevents
// writes to cartridge RAM, ROMSEL pulled high keeps ROM/RAM deselected,
// M2 pulled low keeps bus cycle inactive.
void set_GPIO_dominate_bus()
{
    for (int i = 0; i < 8; i += 1)
    {
        gpio_init(NES_D[i]);
        gpio_set_dir(NES_D[i], GPIO_IN);
        gpio_pull_up(NES_D[i]);
    }
    for (int i = 0; i < 15; i += 1)
    {
        gpio_init(NES_A[i]);
        gpio_set_dir(NES_A[i], GPIO_IN);
        gpio_pull_up(NES_A[i]);
    }
    gpio_init(NES_RW);
    gpio_set_dir(NES_RW, GPIO_IN);
    gpio_pull_up(NES_RW);

    gpio_init(NES_ROMSEL);
    gpio_set_dir(NES_ROMSEL, GPIO_IN);
    gpio_pull_up(NES_ROMSEL);

    gpio_init(NES_M2);
    gpio_set_dir(NES_M2, GPIO_IN);
    gpio_pull_down(NES_M2);
}

/*
 * PIO functions
 */

// initialize the PIO programs: write sniffer + NMI/RESET vector watchers
void setup_PIO()
{

    for (int i = 0; i < 26; i++) // reset all GPIOs connected to NES
        gpio_init(i);
    PIO_offset_write = pio_add_program(BUS_PIO, &memoryBusWrite_program);
    PIO_offset_vector = pio_add_program(BUS_PIO, &vectorWatch_program);
#ifdef RUN_AT_200MHZ
    set_sys_clock_khz(200000, true);
    const float pio_div = 7.0f; // 200mhz / 7 = ~35ns per PIO tick
#else
    set_sys_clock_khz(250000, true);
    const float pio_div = 9.0f; // 250mhz / 9 = ~36ns per PIO tick
#endif
    memoryBusWrite_program_init(BUS_PIO, BUS_SM_WRITE, PIO_offset_write, pio_div);
    vectorWatch_program_init(BUS_PIO, BUS_SM_NMI, PIO_offset_vector, pio_div, VECTOR_WATCH_NMI_TARGET);
    vectorWatch_program_init(BUS_PIO, BUS_SM_RESET, PIO_offset_vector, pio_div, VECTOR_WATCH_RESET_TARGET);
#ifdef ENABLE_PPU_RD_VBLANK
    PIO_offset_ppu = pio_add_program(BUS_PIO, &ppuQuiet_program);
    gpio_init(PPU_RD_PIN);
    gpio_set_dir(PPU_RD_PIN, GPIO_IN);
    gpio_pull_up(PPU_RD_PIN); // keeps the pin inert if the /RD wire is absent
    // 28 laps of 2 ticks (~70ns each at div 7) = ~2us of quiet on /RD marks blanking;
    // rendering never rests longer than ~400ns, so 2us cannot false-trigger mid-frame
    ppuQuiet_program_init(BUS_PIO, BUS_SM_PPU, PIO_offset_ppu, pio_div, PPU_RD_PIN, 28);
#endif
}

void stop_PIO()
{
    pio_sm_set_enabled(BUS_PIO, BUS_SM_WRITE, false);
    pio_sm_set_enabled(BUS_PIO, BUS_SM_NMI, false);
    pio_sm_set_enabled(BUS_PIO, BUS_SM_RESET, false);
    pio_sm_clear_fifos(BUS_PIO, BUS_SM_WRITE);
    pio_sm_clear_fifos(BUS_PIO, BUS_SM_NMI);
    pio_sm_clear_fifos(BUS_PIO, BUS_SM_RESET);
    pio_sm_restart(BUS_PIO, BUS_SM_WRITE);
    pio_sm_restart(BUS_PIO, BUS_SM_NMI);
    pio_sm_restart(BUS_PIO, BUS_SM_RESET);
    pio_remove_program(BUS_PIO, &memoryBusWrite_program, PIO_offset_write);
    pio_remove_program(BUS_PIO, &vectorWatch_program, PIO_offset_vector);
#ifdef ENABLE_PPU_RD_VBLANK
    pio_sm_set_enabled(BUS_PIO, BUS_SM_PPU, false);
    pio_sm_clear_fifos(BUS_PIO, BUS_SM_PPU);
    pio_sm_restart(BUS_PIO, BUS_SM_PPU);
    pio_remove_program(BUS_PIO, &ppuQuiet_program, PIO_offset_ppu);
#endif
}

/*
 * Core 1: bus monitoring
 */

// set once the first NMI vector fetch is seen; disables the $4014 vblank fallback
static bool nmi_vector_seen = false;

// timestamp of the last accepted NMI vblank event. The vector fetch reads
// $FFFA and $FFFB on consecutive cycles (two FIFO pushes); when the snapshot
// runs between them, the second push is only drained ~50us later, so the pair
// must be deduplicated by time, not by the drain loop.
static uint32_t last_nmi_event_us = 0;

// apply one captured write-cycle word to the RAM/SRAM mirrors
static inline void __not_in_flash_func(apply_bus_write)(uint32_t raw)
{
    if ((raw >> 25) & 0x1) // R/W re-check (the PIO already filters reads; belt and suspenders)
        return;
    uint16_t address = (raw >> 8) & 0x7FFF;
    uint8_t data = raw & 0xFF;
    if (address < 0x2000)
    {
        nes_ram[address & 0x07FF] = data;
        if (!flag_internal_ram_written)
        {
            flag_internal_ram_written = true;
        }
    }
    else if (address >= 0x6000 && address < 0x8000)
    {
        nes_sram[address - 0x6000] = data;
    }
}

// drain the write FIFO into the staging area (used while snapshotting)
static inline void __not_in_flash_func(stage_pending_writes)(int *staged)
{
    while (!pio_sm_is_rx_fifo_empty(BUS_PIO, BUS_SM_WRITE))
    {
        uint32_t raw = pio_sm_get(BUS_PIO, BUS_SM_WRITE);
        if (*staged < WRITE_STAGE_SIZE)
        {
            write_stage[(*staged)++] = raw;
#ifdef ENABLE_VBLANK_INSTRUMENTATION
            if ((uint32_t)(*staged) > vbstat_stage_peak)
                vbstat_stage_peak = (uint32_t)(*staged);
#endif
        }
        else
        {
#ifdef ENABLE_VBLANK_INSTRUMENTATION
            vbstat_stage_overflows += 1;
#endif
            apply_bus_write(raw); // staging full (never expected) - degrade gracefully
        }
    }
}

// take a point-in-time snapshot of the mirrors. The copy is chunked, parking
// writes that arrive meanwhile in the staging area, so (a) the 8-deep write
// FIFO never overflows during the ~60us copy and (b) the snapshot is atomic:
// staged writes are replayed onto the live mirrors only after the copy ends.
static void __not_in_flash_func(snapshot_mirrors_atomic)()
{
    int staged = 0;
    const uint32_t chunk = 512;
    for (uint32_t offset = 0; offset < NES_RAM_SIZE; offset += chunk)
    {
        memcpy(nes_ram_snapshot + offset, (const uint8_t *)nes_ram + offset, chunk);
        stage_pending_writes(&staged);
    }
    for (uint32_t offset = 0; offset < NES_SRAM_SIZE; offset += chunk)
    {
        memcpy(nes_sram_snapshot + offset, (const uint8_t *)nes_sram + offset, chunk);
        stage_pending_writes(&staged);
    }
    for (int i = 0; i < staged; i += 1)
        apply_bus_write(write_stage[i]);
}

// core 1 entry point: drain the PIO FIFOs, keep the RAM/SRAM mirrors fresh and
// signal core 0 on vblank (NMI vector fetch / $4014 fallback) and console reset
void __not_in_flash_func(handle_bus_to_detect_memory_writes)()
{
    mutex_init(&cpu_bus_mutex);
    mutex_enter_blocking(&cpu_bus_mutex); // make sure core 1 is fully dedicated to handle the BUS

    // restore GPIOs to functional state (no pulls) before configuring PIO
    // this releases the bus domination pull-ups so the console can communicate with the cartridge
    reset_GPIO();
    sleep_ms(10);

    setup_PIO();

    pio_sm_set_enabled(BUS_PIO, BUS_SM_WRITE, true);
    pio_sm_set_enabled(BUS_PIO, BUS_SM_NMI, true);
    pio_sm_set_enabled(BUS_PIO, BUS_SM_RESET, true);
#ifdef ENABLE_PPU_RD_VBLANK
    pio_sm_set_enabled(BUS_PIO, BUS_SM_PPU, true);
#endif

    uint32_t last_reset_push_us = 0;
    bool frame_signal = false;

    while (1)
    {
        // apply captured write cycles to the RAM/SRAM mirrors
        while (!pio_sm_is_rx_fifo_empty(BUS_PIO, BUS_SM_WRITE))
        {
            uint32_t raw = pio_sm_get(BUS_PIO, BUS_SM_WRITE);
            apply_bus_write(raw);
            if (((raw >> 8) & 0x7FFF) == 0x4014 && !((raw >> 25) & 0x1))
            {
#ifdef ENABLE_VBLANK_INSTRUMENTATION
                // ground truth: OAM DMA is started by game code inside vblank,
                // so it must land shortly after our NMI-vector vblank marker
                vbstat_oam_writes += 1;
                if (nmi_vector_seen)
                {
                    uint32_t lat = time_us_32() - last_nmi_event_us;
                    if (lat <= 2500)
                    {
                        vbstat_oam_in_vblank += 1;
                        if (lat > vbstat_oam_lat_max_us)
                            vbstat_oam_lat_max_us = lat;
                    }
                }
#endif
                // $4014 (OAM DMA) as vblank fallback for games that run with NMI disabled
                if (!nmi_vector_seen)
                    frame_signal = true;
            }
        }

#ifdef ENABLE_PPU_RD_VBLANK
        // PPU /RD went quiet = blanking started (works with NMI disabled too)
        if (!pio_sm_is_rx_fifo_empty(BUS_PIO, BUS_SM_PPU))
        {
            do
            {
                (void)pio_sm_get(BUS_PIO, BUS_SM_PPU);
            } while (!pio_sm_is_rx_fifo_empty(BUS_PIO, BUS_SM_PPU));
            frame_signal = true;
        }
#endif

        // NMI vector fetch ($FFFA/$FFFB read) = vblank start
        if (!pio_sm_is_rx_fifo_empty(BUS_PIO, BUS_SM_NMI))
        {
            do
            {
                (void)pio_sm_get(BUS_PIO, BUS_SM_NMI);
            } while (!pio_sm_is_rx_fifo_empty(BUS_PIO, BUS_SM_NMI));
            nmi_vector_seen = true;
            uint32_t now = time_us_32();
            // accept at most one vblank event per 10ms: dedupes the $FFFA/$FFFB
            // pair when the snapshot ran between the two pushes (NMI can never
            // legitimately re-fire this fast - NTSC 16.6ms, PAL 20ms)
            if (last_nmi_event_us == 0 || (now - last_nmi_event_us) > 10000)
            {
                frame_signal = true;
#ifdef ENABLE_VBLANK_INSTRUMENTATION
                if (last_nmi_event_us != 0)
                {
                    uint32_t period = now - last_nmi_event_us;
                    vbstat_period_sum_us += period;
                    vbstat_nmi_periods += 1;
                    if (period < vbstat_period_min_us)
                        vbstat_period_min_us = period;
                    if (period > vbstat_period_max_us)
                        vbstat_period_max_us = period;
                    if (period > 25000)
                        vbstat_gaps += 1;
                    else if (period < 15000)
                        vbstat_spurious += 1;
                }
#endif
                last_nmi_event_us = now;
            }
        }

        // RESET vector fetch ($FFFC/$FFFD read) = console reset. A genuine fetch
        // reads both bytes on consecutive cycles (~559ns apart); require the pair
        // to reject stray data/dummy reads of a single vector byte.
        if (!pio_sm_is_rx_fifo_empty(BUS_PIO, BUS_SM_RESET))
        {
            int pushes = 0;
            do
            {
                (void)pio_sm_get(BUS_PIO, BUS_SM_RESET);
                pushes += 1;
            } while (!pio_sm_is_rx_fifo_empty(BUS_PIO, BUS_SM_RESET));
            uint32_t now = time_us_32();
            if (pushes >= 2 || (now - last_reset_push_us) <= 3)
            {
                flag_reset_detected = true;
#ifdef ENABLE_VBLANK_INSTRUMENTATION
                vbstat_resets += 1;
#endif
            }
            last_reset_push_us = now;
        }

        // on vblank: snapshot the mirrors and signal core 0 to run a rcheevos frame
        if (frame_signal)
        {
            frame_signal = false;
            if (!flag_vblank_detected)
            {
                snapshot_mirrors_atomic();
                flag_vblank_detected = true;
            }
        }
    }
}

/**
 * Cartridge identification functions (CRC32 related)
 */

// update the CRC32 with a new byte of data
uint32_t update_crc32(const uint8_t data, uint32_t crc)
{
    uint8_t table_index = (uint8_t)((crc ^ data) & 0xFF);
    crc = (crc >> 8) ^ crc_32_tab[table_index];
    return crc;
}

// read a byte from the NES PRG ROM given a rom address
int8_t read_NES_PRG_ROM_Address(uint32_t address)
{
    gpio_put(NES_ROMSEL, true);
    gpio_put(NES_M2, false);

    for (int i = 14; i >= 0; i -= 1)
    {
        uint8_t d = (address >> i) & 0x1;
        gpio_put(NES_A[i], d);
    }
    gpio_put(NES_RW, true);
    gpio_put(NES_M2, true);
    gpio_put(NES_ROMSEL, false);

    sleep_ms(1);

    uint8_t value = 0;
    // read data
    for (int i = 7; i >= 0; i -= 1)
    {
        uint8_t d = gpio_get(NES_D[i]);
        value |= (d << i);
    }
    return value;
}

// calculate CRC32 from the first and last memory rom banks of the PRG ROM
// using its first 512 bytes - same approch used by open source cart reader
// we do this way because how mappers maps the PRG ROM to the CPU address space
void calculate_CRC32_to_idenfity_cartridge()
{
    sleep_ms(250);
    init_GPIO_for_CRC32();
    sleep_ms(250);
    char command[256];
    for (int k = 0; k < 1; k += 1)
    {
        crc_begin = 0xFFFFFFFF;
        gpio_put(NES_RW, 0);
        gpio_put(NES_M2, 0);
        gpio_put(NES_ROMSEL, 1);

        // read first bank sequentially (512 bytes)
        for (int c = 0; c < 512; c++)
        {
            uint32_t byte_from_first_bank = read_NES_PRG_ROM_Address(0x8000 + c);
            crc_begin = update_crc32(byte_from_first_bank, crc_begin);
        }

        // read last bank sequentially (512 bytes)
        for (int c = 0; c < 512; c++)
        {
            uint32_t byte_from_last_bank = read_NES_PRG_ROM_Address(0xE000 + c);
            crc_end = update_crc32(byte_from_last_bank, crc_end);
        }

        crc_begin = ~crc_begin;
        crc_end = ~crc_end;

        sprintf(command, "READ_CRC=%p,%p\r\n", crc_begin, crc_end);
        printf("CRC32 BEGIN: %p\n", crc_begin); // DEBUG
        printf("CRC32 END: %p\n", crc_end);     // DEBUG

        sleep_ms(250);
    }
    uart_puts(UART_ID, command);
    end_GPIO_for_CRC32();

    // after reading, dominate the bus with pull-ups to prevent the console
    // from interacting with the cartridge when the analog switch is turned on
    set_GPIO_dominate_bus();
}

// aux function - startWith string test
bool prefix(const char *pre, const char *str)
{
    return strncmp(pre, str, strlen(pre)) == 0;
}

/*
 * functions to handle the FIFO for achievements the user won and need to be sent to the ESP32
 * to be shown on the screen
 */

void fifo_init(FIFO_t *fifo)
{
    fifo->head = 0;
    fifo->tail = 0;
    fifo->count = 0;
}

bool fifo_is_empty(FIFO_t *fifo)
{
    return fifo->count == 0;
}

bool fifo_is_full(FIFO_t *fifo)
{
    return fifo->count == FIFO_SIZE;
}

bool fifo_enqueue(FIFO_t *fifo, achievement_t value)
{
    if (fifo_is_full(fifo))
    {
        return false; // FIFO cheia
    }
    fifo->buffer[fifo->tail] = value;
    fifo->tail = (fifo->tail + 1) % FIFO_SIZE;
    fifo->count++;
    return true;
}

bool fifo_dequeue(FIFO_t *fifo, achievement_t *value)
{
    if (fifo_is_empty(fifo))
    {
        return false; // FIFO vazia
    }
    *value = fifo->buffer[fifo->head];
    fifo->head = (fifo->head + 1) % FIFO_SIZE;
    fifo->count--;
    return true;
}

// Max MemAddr length before an achievement is silently stubbed to "0=0".
// Merchandise Madness (FF1) has a ~51 KB MemAddr that exhausts SRAM during
// rcheevos parse. Achievements stubbed this way never trigger on-device.
#define MAX_MEMADDR_LEN 8192

// Scan a rcheevos patch JSON string and replace any "MemAddr" value longer
// than MAX_MEMADDR_LEN with "0=0", shifting the remainder left in-place.
// Returns the new (shorter) string length; the buffer is null-terminated.
static size_t filter_large_memaddr(char *json, size_t len)
{
    const char *needle = "\"MemAddr\":\"";
    const size_t needle_len = 11;
    char *pos = json;

    while (1)
    {
        char *found = strstr(pos, needle);
        if (!found)
            break;

        char *v_start = found + needle_len;
        char *v_end = v_start;
        char *json_end = json + len;

        while (v_end < json_end && *v_end != '"')
        {
            if (*v_end == '\\')
                v_end++;
            v_end++;
        }
        if (v_end >= json_end)
            break;

        size_t v_len = v_end - v_start;
        if (v_len > MAX_MEMADDR_LEN)
        {
            printf("FILTER: MemAddr len=%u > %u, stubbing to 0=0\n",
                   (unsigned)v_len, (unsigned)MAX_MEMADDR_LEN);
            // shift tail left (includes null terminator)
            memmove(v_start + 3, v_end, (json_end - v_end) + 1);
            memcpy(v_start, "0=0", 3);
            len -= (v_len - 3);
            json_end = json + len;
            pos = v_start + 4; // skip past replacement and closing quote
        }
        else
        {
            pos = v_end + 1;
        }
    }
    return len;
}

/**
 * Allocate the NES RAM/SRAM mirrors and their snapshots. Called from the
 * load-game callback before do_frame so read_memory_ingame has buffers to
 * read from. These remain allocated for the lifetime of the program.
 */
static bool allocate_nes_mirror_buffers()
{
    nes_ram = (volatile uint8_t *)calloc(NES_RAM_SIZE, sizeof(uint8_t));
    nes_sram = (volatile uint8_t *)calloc(NES_SRAM_SIZE, sizeof(uint8_t));
    nes_ram_snapshot = (uint8_t *)calloc(NES_RAM_SIZE, sizeof(uint8_t));
    nes_sram_snapshot = (uint8_t *)calloc(NES_SRAM_SIZE, sizeof(uint8_t));
    return nes_ram && nes_sram && nes_ram_snapshot && nes_sram_snapshot;
}

/**
 * Shrink the serial buffer to its runtime size. Called from the main loop
 * AFTER the load-game callback has returned and the current RESP= command has
 * been fully consumed — never from inside the callback, since the
 * http_callback caller still holds a pointer into the old serial buffer.
 *
 * The 100KB serial buffer is freed FIRST so the hole at the bottom of the
 * heap is reused by the smaller allocation that follows. (The bus sniffer
 * needs no buffers: core 1 drains the PIO FIFOs directly, no DMA involved.)
 */
static bool swap_to_runtime_serial_buffer()
{
    free(serial_buffer);
    serial_buffer = NULL;
    serial_buffer_head = NULL;
    serial_buffer_size = 0;

    serial_buffer = (u_char *)malloc(SERIAL_BUFFER_RUNTIME_SIZE);

    if (!serial_buffer)
    {
        printf("FATAL: failed to allocate runtime serial buffer\r\n");
        return false;
    }

    serial_buffer_size = SERIAL_BUFFER_RUNTIME_SIZE;
    serial_buffer_head = serial_buffer;
    memset(serial_buffer, '\0', serial_buffer_size);
    return true;
}

// set by rc_client_load_game_callback; main loop performs the deferred swap
volatile bool pending_runtime_swap = false;

/**
 * RetroAchievements (rcheevos) related functions
 */

// read the memory address we keep track and return the data to rcheevos
static uint32_t read_memory_ingame(uint32_t address, uint8_t *buffer, uint32_t num_bytes, rc_client_t *client)
{
    for (uint32_t i = 0; i < num_bytes; i += 1)
    {
        uint32_t addr = address + i;
        if (addr < 0x2000) {
            buffer[i] = nes_ram_snapshot[addr & 0x07FF];
        } else if (addr >= 0x6000 && addr < 0x8000) {
            buffer[i] = nes_sram_snapshot[addr - 0x6000];
        } else {
            buffer[i] = 0; // Default for unmapped/unsupported regions
        }
    }
    return num_bytes;
}

// callback function for the RetroAchievements login call
static void rc_client_login_callback(int result, const char *error_message, rc_client_t *client, void *callback_userdata)
{
    if (result == RC_OK)
    {
        printf("Login success\n");
        state = 6; // load game
    }
    else
    {
        printf("Login failed\n");
    }
}

// callback function for the RetroAchievements load game call
static void rc_client_load_game_callback(int result, const char *error_message, rc_client_t *client, void *callback_userdata)
{
    if (result == RC_OK)
    {
        state = 8; // read from circular buffer
        if (rc_client_is_game_loaded(g_client))
        {
            printf("Game loaded\n");
            const rc_client_game_t *game = rc_client_get_game_info(g_client);
            char url[256];
            rc_client_game_get_image_url(game, url, sizeof(url));
            char aux[512];

            // send game info to ESP32
            sprintf(aux, "GAME_INFO=%lu;%s;%s\r\n", (unsigned long)game->id, game->title, url);
            printf(aux);
            uart_puts(UART_ID, aux);
        }

        // Patch is fully parsed by rcheevos at this point. Allocate the NES
        // RAM/SRAM mirrors so read_memory_ingame has buffers to read from.
        // The serial buffer swap and core 1 launch are deferred to the main
        // loop because http_callback's caller still holds a pointer into the
        // current serial_buffer.
        if (!allocate_nes_mirror_buffers())
        {
            uart_puts(UART_ID, "FATAL_OOM\r\n");
            while (1) tight_loop_contents();
        }

        // Use the ingame memory reader directly since we have a full RAM mirror
        rc_client_set_read_memory_function(g_client, read_memory_ingame);
        rc_client_do_frame(g_client); // to trigger initial state evaluation

        // send achievement summary to ESP32 (after do_frame so unlocks are processed)
        if (rc_client_is_game_loaded(g_client))
        {
            rc_client_user_game_summary_t summary;
            rc_client_get_user_game_summary(g_client, &summary);
            char aux[64];
            sprintf(aux, "ACH_SUMMARY=%u;%u\r\n", summary.num_unlocked_achievements, summary.num_core_achievements);
            printf(aux);
            uart_puts(UART_ID, aux);
        }

        // Defer serial buffer shrink + core 1 launch to the main loop. We
        // cannot free serial_buffer here because the caller of http_callback
        // (above us on the stack) is parsing a command located inside it and
        // still needs the post-command cleanup at the same offset.
        pending_runtime_swap = true;
    }
    else
    {
        printf("Game not loaded\n");
        char aux[512];

        // send game info to ESP32 when we coudn't load the game
        sprintf(aux, "GAME_INFO=%lu;%s;%s\r\n", (unsigned long)0, "No Title", "No URL");
        printf(aux);
        uart_puts(UART_ID, aux);
    }
}

// enqueue the achievements the user won to be sent to the ESP32
static void achievement_triggered(const rc_client_achievement_t *achievement)
{
    achievement_t achievement_data;
    achievement_data.id = achievement->id;
    achievement_data.event = RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED;
    fifo_enqueue(&achievements_fifo, achievement_data);
}

// send the achievements status to the ESP32
static void achievement_status(const rc_client_event_t *event)
{
    achievement_t achievement_data;
    achievement_data.id = 0;
    achievement_data.event = event->type;
    if (event->achievement)
    {
        achievement_data.id = event->achievement->id;
        if (event->achievement->measured_progress)
        {
            strcpy(achievement_data.measured_progress, event->achievement->measured_progress);
        }
        else
        {
            strcpy(achievement_data.measured_progress, "0");
        }
    }

    fifo_enqueue(&achievements_fifo, achievement_data);
}

// rcheevos event handler - used to enqueue the achievements the user won to be sent to the ESP32
static void event_handler(const rc_client_event_t *event, rc_client_t *client)
{
    switch (event->type)
    {
    case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
        achievement_triggered(event->achievement);
        break;
#ifdef ENABLE_INTERNAL_WEB_APP_SUPPORT
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_SHOW:
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_HIDE:
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_UPDATE:
    case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW:
    case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_HIDE:
        achievement_status(event);
        break;
#endif
    default:
        // printf("Unhandled event %d\n", event->type); //debug
        break;
    }
}

// callback function for the asynchronous HTTP call
static void http_callback(int status_code, const char *content, size_t content_size, void *userdata, const char *error_message)
{
    // Prepare a data object to pass the HTTP response to the callback
    rc_api_server_response_t server_response;
    memset(&server_response, 0, sizeof(server_response));
    server_response.body = content;
    server_response.body_length = content_size;
    server_response.http_status_code = status_code;

    // handle non-http errors (socket timeout, no internet available, etc)
    if (status_code == 0 && error_message)
    {
        // assume no server content and pass the error through instead
        server_response.body = error_message;
        server_response.body_length = strlen(error_message);
        // Let rc_client know the error was not catastrophic and could be retried. It may decide to retry or just
        // immediately pass the error to the callback. To prevent possible retries, use RC_API_SERVER_RESPONSE_CLIENT_ERROR.
        server_response.http_status_code = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
    }

    // Get the rc_client callback and call it
    async_callback_data *async_data = (async_callback_data *)userdata;
    async_data->callback(&server_response, async_data->callback_data);
}

// get the current time in milliseconds
rc_clock_t get_pico_millisecs(const rc_client_t *client)
{
    return to_ms_since_boot(get_absolute_time());
}

// This is the HTTP request dispatcher that is provided to the rc_client. Whenever the client
// needs to talk to the server, it will call this function.
static void server_call(const rc_api_request_t *request,
                        rc_client_server_callback_t callback, void *callback_data, rc_client_t *client)
{
    char buffer[512];
    async_data.callback = callback;
    async_data.callback_data = callback_data;
    char method[8];
    if (request->post_data)
    {
        strcpy(method, "POST");
    }
    else
    {
        strcpy(method, "GET");
    }
    sprintf(buffer, "REQ=%02hhX;M:%s;U:%s;D:%s\r\n", request_id, method, request->url, request->post_data);
    async_handlers[async_handlers_index].id = request_id;
    async_handlers[async_handlers_index].async_data.callback = callback;
    async_handlers[async_handlers_index].async_data.callback_data = callback_data;
    async_handlers_index = (async_handlers_index + 1) % MAX_ASYNC_CALLBACKS;
    request_id += 1;
    printf("REQ=%s\n", request->post_data); // DEBUG
    request_ongoing += 1;
    last_request = to_ms_since_boot(get_absolute_time());

    // send request to ESP32
    uart_puts(UART_ID, buffer);
}

// rcheevos log message handler
static void log_message(const char *message, const rc_client_t *client)
{
    printf("%s\n", message);
}

// Initialize the RetroAchievements client
rc_client_t *initialize_retroachievements_client(rc_client_t *g_client, rc_client_read_memory_func_t read_memory, rc_client_server_call_t server_call)
{
    // Create the client instance (using a global variable simplifies this example)
    g_client = rc_client_create(read_memory, server_call);

    // Provide a logging function to simplify debugging
    rc_client_enable_logging(g_client, RC_CLIENT_LOG_LEVEL_VERBOSE, log_message);

    // Enable hardcore mode - we are on a real NES console and using a real cartridge and not a everdrive or game genie
    rc_client_set_hardcore_enabled(g_client, 1);
    return g_client;
}

void shutdown_retroachievements_client(rc_client_t *g_client)
{
    if (g_client)
    {
        // Release resources associated to the client instance
        rc_client_destroy(g_client);
        g_client = NULL;
    }
}

void save_energy()
{
    set_sys_clock_khz(48000, true);
    for (int pin = 26; pin <= 27; pin += 1)
    {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_down(pin); // Evita flutuação
    }
    spi_deinit(spi0);
    spi_deinit(spi1);
    i2c_deinit(i2c0);
    i2c_deinit(i2c1);
    adc_run(false);
}

// main function - entry point
int main()
{

    stdio_init_all();
    save_energy();
    reset_GPIO();

    // allocate the serial buffer FIRST so the 100KB block lands at the bottom of
    // the heap. After the patch response is parsed it's freed and the cleared
    // hole is reused for the smaller runtime buffers (see rc_client_load_game_callback).
    serial_buffer = (u_char *)malloc(SERIAL_BUFFER_INITIAL_SIZE);
    if (serial_buffer == NULL)
    {
        printf("FATAL: failed to allocate %u bytes for serial buffer\r\n", SERIAL_BUFFER_INITIAL_SIZE);
        while (1) tight_loop_contents();
    }
    serial_buffer_size = SERIAL_BUFFER_INITIAL_SIZE;
    serial_buffer_head = serial_buffer;
    memset(serial_buffer, '\0', serial_buffer_size);

    uart_init(UART_ID, BAUD_RATE);

    // config GPIO pins for UART
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    // enables UART
    uart_set_hw_flow(UART_ID, true, true);
    uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(UART_ID, true);

    const char nes_pico_firmaware_version[] = "PICO_FIRMWARE_VERSION=%s\r\n";
    printf(nes_pico_firmaware_version, FIRMWARE_VERSION);

    // Notify ESP32 that Pico is ready for communication
    uart_puts(UART_ID, "PICO_READY\r\n");

    // debug info
    unsigned int frame_counter = 0;
    uint8_t last_frame_detection_strategy = 0; // 0 for bus event (NMI vector / $4014), 1 for timer based

    // main loop for core 0 - handle UART communication and rcheevos processing
    while (true)
    {
        // handle on going requests and timeout - wait 30 seconds for a response before send a new achievement request
        // necessary to send multiples achievements got in a row
        if (request_ongoing > 0)
        {
            uint32_t current_time = to_ms_since_boot(get_absolute_time());
            if (current_time - last_request > 30000)
            {
                printf("request timeout\n");
                request_ongoing = 0;
            }
        }

        // if there is no request on the fly and there is an achievement to be sent, go for it
        if (request_ongoing == 0 && fifo_is_empty(&achievements_fifo) == false)
        {
            achievement_t achievement_data;
            uint32_t achievement_id;
            fifo_dequeue(&achievements_fifo, &achievement_data);
            achievement_id = achievement_data.id;
            if (achievement_data.event == RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED)
            {
                // send the achievement to the ESP32
                const rc_client_achievement_t *achievement = rc_client_get_achievement_info(g_client, achievement_id);
                char url[128];
                const char *title = achievement->title;
                rc_client_achievement_get_image_url(achievement, RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED, url, sizeof(url));
                char aux[512];
                memset(aux, 0, 512);
                sprintf(aux, "A=%lu;%s;%s\r\n", (unsigned long)achievement_id, title, url);
                uart_puts(UART_ID, aux);
                printf(aux);
            }
            else if (achievement_data.event == RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_SHOW || achievement_data.event == RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW || achievement_data.event == RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_UPDATE)
            {
                char aux[256];
                char url[128];
                char command[3];
                if (achievement_data.event == RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW)
                {
                    sprintf(command, "C=");
                }
                else
                {
                    sprintf(command, "P=");
                }
                const rc_client_achievement_t *achievement = rc_client_get_achievement_info(g_client, achievement_id);

                rc_client_achievement_get_image_url(achievement, achievement_data.event, url, sizeof(url));
                sprintf(aux, "%sS;%u;%s;%s;%s\r\n", command, (unsigned int)achievement->id, achievement->title, url, achievement->measured_progress);
                printf(aux);
                uart_puts(UART_ID, aux);
            }
            else if (achievement_data.event == RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_HIDE)
            {
                char aux[128];
                sprintf(aux, "P=H;%u\r\n", achievement_id);
                printf(aux);
                uart_puts(UART_ID, aux);
            }
            else if (achievement_data.event == RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_HIDE)
            {
                char aux[128];
                sprintf(aux, "C=H;%u\r\n", achievement_id);
                printf(aux);
                uart_puts(UART_ID, aux);
            }
        }

        if (state == 1)
        {
            // calculate CRCs to identify the cartridge
            calculate_CRC32_to_idenfity_cartridge();
            state = 2;
        }
        if (state == 6)
        {
            // load the game
            rc_client_begin_load_game(g_client, md5, rc_client_load_game_callback, g_callback_userdata);
            state = 7;
        }
        if (state == 8)
        {
            u_int64_t now = time_us_64();                // get current time in microseconds
            u_int64_t diff = now - last_frame_processed; 

            // we started writing on memory ram - so we can assume user reseted the NES and the game is being played
            if (nes_reseted == 0 && flag_internal_ram_written)
            {
                nes_reseted = 1;
                uart_puts(UART_ID, "NES_RESETED\r\n");
            }

            // console reset detected (RESET vector fetch on the bus): reset the
            // rcheevos runtime state (hit counts, primed indicators), the same
            // way emulators do when the user resets the machine
            if (flag_reset_detected)
            {
                flag_reset_detected = false;
                if (nes_reseted != 0) // ignore the boot reset that starts the game
                {
                    printf("RESET vector fetch - rc_client_reset\n");
                    rc_client_reset(g_client);
                }
            }

            // vblank detected by core 1 (NMI vector fetch, or $4014 fallback)
            if (flag_vblank_detected)
            {
                flag_vblank_detected = false;
                // best place to detect a frame so far
                u_int64_t delta = 8000; // ~ half of the frame time in microseconds for 60hz
                if (last_frame_detection_strategy == 0) {
                    delta = 2500; // ~ 15% of the frame time in microseconds for 60hz - we want to be more strict when we detect frames using the NMI/OAMDMA bus monitoring, to avoid false positives
                }
                if (diff > (FRAME_TIME_US - delta))  
                { // inside a frame window, so process the frame
                    now = time_us_64();
                    last_frame_processed = now;                    
                    rc_client_do_frame(g_client);
                    diff = 0;
                    last_frame_detection_strategy = 0; 
                    frame_counter += 1;
                    if (frame_counter % 1800 == 0) //~ 30 seconds in 60hz
                    {
                        printf("F: %d\n", frame_counter);
                    }
                }
            }

            // simulate a frame every ~16667us (for 60hz) if we cannot detect any frame on the bus
            // (NMI disabled and no OAM DMA, e.g. during forced-blank loading screens)
            u_int64_t window = FRAME_TIME_US << 1; // two frames time window when coming from the bus event strategy
            if (last_frame_detection_strategy == 1) {
                window = FRAME_TIME_US; 
            }
            if (diff > window) // a frame takes 16666 microsecond in 60hz and 20000 microseconds in 50hz
            {
                
                now = time_us_64();                    
                last_frame_processed = now;
                memcpy(nes_ram_snapshot, (void*)nes_ram, NES_RAM_SIZE);
                memcpy(nes_sram_snapshot, (void*)nes_sram, NES_SRAM_SIZE);
                rc_client_do_frame(g_client);
                // printf("DF1=%llu, ", diff);
                last_frame_detection_strategy = 1;
#ifdef ENABLE_VBLANK_INSTRUMENTATION
                vbstat_timer_frames += 1;
#endif
                diff = 0;
                //memory dump during a frame for DEBUG
                // for (int i = 0; i < unique_memory_addresses_count; i += 1)
                // {
                //     if (unique_memory_addresses[i] == 0x0017)
                //     {
                //         printf("DF1=%03X ", memory_data[i]); //detect frame heuristic number 2 - time based
                // }
                // printf ("\n");
            }

#ifdef ENABLE_VBLANK_INSTRUMENTATION
            // report vblank detection statistics every 10s (USB serial only)
            {
                static uint32_t vbstat_last_report_ms = 0;
                static uint32_t vbstat_prev_periods = 0;
                static uint32_t vbstat_prev_sum = 0;
                uint32_t now_ms = to_ms_since_boot(get_absolute_time());
                if (now_ms - vbstat_last_report_ms >= 10000)
                {
                    vbstat_last_report_ms = now_ms;
                    uint32_t periods = vbstat_nmi_periods;
                    uint32_t sum = vbstat_period_sum_us;
                    uint32_t d_periods = periods - vbstat_prev_periods;
                    uint32_t d_sum = sum - vbstat_prev_sum;
                    vbstat_prev_periods = periods;
                    vbstat_prev_sum = sum;
                    uint32_t avg = (d_periods > 0) ? (d_sum / d_periods) : 0;
                    uint32_t mn = (vbstat_period_min_us == 0xFFFFFFFF) ? 0 : vbstat_period_min_us;
                    printf("VBSTAT: nmi_periods=%lu avg=%luus min=%luus max=%luus gaps=%lu spurious=%lu | oam=%lu in_vb=%lu lat_max=%luus | timer_frames=%lu | resets=%lu | stage_peak=%lu ovf=%lu\n",
                           (unsigned long)d_periods, (unsigned long)avg,
                           (unsigned long)mn, (unsigned long)vbstat_period_max_us,
                           (unsigned long)vbstat_gaps, (unsigned long)vbstat_spurious,
                           (unsigned long)vbstat_oam_writes, (unsigned long)vbstat_oam_in_vblank,
                           (unsigned long)vbstat_oam_lat_max_us, (unsigned long)vbstat_timer_frames,
                           (unsigned long)vbstat_resets, (unsigned long)vbstat_stage_peak,
                           (unsigned long)vbstat_stage_overflows);
                    // reset the windowed min/max (benign race with core 1)
                    vbstat_period_min_us = 0xFFFFFFFF;
                    vbstat_period_max_us = 0;
                    vbstat_oam_lat_max_us = 0;
                }
            }
#endif
        }
        // handle UART communication, byte by byte
        if (uart_is_readable(UART_ID))
        {

            char received_char = uart_getc(UART_ID);
            serial_buffer_head[0] = received_char;
            serial_buffer_head += 1;
            // if a command is too big, we clear the buffer
            if ((uint32_t)(serial_buffer_head - serial_buffer) == serial_buffer_size)
            {
                memset(serial_buffer, 0, serial_buffer_size);
                serial_buffer_head = serial_buffer;
                printf("BUFFER_OVERFLOW\r\n");
                continue;
            }
            char *pos = NULL;
            char *current_char = serial_buffer_head - 1;
            // try detect \r\n
            if (current_char[0] == '\n')
            {
                char *previous_char = current_char - 1;
                if (previous_char[0] == '\r')
                {
                    pos = previous_char;
                }
            }

            // if we detect \r\n, we may have a command
            if (pos != NULL)
            {
                // if the command is empty, we clear the buffer and continue
                if (((unsigned char *)pos) - serial_buffer == 0)
                {
                    memset(serial_buffer, 0, 2); // Clear the buffer since we are reading char by char
                    serial_buffer_head = serial_buffer;
                    continue;
                }

                int len = serial_buffer_head - serial_buffer;
                serial_buffer_head[0] = '\0';
                char *command;
                command = serial_buffer;

                // printf("CMD=%s\r\n", command);

                // handle possible commands
                if (prefix("RESP=", command))
                {
                    // handle a http response from the ESP32
                    printf("L:RESP\n");
                    request_ongoing -= 1;
                    char *response_ptr = command + 5;
                    char aux[8];
                    strncpy(aux, response_ptr, 2);
                    aux[2] = '\0';
                    uint8_t request_id = (uint8_t)strtol(aux, NULL, 16);
                    response_ptr += 3;
                    strncpy(aux, response_ptr, 3);
                    aux[3] = '\0';
                    response_ptr += 4;
                    uint16_t http_code = (uint16_t)strtol(aux, NULL, 16);
                    for (int i = 0; i < MAX_ASYNC_CALLBACKS; i += 1)
                    {
                        if (async_handlers[i].id == request_id)
                        {
                            // if (request_id == 2) // debug to print the response
                            // {
                            //     printf("RESP=%s\n", response_ptr);
                            // }
                            async_callback_data async_data = async_handlers[i].async_data;
                            size_t body_len = strlen(response_ptr);

                            // Strip any MemAddr values that would OOM rcheevos parse (e.g. FF1).
                            // Must run before the shrink so the tight buffer is correctly sized.
                            // if (serial_buffer_size > SERIAL_BUFFER_RUNTIME_SIZE)
                            //     body_len = filter_large_memaddr(response_ptr, body_len);

                            // Large response (likely the achievement patch — FF1 hits ~60KB)
                            // while the serial buffer is still at the initial 100KB+. Free the
                            // oversized buffer and malloc a tight one BEFORE invoking rcheevos.
                            // realloc(shrink) in newlib-nano does NOT return the unused tail to
                            // the heap, so we must malloc+memcpy+free to guarantee reclamation.
                            if (body_len > 8192 && serial_buffer_size > SERIAL_BUFFER_RUNTIME_SIZE)
                            {
                                struct mallinfo mi_before = mallinfo();
                                printf("HEAP before shrink: used=%d free=%d\n",
                                       mi_before.uordblks, mi_before.fordblks);

                                u_char *tight = (u_char *)malloc(body_len + 1);
                                if (tight != NULL)
                                {
                                    memcpy(tight, response_ptr, body_len + 1);
                                    free(serial_buffer); // guarantees 100KB returned to heap
                                    serial_buffer = tight;
                                    serial_buffer_size = body_len + 1;
                                    response_ptr = (char *)serial_buffer;
                                    serial_buffer_head = serial_buffer;
                                    len = 0; // skip the post-command memset below

                                    struct mallinfo mi_after = mallinfo();
                                    printf("HEAP after shrink:  used=%d free=%d\n",
                                           mi_after.uordblks, mi_after.fordblks);
                                }
                                else
                                {
                                    printf("HEAP shrink malloc failed, body_len=%u\n",
                                           (unsigned)body_len);
                                }
                            }

                            struct mallinfo mi_pre = mallinfo();
                            printf("HEAP before http_callback: used=%d free=%d\n",
                                   mi_pre.uordblks, mi_pre.fordblks);
                            http_callback(http_code, response_ptr, body_len, &async_data, NULL);
                            struct mallinfo mi_post = mallinfo();
                            printf("HEAP after  http_callback: used=%d free=%d\n",
                                   mi_post.uordblks, mi_post.fordblks);
                            break;
                        }
                    }
                }
                else if (prefix("TOKEN_AND_USER", command))
                {
                    // handle the token and user sent by the ESP32
                    // example TOKEN_AND_USER=odelot,token
                    printf("L:TOKEN_AND_USER\n");
                    char *token_ptr = command + 15;
                    int comma_index = 0;
                    len = strlen(token_ptr);
                    for (int i = 0; i < len; i += 1)
                    {
                        if (token_ptr[i] == ',')
                        {
                            comma_index = i;
                            break;
                        }
                    }
                    memset(ra_token, '\0', 32);
                    memset(ra_user, '\0', 256);
                    strncpy(ra_token, token_ptr, comma_index);
                    strncpy(ra_user, token_ptr + comma_index + 1, len - comma_index - 1 - 2);
                    printf("USER=%s\r\n", ra_user);
                    printf("TOKEN=%s\r\n", ra_token);
                }
                else if (prefix("CRC_FOUND_MD5", command))
                {
                    // handle the MD5 found by the ESP32 using the CRC we sent
                    // we will use it to identify the game in rcheevos
                    printf("L:CRC_FOUND_MD5\n");
                    char *md5_ptr = command + 14;
                    strncpy(md5, md5_ptr, 32);
                    md5[32] = '\0';
                    printf("MD5=%s\r\n", md5);
                }
                else if (prefix("RESET", command)) // RESET
                {
                    // handle a reset command from ESP32 - reinit all states and clear memory
                    printf("L:RESET\r\n");
                    // force pico reset - need to wait a while in the esp32
                    watchdog_reboot(0, 0, 0); // TODO: maybe let esp32 know PICO restarted
                }
                else if (prefix("SYNC", command)) // SYNC - handshake with ESP32
                {
                    printf("L:SYNC\r\n");
                    uart_puts(UART_ID, "SYNC_ACK\r\n");
                }
                else if (prefix("READ_CRC", command))
                {
                    printf("L:READ_CRC\n");
                    state = 1;
                    printf("STATE=%d\r\n", state);
                }
                else if (prefix("START_WATCH", command))
                {
                    // start watch the bus for memory writes
                    printf("L:START_WATCH\n");

                    // init rcheevos
                    g_client = initialize_retroachievements_client(g_client, read_memory_ingame, server_call);
                    rc_client_get_user_agent_clause(g_client, rcheevos_userdata, sizeof(rcheevos_userdata)); // TODO: send to esp32 before doing requests
                    printf("USER_AGENT=%s\r\n", rcheevos_userdata);
                    rc_client_set_event_handler(g_client, event_handler);
                    rc_client_set_get_time_millisecs_function(g_client, get_pico_millisecs);
                    rc_client_begin_login_with_token(g_client, ra_user, ra_token, rc_client_login_callback, g_callback_userdata);
                    state = 5;
                }
                memset(serial_buffer, 0, len); // Clear the buffer since we are reading char by char
                serial_buffer_head = serial_buffer;

                // The load-game callback (triggered above via http_callback) sets
                // this flag to request the serial buffer shrink + core 1 launch.
                // We do it here, after the current command has been fully
                // consumed and the cleanup above used the old (large) buffer safely.
                if (pending_runtime_swap)
                {
                    pending_runtime_swap = false;
                    if (!swap_to_runtime_serial_buffer())
                    {
                        uart_puts(UART_ID, "FATAL_OOM\r\n");
                        while (1) tight_loop_contents();
                    }
                    multicore_launch_core1(handle_bus_to_detect_memory_writes);
                }
            }
        }
    }
}