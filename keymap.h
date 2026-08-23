#pragma once

#include <string>
#include <optional>
#include <Windows.h>

struct key_to_vk_type {
  std::string keyName;
  int vkCode;
};

extern key_to_vk_type g_key_to_vk[];

extern const size_t g_key_to_vk_size;

std::optional<WORD> keyNameToVk(const std::string& name);
std::string keyName(WORD vkCode);
std::wstring displayKeyName(WORD vkCode);
