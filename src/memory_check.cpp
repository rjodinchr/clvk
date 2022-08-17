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
