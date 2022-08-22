#pragma once

#include <string>
#include "objects.hpp"

int getVirtualMem();

void alloc_check();

void alloc_update_desc(void* id, std::string str);

void alloc_add(void* id, object_magic magic, std::string str, int);

void alloc_del(void* id, object_magic magic, std::string str, int);

void alloc_add(void* id, object_magic magic);

void alloc_del(void* id, object_magic magic);

void logMemoryReportStats();

VKAPI_ATTR void VKAPI_CALL
MemoryReportCallback(const VkDeviceMemoryReportCallbackDataEXT* callbackDataPtr,
                     __attribute__((unused)) void* userData);
