// Commander X16 Emulator
// Copyright (c) 2019 Michael Steil
// All rights reserved. License: 2-clause BSD
#ifndef _SD_CARD_H_
#define _SD_CARD_H_
#include <inttypes.h>
#include <stdbool.h>
#include <SDL.h>

extern bool sdcard_attached;
void sdcard_set_path(char const *path);
void sdcard_load_from_memory(uint8_t *content, size_t content_size);
bool sdcard_path_is_set();
void sdcard_attach();
void sdcard_attach_from_memory(uint8_t *content, size_t content_size);
void sdcard_detach();

void sdcard_select(bool select);
uint8_t sdcard_handle(uint8_t inbyte);

#endif
