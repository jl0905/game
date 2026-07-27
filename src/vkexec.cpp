#include <string.h>

// raylib.h cannot coexist with windows.h (CloseWindow/ShowCursor/Rectangle
// clash), so the few raylib calls used here are declared by hand.
extern "C" bool IsWindowReady(void);
extern "C" void TraceLog(int logLevel, const char* text, ...);
enum { RL_LOG_INFO = 3, RL_LOG_WARNING = 4 };

// ---------------------------------------------------------------------------
// The in-game Vulkan executor (V162, phase 2 tail â€” foundation). When
// settings.cfg says `renderer vulkan`, the GAME PROCESS itself now brings a
// Vulkan device up at first flush: dynamic vulkan-1.dll load, instance,
// discrete-GPU pick, device + graphics queue. The frame executor lands on
// top of this in the next steps (offscreen render of rdr::Buckets() +
// present interop, then the native window swap â€” RENDERER.md), each of
// which is already running code in tools/vkarmy.c awaiting the lift.
// Headless runs and `renderer raylib` never touch any of this.
// ---------------------------------------------------------------------------
#if defined(_WIN32)
#define VK_NO_PROTOTYPES
#include <windows.h>
#include <vulkan/vulkan.h>

namespace rdr {

namespace {
struct VkExec {
    bool tried = false, live = false;
    char gpuName[256] = { 0 };
    VkInstance inst = VK_NULL_HANDLE;
    VkDevice dev = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t qfam = 0;
};
VkExec g_vk;

void BootDevice() {
    g_vk.tried = true;
    HMODULE lib = LoadLibraryA("vulkan-1.dll");
    if (!lib) return;
    auto gipa = (PFN_vkGetInstanceProcAddr)GetProcAddress(lib, "vkGetInstanceProcAddr");
    auto createInstance = (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "openwarband";
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &app;
    if (createInstance(&ici, NULL, &g_vk.inst) != VK_SUCCESS) return;

    auto enumDevs = (PFN_vkEnumeratePhysicalDevices)gipa(g_vk.inst, "vkEnumeratePhysicalDevices");
    auto getProps = (PFN_vkGetPhysicalDeviceProperties)gipa(g_vk.inst, "vkGetPhysicalDeviceProperties");
    auto getQ     = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)gipa(g_vk.inst, "vkGetPhysicalDeviceQueueFamilyProperties");
    auto createDev= (PFN_vkCreateDevice)gipa(g_vk.inst, "vkCreateDevice");
    uint32_t nd = 0;
    enumDevs(g_vk.inst, &nd, NULL);
    VkPhysicalDevice devs[8];
    if (nd > 8) nd = 8;
    enumDevs(g_vk.inst, &nd, devs);
    VkPhysicalDevice pick = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props{};
    for (int pass = 0; pass < 2 && !pick; ++pass)
        for (uint32_t i = 0; i < nd && !pick; ++i) {
            VkPhysicalDeviceProperties p;
            getProps(devs[i], &p);
            if (pass == 0 && p.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                continue;
            uint32_t nq = 0;
            getQ(devs[i], &nq, NULL);
            VkQueueFamilyProperties qf[16];
            if (nq > 16) nq = 16;
            getQ(devs[i], &nq, qf);
            for (uint32_t q = 0; q < nq; ++q)
                if (qf[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                    pick = devs[i];
                    props = p;
                    g_vk.qfam = q;
                    break;
                }
        }
    if (!pick) return;
    const float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qci.queueFamilyIndex = g_vk.qfam;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    if (createDev(pick, &dci, NULL, &g_vk.dev) != VK_SUCCESS) return;
    auto getQueue = (PFN_vkGetDeviceQueue)gipa(g_vk.inst, "vkGetDeviceQueue");
    getQueue(g_vk.dev, g_vk.qfam, 0, &g_vk.queue);
    strncpy(g_vk.gpuName, props.deviceName, sizeof(g_vk.gpuName) - 1);
    g_vk.live = true;
}
}  // namespace

// Called by Flush() when renderer=vulkan: boots the device once and reports.
// Returns false while the frame executor is still incomplete, in which case
// the caller runs the GL path so the player always has a correct frame.
bool VulkanExecutorReady() {
    if (!g_vk.tried && IsWindowReady()) {
        BootDevice();
        if (g_vk.live)
            TraceLog(RL_LOG_INFO,
                     "rdr: VULKAN DEVICE LIVE in-game on %s (queue family %u) - "
                     "frame executor migration continues per RENDERER.md",
                     g_vk.gpuName, g_vk.qfam);
        else
            TraceLog(RL_LOG_WARNING, "rdr: vulkan requested but device boot failed; GL fallback");
    }
    return false;   // flips true when the offscreen executor lands
}

}  // namespace rdr

#else
namespace rdr { bool VulkanExecutorReady() { return false; } }
#endif
