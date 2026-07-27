// vkwin (V178, RENDERER.md final step, proven standalone): the NATIVE
// WINDOW/INPUT SWAP. A raw Win32 window — RegisterClass/CreateWindow, no
// GLFW, no raylib anywhere — with a real Vulkan swapchain presenting the
// instanced-box battle scene DIRECTLY (no offscreen, no readback, no GL
// composite), plus the input half of the swap: WM_* messages captured into
// exactly the state the game's Gather*Input readers need (WASD, mouse
// delta via cursor recentering, LMB/RMB, wheel), proven live by flying the
// camera with them. Resize recreates the swapchain; Esc/close exits clean.
//
//   build/vkwin.exe [--frames N]   (N>0: auto-exit after N frames for
//                                   headless verification; default: run
//                                   until Esc/close)
// Prints "vkwin: %d frames, avg %.2f ms (%.0f fps), %d key events,
// %.0f px mouse travel on <gpu>" on exit — the swap's go/no-go line.
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#include <vulkan/vulkan.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- tiny matrix kit (column-major, Vulkan clip space) — from vkarmy ----
typedef struct { float m[16]; } M4;
static M4 m4mul(M4 a, M4 b) {
    M4 r;
    for (int c = 0; c < 4; ++c)
        for (int rr = 0; rr < 4; ++rr) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
            r.m[c * 4 + rr] = s;
        }
    return r;
}
static M4 m4persp(float fovy, float aspect, float zn, float zf) {
    const float f = 1.0f / tanf(fovy * 0.5f);
    M4 r = { 0 };
    r.m[0] = f / aspect;
    r.m[5] = -f;                       // Vulkan: y flips
    r.m[10] = zf / (zn - zf);
    r.m[11] = -1.0f;
    r.m[14] = (zn * zf) / (zn - zf);
    return r;
}
static M4 m4look(float ex, float ey, float ez, float cx, float cy, float cz) {
    float fx = cx - ex, fy = cy - ey, fz = cz - ez;
    float fl = sqrtf(fx * fx + fy * fy + fz * fz);
    fx /= fl; fy /= fl; fz /= fl;
    float sx = -fz, sy = 0.0f, sz = fx;              // f x up(0,1,0)
    float sl = sqrtf(sx * sx + sy * sy + sz * sz);
    if (sl < 1e-5f) { sx = 1; sy = 0; sz = 0; sl = 1; }
    sx /= sl; sy /= sl; sz /= sl;
    float ux = sy * fz - sz * fy, uy = sz * fx - sx * fz, uz = sx * fy - sy * fx;
    M4 r = { 0 };
    r.m[0] = sx; r.m[4] = sy; r.m[8] = sz;
    r.m[1] = ux; r.m[5] = uy; r.m[9] = uz;
    r.m[2] = -fx; r.m[6] = -fy; r.m[10] = -fz;
    r.m[12] = -(sx * ex + sy * ey + sz * ez);
    r.m[13] = -(ux * ex + uy * ey + uz * ez);
    r.m[14] = fx * ex + fy * ey + fz * ez;
    r.m[15] = 1.0f;
    return r;
}

typedef struct { float col0[4], col1[4], col2[4], col3[4], color[4]; } Inst;

// ---------------------------------------------------------------------------
// The input half of the swap: the exact per-frame state the game's
// GatherCampaignInput/GatherBattleInput read from raylib today, captured
// from raw WM_* messages instead. This struct IS the contract — wiring it
// under src/input.h's Gather functions is the remaining game-side work.
// ---------------------------------------------------------------------------
typedef struct {
    unsigned char key[256];        // VK_* held state   (IsKeyDown)
    unsigned char keyPressed[256]; // went down this frame (IsKeyPressed)
    int lmb, rmb;                  // held
    int lmbPressed, rmbPressed;    // went down this frame
    float mouseDx, mouseDy;        // per-frame delta (mouse-look)
    float wheel;                   // per-frame wheel steps
} WinInput;
static WinInput g_in;
static int g_wantClose = 0, g_resized = 0;
static long g_keyEvents = 0;
static float g_mouseTravel = 0.0f;

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_CLOSE:
        case WM_DESTROY: g_wantClose = 1; return 0;
        case WM_SIZE: g_resized = 1; return 0;
        case WM_KEYDOWN:
            if (w < 256 && !g_in.key[w]) { g_in.keyPressed[w] = 1; g_keyEvents++; }
            if (w < 256) g_in.key[w] = 1;
            if (w == VK_ESCAPE) g_wantClose = 1;
            return 0;
        case WM_KEYUP:
            if (w < 256) g_in.key[w] = 0;
            g_keyEvents++;
            return 0;
        case WM_LBUTTONDOWN: if (!g_in.lmb) g_in.lmbPressed = 1; g_in.lmb = 1; return 0;
        case WM_LBUTTONUP:   g_in.lmb = 0; return 0;
        case WM_RBUTTONDOWN: if (!g_in.rmb) g_in.rmbPressed = 1; g_in.rmb = 1; return 0;
        case WM_RBUTTONUP:   g_in.rmb = 0; return 0;
        case WM_MOUSEWHEEL:  g_in.wheel += GET_WHEEL_DELTA_WPARAM(w) / (float)WHEEL_DELTA; return 0;
        default: return DefWindowProcA(h, m, w, l);
    }
}

