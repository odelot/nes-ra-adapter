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
    
    * Core 1 monitors the bus for specific memory addresses and sends relevant data to 
      Core 0. We intercept the bus using PIO and apply a heuristic to detect stable values 
      during write operations. To ensure data integrity, we use DMA to transfer the PIO data
      into a ping-pong buffer, which is then processed by Core 1. When a memory address of 
      interest is identified, the data is forwarded to Core 0.

    Inter-core communication is managed via a circular buffer. Please note that the available 
    space for serial communication is limited to around 32KB, which restricts the size of 
    the achievement list response.

   Date:             2025-03-29
   Version:          0.1
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
#include "hardware/dma.h"
#include "hardware/structs/systick.h"
#include "hardware/timer.h"

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

// run at 200mhz can save energy and need to be tested if it is stable - it saves ~0.010A
// #define RUN_AT_200MHZ

#define BUS_PIO pio0
#define BUS_SM 0

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

volatile io_ro_32 *rxf;
mutex_t cpu_bus_mutex;

/*
 * Serial buffer to handle commands from the ESP32
 */

#define SERIAL_BUFFER_SIZE 32768
u_char serial_buffer[SERIAL_BUFFER_SIZE];
u_char *serial_buffer_head = serial_buffer;

/*
 * Circular Buffer for memory writes detected on the BUS
 * Used to send data from Core 1 to Core 0
 */

#define MEMORY_BUFFER_SIZE 4096
volatile int memory_head = 0;
volatile int memory_tail = 0;

struct _memory_unit
{
    uint32_t address;
    uint8_t data;
};

typedef struct _memory_unit memory_unit;
memory_unit memory_buffer[MEMORY_BUFFER_SIZE];

uint16_t unique_memory_addresses_count = 0;
uint16_t *unique_memory_addresses = NULL;
uint8_t *memory_data = NULL;

/*
 * global variables for using DMA to read the BUS
 * we use two buffers to ping-pong between them
 * when one is being feed, the other is being read
 */

#define BUFFER_SIZE 4096 // Tamanho de cada buffer

volatile uint32_t buffer_a[BUFFER_SIZE];
volatile uint32_t buffer_b[BUFFER_SIZE];

// flags to control the buffers
volatile bool read_A;
volatile bool reading_A;
volatile bool reading_B;

// DMA channels
int dma_chan_0, dma_chan_1;

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

#define FIFO_SIZE 5

