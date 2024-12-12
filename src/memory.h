// Commander X16 Emulator
// Copyright (c) 2019 Michael Steil
// All rights reserved. License: 2-clause BSD

#ifndef _MEMORY_H_
#define _MEMORY_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define USE_CURRENT_BANK (-1)
#define debug_read6502(a, b) real_read6502(a, true, b)

uint8_t read6502(uint16_t address);
uint8_t real_read6502(uint16_t address, bool debugOn, int16_t bank);
void write6502(uint16_t address, uint8_t value);
void vp6502();

void memory_init();
void memory_reset();
void memory_report_uninitialized_access(bool);
void memory_report_usage_statistics(const char *filename);
void memory_randomize_ram(bool);

void memory_set_ram_bank(uint8_t bank);
void memory_set_rom_bank(uint8_t bank);

uint8_t memory_get_ram_bank();
uint8_t memory_get_rom_bank();

uint8_t emu_read(uint8_t reg, bool debugOn);
void emu_write(uint8_t reg, uint8_t value);

#endif
