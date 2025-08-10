#ifndef KEYMAP_H
#define KEYMAP_H

#include <string>
#include <windows.h>

struct key_to_vk_type {
  std::string keyName;
  int vkCode;
};

extern key_to_vk_type g_key_to_vk[];

extern const size_t g_key_to_vk_size;

#endif