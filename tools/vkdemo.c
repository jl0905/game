// vkdemo (V156, RENDERER.md phase 2, milestone 1): Vulkan opens a window,
// builds a swapchain, and PRESENTS — clear-color frames pulsing through a
// dawn palette on this machine's own GPU. Everything is loaded dynamically
// from the driver's vulkan-1.dll against the vendored Khronos headers: no
// SDK, no import lib, MinGW-clean.
//
//   Build: the `vkdemo` CMake target (see CMakeLists.txt).
//   Run:   build/vkdemo.exe [frames]   (default 240, then reports and exits;
//                                       close the window to stop earlier)
// Prints "vkdemo: PRESENTED n frames on <gpu>" on success — the phase-2
// go/no-go line.

#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static PFN_vkGetInstanceProcAddr gipa;
static PFN_vkGetDeviceProcAddr gdpa;
#define INST_FN(inst, name) PFN_##name name = (PFN_##name)gipa(inst, #name)
#define DEV_FN(dev, name)   PFN_##name name = (PFN_##name)gdpa(dev, #name)

static int g_wantClose = 0;
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CLOSE || m == WM_DESTROY) { g_wantClose = 1; return 0; }
    return DefWindowProcA(h, m, w, l);
}

int main(int argc, char** argv) {
    const int wantFrames = argc > 1 ? atoi(argv[1]) : 240;

    HMODULE lib = LoadLibraryA("vulkan-1.dll");
    if (!lib) { printf("vkdemo: no loader\n"); return 1; }
    gipa = (PFN_vkGetInstanceProcAddr)GetProcAddress(lib, "vkGetInstanceProcAddr");

    // ---- instance ----
    INST_FN(NULL, vkCreateInstance);
    const char* iext[] = { "VK_KHR_surface", "VK_KHR_win32_surface" };
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "openwarband-vkdemo";
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = iext;
    VkInstance inst;
    if (vkCreateInstance(&ici, NULL, &inst) != VK_SUCCESS) {
        printf("vkdemo: instance FAILED\n");
        return 1;
    }
    gdpa = (PFN_vkGetDeviceProcAddr)gipa(inst, "vkGetDeviceProcAddr");

    // ---- window + surface ----
    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "vkdemo";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, "vkdemo", "OpenWarband Vulkan demo (V156)",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 960, 540,
                                NULL, NULL, wc.hInstance, NULL);
    INST_FN(inst, vkCreateWin32SurfaceKHR);
    VkWin32SurfaceCreateInfoKHR sci = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
    sci.hinstance = wc.hInstance;
    sci.hwnd = hwnd;
    VkSurfaceKHR surface;
    if (vkCreateWin32SurfaceKHR(inst, &sci, NULL, &surface) != VK_SUCCESS) {
        printf("vkdemo: surface FAILED\n");
        return 1;
    }

    // ---- physical device: prefer the discrete GPU ----
    INST_FN(inst, vkEnumeratePhysicalDevices);
    INST_FN(inst, vkGetPhysicalDeviceProperties);
    INST_FN(inst, vkGetPhysicalDeviceQueueFamilyProperties);
    INST_FN(inst, vkGetPhysicalDeviceSurfaceSupportKHR);
    INST_FN(inst, vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    INST_FN(inst, vkGetPhysicalDeviceSurfaceFormatsKHR);
    INST_FN(inst, vkCreateDevice);
    uint32_t nd = 0;
    vkEnumeratePhysicalDevices(inst, &nd, NULL);
    VkPhysicalDevice devs[8];
    if (nd > 8) nd = 8;
    vkEnumeratePhysicalDevices(inst, &nd, devs);
    VkPhysicalDevice phys = NULL;
    VkPhysicalDeviceProperties props;
    uint32_t qfam = 0;
    for (int pass = 0; pass < 2 && !phys; ++pass) {
        for (uint32_t i = 0; i < nd; ++i) {
            VkPhysicalDeviceProperties p;
            vkGetPhysicalDeviceProperties(devs[i], &p);
            if (pass == 0 && p.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                continue;   // first pass: discrete only
            uint32_t nq = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &nq, NULL);
            VkQueueFamilyProperties qf[16];
            if (nq > 16) nq = 16;
            vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &nq, qf);
            for (uint32_t q = 0; q < nq; ++q) {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(devs[i], q, surface, &present);
                if ((qf[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                    phys = devs[i];
                    props = p;
                    qfam = q;
                    break;
                }
            }
            if (phys) break;
        }
    }
    if (!phys) { printf("vkdemo: no usable GPU\n"); return 1; }

    // ---- device + queue ----
    const float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qci.queueFamilyIndex = qfam;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    const char* dext[] = { "VK_KHR_swapchain" };
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = dext;
    VkDevice dev;
    if (vkCreateDevice(phys, &dci, NULL, &dev) != VK_SUCCESS) {
        printf("vkdemo: device FAILED\n");
        return 1;
    }
    DEV_FN(dev, vkGetDeviceQueue);
    DEV_FN(dev, vkCreateSwapchainKHR);
    DEV_FN(dev, vkGetSwapchainImagesKHR);
    DEV_FN(dev, vkCreateImageView);
    DEV_FN(dev, vkCreateCommandPool);
    DEV_FN(dev, vkAllocateCommandBuffers);
    DEV_FN(dev, vkBeginCommandBuffer);
    DEV_FN(dev, vkEndCommandBuffer);
    DEV_FN(dev, vkCmdPipelineBarrier);
    DEV_FN(dev, vkCmdClearColorImage);
    DEV_FN(dev, vkCreateSemaphore);
    DEV_FN(dev, vkCreateFence);
    DEV_FN(dev, vkWaitForFences);
    DEV_FN(dev, vkResetFences);
    DEV_FN(dev, vkResetCommandBuffer);
    DEV_FN(dev, vkAcquireNextImageKHR);
    DEV_FN(dev, vkQueueSubmit);
    DEV_FN(dev, vkQueuePresentKHR);
    DEV_FN(dev, vkDeviceWaitIdle);
    VkQueue queue;
    vkGetDeviceQueue(dev, qfam, 0, &queue);

    // ---- swapchain ----
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps);
    uint32_t nf = 1;
    VkSurfaceFormatKHR fmt;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &nf, &fmt);
    VkSwapchainCreateInfoKHR sc = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    sc.surface = surface;
    sc.minImageCount = caps.minImageCount + 1 <= caps.maxImageCount
                           ? caps.minImageCount + 1 : caps.minImageCount;
    sc.imageFormat = fmt.format;
    sc.imageColorSpace = fmt.colorSpace;
    sc.imageExtent = caps.currentExtent;
    sc.imageArrayLayers = 1;
    sc.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    sc.preTransform = caps.currentTransform;
    sc.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sc.presentMode = VK_PRESENT_MODE_FIFO_KHR;   // vsync, universally supported
    sc.clipped = VK_TRUE;
    VkSwapchainKHR swap;
    if (vkCreateSwapchainKHR(dev, &sc, NULL, &swap) != VK_SUCCESS) {
        printf("vkdemo: swapchain FAILED\n");
        return 1;
    }
    uint32_t ni = 0;
    vkGetSwapchainImagesKHR(dev, swap, &ni, NULL);
    VkImage images[8];
    if (ni > 8) ni = 8;
    vkGetSwapchainImagesKHR(dev, swap, &ni, images);

    // ---- commands + sync ----
    VkCommandPoolCreateInfo cpci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = qfam;
    VkCommandPool pool;
    vkCreateCommandPool(dev, &cpci, NULL, &pool);
    VkCommandBufferAllocateInfo cbai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cbai.commandPool = pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(dev, &cbai, &cmd);
    VkSemaphoreCreateInfo semci = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkSemaphore semAcquire, semRender;
    vkCreateSemaphore(dev, &semci, NULL, &semAcquire);
    vkCreateSemaphore(dev, &semci, NULL, &semRender);
    VkFenceCreateInfo fci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkFence fence;
    vkCreateFence(dev, &fci, NULL, &fence);

    // ---- present loop: a dawn sky pulsing through the swapchain ----
    int frames = 0;
    while (frames < wantFrames && !g_wantClose) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);
        vkResetFences(dev, 1, &fence);

        uint32_t idx = 0;
        if (vkAcquireNextImageKHR(dev, swap, UINT64_MAX, semAcquire, NULL,
                                  &idx) != VK_SUCCESS)
            break;

        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        vkBeginCommandBuffer(cmd, &bi);

        VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkImageMemoryBarrier toClear = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        toClear.srcAccessMask = 0;
        toClear.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toClear.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toClear.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toClear.srcQueueFamilyIndex = toClear.dstQueueFamilyIndex = qfam;
        toClear.image = images[idx];
        toClear.subresourceRange = range;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0,
                             NULL, 1, &toClear);

        const float t = frames / 60.0f;
        VkClearColorValue col;
        col.float32[0] = 0.25f + 0.20f * sinf(t * 0.9f);          // dawn reds
        col.float32[1] = 0.16f + 0.10f * sinf(t * 0.7f + 1.5f);
        col.float32[2] = 0.30f + 0.18f * sinf(t * 0.5f + 3.0f);   // night blues
        col.float32[3] = 1.0f;
        vkCmdClearColorImage(cmd, images[idx],
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &col, 1,
                             &range);

        VkImageMemoryBarrier toPresent = toClear;
        toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toPresent.dstAccessMask = 0;
        toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL,
                             0, NULL, 1, &toPresent);
        vkEndCommandBuffer(cmd);

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &semAcquire;
        si.pWaitDstStageMask = &waitStage;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &semRender;
        vkQueueSubmit(queue, 1, &si, fence);

        VkPresentInfoKHR pi = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &semRender;
        pi.swapchainCount = 1;
        pi.pSwapchains = &swap;
        pi.pImageIndices = &idx;
        if (vkQueuePresentKHR(queue, &pi) != VK_SUCCESS) break;
        frames++;
    }
    vkDeviceWaitIdle(dev);
    printf("vkdemo: PRESENTED %d frames on %s\n", frames, props.deviceName);
    return frames > 0 ? 0 : 1;
}
