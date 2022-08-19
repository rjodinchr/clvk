#include <iomanip>
#include <iostream>
#include <map>
#include <string>

#include "objects.hpp"
#include "utils.hpp"

std::map<void*, std::pair<std::string, std::string>> alloc;
std::map<object_magic, std::string> magic2str = {
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
    CVK_ASSERT(alloc.find(id) != alloc.end());
    alloc[id].second = str;
    cvk_error("update %p %s: %s", id, alloc[id].first.c_str(),
              alloc[id].second.c_str());
}

void alloc_check() {
    for (auto i : alloc) {
        cvk_error("ERROR: %p not free %s: %s", i.first, i.second.first.c_str(),
                  i.second.second.c_str());
    }
}

void alloc_add(void* id, object_magic magic) {
    cvk_error("alloc %p %s", (void*)id, magic2str[magic].c_str());
    auto find = alloc.find(id);
    if (find != alloc.end()) {
        cvk_error("ERROR: %p already allocated (%s: %s)", id,
                  find->second.first.c_str(), find->second.second.c_str());
        CVK_ASSERT(false);
    }
    alloc[id] = std::make_pair(magic2str[magic], "#");
}

void alloc_del(void* id, object_magic magic) {
    auto find = alloc.find(id);
    if (find == alloc.end()) {
        cvk_error("ERROR: %p already free (%s)", id, magic2str[magic].c_str());
        CVK_ASSERT(false);
    }
    cvk_error("free %p %s: %s", id, find->second.first.c_str(),
              find->second.second.c_str());

    alloc.erase(id);
}

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
