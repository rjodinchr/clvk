#include <iomanip>
#include <iostream>
#include <map>
#include <string>

#include "objects.hpp"
#include "utils.hpp"

#include "stdlib.h"
#include "string.h"

int parseLine(char* line) {
    // This assumes that a digit will be found and the line ends in " Kb".
    int i = strlen(line);
    const char* p = line;
    while (*p < '0' || *p > '9')
        p++;
    line[i - 3] = '\0';
    i = atoi(p);
    return i;
}

int getVirtualMem() { // Note: this value is in KB!
    FILE* file = fopen("/proc/self/status", "r");
    int result = -1;
    char line[128];

    while (fgets(line, 128, file) != NULL) {
        if (strncmp(line, "VmSize:", 7) == 0) {
            result = parseLine(line);
            break;
        }
    }
    fclose(file);
    return result;
}

std::map<void*, std::tuple<std::string, std::string, int>> alloc;
std::map<object_magic, std::string> magic2str = {
    {object_magic::vk, "vk"},
    {object_magic::platform, "platform"},
    {object_magic::device, "device"},
    {object_magic::context, "context"},
    {object_magic::command_queue, "command_queue"},
    {object_magic::event, "event"},
    {object_magic::program, "program"},
    {object_magic::kernel, "kernel"},
    {object_magic::memory_object, "memory_object"},
    {object_magic::sampler, "sampler"},
};

void alloc_update_desc(void* id, std::string str) {
    if (alloc.find(id) == alloc.end()) {
        cvk_error("ERROR: %p does not exist (%s)", id, str.c_str());
        return;
    }
    std::get<1>(alloc[id]) = str;
    cvk_error("update %p %s: %s", id, std::get<0>(alloc[id]).c_str(),
              std::get<1>(alloc[id]).c_str());
}

void alloc_check() {
    for (auto i : alloc) {
        cvk_error("ERROR: %p not free %s: %s (%u)", i.first,
                  std::get<0>(i.second).c_str(), std::get<1>(i.second).c_str(),
                  std::get<2>(i.second));
    }
}

void alloc_add(void* id, object_magic magic, std::string str, int size) {
    auto vmem = getVirtualMem();
    if (size != 0) {
        size = vmem - size;
    }
    cvk_error("alloc %p %s-%s-%d (total: %d)", (void*)id,
              magic2str[magic].c_str(), str.c_str(), size, vmem);
    auto find = alloc.find(id);
    if (find != alloc.end()) {
        cvk_error("ERROR: %p already allocated (%s: %s-%d)", id,
                  std::get<0>(find->second).c_str(),
                  std::get<1>(find->second).c_str(), std::get<2>(find->second));
        return;
    }
    alloc[id] = std::make_tuple(magic2str[magic], str, size);
}

void alloc_del(void* id, object_magic magic, std::string str, int size) {
    auto vmem = getVirtualMem();
    if (size != 0) {
        size = size - vmem;
    }
    auto find = alloc.find(id);
    if (find == alloc.end()) {
        cvk_error("ERROR: %p already free (%s : %s-%d)", id,
                  magic2str[magic].c_str(), str.c_str(), size);
        return;
    }
    if (size != std::get<2>(find->second)) {
        cvk_error("ERROR: %p size mismtached expected %d got %d (%s : %s)", id,
                  std::get<2>(find->second), size, magic2str[magic].c_str(),
                  str.c_str());
    }
    cvk_error("free %p %s: %s-%s-%d (total: %d)", id,
              std::get<0>(find->second).c_str(),
              std::get<1>(find->second).c_str(), str.c_str(), size,
              vmem);

    alloc.erase(id);
}

void alloc_add(void* id, object_magic magic) { alloc_add(id, magic, "#", 0); }

void alloc_del(void* id, object_magic magic) { alloc_del(id, magic, "#", 0); }

struct MemorySizes {
    VkDeviceSize allocatedMemory;
    VkDeviceSize allocatedMemoryMax;
    VkDeviceSize importedMemory;
    VkDeviceSize importedMemoryMax;
};
VkDeviceSize mCurrentTotalAllocatedMemory = 0;
VkDeviceSize mMaxTotalAllocatedMemory = 0;
VkDeviceSize mCurrentTotalImportedMemory = 0;
VkDeviceSize mMaxTotalImportedMemory = 0;
std::map<uint64_t, int> mUniqueIDCounts;
std::map<VkObjectType, MemorySizes> mSizesPerType;
std::mutex mMemoryReportMutex;