// Mouse-look via cursor recentering (the raylib DisableCursor pattern): the
// per-frame delta is how far the cursor strayed from the client centre.
static void PumpMouseDelta(HWND hwnd, int focus) {
    g_in.mouseDx = g_in.mouseDy = 0.0f;
    if (!focus) return;
    RECT rc;
    GetClientRect(hwnd, &rc);
    POINT centre = { (rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2 };
    POINT cur;
    GetCursorPos(&cur);
    ScreenToClient(hwnd, &cur);
    g_in.mouseDx = (float)(cur.x - centre.x);
    g_in.mouseDy = (float)(cur.y - centre.y);
    g_mouseTravel += fabsf(g_in.mouseDx) + fabsf(g_in.mouseDy);
    POINT scr = centre;
    ClientToScreen(hwnd, &scr);
    SetCursorPos(scr.x, scr.y);
}

static PFN_vkGetInstanceProcAddr gipa;
static PFN_vkGetDeviceProcAddr gdpa;
#define INST_FN(inst, name) PFN_##name name = (PFN_##name)gipa(inst, #name)
#define DEV_LOAD(dev, name) name = (PFN_##name)gdpa(dev, #name)

// Device-level entry points used across the recreate path live at file
// scope so helpers can share them (VK_NO_PROTOTYPES frees the names).
static PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR;
static PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR;
static PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR;
static PFN_vkCreateImageView vkCreateImageView;
static PFN_vkDestroyImageView vkDestroyImageView;
static PFN_vkCreateImage vkCreateImage;
static PFN_vkDestroyImage vkDestroyImage;
static PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements;
static PFN_vkAllocateMemory vkAllocateMemory;
static PFN_vkFreeMemory vkFreeMemory;
static PFN_vkBindImageMemory vkBindImageMemory;
static PFN_vkCreateFramebuffer vkCreateFramebuffer;
static PFN_vkDestroyFramebuffer vkDestroyFramebuffer;
static PFN_vkDeviceWaitIdle vkDeviceWaitIdle;
static PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR pGetCaps;

static VkDevice g_dev;
static VkPhysicalDevice g_phys;
static VkSurfaceKHR g_surface;
static VkPhysicalDeviceMemoryProperties g_memp;
static VkRenderPass g_rp;
static VkFormat g_fmt;
static VkSwapchainKHR g_swap = VK_NULL_HANDLE;
static VkExtent2D g_extent;
static uint32_t g_nImages = 0;
static VkImage g_images[8];
static VkImageView g_views[8];
static VkImage g_depthImg = VK_NULL_HANDLE;
static VkDeviceMemory g_depthMem = VK_NULL_HANDLE;
static VkImageView g_depthView = VK_NULL_HANDLE;
static VkFramebuffer g_fbs[8];

static uint32_t FindMem(VkMemoryRequirements req, VkMemoryPropertyFlags flags) {
    for (uint32_t i = 0; i < g_memp.memoryTypeCount; ++i)
        if ((req.memoryTypeBits & (1u << i)) &&
            (g_memp.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    return 0;
}

static void DestroySwapchainObjects(void) {
    if (!g_swap) return;
    vkDeviceWaitIdle(g_dev);
    for (uint32_t i = 0; i < g_nImages; ++i) {
        vkDestroyFramebuffer(g_dev, g_fbs[i], NULL);
        vkDestroyImageView(g_dev, g_views[i], NULL);
    }
    vkDestroyImageView(g_dev, g_depthView, NULL);
    vkDestroyImage(g_dev, g_depthImg, NULL);
    vkFreeMemory(g_dev, g_depthMem, NULL);
    vkDestroySwapchainKHR(g_dev, g_swap, NULL);
    g_swap = VK_NULL_HANDLE;
    g_nImages = 0;
}

// Build (or rebuild, on WM_SIZE) swapchain + depth + framebuffers.
static int CreateSwapchainObjects(void) {
    VkSurfaceCapabilitiesKHR caps;
    pGetCaps(g_phys, g_surface, &caps);
    if (caps.currentExtent.width == 0 || caps.currentExtent.height == 0)
        return 0;   // minimised: wait
    g_extent = caps.currentExtent;
    VkSwapchainCreateInfoKHR sc = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    sc.surface = g_surface;
    sc.minImageCount = caps.minImageCount + 1;
    sc.imageFormat = g_fmt;
    sc.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    sc.imageExtent = g_extent;
    sc.imageArrayLayers = 1;
    sc.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sc.preTransform = caps.currentTransform;
    sc.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sc.presentMode = VK_PRESENT_MODE_FIFO_KHR;   // vsync: the game's mode
    sc.clipped = VK_TRUE;
    if (vkCreateSwapchainKHR(g_dev, &sc, NULL, &g_swap) != VK_SUCCESS) return 0;
    vkGetSwapchainImagesKHR(g_dev, g_swap, &g_nImages, NULL);
    if (g_nImages > 8) g_nImages = 8;
    vkGetSwapchainImagesKHR(g_dev, g_swap, &g_nImages, g_images);
    for (uint32_t i = 0; i < g_nImages; ++i) {
        VkImageViewCreateInfo vci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vci.image = g_images[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = g_fmt;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.layerCount = 1;
        vkCreateImageView(g_dev, &vci, NULL, &g_views[i]);
    }
    VkImageCreateInfo dimg = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    dimg.imageType = VK_IMAGE_TYPE_2D;
    dimg.format = VK_FORMAT_D32_SFLOAT;
    dimg.extent.width = g_extent.width;
    dimg.extent.height = g_extent.height;
    dimg.extent.depth = 1;
    dimg.mipLevels = 1;
    dimg.arrayLayers = 1;
    dimg.samples = VK_SAMPLE_COUNT_1_BIT;
    dimg.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    vkCreateImage(g_dev, &dimg, NULL, &g_depthImg);
    VkMemoryRequirements dreq;
    vkGetImageMemoryRequirements(g_dev, g_depthImg, &dreq);
    VkMemoryAllocateInfo dai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    dai.allocationSize = dreq.size;
    dai.memoryTypeIndex = FindMem(dreq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(g_dev, &dai, NULL, &g_depthMem);
    vkBindImageMemory(g_dev, g_depthImg, g_depthMem, 0);
    VkImageViewCreateInfo dvci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    dvci.image = g_depthImg;
    dvci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    dvci.format = VK_FORMAT_D32_SFLOAT;
    dvci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    dvci.subresourceRange.levelCount = 1;
    dvci.subresourceRange.layerCount = 1;
    vkCreateImageView(g_dev, &dvci, NULL, &g_depthView);
    for (uint32_t i = 0; i < g_nImages; ++i) {
        VkImageView atts[2] = { g_views[i], g_depthView };
        VkFramebufferCreateInfo fci = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fci.renderPass = g_rp;
        fci.attachmentCount = 2;
        fci.pAttachments = atts;
        fci.width = g_extent.width;
        fci.height = g_extent.height;
        fci.layers = 1;
        vkCreateFramebuffer(g_dev, &fci, NULL, &g_fbs[i]);
    }
    return 1;
}

static void* readFile(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    void* buf = malloc(*size);
    fread(buf, 1, *size, f);
    fclose(f);
    return buf;
}

// Unit cube, 36 verts, pos+normal interleaved (matches box.vert binding 0).
static void FillCube(float* v) {
    const float n[6][3] = { { 0, 0, 1 }, { 0, 0, -1 }, { 1, 0, 0 },
                            { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 } };
    int k = 0;
    for (int f = 0; f < 6; ++f) {
        const float* nn = n[f];
        const float u[3] = { nn[1], nn[2], nn[0] };
        const float w[3] = { nn[1] * u[2] - nn[2] * u[1],
                             nn[2] * u[0] - nn[0] * u[2],
                             nn[0] * u[1] - nn[1] * u[0] };
        const int idx[6][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 },
                                { -1, -1 }, { 1, 1 }, { -1, 1 } };
        for (int t = 0; t < 6; ++t) {
            for (int a = 0; a < 3; ++a)
                v[k++] = 0.5f * (nn[a] + idx[t][0] * u[a] + idx[t][1] * w[a]);
            v[k++] = nn[0]; v[k++] = nn[1]; v[k++] = nn[2];
        }
    }
}

static void InstBox(Inst* out, float x, float y, float z, float sx, float sy,
                    float sz, float r, float g, float b) {
    memset(out, 0, sizeof(*out));
    out->col0[0] = sx;
    out->col1[1] = sy;
    out->col2[2] = sz;
    out->col3[0] = x; out->col3[1] = y; out->col3[2] = z; out->col3[3] = 1.0f;
    out->color[0] = r; out->color[1] = g; out->color[2] = b; out->color[3] = 1.0f;
}

int main(int argc, char** argv) {
    int wantFrames = 0;   // 0 = interactive until Esc/close
    for (int i = 1; i < argc - 1; ++i)
        if (strcmp(argv[i], "--frames") == 0) wantFrames = atoi(argv[i + 1]);
    enum { SOLDIERS = 800, INSTANCES = SOLDIERS + 1 };

    HMODULE lib = LoadLibraryA("vulkan-1.dll");
    if (!lib) { printf("vkwin: no loader\n"); return 1; }
    gipa = (PFN_vkGetInstanceProcAddr)GetProcAddress(lib, "vkGetInstanceProcAddr");

    INST_FN(NULL, vkCreateInstance);
    const char* iext[] = { "VK_KHR_surface", "VK_KHR_win32_surface" };
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "openwarband-vkwin";
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = iext;
    VkInstance inst;
    if (vkCreateInstance(&ici, NULL, &inst)) { printf("vkwin: instance FAILED\n"); return 1; }
    gdpa = (PFN_vkGetDeviceProcAddr)gipa(inst, "vkGetDeviceProcAddr");

    // ---- the native window: raw Win32, no GLFW, no raylib ----
    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "vkwin";
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, "vkwin",
                                "OpenWarband native Vulkan window (V178) - WASD+mouse fly, Esc quits",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT,
                                CW_USEDEFAULT, 1280, 720, NULL, NULL,
                                wc.hInstance, NULL);
    INST_FN(inst, vkCreateWin32SurfaceKHR);
    VkWin32SurfaceCreateInfoKHR sfci = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
    sfci.hinstance = wc.hInstance;
    sfci.hwnd = hwnd;
    if (vkCreateWin32SurfaceKHR(inst, &sfci, NULL, &g_surface)) {
        printf("vkwin: surface FAILED\n");
        return 1;
    }

    INST_FN(inst, vkEnumeratePhysicalDevices);
    INST_FN(inst, vkGetPhysicalDeviceProperties);
    INST_FN(inst, vkGetPhysicalDeviceQueueFamilyProperties);
    INST_FN(inst, vkGetPhysicalDeviceSurfaceSupportKHR);
    INST_FN(inst, vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    INST_FN(inst, vkGetPhysicalDeviceSurfaceFormatsKHR);
    INST_FN(inst, vkGetPhysicalDeviceMemoryProperties);
    INST_FN(inst, vkCreateDevice);
    pGetCaps = vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
    uint32_t nd = 0;
    vkEnumeratePhysicalDevices(inst, &nd, NULL);
    VkPhysicalDevice devs[8];
    if (nd > 8) nd = 8;
    vkEnumeratePhysicalDevices(inst, &nd, devs);
    VkPhysicalDeviceProperties props;
    uint32_t qfam = 0;
    for (int pass = 0; pass < 2 && !g_phys; ++pass)
        for (uint32_t i = 0; i < nd && !g_phys; ++i) {
            VkPhysicalDeviceProperties p;
            vkGetPhysicalDeviceProperties(devs[i], &p);
            if (pass == 0 && p.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                continue;
            uint32_t nq = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &nq, NULL);
            VkQueueFamilyProperties qf[16];
            if (nq > 16) nq = 16;
            vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &nq, qf);
            for (uint32_t q = 0; q < nq; ++q) {
                VkBool32 pres = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(devs[i], q, g_surface, &pres);
                if ((qf[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && pres) {
                    g_phys = devs[i]; props = p; qfam = q; break;
                }
            }
        }
    if (!g_phys) { printf("vkwin: no GPU\n"); return 1; }
    vkGetPhysicalDeviceMemoryProperties(g_phys, &g_memp);

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
    if (vkCreateDevice(g_phys, &dci, NULL, &g_dev)) { printf("vkwin: device FAILED\n"); return 1; }

    DEV_LOAD(g_dev, vkCreateSwapchainKHR);
    DEV_LOAD(g_dev, vkDestroySwapchainKHR);
    DEV_LOAD(g_dev, vkGetSwapchainImagesKHR);
    DEV_LOAD(g_dev, vkCreateImageView);
    DEV_LOAD(g_dev, vkDestroyImageView);
    DEV_LOAD(g_dev, vkCreateImage);
    DEV_LOAD(g_dev, vkDestroyImage);
    DEV_LOAD(g_dev, vkGetImageMemoryRequirements);
    DEV_LOAD(g_dev, vkAllocateMemory);
    DEV_LOAD(g_dev, vkFreeMemory);
    DEV_LOAD(g_dev, vkBindImageMemory);
    DEV_LOAD(g_dev, vkCreateFramebuffer);
    DEV_LOAD(g_dev, vkDestroyFramebuffer);
    DEV_LOAD(g_dev, vkDeviceWaitIdle);
    PFN_vkGetDeviceQueue vkGetDeviceQueue = (PFN_vkGetDeviceQueue)gdpa(g_dev, "vkGetDeviceQueue");
    PFN_vkCreateBuffer vkCreateBuffer = (PFN_vkCreateBuffer)gdpa(g_dev, "vkCreateBuffer");
    PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements =
        (PFN_vkGetBufferMemoryRequirements)gdpa(g_dev, "vkGetBufferMemoryRequirements");
    PFN_vkBindBufferMemory vkBindBufferMemory = (PFN_vkBindBufferMemory)gdpa(g_dev, "vkBindBufferMemory");
    PFN_vkMapMemory vkMapMemory = (PFN_vkMapMemory)gdpa(g_dev, "vkMapMemory");
    PFN_vkCreateRenderPass vkCreateRenderPass = (PFN_vkCreateRenderPass)gdpa(g_dev, "vkCreateRenderPass");
    PFN_vkCreateShaderModule vkCreateShaderModule = (PFN_vkCreateShaderModule)gdpa(g_dev, "vkCreateShaderModule");
    PFN_vkCreatePipelineLayout vkCreatePipelineLayout = (PFN_vkCreatePipelineLayout)gdpa(g_dev, "vkCreatePipelineLayout");
    PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelines = (PFN_vkCreateGraphicsPipelines)gdpa(g_dev, "vkCreateGraphicsPipelines");
    PFN_vkCreateCommandPool vkCreateCommandPool = (PFN_vkCreateCommandPool)gdpa(g_dev, "vkCreateCommandPool");
    PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers = (PFN_vkAllocateCommandBuffers)gdpa(g_dev, "vkAllocateCommandBuffers");
    PFN_vkBeginCommandBuffer vkBeginCommandBuffer = (PFN_vkBeginCommandBuffer)gdpa(g_dev, "vkBeginCommandBuffer");
    PFN_vkEndCommandBuffer vkEndCommandBuffer = (PFN_vkEndCommandBuffer)gdpa(g_dev, "vkEndCommandBuffer");
    PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass = (PFN_vkCmdBeginRenderPass)gdpa(g_dev, "vkCmdBeginRenderPass");
    PFN_vkCmdEndRenderPass vkCmdEndRenderPass = (PFN_vkCmdEndRenderPass)gdpa(g_dev, "vkCmdEndRenderPass");
    PFN_vkCmdBindPipeline vkCmdBindPipeline = (PFN_vkCmdBindPipeline)gdpa(g_dev, "vkCmdBindPipeline");
    PFN_vkCmdBindVertexBuffers vkCmdBindVertexBuffers = (PFN_vkCmdBindVertexBuffers)gdpa(g_dev, "vkCmdBindVertexBuffers");
    PFN_vkCmdPushConstants vkCmdPushConstants = (PFN_vkCmdPushConstants)gdpa(g_dev, "vkCmdPushConstants");
    PFN_vkCmdDraw vkCmdDraw = (PFN_vkCmdDraw)gdpa(g_dev, "vkCmdDraw");
    PFN_vkCmdSetViewport vkCmdSetViewport = (PFN_vkCmdSetViewport)gdpa(g_dev, "vkCmdSetViewport");
    PFN_vkCmdSetScissor vkCmdSetScissor = (PFN_vkCmdSetScissor)gdpa(g_dev, "vkCmdSetScissor");
    PFN_vkCreateSemaphore vkCreateSemaphore = (PFN_vkCreateSemaphore)gdpa(g_dev, "vkCreateSemaphore");
    PFN_vkCreateFence vkCreateFence = (PFN_vkCreateFence)gdpa(g_dev, "vkCreateFence");
    PFN_vkWaitForFences vkWaitForFences = (PFN_vkWaitForFences)gdpa(g_dev, "vkWaitForFences");
    PFN_vkResetFences vkResetFences = (PFN_vkResetFences)gdpa(g_dev, "vkResetFences");
    PFN_vkResetCommandBuffer vkResetCommandBuffer = (PFN_vkResetCommandBuffer)gdpa(g_dev, "vkResetCommandBuffer");
    PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR = (PFN_vkAcquireNextImageKHR)gdpa(g_dev, "vkAcquireNextImageKHR");
    PFN_vkQueueSubmit vkQueueSubmit = (PFN_vkQueueSubmit)gdpa(g_dev, "vkQueueSubmit");
    PFN_vkQueuePresentKHR vkQueuePresentKHR = (PFN_vkQueuePresentKHR)gdpa(g_dev, "vkQueuePresentKHR");
    VkQueue queue;
    vkGetDeviceQueue(g_dev, qfam, 0, &queue);

    // ---- surface format + render pass (colour clear->present, depth) ----
    uint32_t nf = 1;
    VkSurfaceFormatKHR fmt;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_phys, g_surface, &nf, &fmt);
    g_fmt = fmt.format;
    {
        VkAttachmentDescription at[2] = { 0 };
        at[0].format = g_fmt;
        at[0].samples = VK_SAMPLE_COUNT_1_BIT;
        at[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        at[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        at[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        at[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        at[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        at[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        at[1].format = VK_FORMAT_D32_SFLOAT;
        at[1].samples = VK_SAMPLE_COUNT_1_BIT;
        at[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        at[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        at[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        at[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        at[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        at[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference colRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference depRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
        VkSubpassDescription sub = { 0 };
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &colRef;
        sub.pDepthStencilAttachment = &depRef;
        VkRenderPassCreateInfo rpci = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        rpci.attachmentCount = 2;
        rpci.pAttachments = at;
        rpci.subpassCount = 1;
        rpci.pSubpasses = &sub;
        vkCreateRenderPass(g_dev, &rpci, NULL, &g_rp);
    }
    if (!CreateSwapchainObjects()) { printf("vkwin: swapchain FAILED\n"); return 1; }

    // ---- pipeline: the proven instanced-box stage, dynamic viewport ----
    size_t vsSize, fsSize;
    void* vsCode = readFile("assets/spv/box.vert.spv", &vsSize);
    void* fsCode = readFile("assets/spv/box.frag.spv", &fsSize);
    if (!vsCode || !fsCode) { printf("vkwin: missing assets/spv/box.*.spv (run from repo root)\n"); return 1; }
    VkShaderModuleCreateInfo smci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    smci.codeSize = vsSize;
    smci.pCode = (const uint32_t*)vsCode;
    VkShaderModule vs, fs;
    vkCreateShaderModule(g_dev, &smci, NULL, &vs);
    smci.codeSize = fsSize;
    smci.pCode = (const uint32_t*)fsCode;
    vkCreateShaderModule(g_dev, &smci, NULL, &fs);

    VkPushConstantRange pcr = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 80 };
    VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    VkPipelineLayout layout;
    vkCreatePipelineLayout(g_dev, &plci, NULL, &layout);

    VkPipelineShaderStageCreateInfo stages[2] = { { 0 }, { 0 } };
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";
    VkVertexInputBindingDescription binds[2] = {
        { 0, 6 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX },
        { 1, sizeof(Inst),      VK_VERTEX_INPUT_RATE_INSTANCE },
    };
    VkVertexInputAttributeDescription attrs[7] = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12 },
        { 2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0 },
        { 3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 16 },
        { 4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 32 },
        { 5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 48 },
        { 6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 64 },
    };
    VkPipelineVertexInputStateCreateInfo vin = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vin.vertexBindingDescriptionCount = 2;
    vin.pVertexBindingDescriptions = binds;
    vin.vertexAttributeDescriptionCount = 7;
    vin.pVertexAttributeDescriptions = attrs;
    VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vps = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vps.viewportCount = 1;
    vps.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState cba = { 0 };
    cba.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dsci = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dsci.dynamicStateCount = 2;
    dsci.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo gpci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    gpci.stageCount = 2;
    gpci.pStages = stages;
    gpci.pVertexInputState = &vin;
    gpci.pInputAssemblyState = &ia;
    gpci.pViewportState = &vps;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState = &ms;
    gpci.pDepthStencilState = &ds;
    gpci.pColorBlendState = &cb;
    gpci.pDynamicState = &dsci;
    gpci.layout = layout;
    gpci.renderPass = g_rp;
    VkPipeline pipe;
    if (vkCreateGraphicsPipelines(g_dev, NULL, 1, &gpci, NULL, &pipe)) {
        printf("vkwin: pipeline FAILED\n");
        return 1;
    }

    // ---- geometry: cube + instances (ground slab + marching army) ----
    float cube[36 * 6];
    FillCube(cube);
    VkBuffer vbuf, ibuf;
    VkDeviceMemory vmem, imem;
    void* imap = NULL;
    {
        VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bci.size = sizeof(cube);
        bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        vkCreateBuffer(g_dev, &bci, NULL, &vbuf);
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(g_dev, vbuf, &req);
        VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = FindMem(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkAllocateMemory(g_dev, &mai, NULL, &vmem);
        vkBindBufferMemory(g_dev, vbuf, vmem, 0);
        void* map;
        vkMapMemory(g_dev, vmem, 0, sizeof(cube), 0, &map);
        memcpy(map, cube, sizeof(cube));

        bci.size = sizeof(Inst) * INSTANCES;
        vkCreateBuffer(g_dev, &bci, NULL, &ibuf);
        vkGetBufferMemoryRequirements(g_dev, ibuf, &req);
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = FindMem(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkAllocateMemory(g_dev, &mai, NULL, &imem);
        vkBindBufferMemory(g_dev, ibuf, imem, 0);
        vkMapMemory(g_dev, imem, 0, sizeof(Inst) * INSTANCES, 0, &imap);
    }

    VkCommandPoolCreateInfo cpci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = qfam;
    VkCommandPool pool;
    vkCreateCommandPool(g_dev, &cpci, NULL, &pool);
    VkCommandBufferAllocateInfo cbai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cbai.commandPool = pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(g_dev, &cbai, &cmd);
    VkSemaphoreCreateInfo semci = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkSemaphore semAcquire, semRender;
    vkCreateSemaphore(g_dev, &semci, NULL, &semAcquire);
    vkCreateSemaphore(g_dev, &semci, NULL, &semRender);
    VkFenceCreateInfo fenci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fenci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkFence fence;
    vkCreateFence(g_dev, &fenci, NULL, &fence);

    // ---- the fly camera the input drives ----
    float camX = 0.0f, camY = 6.0f, camZ = -26.0f;
    float yaw = 0.0f, pitch = -0.15f;   // yaw 0 = looking +z
    float speed = 12.0f;

    LARGE_INTEGER qpf, t0, tPrev;
    QueryPerformanceFrequency(&qpf);
    QueryPerformanceCounter(&t0);
    tPrev = t0;
    int frames = 0;

    while (!g_wantClose && (wantFrames <= 0 || frames < wantFrames)) {
        memset(g_in.keyPressed, 0, sizeof(g_in.keyPressed));
        g_in.lmbPressed = g_in.rmbPressed = 0;
        g_in.wheel = 0.0f;
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        const int focus = GetForegroundWindow() == hwnd;
        PumpMouseDelta(hwnd, focus && wantFrames <= 0);   // interactive only

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        const float dt = (float)((double)(now.QuadPart - tPrev.QuadPart) / qpf.QuadPart);
        tPrev = now;

        // ---- drive the camera from the captured input (the proof) ----
        yaw   += g_in.mouseDx * 0.0032f;
        pitch -= g_in.mouseDy * 0.0032f;
        if (pitch > 1.5f) pitch = 1.5f;
        if (pitch < -1.5f) pitch = -1.5f;
        speed *= 1.0f + g_in.wheel * 0.15f;
        if (speed < 1.0f) speed = 1.0f;
        if (speed > 80.0f) speed = 80.0f;
        const float fx = sinf(yaw) * cosf(pitch), fy = sinf(pitch),
                    fz = cosf(yaw) * cosf(pitch);
        const float rx = cosf(yaw), rz = -sinf(yaw);
        float mvF = (g_in.key['W'] ? 1.0f : 0.0f) - (g_in.key['S'] ? 1.0f : 0.0f);
        float mvR = (g_in.key['D'] ? 1.0f : 0.0f) - (g_in.key['A'] ? 1.0f : 0.0f);
        if (g_in.lmb) mvF += 1.0f;   // LMB doubles as "forward" so buttons prove too
        camX += (fx * mvF + rx * mvR) * speed * dt;
        camY += fy * mvF * speed * dt + ((g_in.key[VK_SPACE] ? 1.0f : 0.0f) -
                                         (g_in.key[VK_SHIFT] ? 1.0f : 0.0f)) * speed * dt;
        camZ += (fz * mvF + rz * mvR) * speed * dt;

        // ---- swapchain health ----
        if (g_resized) {
            g_resized = 0;
            DestroySwapchainObjects();
            if (!CreateSwapchainObjects()) continue;   // minimised
        }
        if (!g_swap) {
            if (!CreateSwapchainObjects()) continue;
        }

        // ---- march the army ----
        const float t = (float)((double)(now.QuadPart - t0.QuadPart) / qpf.QuadPart);
        Inst* inst = (Inst*)imap;
        InstBox(&inst[0], 0, -0.5f, 0, 120, 1, 120, 0.30f, 0.38f, 0.24f);   // ground
        for (int i = 0; i < SOLDIERS; ++i) {
            const int col = i % 40, row = i / 40;
            const int team = row % 2;
            const float phase = t * 2.0f + i * 0.37f;
            InstBox(&inst[1 + i],
                    (col - 19.5f) * 1.6f,
                    0.9f + fabsf(sinf(phase)) * 0.22f,
                    (row - 10.0f) * 2.2f + sinf(t * 0.6f) * 3.0f,
                    0.62f, 1.8f, 0.5f,
                    team ? 0.75f : 0.28f, 0.30f, team ? 0.25f : 0.70f);
        }

        vkWaitForFences(g_dev, 1, &fence, VK_TRUE, UINT64_MAX);
        uint32_t idx = 0;
        VkResult ar = vkAcquireNextImageKHR(g_dev, g_swap, UINT64_MAX, semAcquire,
                                            NULL, &idx);
        if (ar == VK_ERROR_OUT_OF_DATE_KHR) { g_resized = 1; continue; }
        if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) break;
        vkResetFences(g_dev, 1, &fence);

        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        vkBeginCommandBuffer(cmd, &bi);
        VkClearValue clears[2];
        clears[0].color.float32[0] = 0.36f;
        clears[0].color.float32[1] = 0.58f;
        clears[0].color.float32[2] = 0.84f;   // day sky
        clears[0].color.float32[3] = 1.0f;
        clears[1].depthStencil.depth = 1.0f;
        clears[1].depthStencil.stencil = 0;
        VkRenderPassBeginInfo rbi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        rbi.renderPass = g_rp;
        rbi.framebuffer = g_fbs[idx];
        rbi.renderArea.extent = g_extent;
        rbi.clearValueCount = 2;
        rbi.pClearValues = clears;
        vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        VkViewport vp = { 0, 0, (float)g_extent.width, (float)g_extent.height, 0, 1 };
        VkRect2D scis = { { 0, 0 }, g_extent };
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &scis);
        VkBuffer bufs[2] = { vbuf, ibuf };
        VkDeviceSize offs[2] = { 0, 0 };
        vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offs);
        const M4 proj = m4persp(1.05f, (float)g_extent.width / (float)g_extent.height,
                                0.1f, 500.0f);
        const M4 view = m4look(camX, camY, camZ, camX + fx, camY + fy, camZ + fz);
        const M4 vpm = m4mul(proj, view);
        float pc[20];
        memcpy(pc, vpm.m, 64);
        pc[16] = -0.45f; pc[17] = -0.75f; pc[18] = -0.35f; pc[19] = 0.0f;   // sun
        vkCmdPushConstants(cmd, layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, 80, pc);
        vkCmdDraw(cmd, 36, INSTANCES, 0, 0);
        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
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
        pi.pSwapchains = &g_swap;
        pi.pImageIndices = &idx;
        VkResult pr = vkQueuePresentKHR(queue, &pi);
        if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) g_resized = 1;
        else if (pr != VK_SUCCESS) break;
        frames++;
    }

    vkDeviceWaitIdle(g_dev);
    LARGE_INTEGER tEnd;
    QueryPerformanceCounter(&tEnd);
    const double secs = (double)(tEnd.QuadPart - t0.QuadPart) / qpf.QuadPart;
    const double avg = frames ? secs * 1000.0 / frames : 0.0;
    printf("vkwin: %d frames, avg %.2f ms (%.0f fps), %ld key events, "
           "%.0f px mouse travel on %s\n",
           frames, avg, avg > 0 ? 1000.0 / avg : 0.0, g_keyEvents,
           g_mouseTravel, props.deviceName);
    return frames > 0 ? 0 : 1;
}
