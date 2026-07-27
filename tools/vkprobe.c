// vkprobe (V155): proves the Vulkan lane end-to-end on this machine with
// zero SDK — vendored Khronos headers + the driver-shipped loader, loaded
// dynamically (no import library, so MinGW links it with no fuss).
// Prints every physical device and whether it carries the ray-tracing
// extensions the RENDERER.md endgame needs.
//
//   gcc tools/vkprobe.c -Ithird_party/vulkan-headers -o build/vkprobe.exe
//   build/vkprobe.exe
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

int main(void) {
    HMODULE lib = LoadLibraryA("vulkan-1.dll");
    if (!lib) { printf("vkprobe: NO LOADER (vulkan-1.dll missing)\n"); return 1; }
    PFN_vkGetInstanceProcAddr gipa =
        (PFN_vkGetInstanceProcAddr)GetProcAddress(lib, "vkGetInstanceProcAddr");
    PFN_vkCreateInstance createInstance =
        (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");

    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "openwarband-vkprobe";
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &app;
    VkInstance inst;
    if (createInstance(&ici, NULL, &inst) != VK_SUCCESS) {
        printf("vkprobe: instance creation FAILED\n");
        return 1;
    }
    printf("vkprobe: Vulkan instance OK (headers %d.%d.%d)\n",
           VK_API_VERSION_MAJOR(VK_HEADER_VERSION_COMPLETE),
           VK_API_VERSION_MINOR(VK_HEADER_VERSION_COMPLETE),
           VK_HEADER_VERSION);

    PFN_vkEnumeratePhysicalDevices enumDevs =
        (PFN_vkEnumeratePhysicalDevices)gipa(inst, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties getProps =
        (PFN_vkGetPhysicalDeviceProperties)gipa(inst, "vkGetPhysicalDeviceProperties");
    PFN_vkEnumerateDeviceExtensionProperties enumExt =
        (PFN_vkEnumerateDeviceExtensionProperties)gipa(
            inst, "vkEnumerateDeviceExtensionProperties");

    uint32_t n = 0;
    enumDevs(inst, &n, NULL);
    VkPhysicalDevice devs[8];
    if (n > 8) n = 8;
    enumDevs(inst, &n, devs);
    for (uint32_t i = 0; i < n; ++i) {
        VkPhysicalDeviceProperties p;
        getProps(devs[i], &p);
        uint32_t ne = 0;
        enumExt(devs[i], NULL, &ne, NULL);
        VkExtensionProperties* ext =
            (VkExtensionProperties*)malloc(ne * sizeof(*ext));
        enumExt(devs[i], NULL, &ne, ext);
        int rtPipe = 0, accel = 0, rayQuery = 0;
        for (uint32_t e = 0; e < ne; ++e) {
            if (!strcmp(ext[e].extensionName, "VK_KHR_ray_tracing_pipeline")) rtPipe = 1;
            if (!strcmp(ext[e].extensionName, "VK_KHR_acceleration_structure")) accel = 1;
            if (!strcmp(ext[e].extensionName, "VK_KHR_ray_query")) rayQuery = 1;
        }
        printf("device %u: %s (api %d.%d)\n", i, p.deviceName,
               VK_API_VERSION_MAJOR(p.apiVersion),
               VK_API_VERSION_MINOR(p.apiVersion));
        printf("    ray_tracing_pipeline=%s  acceleration_structure=%s  ray_query=%s\n",
               rtPipe ? "YES" : "no", accel ? "YES" : "no", rayQuery ? "YES" : "no");
        free(ext);
    }
    printf("vkprobe: done\n");
    return 0;
}