const char* GetVkObjectTypeName(VkObjectType type) {
    switch (type) {
    case VK_OBJECT_TYPE_UNKNOWN:
        return "Unknown";
    case VK_OBJECT_TYPE_INSTANCE:
        return "Instance";
    case VK_OBJECT_TYPE_PHYSICAL_DEVICE:
        return "Physical Device";
    case VK_OBJECT_TYPE_DEVICE:
        return "Device";
    case VK_OBJECT_TYPE_QUEUE:
        return "Queue";
    case VK_OBJECT_TYPE_SEMAPHORE:
        return "Semaphore";
    case VK_OBJECT_TYPE_COMMAND_BUFFER:
        return "Command Buffer";
    case VK_OBJECT_TYPE_FENCE:
        return "Fence";
    case VK_OBJECT_TYPE_DEVICE_MEMORY:
        return "Device Memory";
    case VK_OBJECT_TYPE_BUFFER:
        return "Buffer";
    case VK_OBJECT_TYPE_IMAGE:
        return "Image";
    case VK_OBJECT_TYPE_EVENT:
        return "Event";
    case VK_OBJECT_TYPE_QUERY_POOL:
        return "Query Pool";
    case VK_OBJECT_TYPE_BUFFER_VIEW:
        return "Buffer View";
    case VK_OBJECT_TYPE_IMAGE_VIEW:
        return "Image View";
    case VK_OBJECT_TYPE_SHADER_MODULE:
        return "Shader Module";
    case VK_OBJECT_TYPE_PIPELINE_CACHE:
        return "Pipeline Cache";
    case VK_OBJECT_TYPE_PIPELINE_LAYOUT:
        return "Pipeline Layout";
    case VK_OBJECT_TYPE_RENDER_PASS:
        return "Render Pass";
    case VK_OBJECT_TYPE_PIPELINE:
        return "Pipeline";
    case VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT:
        return "Descriptor Set Layout";
    case VK_OBJECT_TYPE_SAMPLER:
        return "Sampler";
    case VK_OBJECT_TYPE_DESCRIPTOR_POOL:
        return "Descriptor Pool";
    case VK_OBJECT_TYPE_DESCRIPTOR_SET:
        return "Descriptor Set";
    case VK_OBJECT_TYPE_FRAMEBUFFER:
        return "Framebuffer";
    case VK_OBJECT_TYPE_COMMAND_POOL:
        return "Command Pool";
    case VK_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION:
        return "Sampler YCbCr Conversion";
    case VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE:
        return "Descriptor Update Template";
    case VK_OBJECT_TYPE_SURFACE_KHR:
        return "Surface";
    case VK_OBJECT_TYPE_SWAPCHAIN_KHR:
        return "Swapchain";
    case VK_OBJECT_TYPE_DISPLAY_KHR:
        return "Display";
    case VK_OBJECT_TYPE_DISPLAY_MODE_KHR:
        return "Display Mode";
    case VK_OBJECT_TYPE_DEBUG_REPORT_CALLBACK_EXT:
        return "Debug Report Callback";
    case VK_OBJECT_TYPE_INDIRECT_COMMANDS_LAYOUT_NV:
        return "Indirect Commands Layout";
    case VK_OBJECT_TYPE_DEBUG_UTILS_MESSENGER_EXT:
        return "Debug Utils Messenger";
    case VK_OBJECT_TYPE_VALIDATION_CACHE_EXT:
        return "Validation Cache";
    case VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_NV:
        return "Acceleration Structure";
    default:
        return "<Unrecognized>";
    }
}

void logMemoryReportStats() {
    std::unique_lock<std::mutex> lock(mMemoryReportMutex);

    std::cerr << std::right
              << "GPU Memory Totals:       Allocated=" << std::setw(10)
              << mCurrentTotalAllocatedMemory << " (max=" << std::setw(10)
              << mMaxTotalAllocatedMemory << ");  Imported=" << std::setw(10)
              << mCurrentTotalImportedMemory << " (max=" << std::setw(10)
              << mMaxTotalImportedMemory << ")";
    std::cerr << "Sub-Totals per type:";
    for (const auto& it : mSizesPerType) {
        VkObjectType objectType = it.first;
        MemorySizes memorySizes = it.second;
        VkDeviceSize allocatedMemory = memorySizes.allocatedMemory;
        VkDeviceSize allocatedMemoryMax = memorySizes.allocatedMemoryMax;
        VkDeviceSize importedMemory = memorySizes.importedMemory;
        VkDeviceSize importedMemoryMax = memorySizes.importedMemoryMax;
        std::cerr << std::right << "- Type=" << std::setw(15)
                  << GetVkObjectTypeName(objectType)
                  << ":  Allocated=" << std::setw(10) << allocatedMemory
                  << " (max=" << std::setw(10) << allocatedMemoryMax
                  << ");  Imported=" << std::setw(10) << importedMemory
                  << " (max=" << std::setw(10) << importedMemoryMax << ")"
                  << std::endl;
    }
}

