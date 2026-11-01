// =============================================================================
// URTC Firmware - Virtual bootloader: the real bootloader of the master behind an SLCAN-style text port
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// A host program (never part of the firmware) that runs the bootloader of the master as it is: bootloader_main.c with its listening window,
// its main loop, its heartbeat and its inactivity timeout, bootloader_protocol.c, bootloader_flash.c, bootloader_crypto.c and the display driver,
// compiled against the fake HAL, with a fake flash that only lets bits go from 1 to 0 until a page is erased. Only main() is renamed.
// It reads one SLCAN line per frame on standard input and writes every frame the bootloader transmits as an SLCAN line on standard output,
// so a host tool (the flasher, the tester, a fuzzer) can talk to it like to an adapter:
//
//   tIIILdd...\r   a standard frame (identifier in three hex digits, length, data)   <->  same
//   S6\r O\r C\r   accepted and ignored (bit rate, open, close)                        ->  \r
//   V\r            answered with the version of the bootloader as an SLCAN version line
//   Wnnnn\r        (this program only) let nnnn milliseconds of the bootloader's clock pass: how a test reaches its 10 s timeout at once
//   X\r            printed by the program when the bootloader reset the chip (the program starts it again, flash and backup register kept)
//   J\r            printed when the bootloader jumped to the application; the program ends there, as the bootloader does
//
// The clock of the bootloader is the fake tick: it advances one millisecond for every millisecond the program waits for a line (so the heartbeat
// of one second and the window of 600 ms are real time) and by whatever W asks. What is replaced: the hardware, the display (its bus is faked),
// and the jump into the application (it cannot run on the host). The application is not simulated here: the virtual board of the application
// (tests/virtual_board.c) is another program.
#define _GNU_SOURCE
#include <poll.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// The bootloader's own file, with only its main() renamed (it owns the handles of the peripherals too).
#define main bootloader_main_entry
#include "bootloader_main.c"
#undef main

// The bootloader never returns from a jump: the program says so and ends.
void __wrap_JumpToApplication(void)
{
    printf("J\r\n");
    fflush(stdout);
    exit(0);
}

static int hex_digit(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Parses "tIIILdd..." into id, dlc, data; returns 1 when the line is a well-formed standard frame.
static int parse_frame(const char *line, uint32_t *id, uint32_t *dlc, uint8_t data[8])
{
    if (line[0] != 't') return 0;
    int a = hex_digit(line[1]), b = hex_digit(line[2]), c = hex_digit(line[3]), l = hex_digit(line[4]);
    if (a < 0 || b < 0 || c < 0 || l < 0 || l > 8 || a > 7) return 0;
    *id = (uint32_t)((a << 8) | (b << 4) | c);
    *dlc = (uint32_t)l;
    for (int i = 0; i < l; i++) {
        int hi = hex_digit(line[5 + 2 * i]), lo = hex_digit(line[6 + 2 * i]);
        if (hi < 0 || lo < 0) return 0;
        data[i] = (uint8_t)((hi << 4) | lo);
    }
    return line[5 + 2 * l] == 0;
}

static void print_tx(void)
{
    for (int i = 0; i < fake_tx_count; i++) {
        printf("t%03X%u", (unsigned)fake_tx[i].id, (unsigned)fake_tx[i].dlc);
        for (uint32_t k = 0; k < fake_tx[i].dlc && k < 8; k++) printf("%02X", fake_tx[i].data[k]);
        printf("\r\n");
    }
    fake_tx_count = 0;
    fflush(stdout);
}

// Reads one line without waiting longer than a millisecond; returns 1 with the line in `line`, 0 when none arrived, -1 at the end of the input.
static int read_line(char *line, size_t size)
{
    static char pending[256];
    static size_t have;
    for (;;) {
        char *nl = memchr(pending, '\n', have);
        if (nl) {
            size_t n = (size_t)(nl - pending);
            if (n >= size) n = size - 1;
            memcpy(line, pending, n);
            line[n] = 0;
            memmove(pending, nl + 1, have - (size_t)(nl + 1 - pending));
            have -= (size_t)(nl + 1 - pending);
            return 1;
        }
        struct pollfd p = { 0, POLLIN, 0 };
        int r = poll(&p, 1, 1);
        if (r == 0) return 0;
        if (r < 0) return -1;
        ssize_t got = read(0, pending + have, sizeof pending - have);
        if (got <= 0) return -1;
        have += (size_t)got;
        if (have == sizeof pending && !memchr(pending, '\n', have)) have = 0;   // a line longer than any frame: dropped
    }
}

// The bootloader polls its receive FIFO; when it is empty this is where the next line of the port is taken.
static void next_line_hook(void)
{
    print_tx();
    char line[256];
    for (;;) {
        int r = read_line(line, sizeof line);
        if (r < 0) exit(0);
        if (r == 0) { fake_tick += 1; return; }            // a millisecond went by with nothing on the bus
        size_t n = strcspn(line, "\r\n");
        line[n] = 0;
        // an SLCAN client ends its lines with a carriage return: the line may hold several of them
        char *save = 0;
        for (char *part = strtok_r(line, "\r", &save); part; part = strtok_r(0, "\r", &save)) {
            uint32_t id, dlc;
            uint8_t data[8];
            if (parse_frame(part, &id, &dlc, data)) {
                if (fake_rx_queue_count >= 8) continue;      // the FIFO of the chip holds three frames; a client that floods it loses them
                FakeRxFrame *f = &fake_rx_queue[fake_rx_queue_count++];
                memset(f, 0, sizeof *f);
                f->hdr.StdId = id;
                f->hdr.DLC = dlc;
                memcpy(f->data, data, dlc);
                fake_tick += 1;
            } else if (part[0] == 'W') {
                fake_tick += (uint32_t)strtoul(part + 1, 0, 10) % 60001u;
            } else if (part[0] == 'V') {
                printf("V%02d%02d\r\n", BOOTLOADER_VERSION_MAJOR, BOOTLOADER_VERSION_MINOR);
            } else if (part[0] == 'S' || part[0] == 'O' || part[0] == 'C') {
                printf("\r\n");
            } else if (part[0]) {
                printf("\a\r\n");                            // an SLCAN adapter answers a line it does not understand with a bell
            }
            fflush(stdout);
        }
        if (fake_rx_queue_count) return;
    }
}

int main(void)
{
    if (!fake_flash_init()) {
        fprintf(stderr, "the fake flash cannot be mapped on this host (a 64-bit Linux is needed)\n");
        return 2;
    }
    fake_hal_reset_recording();
    fake_tick = 100;
    fake_tick_step = 0;
    fake_rx_empty_hook = next_line_hook;
    for (;;) {
        fake_reset_jmp_armed = 1;
        if (setjmp(fake_reset_jmp) == 0) {
            bootloader_main_entry();
            return 0;
        }
        // the chip reset: the flash and the backup register stay, the RAM of the bootloader starts again
        fake_reset_jmp_armed = 0;
        print_tx();
        printf("X\r\n");
        fflush(stdout);
        update_in_progress = 0;
        update_failed = 0;
        update_bytes_received = 0;
        update_total_size = 0;
        fake_rx_queue_count = 0;
    }
}