typedef struct
{
    uint32_t buffer[FIFO_SIZE];
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

/**
 * DMA functions
 */

// DMA interruption handler, not in memory to speed it up
void __not_in_flash_func(dma_handler)()
{

    // Did channel0 triggered the irq?
    if (dma_channel_get_irq0_status(dma_chan_0))
    {
        // Clear the irq
        dma_channel_acknowledge_irq0(dma_chan_0);
        // Rewrite the write address without triggering the channel
        dma_channel_set_write_addr(dma_chan_0, buffer_a, false);
        if (reading_B)
        {
            // we will start using buffer_b while it is still being read <o>
            printf("m_"); // m de merda // shit // we should avoid this to happen
        }
    }
    else
    {
        // Clear the irq
        dma_channel_acknowledge_irq0(dma_chan_1);
        // Rewrite the write address without triggering the channel
        dma_channel_set_write_addr(dma_chan_1, buffer_b, false);        
        if (reading_A)
        {
            printf("m_"); // m de merda // shit // we should avoid this to happen
        }
    }
}

// setup both dma channels
void setup_dma()
{
    memset((void *)buffer_a, 0, BUFFER_SIZE * sizeof(uint32_t));
    memset((void *)buffer_b, 0, BUFFER_SIZE * sizeof(uint32_t));

    dma_chan_0 = dma_claim_unused_channel(true);
    dma_chan_1 = dma_claim_unused_channel(true);

    // channel 0 config
    dma_channel_config c0 = dma_channel_get_default_config(dma_chan_0);
    channel_config_set_transfer_data_size(&c0, DMA_SIZE_32);
    channel_config_set_read_increment(&c0, false);
    channel_config_set_write_increment(&c0, true);
    channel_config_set_dreq(&c0, pio_get_dreq(BUS_PIO, BUS_SM, false));
    channel_config_set_chain_to(&c0, dma_chan_1); // after full, active channel 1
    channel_config_set_high_priority(&c0, true);
    channel_config_set_enable(&c0, true);

    dma_channel_set_irq0_enabled(dma_chan_0, true); // Enable IRQ 0

    // channel 1 config
    dma_channel_config c1 = dma_channel_get_default_config(dma_chan_1);
    channel_config_set_transfer_data_size(&c1, DMA_SIZE_32);
    channel_config_set_read_increment(&c1, false);
    channel_config_set_write_increment(&c1, true);
    channel_config_set_dreq(&c1, pio_get_dreq(BUS_PIO, BUS_SM, false));
    channel_config_set_chain_to(&c1, dma_chan_0); // after full, active channel 0
    channel_config_set_high_priority(&c1, true);
    channel_config_set_enable(&c1, true);

    dma_channel_set_irq0_enabled(dma_chan_1, true); // Enable IRQ 0

    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
    irq_set_enabled(DMA_IRQ_0, true);
    irq_set_priority(DMA_IRQ_0, 0);

    dma_channel_configure(
        dma_chan_1, &c1,
        buffer_b,              // Target
        &BUS_PIO->rxf[BUS_SM], // Source: FIFO from PIO
        BUFFER_SIZE,           // Transfer size
        false);

    dma_channel_configure(
        dma_chan_0, &c0,
        buffer_a,              // Target
        &BUS_PIO->rxf[BUS_SM], // Source: FIFO from PIO
        BUFFER_SIZE,           // Transfer size
        true);
}

/*
 * PIO functions
 */

// initialize the PIO program
void setupPIO()
{
    for (int i = 0; i < 26; i++) // reset all GPIOs connected to NES
        gpio_init(i);
    uint offset = pio_add_program(BUS_PIO, &memoryBus_program);
#ifdef RUN_AT_200MHZ
    memoryBus_program_init(BUS_PIO, BUS_SM, offset, (float)6.0f); // div = 6 for 200mhz
#else
    memoryBus_program_init(BUS_PIO, BUS_SM, offset, (float)9.0f); // div = 9 for 250mhz
#endif
}

// returns the number of elements in the circular buffer
unsigned int memory_buffer_size()
{
    return (memory_head - memory_tail + MEMORY_BUFFER_SIZE) % MEMORY_BUFFER_SIZE;
}

// add a memory write to the circular buffer
void add_to_memory_buffer(uint32_t address, uint8_t data)
{
    memory_buffer[memory_head].address = address;
    memory_buffer[memory_head].data = data;
    memory_head = (memory_head + 1) % MEMORY_BUFFER_SIZE;
    if (memory_head == memory_tail)
    {
        printf("Buffer full\n"); // this also should not happen
        // Buffer full, discard or replace oldest data
        memory_tail = (memory_tail + 1) % MEMORY_BUFFER_SIZE;
    }
}

// read a captured memory write from the circular buffer
memory_unit read_from_memory_buffer()
{
    if (memory_head == memory_tail)
    {
        // empty buffer
        memory_unit empty;
        empty.address = 0;
        empty.data = 0;
        return empty;
    }
    memory_unit data = memory_buffer[memory_tail];
    memory_tail = (memory_tail + 1) % MEMORY_BUFFER_SIZE;
    return data;
}

// print DMA buffer for debug (index and neighbors)
void print_buffer(uint32_t *buffer, int index)
{
    int min = index - 7;
    int max = index + 1;
    if (min < 0)
    {
        min = 0;
    }
    if (max >= BUFFER_SIZE)
    {
        max = BUFFER_SIZE - 1;
    }
    for (int i = min; i <= max; i += 1)
    {
        printf("%p\n", buffer[i]);
    }
    printf("\n");
}

// check if the address is in the list of unique addresses and if affirmative,
// add to the circular buffer
inline void try_add_to_circular_buffer(uint16_t address, uint8_t data)
{
    // sequencial search
    // for (int j = 0; j < unique_memory_addresses_count; j += 1)
    // {
    //     if (unique_memory_addresses[j] == address)
    //     {
    //         add_to_memory_buffer(address, data);
    //         return;
    //     }
    // }

    // tests on the raspberry pico showed binary search to be faster than sequential search when
    // the vector is larger than about 7 elements - we typically monitor more than 7 memory addresses per game

    // binary search
    int bot = 0;
    int top = unique_memory_addresses_count - 1;
    while (bot < top)
    {
        int mid = top - (top - bot) / 2;

        if (address < unique_memory_addresses[mid])
        {
            top = mid - 1;
        }
        else
        {
            bot = mid;
        }
    }
    if (unique_memory_addresses[top] == address)
    {
        add_to_memory_buffer(address, data);
    }
}

// handle detection of memory writes in the NES BUS, using DMA and PIO
void handle_bus_to_detect_memory_writes()
{
    mutex_init(&cpu_bus_mutex);
    mutex_enter_blocking(&cpu_bus_mutex); // make sure core 1 is fully dedicated to handle the BUS

    setupPIO();
    setup_dma();

    // enabble PIO
    pio_sm_set_enabled(BUS_PIO, BUS_SM, true);

    uint32_t raw_bus_data;
    uint16_t address_value = 0;
    uint16_t last_address_value = 0;
    uint8_t data_value = 0;
    uint8_t last_data_value = 0;
    uint8_t rw = 0;
    uint8_t last_rw = 0;
    read_A = true;
    reading_A = false;
    reading_B = false;

    // handle DMA ping-pong and process the buffer that is not being feed
    while (1)
    {
        if (!dma_channel_is_busy(dma_chan_0) && read_A)
        {
            // get begin timestamp
            // uint32_t begin = time_us_32();

            reading_A = true;
            read_A = false;
            for (int i = 0; i < BUFFER_SIZE; i += 1)
            {

                raw_bus_data = buffer_a[i];
                address_value = (raw_bus_data >> 8) & 0x7FFF;
                data_value = raw_bus_data;
                rw = (raw_bus_data >> 25) & 0x1;

                // detect a stable value that was being written and try to add to the circular buffer
                if (address_value != last_address_value && last_rw == 0)
                {
                    try_add_to_circular_buffer(last_address_value, last_data_value);
                }
                last_address_value = address_value;
                last_data_value = data_value;
                last_rw = rw;
            }
            reading_A = false;

            // get end timestamp
            // uint32_t end = time_us_32();
            // printf("T: %d\n", end - begin);
        }
        else if (!dma_channel_is_busy(dma_chan_1) && !read_A)
        {
            // get begin timestamp
            // uint32_t begin = time_us_32();

            reading_B = true;
            read_A = true;
            for (int i = 0; i < BUFFER_SIZE; i += 1)
            {
                raw_bus_data = buffer_b[i];
                address_value = (raw_bus_data >> 8) & 0x7FFF;
                data_value = raw_bus_data;
                rw = (raw_bus_data >> 25) & 0x1;
                // detect a stable value that was being written and try to add to the circular buffer
                if (address_value != last_address_value && last_rw == 0)
                {
                    try_add_to_circular_buffer(last_address_value, last_data_value);
                }
                last_address_value = address_value;
                last_data_value = data_value;
                last_rw = rw;
            }
            reading_B = false;

            // get end timestamp
            // uint32_t end = time_us_32();
            // printf("T: %d\n", end - begin);
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

        for (int c = 0; c < 512; c++)
        {
            uint32_t byte_from_first_bank = read_NES_PRG_ROM_Address(0x8000 + c);
            uint32_t byte_from_last_bank = read_NES_PRG_ROM_Address(0xE000 + c);
            crc_begin = update_crc32(byte_from_first_bank, crc_begin);
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

bool fifo_enqueue(FIFO_t *fifo, uint32_t value)
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

bool fifo_dequeue(FIFO_t *fifo, uint32_t *value)
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

void fifo_print(FIFO_t *fifo)
{
    printf("FIFO: ");
    int index = fifo->head;
    for (int i = 0; i < fifo->count; i++)
    {
        printf("%u ", fifo->buffer[index]);
        index = (index + 1) % FIFO_SIZE;
    }
    printf("\n");
}

/**
 * RetroAchievements (rcheevos) related functions
 */

// not all address are validated, so it is better not use it
static uint32_t read_memory_do_nothing(uint32_t address, uint8_t *buffer, uint32_t num_bytes, rc_client_t *client)
{
    return num_bytes;
}

// add the OAMDMA address to the list of monitored addresses
static uint32_t add_oamdma_address()
{
    uint8_t found = 0;
    uint16_t address = 0x4014;
    for (int i = 0; i < unique_memory_addresses_count; i += 1)
    {
        if (address == unique_memory_addresses[i])
        {
            found = 1;
            break;
        }
    }
    if (found == 0)
    {
        printf("add oamdma address %p\n", address);
        unique_memory_addresses_count += 1;
        unique_memory_addresses = (uint16_t *)realloc(unique_memory_addresses, unique_memory_addresses_count * sizeof(uint16_t));
        unique_memory_addresses[unique_memory_addresses_count - 1] = address;
    }
}

// capture the memory address of interest and add to the list of monitored addresses
static uint32_t read_memory_init(uint32_t address, uint8_t *buffer, uint32_t num_bytes, rc_client_t *client)
{
    // handle address mirror
    if (address <= 0x1FFF)
    {
        address &= 0x07FF;
    }

    for (int j = 0; j < num_bytes; j += 1)
    {
        address += j;
        uint8_t found = 0;
        for (int i = 0; i < unique_memory_addresses_count; i += 1)
        {
            if (address == unique_memory_addresses[i])
            {
                found = 1;
                break;
            }
        }
        if (found == 0)
        {
            printf("init address %p, num_bytes: %d\n", address, num_bytes);
            unique_memory_addresses_count += 1;
            unique_memory_addresses = (uint16_t *)realloc(unique_memory_addresses, unique_memory_addresses_count * sizeof(uint16_t));
            unique_memory_addresses[unique_memory_addresses_count - 1] = address;
        }
        else
        {
            printf("init address %p, num_bytes: %d (already monitored)\n", address, num_bytes);
        }
        buffer[j] = 0;
    }
    return num_bytes;
}

// read the memory address we keep track and return the data to rcheevos
static uint32_t read_memory_ingame(uint32_t address, uint8_t *buffer, uint32_t num_bytes, rc_client_t *client)
{
    // handle address mirror
    if (address <= 0x1FFF)
    {
        address &= 0x07FF;
    }
    for (int i = 0; i < unique_memory_addresses_count; i += 1)
    {
        if (address == unique_memory_addresses[i])
        {
            for (int j = 0; j < num_bytes; j += 1)
            {
                buffer[j] = memory_data[i + j];
            }
            break;
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
        rc_client_set_read_memory_function(g_client, read_memory_init);
        rc_client_do_frame(g_client); // to trigger the read_memory_init and capture the memory address of interest

        // add OAM DMA address to the list of monitored addresses - useful to detect frames
        add_oamdma_address();

        // bubble sort unique_memory_addresses
        for (int i = 0; i < unique_memory_addresses_count; i += 1)
        {
            for (int j = 0; j < unique_memory_addresses_count - i - 1; j += 1)
            {
                if (unique_memory_addresses[j] > unique_memory_addresses[j + 1])
                {
                    uint16_t temp = unique_memory_addresses[j];
                    unique_memory_addresses[j] = unique_memory_addresses[j + 1];
                    unique_memory_addresses[j + 1] = temp;
                }
            }
        }

        memory_data = (uint8_t *)malloc(unique_memory_addresses_count * sizeof(uint8_t));
        memset(memory_data, 0, unique_memory_addresses_count * sizeof(uint8_t));

        // change the read_memory function to the one that will return the data from the circular buffer
        rc_client_set_read_memory_function(g_client, read_memory_ingame);

        // lunch the core 1 to handle the memory write detection on the BUS
        multicore_launch_core1(handle_bus_to_detect_memory_writes);
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
    fifo_enqueue(&achievements_fifo, achievement->id);
}

// rcheevos event handler - used to enqueue the achievements the user won to be sent to the ESP32
static void event_handler(const rc_client_event_t *event, rc_client_t *client)
{
    switch (event->type)
    {
    case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
        achievement_triggered(event->achievement);
        break;

    default:
        printf("Unhandled event %d\n", event->type);
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
    async_handlers_index = async_handlers_index + 1 % MAX_ASYNC_CALLBACKS;
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
rc_client_t* initialize_retroachievements_client(rc_client_t *g_client, rc_client_read_memory_func_t read_memory, rc_client_server_call_t server_call)
{
     // Create the client instance (using a global variable simplifies this example)
    g_client = rc_client_create(read_memory, server_call);

    // Provide a logging function to simplify debugging
    rc_client_enable_logging(g_client, RC_CLIENT_LOG_LEVEL_VERBOSE, log_message);


    // Disable hardcore - if we goof something up in the implementation, we don't want our
    // account disabled for cheating.
    rc_client_set_hardcore_enabled(g_client, 0);
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

// main function - entry point
int main()
{

// overclock raspberry pi pico
#ifdef RUN_AT_200MHZ
    set_sys_clock_khz(200000, true);
#else
    set_sys_clock_khz(250000, true);
#endif

    stdio_init_all();
    reset_GPIO();

    // clear serial buffer
    memset(serial_buffer, '\0', SERIAL_BUFFER_SIZE);

    uart_init(UART_ID, BAUD_RATE);

    // config GPIO pins for UART
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    // enables UART
    uart_set_hw_flow(UART_ID, true, true);
    uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(UART_ID, true);

    printf("PICO_FIRMWARE_VERSION=0.1\r\n");

    // debug info
    unsigned int frame_counter = 0;

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
            uint32_t achievement_id;
            fifo_dequeue(&achievements_fifo, &achievement_id);
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
            // read from circular buffer detectd memory writes
            if (memory_buffer_size() > 0)
            {
                memory_unit memory = read_from_memory_buffer();

                // we started writing on memory ram - so we can assume user reseted the NES and the game is being played
                if (nes_reseted == 0 && memory.address < 0x07FF)
                {
                    nes_reseted = 1;
                    uart_puts(UART_ID, "NES_RESETED\r\n");

                    // for debug - print the memory addresses we are monitoring after the reset is detected
                    for (int i = 0; i < unique_memory_addresses_count; i += 1)
                    {
                        printf("%03X ", unique_memory_addresses[i]);
                    }
                    printf("\n");
                }

                // if memory address is 0x4014, we can assume a frame is being processed
                if (memory.address == 0x4014)
                {
                    // best place to detect a frame so far
                    rc_client_do_frame(g_client);
                    last_frame_processed = to_ms_since_boot(get_absolute_time());

                    // memory dump during a frame for DEBUG
                    // printf("_F");
                    // for (int i = 0; i < unique_memory_addresses_count; i += 1)
                    // {
                    //     //  "0xH006c=0_0xH0029=33_0xP0101>d0xP0101", // mega man 5
                    //     if (unique_memory_addresses[i] == 0x006C || unique_memory_addresses[i] == 0x0029 || unique_memory_addresses[i] == 0x0101)
                    //     {
                    //         printf("%03X ", memory_data[i]);
                    //     }
                    // }
                    // printf ("\n");

                    // debug memory circular buffer size - prints every 30 seconds with the circular buffer between the cores
                    // are not empty - helps us monitoring the size of the circular buffer
                    frame_counter += 1;
                    if (frame_counter % 1800 == 0) //~ 30 seconds
                    {
                        if (memory_buffer_size() > 0)
                        {
                            printf("F: %d, BS: %d\n", frame_counter, memory_buffer_size());
                        }
                    }
                }
                // if not, it is a memory of interest to detect achievements
                else
                {
                    if (memory.address <= 0x1FFF)
                    {
                        memory.address = memory.address & 0x07FF; // handle ram mirror
                    }

                    // TODO: use binary search to find the index of the memory address
                    for (int i = 0; i < unique_memory_addresses_count; i += 1)
                    {
                        if (memory.address == unique_memory_addresses[i])
                        {
                            memory_data[i] = memory.data;
                            break;
                        }
                    }
                }

                // simulate a frame every 18ms if we cannot detect any frame using the OAMDMA address monitoring
                // example of need: punchout
                u_int64_t now = to_ms_since_boot(get_absolute_time());
                if ((now - last_frame_processed) > 18)
                {
                    // printf("sF_");
                    rc_client_do_frame(g_client);
                    last_frame_processed = now;
                    // memory dump during a frame for DEBUG
                    // for (int i = 0; i < unique_memory_addresses_count; i += 1)
                    // {
                    //     //  "0xH006c=0_0xH0029=33_0xP0101>d0xP0101", // mega man 5
                    //     if (unique_memory_addresses[i] == 0x006C || unique_memory_addresses[i] == 0x0029 || unique_memory_addresses[i] == 0x0101)
                    //     {
                    //         printf("%03X ", memory_data[i]);
                    //     }
                    // }
                    // printf ("\n");
                }
            }
        }
        // handle UART communication, byte by byte
        if (uart_is_readable(UART_ID))
        {

            char received_char = uart_getc(UART_ID);
            serial_buffer_head[0] = received_char;
            serial_buffer_head += 1;
            // if a command is too big, we clear the buffer
            if (serial_buffer_head - serial_buffer == SERIAL_BUFFER_SIZE)
            {
                memset(serial_buffer, 0, SERIAL_BUFFER_SIZE);
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
                            http_callback(http_code, response_ptr, strlen(response_ptr), &async_data, NULL);
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
                    uint f_pll_sys = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_PLL_SYS_CLKSRC_PRIMARY);
                    printf("pll_sys  = %dkHz\n", f_pll_sys);
                    fifo_init(&achievements_fifo);
                    state = 0;
                    nes_reseted = 0;
                    memset(md5, '\0', 33);
                    crc_begin = 0xFFFFFFFF;
                    reset_GPIO();

                    free(unique_memory_addresses);
                    free(memory_data);
                    unique_memory_addresses = NULL;
                    memory_data = NULL;
                    unique_memory_addresses_count = 0;
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
                    g_client = initialize_retroachievements_client(g_client, read_memory_do_nothing, server_call);
                    rc_client_get_user_agent_clause(g_client, rcheevos_userdata, sizeof(rcheevos_userdata)); // TODO: send to esp32 before doing requests
                    rc_client_set_event_handler(g_client, event_handler);
                    rc_client_set_get_time_millisecs_function(g_client, get_pico_millisecs);
                    rc_client_begin_login_with_token(g_client, ra_user, ra_token, rc_client_login_callback, g_callback_userdata);
                    state = 5;
                }
                memset(serial_buffer, 0, len); // Clear the buffer since we are reading char by char
                serial_buffer_head = serial_buffer;
            }
        }
    }
}