VKAPI_ATTR void VKAPI_CALL
MemoryReportCallback(const VkDeviceMemoryReportCallbackDataEXT* callbackDataPtr,
                     __attribute__((unused)) void* userData) {
    std::unique_lock<std::mutex> lock(mMemoryReportMutex);

    const VkDeviceMemoryReportCallbackDataEXT& callbackData = *callbackDataPtr;
    VkDeviceSize size = 0;
    std::string reportType;

    switch (callbackData.type) {
    case VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_ALLOCATE_EXT:
        reportType = "Allocate";
        if ((mUniqueIDCounts[callbackData.memoryObjectId] += 1) > 1) {
            break;
        }
        size = mSizesPerType[callbackData.objectType].allocatedMemory +
               callbackData.size;
        mSizesPerType[callbackData.objectType].allocatedMemory = size;
        if (mSizesPerType[callbackData.objectType].allocatedMemoryMax < size) {
            mSizesPerType[callbackData.objectType].allocatedMemoryMax = size;
        }
        mCurrentTotalAllocatedMemory += callbackData.size;
        if (mMaxTotalAllocatedMemory < mCurrentTotalAllocatedMemory) {
            mMaxTotalAllocatedMemory = mCurrentTotalAllocatedMemory;
        }
        break;
    case VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_FREE_EXT:
        reportType = "Free";
        CVK_ASSERT(mUniqueIDCounts[callbackData.memoryObjectId] > 0);
        mUniqueIDCounts[callbackData.memoryObjectId] -= 1;
        size = mSizesPerType[callbackData.objectType].allocatedMemory -
               callbackData.size;
        mSizesPerType[callbackData.objectType].allocatedMemory = size;
        mCurrentTotalAllocatedMemory -= callbackData.size;
        break;
    case VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_IMPORT_EXT:
        reportType = "Import";
        if ((mUniqueIDCounts[callbackData.memoryObjectId] += 1) > 1) {
            break;
        }
        size = mSizesPerType[callbackData.objectType].importedMemory +
               callbackData.size;
        mSizesPerType[callbackData.objectType].importedMemory = size;
        if (mSizesPerType[callbackData.objectType].importedMemoryMax < size) {
            mSizesPerType[callbackData.objectType].importedMemoryMax = size;
        }
        mCurrentTotalImportedMemory += callbackData.size;
        if (mMaxTotalImportedMemory < mCurrentTotalImportedMemory) {
            mMaxTotalImportedMemory = mCurrentTotalImportedMemory;
        }
        break;
    case VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_UNIMPORT_EXT:
        reportType = "Un-Import";
        CVK_ASSERT(mUniqueIDCounts[callbackData.memoryObjectId] > 0);
        mUniqueIDCounts[callbackData.memoryObjectId] -= 1;
        size = mSizesPerType[callbackData.objectType].importedMemory -
               callbackData.size;
        mSizesPerType[callbackData.objectType].importedMemory = size;
        mCurrentTotalImportedMemory -= callbackData.size;
        break;
    case VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_ALLOCATION_FAILED_EXT:
        reportType = "allocFail";
        break;
    default:
        CVK_ASSERT(false && "unreachable");
        return;
    }

    std::cerr << std::right << std::setw(9) << reportType
              << ": size=" << std::setw(10) << callbackData.size
              << "; type=" << std::setw(15) << std::left
              << GetVkObjectTypeName(callbackData.objectType)
              << "; heapIdx=" << callbackData.heapIndex << "; id=" << std::hex
              << callbackData.memoryObjectId << "; handle=" << std::hex
              << callbackData.objectHandle << ": Total=" << std::right
              << std::setw(10) << std::dec << size << std::endl;
}
