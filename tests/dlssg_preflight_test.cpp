// Production bridge harness: no Vulkan device or NVIDIA runtime. The SDK fixture is a separate DLL, so declarations
// and structures really cross a dynamic-library boundary. Run through dlssg_preflight_test.py.
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include "vk_dlssg.h"

extern "C" void rt_log(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);
    std::putchar('\n');
}

int main(int argc, char **argv) {
    unsigned vendor = argc > 1 && !std::strcmp(argv[1], "amd") ? 0x1002 : 0x10de;
    auto physical = argc > 1 && !std::strcmp(argv[1], "null") ? VK_NULL_HANDLE : (VkPhysicalDevice)(uintptr_t)0x1234;
    int first = vk_dlssg_probe(physical, vendor);
    if (vk_dlssg_probe(physical, vendor) != first) return 1;
    vk_dlssg_reset_probe();
    int second = vk_dlssg_probe(physical, vendor);
    if (vk_dlssg_probe(physical, vendor) != second) return 2;
    std::printf("results: %d %d\n", first, second);
    return 0;
}
