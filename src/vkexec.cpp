#include <string.h>
#include <stdlib.h>
#include <math.h>

// raylib.h cannot coexist with windows.h (CloseWindow/ShowCursor/Rectangle
// clash), so the few raylib calls used here are declared by hand.
extern "C" bool IsWindowReady(void);
extern "C" void TraceLog(int logLevel, const char* text, ...);
enum { RL_LOG_INFO = 3, RL_LOG_WARNING = 4 };

// ---------------------------------------------------------------------------
// The in-game Vulkan executor (V162 boot, V163 frame executor). When
// settings.cfg says `renderer vulkan`, the GAME PROCESS brings a Vulkan
// device up at first flush, then renders the frame's recorded instance
// buckets on it: offscreen R8G8B8A8+D32 target, the proven vkarmy box
// pipeline (assets/spv/box.*.spv), one instanced draw per frame, readback
// to host memory. rdr.cpp composites the result over the GL scene â€” the
// present-interop stage of RENDERER.md; the native window swap retires the
// composite. Headless runs and `renderer raylib` never touch any of this.
// ---------------------------------------------------------------------------
#if defined(_WIN32)
#define VK_NO_PROTOTYPES
#include <windows.h>
#include <vulkan/vulkan.h>
#include <stdio.h>

namespace rdr {

namespace {

#define IFN(name) PFN_##name name = (PFN_##name)g_gipa(g_vk.inst, #name)
#define DFN(name) PFN_##name name = (PFN_##name)g_gdpa(g_vk.dev, #name)

PFN_vkGetInstanceProcAddr g_gipa = nullptr;
PFN_vkGetDeviceProcAddr g_gdpa = nullptr;

struct VkExec {
    bool tried = false, live = false, frameReady = false, frameFailed = false;
    char gpuName[256] = { 0 };
    VkInstance inst = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice dev = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t qfam = 0;
    VkPhysicalDeviceMemoryProperties memProps{};
    // frame resources
    int width = 0, height = 0;
    VkImage color = VK_NULL_HANDLE, depth = VK_NULL_HANDLE;
    VkDeviceMemory colorMem = VK_NULL_HANDLE, depthMem = VK_NULL_HANDLE;
    VkImageView colorView = VK_NULL_HANDLE, depthView = VK_NULL_HANDLE;
    VkRenderPass rp = VK_NULL_HANDLE;
    VkFramebuffer fb = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipe = VK_NULL_HANDLE;
    VkBuffer cubeBuf = VK_NULL_HANDLE;
    // V179: the pill primitive - a unit sphere drawn with the same
    // pipelines, fed by its own 2-slot instance buffers.
    VkBuffer sphereBuf = VK_NULL_HANDLE;
    VkDeviceMemory sphereMem = VK_NULL_HANDLE;
    int sphereVerts = 0;
    VkBuffer pillBuf[2] = {};
    VkDeviceMemory pillMem[2] = {};
    void* pillMap[2] = {};
    int pillCap[2] = {};
    // V185: the capsule shaft - a unit centred cylinder with its own
    // pipelined instance streams (plain + skinned).
    VkBuffer cylBuf = VK_NULL_HANDLE;
    VkDeviceMemory cylMem = VK_NULL_HANDLE;
    int cylVerts = 0;
    VkBuffer cylInst[2] = {}, skinCylBuf[2] = {};
    VkDeviceMemory cylInstMem[2] = {}, skinCylMem[2] = {};
    void* cylInstMap[2] = {};
    void* skinCylMap[2] = {};
    int cylInstCap[2] = {}, skinCylCap[2] = {};
    VkDeviceMemory cubeMem = VK_NULL_HANDLE;
    // Pipelined executor (V166): two in-flight slots. The CPU fills slot N
    // and presents slot N-1, so the fence wait lands on work the GPU
    // finished a frame ago instead of stalling on the frame just submitted.
    VkBuffer instBuf[2] = {}, readBuf[2] = {};
    VkDeviceMemory instMem[2] = {}, readMem[2] = {};
    void* instMap[2] = {};
    void* readMap[2] = {};
    int instCap[2] = {};
    VkCommandBuffer cmd[2] = {};
    VkFence fence[2] = {};
    bool submitted[2] = {};
    int frame = 0;
    VkCommandPool pool = VK_NULL_HANDLE;
    // terrain occluder (V164): depth-only static mesh
    VkPipeline meshPipe = VK_NULL_HANDLE;
    VkBuffer terrBuf = VK_NULL_HANDLE;
    VkDeviceMemory terrMem = VK_NULL_HANDLE;
    int terrCount = 0;
    float* terrPending = nullptr;      // staged before the device is up
    int terrPendingCount = 0;
    // HUD/text layer (V173)
    VkPipeline uiPipe = VK_NULL_HANDLE;
    VkPipelineLayout uiLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout uiDsl = VK_NULL_HANDLE;
    VkDescriptorSet uiDset = VK_NULL_HANDLE;
    VkImage atlasImg = VK_NULL_HANDLE;
    VkImageView atlasView = VK_NULL_HANDLE;
    VkSampler atlasSamp = VK_NULL_HANDLE;
    bool atlasSet = false, uiFailed = false;
    VkBuffer uiBuf = VK_NULL_HANDLE, uiRead = VK_NULL_HANDLE;
    VkDeviceMemory uiMem = VK_NULL_HANDLE, uiReadMem = VK_NULL_HANDLE;
    void* uiMap = nullptr;
    void* uiReadMap = nullptr;
    int uiCap = 0, uiReadSize = 0;
    VkCommandBuffer uiCmd = VK_NULL_HANDLE;
    VkFence uiFence = VK_NULL_HANDLE;
    // textured UI quads (V177)
    VkPipeline imgPipe = VK_NULL_HANDLE;
    VkDescriptorPool texPool = VK_NULL_HANDLE;
    int texCount = 0;
    VkDescriptorSet texSet[16] = {};
    // V178: kept mapped for dynamic textures (per-frame re-upload)
    unsigned char* texMap[16] = {};
    size_t texPitch[16] = {};
    int texW[16] = {}, texH[16] = {};
    // Sun-shadow pass (V178): 2048 D32 map + depth-only pipelines + a
    // shadow-sampling main pipeline. shReady gates the whole feature; when
    // the shaders are missing the legacy no-shadow pipe keeps rendering.
    bool shTried = false, shReady = false;
    VkImage shImg = VK_NULL_HANDLE;
    VkDeviceMemory shMem = VK_NULL_HANDLE;
    VkImageView shView = VK_NULL_HANDLE;
    VkSampler shSamp = VK_NULL_HANDLE;
    VkRenderPass shRp = VK_NULL_HANDLE;
    VkFramebuffer shFb = VK_NULL_HANDLE;
    VkPipelineLayout shLayout = VK_NULL_HANDLE;    // 64B lightVP push
    VkPipeline shBoxPipe = VK_NULL_HANDLE, shMeshPipe = VK_NULL_HANDLE;
    VkDescriptorSetLayout shDsl = VK_NULL_HANDLE;
    VkDescriptorSet shSet = VK_NULL_HANDLE;
    VkPipelineLayout litLayout = VK_NULL_HANDLE;   // 144B push + shadow map
    VkPipeline litPipe = VK_NULL_HANDLE;
    // Armour-as-texture (V180): the 256x1024 skin atlas (4 rows: cloth,
    // leather, mail, plate), a 160B-push pipeline sampling shadow map +
    // atlas, and 2-slot instance buffers for the skinned box/pill streams.
    // skinReady gates it all; on any failure the skinned streams draw
    // through the plain lit pipeline (untextured) rather than vanishing.
    unsigned char* skinPending = nullptr;          // staged before device up
    int skinPendW = 0, skinPendH = 0;
    bool skinTried = false, skinReady = false;
    VkImage skinImg = VK_NULL_HANDLE;
    VkDeviceMemory skinMem = VK_NULL_HANDLE;
    VkImageView skinView = VK_NULL_HANDLE;
    VkDescriptorSetLayout skinDsl = VK_NULL_HANDLE;
    VkDescriptorSet skinSet = VK_NULL_HANDLE;
    VkPipelineLayout skinLayout = VK_NULL_HANDLE;  // 160B push
    VkPipeline skinPipe = VK_NULL_HANDLE;
    VkBuffer skinBoxBuf[2] = {}, skinPillBuf[2] = {};
    VkDeviceMemory skinBoxMem[2] = {}, skinPillMem[2] = {};
    void* skinBoxMap[2] = {};
    void* skinPillMap[2] = {};
    int skinBoxCap[2] = {}, skinPillCap[2] = {};
};
VkExec g_vk;

void BootDevice() {
    g_vk.tried = true;
    HMODULE lib = LoadLibraryA("vulkan-1.dll");
    if (!lib) return;
    g_gipa = (PFN_vkGetInstanceProcAddr)GetProcAddress(lib, "vkGetInstanceProcAddr");
    auto createInstance = (PFN_vkCreateInstance)g_gipa(NULL, "vkCreateInstance");
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "openwarband";
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &app;
    if (createInstance(&ici, NULL, &g_vk.inst) != VK_SUCCESS) return;

    IFN(vkEnumeratePhysicalDevices);
    IFN(vkGetPhysicalDeviceProperties);
    IFN(vkGetPhysicalDeviceQueueFamilyProperties);
    IFN(vkGetPhysicalDeviceMemoryProperties);
    IFN(vkCreateDevice);
    IFN(vkGetDeviceProcAddr);
    g_gdpa = vkGetDeviceProcAddr;
    uint32_t nd = 0;
    vkEnumeratePhysicalDevices(g_vk.inst, &nd, NULL);
    VkPhysicalDevice devs[8];
    if (nd > 8) nd = 8;
    vkEnumeratePhysicalDevices(g_vk.inst, &nd, devs);
    VkPhysicalDeviceProperties props{};
    for (int pass = 0; pass < 2 && !g_vk.phys; ++pass)
        for (uint32_t i = 0; i < nd && !g_vk.phys; ++i) {
            VkPhysicalDeviceProperties p;
            vkGetPhysicalDeviceProperties(devs[i], &p);
            if (pass == 0 && p.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                continue;
            uint32_t nq = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &nq, NULL);
            VkQueueFamilyProperties qf[16];
            if (nq > 16) nq = 16;
            vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &nq, qf);
            for (uint32_t q = 0; q < nq; ++q)
                if (qf[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                    g_vk.phys = devs[i];
                    props = p;
                    g_vk.qfam = q;
                    break;
                }
        }
    if (!g_vk.phys) return;
    vkGetPhysicalDeviceMemoryProperties(g_vk.phys, &g_vk.memProps);
    const float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qci.queueFamilyIndex = g_vk.qfam;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    if (vkCreateDevice(g_vk.phys, &dci, NULL, &g_vk.dev) != VK_SUCCESS) return;
    DFN(vkGetDeviceQueue);
    vkGetDeviceQueue(g_vk.dev, g_vk.qfam, 0, &g_vk.queue);
    strncpy(g_vk.gpuName, props.deviceName, sizeof(g_vk.gpuName) - 1);
    g_vk.live = true;
}

uint32_t MemType(uint32_t bits, VkMemoryPropertyFlags want) {
    for (uint32_t i = 0; i < g_vk.memProps.memoryTypeCount; ++i)
        if ((bits & (1u << i)) &&
            (g_vk.memProps.memoryTypes[i].propertyFlags & want) == want)
            return i;
    return 0;
}

bool MakeBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                VkMemoryPropertyFlags memFlags, VkBuffer* buf,
                VkDeviceMemory* mem, void** map) {
    DFN(vkCreateBuffer); DFN(vkGetBufferMemoryRequirements);
    DFN(vkAllocateMemory); DFN(vkBindBufferMemory); DFN(vkMapMemory);
    VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bci.size = size;
    bci.usage = usage;
    if (vkCreateBuffer(g_vk.dev, &bci, NULL, buf) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(g_vk.dev, *buf, &req);
    VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = MemType(req.memoryTypeBits, memFlags);
    if (vkAllocateMemory(g_vk.dev, &mai, NULL, mem) != VK_SUCCESS) return false;
    vkBindBufferMemory(g_vk.dev, *buf, *mem, 0);
    if (map) vkMapMemory(g_vk.dev, *mem, 0, size, 0, map);
    return true;
}

bool MakeImage(int w, int h, VkFormat fmt, VkImageUsageFlags usage,
               VkImageAspectFlags aspect, VkImage* img, VkDeviceMemory* mem,
               VkImageView* view) {
    DFN(vkCreateImage); DFN(vkGetImageMemoryRequirements);
    DFN(vkAllocateMemory); DFN(vkBindImageMemory); DFN(vkCreateImageView);
    VkImageCreateInfo ici = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = fmt;
    ici.extent = { (uint32_t)w, (uint32_t)h, 1 };
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.usage = usage;
    if (vkCreateImage(g_vk.dev, &ici, NULL, img) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(g_vk.dev, *img, &req);
    VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = MemType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(g_vk.dev, &mai, NULL, mem) != VK_SUCCESS) return false;
    vkBindImageMemory(g_vk.dev, *img, *mem, 0);
    VkImageViewCreateInfo vci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vci.image = *img;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = fmt;
    vci.subresourceRange = { aspect, 0, 1, 0, 1 };
    return vkCreateImageView(g_vk.dev, &vci, NULL, view) == VK_SUCCESS;
}

void* ReadSpv(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    void* data = malloc(*size);
    fread(data, 1, *size, f);
    fclose(f);
    return data;
}

// Unit cube centred on the origin, 36 verts, pos+normal interleaved.
int FillSphere(float** out);   // defined with the buffer helpers below
int FillCylinder(float** out); // V185: capsule shaft mesh

void FillCube(float* v) {
    const float n[6][3] = { { 0, 0, 1 }, { 0, 0, -1 }, { 1, 0, 0 },
                            { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 } };
    int k = 0;
    for (int f = 0; f < 6; ++f) {
        const float* nn = n[f];
        const float u[3] = { nn[1], nn[2], nn[0] };   // any perpendicular
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

bool BuildFrameResources(int w, int h) {
    DFN(vkCreateRenderPass); DFN(vkCreateFramebuffer); DFN(vkCreateShaderModule);
    DFN(vkCreatePipelineLayout); DFN(vkCreateGraphicsPipelines);
    DFN(vkCreateCommandPool); DFN(vkAllocateCommandBuffers); DFN(vkCreateFence);
    DFN(vkDestroyImage); DFN(vkDestroyImageView); DFN(vkFreeMemory);
    DFN(vkDestroyFramebuffer); DFN(vkDestroyBuffer); DFN(vkUnmapMemory);

    if (g_vk.fb) {                       // resize: drop the old target
        vkDestroyFramebuffer(g_vk.dev, g_vk.fb, NULL);
        vkDestroyImageView(g_vk.dev, g_vk.colorView, NULL);
        vkDestroyImageView(g_vk.dev, g_vk.depthView, NULL);
        vkDestroyImage(g_vk.dev, g_vk.color, NULL);
        vkDestroyImage(g_vk.dev, g_vk.depth, NULL);
        vkFreeMemory(g_vk.dev, g_vk.colorMem, NULL);
        vkFreeMemory(g_vk.dev, g_vk.depthMem, NULL);
        for (int i = 0; i < 2; ++i) {
            vkUnmapMemory(g_vk.dev, g_vk.readMem[i]);
            vkDestroyBuffer(g_vk.dev, g_vk.readBuf[i], NULL);
            vkFreeMemory(g_vk.dev, g_vk.readMem[i], NULL);
            g_vk.submitted[i] = false;
        }
        g_vk.fb = VK_NULL_HANDLE;
    }

    if (!MakeImage(w, h, VK_FORMAT_R8G8B8A8_UNORM,
                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                   VK_IMAGE_ASPECT_COLOR_BIT, &g_vk.color, &g_vk.colorMem, &g_vk.colorView))
        return false;
    if (!MakeImage(w, h, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                   VK_IMAGE_ASPECT_DEPTH_BIT, &g_vk.depth, &g_vk.depthMem, &g_vk.depthView))
        return false;
    // Readback memory MUST be host-cached: the GL upload reads every byte
    // back on the CPU, and uncached (write-combined) reads of a 3.6MB frame
    // cost tens of ms. Fall back to plain coherent if no cached type exists.
    VkMemoryPropertyFlags readFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                      VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    bool haveCached = false;
    for (uint32_t i = 0; i < g_vk.memProps.memoryTypeCount; ++i)
        if ((g_vk.memProps.memoryTypes[i].propertyFlags & readFlags) == readFlags)
            haveCached = true;
    if (!haveCached)
        readFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (int i = 0; i < 2; ++i)
        if (!MakeBuffer((VkDeviceSize)w * h * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, readFlags,
                        &g_vk.readBuf[i], &g_vk.readMem[i], &g_vk.readMap[i]))
            return false;

    if (!g_vk.rp) {
        VkAttachmentDescription at[2] = {};
        at[0].format = VK_FORMAT_R8G8B8A8_UNORM;
        at[0].samples = VK_SAMPLE_COUNT_1_BIT;
        at[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        at[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        at[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        at[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        at[1].format = VK_FORMAT_D32_SFLOAT;
        at[1].samples = VK_SAMPLE_COUNT_1_BIT;
        at[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        at[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        at[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        at[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference cr = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference dr = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
        VkSubpassDescription sp = {};
        sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sp.colorAttachmentCount = 1;
        sp.pColorAttachments = &cr;
        sp.pDepthStencilAttachment = &dr;
        VkRenderPassCreateInfo rpci = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        rpci.attachmentCount = 2;
        rpci.pAttachments = at;
        rpci.subpassCount = 1;
        rpci.pSubpasses = &sp;
        if (vkCreateRenderPass(g_vk.dev, &rpci, NULL, &g_vk.rp) != VK_SUCCESS)
            return false;
    }

    VkImageView views[2] = { g_vk.colorView, g_vk.depthView };
    VkFramebufferCreateInfo fci = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    fci.renderPass = g_vk.rp;
    fci.attachmentCount = 2;
    fci.pAttachments = views;
    fci.width = (uint32_t)w;
    fci.height = (uint32_t)h;
    fci.layers = 1;
    if (vkCreateFramebuffer(g_vk.dev, &fci, NULL, &g_vk.fb) != VK_SUCCESS)
        return false;

    if (!g_vk.pipe) {
        size_t vsSize, fsSize;
        void* vsCode = ReadSpv("assets/spv/box.vert.spv", &vsSize);
        void* fsCode = ReadSpv("assets/spv/box.frag.spv", &fsSize);
        if (!vsCode || !fsCode) {
            TraceLog(RL_LOG_WARNING, "rdr: vulkan executor missing assets/spv/box.*.spv");
            return false;
        }
        VkShaderModuleCreateInfo smci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        smci.codeSize = vsSize;
        smci.pCode = (const uint32_t*)vsCode;
        VkShaderModule vs, fs;
        vkCreateShaderModule(g_vk.dev, &smci, NULL, &vs);
        smci.codeSize = fsSize;
        smci.pCode = (const uint32_t*)fsCode;
        vkCreateShaderModule(g_vk.dev, &smci, NULL, &fs);
        free(vsCode); free(fsCode);

        VkPushConstantRange pcr = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 80 };
        VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges = &pcr;
        vkCreatePipelineLayout(g_vk.dev, &plci, NULL, &g_vk.layout);

        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs;
        stages[0].pName = "main";
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fs;
        stages[1].pName = "main";
        VkVertexInputBindingDescription binds[2] = {
            { 0, 6 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX },
            { 1, 80,                VK_VERTEX_INPUT_RATE_INSTANCE },
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
        rs.cullMode = VK_CULL_MODE_NONE;   // negative-viewport GL parity; cull later
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo dst = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        dst.depthTestEnable = VK_TRUE;
        dst.depthWriteEnable = VK_TRUE;
        dst.depthCompareOp = VK_COMPARE_OP_LESS;
        VkPipelineColorBlendAttachmentState cba = {};
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
        gpci.pDepthStencilState = &dst;
        gpci.pColorBlendState = &cb;
        gpci.pDynamicState = &dsci;
        gpci.layout = g_vk.layout;
        gpci.renderPass = g_vk.rp;
        if (vkCreateGraphicsPipelines(g_vk.dev, NULL, 1, &gpci, NULL, &g_vk.pipe) != VK_SUCCESS)
            return false;

        float cube[36 * 6];
        FillCube(cube);
        void* cubeMap = nullptr;
        if (!MakeBuffer(sizeof(cube), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        &g_vk.cubeBuf, &g_vk.cubeMem, &cubeMap))
            return false;
        memcpy(cubeMap, cube, sizeof(cube));

        // The pill's unit sphere (V179): same vertex layout as the cube, so
        // every pipeline (main, lit, shadow depth) draws it unchanged.
        float* sph = nullptr;
        g_vk.sphereVerts = FillSphere(&sph);
        void* sphMap = nullptr;
        if (MakeBuffer((VkDeviceSize)g_vk.sphereVerts * 6 * sizeof(float),
                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       &g_vk.sphereBuf, &g_vk.sphereMem, &sphMap))
            memcpy(sphMap, sph, (size_t)g_vk.sphereVerts * 6 * sizeof(float));
        else
            g_vk.sphereBuf = VK_NULL_HANDLE;
        free(sph);

        // The capsule shaft (V185): unit centred cylinder, same layout, so
        // every pipeline draws it unchanged. Convention matches the GL
        // BuildUnitCylinder in battle.cpp exactly (radius 0.5, y -0.5..0.5).
        float* cyl = nullptr;
        g_vk.cylVerts = FillCylinder(&cyl);
        void* cylMap = nullptr;
        if (MakeBuffer((VkDeviceSize)g_vk.cylVerts * 6 * sizeof(float),
                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       &g_vk.cylBuf, &g_vk.cylMem, &cylMap))
            memcpy(cylMap, cyl, (size_t)g_vk.cylVerts * 6 * sizeof(float));
        else
            g_vk.cylBuf = VK_NULL_HANDLE;
        free(cyl);

        // Terrain pipeline (V164): mesh.vert + the same Lambert fragment,
        // but colour writes OFF â€” it exists to give the soldiers correct
        // hillside occlusion in the composite while GL still paints the
        // ground. Flipping colorWriteMask on is the whole-terrain switch.
        size_t mvsSize;
        void* mvsCode = ReadSpv("assets/spv/mesh.vert.spv", &mvsSize);
        if (mvsCode) {
            smci.codeSize = mvsSize;
            smci.pCode = (const uint32_t*)mvsCode;
            VkShaderModule mvsMod;
            vkCreateShaderModule(g_vk.dev, &smci, NULL, &mvsMod);
            free(mvsCode);
            VkPipelineShaderStageCreateInfo mst[2] = { stages[0], stages[1] };
            mst[0].module = mvsMod;
            VkVertexInputBindingDescription mb = { 0, 10 * sizeof(float),
                                                   VK_VERTEX_INPUT_RATE_VERTEX };
            VkVertexInputAttributeDescription ma[3] = {
                { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
                { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12 },
                { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 24 },
            };
            VkPipelineVertexInputStateCreateInfo mvin = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
            mvin.vertexBindingDescriptionCount = 1;
            mvin.pVertexBindingDescriptions = &mb;
            mvin.vertexAttributeDescriptionCount = 3;
            mvin.pVertexAttributeDescriptions = ma;
            VkPipelineColorBlendAttachmentState mcba = {};
            mcba.colorWriteMask = 0;               // depth-only occluder
            VkPipelineColorBlendStateCreateInfo mcb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
            mcb.attachmentCount = 1;
            mcb.pAttachments = &mcba;
            VkGraphicsPipelineCreateInfo mgp = gpci;
            mgp.pStages = mst;
            mgp.pVertexInputState = &mvin;
            mgp.pColorBlendState = &mcb;
            vkCreateGraphicsPipelines(g_vk.dev, NULL, 1, &mgp, NULL, &g_vk.meshPipe);
        }

        VkCommandPoolCreateInfo cpci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cpci.queueFamilyIndex = g_vk.qfam;
        vkCreateCommandPool(g_vk.dev, &cpci, NULL, &g_vk.pool);
        VkCommandBufferAllocateInfo cbai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        cbai.commandPool = g_vk.pool;
        cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 2;
        vkAllocateCommandBuffers(g_vk.dev, &cbai, g_vk.cmd);
        VkFenceCreateInfo fenci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        vkCreateFence(g_vk.dev, &fenci, NULL, &g_vk.fence[0]);
        vkCreateFence(g_vk.dev, &fenci, NULL, &g_vk.fence[1]);
    }

    g_vk.width = w;
    g_vk.height = h;
    return true;
}

// V179: unit-diameter UV sphere, pos+normal interleaved (6 floats/vert),
// triangle list. stacks x slices x 6 verts; poles degenerate harmlessly.
int FillSphere(float** out) {
    const int ST = 10, SL = 12;
    const int n = ST * SL * 6;
    float* v = (float*)malloc((size_t)n * 6 * sizeof(float));
    int k = 0;
    auto emit = [&](float phi, float theta) {
        const float sp = sinf(phi), cp = cosf(phi);
        const float dx = sp * cosf(theta), dy = cp, dz = sp * sinf(theta);
        v[k++] = dx * 0.5f; v[k++] = dy * 0.5f; v[k++] = dz * 0.5f;
        v[k++] = dx;        v[k++] = dy;        v[k++] = dz;
    };
    const float PIF = 3.14159265f;
    for (int i = 0; i < ST; ++i)
        for (int j = 0; j < SL; ++j) {
            const float p0 = PIF * i / ST, p1 = PIF * (i + 1) / ST;
            const float t0 = 2 * PIF * j / SL, t1 = 2 * PIF * (j + 1) / SL;
            emit(p0, t0); emit(p1, t0); emit(p1, t1);
            emit(p0, t0); emit(p1, t1); emit(p0, t1);
        }
    *out = v;
    return n;
}

// V185: matching generator for the GL BuildUnitCylinder - 16 slices, open
// ends, radius 0.5, y in [-0.5, 0.5], outward normals.
int FillCylinder(float** out) {
    const int SL = 16;
    const int n = SL * 6;
    float* v = (float*)malloc((size_t)n * 6 * sizeof(float));
    int k = 0;
    const float PIF = 3.14159265f;
    auto emit = [&](float ang, float y) {
        const float nx = cosf(ang), nz = sinf(ang);
        v[k++] = nx * 0.5f; v[k++] = y; v[k++] = nz * 0.5f;
        v[k++] = nx;        v[k++] = 0; v[k++] = nz;
    };
    for (int i = 0; i < SL; ++i) {
        const float a0 = 2 * PIF * i / SL, a1 = 2 * PIF * (i + 1) / SL;
        emit(a0, -0.5f); emit(a0, 0.5f); emit(a1, 0.5f);
        emit(a0, -0.5f); emit(a1, 0.5f); emit(a1, -0.5f);
    }
    *out = v;
    return n;
}

bool EnsurePillCap(int slot, int count) {   // (decl'd above BuildFrameResources)
    if (count <= g_vk.pillCap[slot]) return true;
    DFN(vkDestroyBuffer); DFN(vkUnmapMemory); DFN(vkFreeMemory); DFN(vkDeviceWaitIdle);
    if (g_vk.pillBuf[slot]) {
        vkDeviceWaitIdle(g_vk.dev);
        vkUnmapMemory(g_vk.dev, g_vk.pillMem[slot]);
        vkDestroyBuffer(g_vk.dev, g_vk.pillBuf[slot], NULL);
        vkFreeMemory(g_vk.dev, g_vk.pillMem[slot], NULL);
        g_vk.pillBuf[slot] = VK_NULL_HANDLE;
    }
    const int cap = count < 1024 ? 1024 : count * 2;
    if (!MakeBuffer((VkDeviceSize)cap * 80, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    &g_vk.pillBuf[slot], &g_vk.pillMem[slot], &g_vk.pillMap[slot]))
        return false;
    g_vk.pillCap[slot] = cap;
    return true;
}

bool EnsureInstCap(int slot, int count) {
    if (count <= g_vk.instCap[slot]) return true;
    DFN(vkDestroyBuffer); DFN(vkUnmapMemory); DFN(vkFreeMemory); DFN(vkDeviceWaitIdle);
    if (g_vk.instBuf[slot]) {
        vkDeviceWaitIdle(g_vk.dev);
        vkUnmapMemory(g_vk.dev, g_vk.instMem[slot]);
        vkDestroyBuffer(g_vk.dev, g_vk.instBuf[slot], NULL);
        vkFreeMemory(g_vk.dev, g_vk.instMem[slot], NULL);
        g_vk.instBuf[slot] = VK_NULL_HANDLE;
    }
    const int cap = count < 4096 ? 4096 : count * 2;
    if (!MakeBuffer((VkDeviceSize)cap * 80, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    &g_vk.instBuf[slot], &g_vk.instMem[slot], &g_vk.instMap[slot]))
        return false;
    g_vk.instCap[slot] = cap;
    return true;
}

// V178: the sun-shadow stack. Built once, lazily, after the main render
// pass exists (litPipe targets it). Any failure leaves shReady false and
// the legacy no-shadow pipeline in charge - parity is never at risk.
constexpr int SHADOW_RES_VK = 2048;

bool BuildShadowResources() {
    if (g_vk.shReady) return true;
    if (g_vk.shTried) return false;
    g_vk.shTried = true;
    if (!g_vk.rp || !g_vk.pipe) return false;
    DFN(vkCreateRenderPass); DFN(vkCreateFramebuffer); DFN(vkCreateShaderModule);
    DFN(vkCreatePipelineLayout); DFN(vkCreateGraphicsPipelines);
    DFN(vkCreateSampler); DFN(vkCreateDescriptorSetLayout);
    DFN(vkCreateDescriptorPool); DFN(vkAllocateDescriptorSets);
    DFN(vkUpdateDescriptorSets);

    size_t bsv, msv, blv, blf;
    void* bsvC = ReadSpv("assets/spv/boxsh.vert.spv", &bsv);
    void* msvC = ReadSpv("assets/spv/meshsh.vert.spv", &msv);
    void* blvC = ReadSpv("assets/spv/boxlit.vert.spv", &blv);
    void* blfC = ReadSpv("assets/spv/boxlit.frag.spv", &blf);
    if (!bsvC || !msvC || !blvC || !blfC) {
        free(bsvC); free(msvC); free(blvC); free(blfC);
        TraceLog(RL_LOG_WARNING, "rdr: vulkan shadow shaders missing; no-shadow path stays");
        return false;
    }

    // The map: D32, rendered by a depth-only pass, sampled by the lit pass.
    if (!MakeImage(SHADOW_RES_VK, SHADOW_RES_VK, VK_FORMAT_D32_SFLOAT,
                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                   VK_IMAGE_ASPECT_DEPTH_BIT, &g_vk.shImg, &g_vk.shMem, &g_vk.shView))
        return false;

    {   // depth-only render pass ending in SHADER_READ_ONLY for the lit pass
        VkAttachmentDescription at = {};
        at.format = VK_FORMAT_D32_SFLOAT;
        at.samples = VK_SAMPLE_COUNT_1_BIT;
        at.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        at.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        at.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        at.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkAttachmentReference dr = { 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
        VkSubpassDescription sp = {};
        sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sp.pDepthStencilAttachment = &dr;
        VkSubpassDependency dep = {};
        dep.srcSubpass = 0;
        dep.dstSubpass = VK_SUBPASS_EXTERNAL;
        dep.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dep.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        VkRenderPassCreateInfo rpci = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        rpci.attachmentCount = 1;
        rpci.pAttachments = &at;
        rpci.subpassCount = 1;
        rpci.pSubpasses = &sp;
        rpci.dependencyCount = 1;
        rpci.pDependencies = &dep;
        if (vkCreateRenderPass(g_vk.dev, &rpci, NULL, &g_vk.shRp) != VK_SUCCESS)
            return false;
        VkFramebufferCreateInfo fci = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fci.renderPass = g_vk.shRp;
        fci.attachmentCount = 1;
        fci.pAttachments = &g_vk.shView;
        fci.width = SHADOW_RES_VK;
        fci.height = SHADOW_RES_VK;
        fci.layers = 1;
        if (vkCreateFramebuffer(g_vk.dev, &fci, NULL, &g_vk.shFb) != VK_SUCCESS)
            return false;
    }

    {   // sampler + descriptor for the lit pass to read the map
        VkSamplerCreateInfo smp = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        smp.magFilter = VK_FILTER_NEAREST;   // manual 3x3 PCF wants raw depth
        smp.minFilter = VK_FILTER_NEAREST;
        smp.addressModeU = smp.addressModeV = smp.addressModeW =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vkCreateSampler(g_vk.dev, &smp, NULL, &g_vk.shSamp);
        VkDescriptorSetLayoutBinding b = {};
        b.binding = 0;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo dlci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        dlci.bindingCount = 1;
        dlci.pBindings = &b;
        vkCreateDescriptorSetLayout(g_vk.dev, &dlci, NULL, &g_vk.shDsl);
        VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
        VkDescriptorPoolCreateInfo dpci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        dpci.maxSets = 1;
        dpci.poolSizeCount = 1;
        dpci.pPoolSizes = &ps;
        VkDescriptorPool pool;
        vkCreateDescriptorPool(g_vk.dev, &dpci, NULL, &pool);
        VkDescriptorSetAllocateInfo dsai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        dsai.descriptorPool = pool;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &g_vk.shDsl;
        if (vkAllocateDescriptorSets(g_vk.dev, &dsai, &g_vk.shSet) != VK_SUCCESS)
            return false;
        VkDescriptorImageInfo dii = { g_vk.shSamp, g_vk.shView,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkWriteDescriptorSet wds = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        wds.dstSet = g_vk.shSet;
        wds.dstBinding = 0;
        wds.descriptorCount = 1;
        wds.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wds.pImageInfo = &dii;
        vkUpdateDescriptorSets(g_vk.dev, 1, &wds, 0, NULL);
    }

    VkShaderModuleCreateInfo smci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    VkShaderModule bsvM, msvM, blvM, blfM;
    smci.codeSize = bsv;  smci.pCode = (const uint32_t*)bsvC;
    vkCreateShaderModule(g_vk.dev, &smci, NULL, &bsvM);
    smci.codeSize = msv;  smci.pCode = (const uint32_t*)msvC;
    vkCreateShaderModule(g_vk.dev, &smci, NULL, &msvM);
    smci.codeSize = blv;  smci.pCode = (const uint32_t*)blvC;
    vkCreateShaderModule(g_vk.dev, &smci, NULL, &blvM);
    smci.codeSize = blf;  smci.pCode = (const uint32_t*)blfC;
    vkCreateShaderModule(g_vk.dev, &smci, NULL, &blfM);
    free(bsvC); free(msvC); free(blvC); free(blfC);

    {   // layouts: 64B lightVP for the depth pass, 144B + map for the lit pass
        VkPushConstantRange spc = { VK_SHADER_STAGE_VERTEX_BIT, 0, 64 };
        VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges = &spc;
        vkCreatePipelineLayout(g_vk.dev, &plci, NULL, &g_vk.shLayout);
        VkPushConstantRange lpc = { VK_SHADER_STAGE_VERTEX_BIT |
                                    VK_SHADER_STAGE_FRAGMENT_BIT, 0, 144 };
        plci.pPushConstantRanges = &lpc;
        plci.setLayoutCount = 1;
        plci.pSetLayouts = &g_vk.shDsl;
        vkCreatePipelineLayout(g_vk.dev, &plci, NULL, &g_vk.litLayout);
    }

    // Shared fixed state for the three new pipelines.
    VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vps = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vps.viewportCount = 1;
    vps.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo dst = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    dst.depthTestEnable = VK_TRUE;
    dst.depthWriteEnable = VK_TRUE;
    dst.depthCompareOp = VK_COMPARE_OP_LESS;
    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dsci = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dsci.dynamicStateCount = 2;
    dsci.pDynamicStates = dyn;

    VkVertexInputBindingDescription boxBinds[2] = {
        { 0, 6 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX },
        { 1, 80,                VK_VERTEX_INPUT_RATE_INSTANCE },
    };
    VkVertexInputAttributeDescription boxAttrs[7] = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12 },
        { 2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0 },
        { 3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 16 },
        { 4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 32 },
        { 5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 48 },
        { 6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 64 },
    };
    VkPipelineVertexInputStateCreateInfo boxVin = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    boxVin.vertexBindingDescriptionCount = 2;
    boxVin.pVertexBindingDescriptions = boxBinds;
    boxVin.vertexAttributeDescriptionCount = 7;
    boxVin.pVertexAttributeDescriptions = boxAttrs;

    VkVertexInputBindingDescription meshBind = { 0, 10 * sizeof(float),
                                                 VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription meshAttr = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };
    VkPipelineVertexInputStateCreateInfo meshVin = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    meshVin.vertexBindingDescriptionCount = 1;
    meshVin.pVertexBindingDescriptions = &meshBind;
    meshVin.vertexAttributeDescriptionCount = 1;
    meshVin.pVertexAttributeDescriptions = &meshAttr;

    {   // depth-only pipelines: vertex stage only, no colour attachments
        VkPipelineShaderStageCreateInfo st = {};
        st.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        st.stage = VK_SHADER_STAGE_VERTEX_BIT;
        st.module = bsvM;
        st.pName = "main";
        VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        VkGraphicsPipelineCreateInfo gp = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        gp.stageCount = 1;
        gp.pStages = &st;
        gp.pVertexInputState = &boxVin;
        gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vps;
        gp.pRasterizationState = &rs;
        gp.pMultisampleState = &ms;
        gp.pDepthStencilState = &dst;
        gp.pColorBlendState = &cb;
        gp.pDynamicState = &dsci;
        gp.layout = g_vk.shLayout;
        gp.renderPass = g_vk.shRp;
        if (vkCreateGraphicsPipelines(g_vk.dev, NULL, 1, &gp, NULL, &g_vk.shBoxPipe) != VK_SUCCESS)
            return false;
        st.module = msvM;
        gp.pVertexInputState = &meshVin;
        vkCreateGraphicsPipelines(g_vk.dev, NULL, 1, &gp, NULL, &g_vk.shMeshPipe);
    }

    {   // the shadow-sampling lit pipeline, targeting the main render pass
        VkPipelineShaderStageCreateInfo st2[2] = {};
        st2[0].sType = st2[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        st2[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        st2[0].module = blvM;
        st2[0].pName = "main";
        st2[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        st2[1].module = blfM;
        st2[1].pName = "main";
        VkPipelineColorBlendAttachmentState cba = {};
        cba.colorWriteMask = 0xF;
        VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        cb.attachmentCount = 1;
        cb.pAttachments = &cba;
        VkGraphicsPipelineCreateInfo gp = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        gp.stageCount = 2;
        gp.pStages = st2;
        gp.pVertexInputState = &boxVin;
        gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vps;
        gp.pRasterizationState = &rs;
        gp.pMultisampleState = &ms;
        gp.pDepthStencilState = &dst;
        gp.pColorBlendState = &cb;
        gp.pDynamicState = &dsci;
        gp.layout = g_vk.litLayout;
        gp.renderPass = g_vk.rp;
        if (vkCreateGraphicsPipelines(g_vk.dev, NULL, 1, &gp, NULL, &g_vk.litPipe) != VK_SUCCESS)
            return false;
    }

    g_vk.shReady = true;
    TraceLog(RL_LOG_INFO,
             "rdr: VULKAN SHADOW PASS LIVE - %dx%d sun depth map on %s",
             SHADOW_RES_VK, SHADOW_RES_VK, g_vk.gpuName);
    return true;
}

// Generic 2-slot instance-buffer grower shared by the V180 skinned streams.
bool EnsureStreamCap(VkBuffer buf[2], VkDeviceMemory mem[2], void* map[2],
                     int cap[2], int slot, int count) {
    if (count <= cap[slot]) return true;
    DFN(vkDestroyBuffer); DFN(vkUnmapMemory); DFN(vkFreeMemory); DFN(vkDeviceWaitIdle);
    if (buf[slot]) {
        vkDeviceWaitIdle(g_vk.dev);
        vkUnmapMemory(g_vk.dev, mem[slot]);
        vkDestroyBuffer(g_vk.dev, buf[slot], NULL);
        vkFreeMemory(g_vk.dev, mem[slot], NULL);
        buf[slot] = VK_NULL_HANDLE;
    }
    const int cap2 = count < 2048 ? 2048 : count * 2;
    if (!MakeBuffer((VkDeviceSize)cap2 * 80, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    &buf[slot], &mem[slot], &map[slot]))
        return false;
    cap[slot] = cap2;
    return true;
}

// V180: build the skin atlas image + the two-binding descriptor (shadow map
// + atlas) + the 160B skinned lit pipeline. Requires the shadow stack (for
// the shadow view/sampler and the main render pass) and staged atlas pixels.
bool BuildSkinResources() {
    if (g_vk.skinReady) return true;
    if (g_vk.skinTried || !g_vk.skinPending || !g_vk.shReady || !g_vk.rp)
        return false;
    g_vk.skinTried = true;
    DFN(vkCreateImage); DFN(vkGetImageMemoryRequirements); DFN(vkAllocateMemory);
    DFN(vkBindImageMemory); DFN(vkMapMemory); DFN(vkCreateImageView);
    DFN(vkGetImageSubresourceLayout); DFN(vkCreateDescriptorSetLayout);
    DFN(vkCreateDescriptorPool); DFN(vkAllocateDescriptorSets);
    DFN(vkUpdateDescriptorSets); DFN(vkCreateShaderModule);
    DFN(vkCreatePipelineLayout); DFN(vkCreateGraphicsPipelines);

    const int w = g_vk.skinPendW, h = g_vk.skinPendH;
    VkImageCreateInfo ici = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent = { (uint32_t)w, (uint32_t)h, 1 };
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_LINEAR;
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
    if (vkCreateImage(g_vk.dev, &ici, NULL, &g_vk.skinImg) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(g_vk.dev, g_vk.skinImg, &req);
    VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = MemType(req.memoryTypeBits,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(g_vk.dev, &mai, NULL, &g_vk.skinMem) != VK_SUCCESS) return false;
    vkBindImageMemory(g_vk.dev, g_vk.skinImg, g_vk.skinMem, 0);
    VkImageSubresource sub = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
    VkSubresourceLayout lay;
    vkGetImageSubresourceLayout(g_vk.dev, g_vk.skinImg, &sub, &lay);
    void* amap = nullptr;
    vkMapMemory(g_vk.dev, g_vk.skinMem, 0, req.size, 0, &amap);
    for (int y = 0; y < h; ++y)
        memcpy((unsigned char*)amap + lay.offset + (size_t)y * lay.rowPitch,
               g_vk.skinPending + (size_t)y * w * 4, (size_t)w * 4);
    free(g_vk.skinPending);
    g_vk.skinPending = nullptr;
    VkImageViewCreateInfo vci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vci.image = g_vk.skinImg;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VK_FORMAT_R8G8B8A8_UNORM;
    vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(g_vk.dev, &vci, NULL, &g_vk.skinView) != VK_SUCCESS)
        return false;

    {   // set 0: binding 0 shadow map, binding 1 skin atlas
        VkDescriptorSetLayoutBinding b[2] = {};
        for (int i = 0; i < 2; ++i) {
            b[i].binding = (uint32_t)i;
            b[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            b[i].descriptorCount = 1;
            b[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo dlci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        dlci.bindingCount = 2;
        dlci.pBindings = b;
        vkCreateDescriptorSetLayout(g_vk.dev, &dlci, NULL, &g_vk.skinDsl);
        VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 };
        VkDescriptorPoolCreateInfo dpci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        dpci.maxSets = 1;
        dpci.poolSizeCount = 1;
        dpci.pPoolSizes = &ps;
        VkDescriptorPool pool;
        vkCreateDescriptorPool(g_vk.dev, &dpci, NULL, &pool);
        VkDescriptorSetAllocateInfo dsai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        dsai.descriptorPool = pool;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &g_vk.skinDsl;
        if (vkAllocateDescriptorSets(g_vk.dev, &dsai, &g_vk.skinSet) != VK_SUCCESS)
            return false;
        VkDescriptorImageInfo dii[2] = {
            { g_vk.shSamp, g_vk.shView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { g_vk.atlasSamp ? g_vk.atlasSamp : g_vk.shSamp, g_vk.skinView,
              VK_IMAGE_LAYOUT_GENERAL },
        };
        VkWriteDescriptorSet wds[2] = {};
        for (int i = 0; i < 2; ++i) {
            wds[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wds[i].dstSet = g_vk.skinSet;
            wds[i].dstBinding = (uint32_t)i;
            wds[i].descriptorCount = 1;
            wds[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            wds[i].pImageInfo = &dii[i];
        }
        vkUpdateDescriptorSets(g_vk.dev, 2, wds, 0, NULL);
    }

    size_t vs, fs;
    void* vsC = ReadSpv("assets/spv/boxskin.vert.spv", &vs);
    void* fsC = ReadSpv("assets/spv/boxskin.frag.spv", &fs);
    if (!vsC || !fsC) {
        TraceLog(RL_LOG_WARNING, "rdr: boxskin shaders missing; skinned bodies untextured");
        free(vsC); free(fsC);
        return false;
    }
    VkShaderModuleCreateInfo smci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    VkShaderModule vsM, fsM;
    smci.codeSize = vs;  smci.pCode = (const uint32_t*)vsC;
    vkCreateShaderModule(g_vk.dev, &smci, NULL, &vsM);
    smci.codeSize = fs;  smci.pCode = (const uint32_t*)fsC;
    vkCreateShaderModule(g_vk.dev, &smci, NULL, &fsM);
    free(vsC); free(fsC);

    VkPushConstantRange pcr = { VK_SHADER_STAGE_VERTEX_BIT |
                                VK_SHADER_STAGE_FRAGMENT_BIT, 0, 160 };
    VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &g_vk.skinDsl;
    vkCreatePipelineLayout(g_vk.dev, &plci, NULL, &g_vk.skinLayout);

    VkPipelineShaderStageCreateInfo st2[2] = {};
    st2[0].sType = st2[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st2[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    st2[0].module = vsM;
    st2[0].pName = "main";
    st2[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    st2[1].module = fsM;
    st2[1].pName = "main";
    VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vps = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vps.viewportCount = 1;
    vps.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rsr = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rsr.polygonMode = VK_POLYGON_MODE_FILL;
    rsr.cullMode = VK_CULL_MODE_NONE;
    rsr.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rsr.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo dst = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    dst.depthTestEnable = VK_TRUE;
    dst.depthWriteEnable = VK_TRUE;
    dst.depthCompareOp = VK_COMPARE_OP_LESS;
    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dsci = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dsci.dynamicStateCount = 2;
    dsci.pDynamicStates = dyn;
    VkVertexInputBindingDescription binds[2] = {
        { 0, 6 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX },
        { 1, 80,                VK_VERTEX_INPUT_RATE_INSTANCE },
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
    VkPipelineColorBlendAttachmentState cba = {};
    cba.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    VkGraphicsPipelineCreateInfo gp = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    gp.stageCount = 2;
    gp.pStages = st2;
    gp.pVertexInputState = &vin;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vps;
    gp.pRasterizationState = &rsr;
    gp.pMultisampleState = &ms;
    gp.pDepthStencilState = &dst;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &dsci;
    gp.layout = g_vk.skinLayout;
    gp.renderPass = g_vk.rp;
    if (vkCreateGraphicsPipelines(g_vk.dev, NULL, 1, &gp, NULL, &g_vk.skinPipe) != VK_SUCCESS)
        return false;
    g_vk.skinReady = true;
    TraceLog(RL_LOG_INFO,
             "rdr: VULKAN SKIN PIPELINE LIVE - armour renders as texture on %s",
             g_vk.gpuName);
    return true;
}

}  // namespace

// V180: stage the armour atlas (RGBA, 256x1024, 4 rows). Safe before the
// device exists; uploaded and wired on the next Vulkan frame.
void VulkanSetSkinAtlas(const unsigned char* rgba, int w, int h) {
    if (g_vk.skinReady || g_vk.skinPending) return;
    if (!rgba || w <= 0 || h <= 0) return;
    const size_t bytes = (size_t)w * h * 4;
    g_vk.skinPending = (unsigned char*)malloc(bytes);
    memcpy(g_vk.skinPending, rgba, bytes);
    g_vk.skinPendW = w;
    g_vk.skinPendH = h;
    g_vk.skinTried = false;   // allow a build attempt with the new pixels
}

// V164: stage the battlefield mesh (10 floats/vert: pos3 nrm3 col4). Called
// at terrain bake time â€” possibly before the device exists, so the copy is
// staged and uploaded lazily on the next Vulkan frame.
void VulkanSetTerrain(const float* verts, int vertCount) {
    free(g_vk.terrPending);
    g_vk.terrPending = nullptr;
    g_vk.terrPendingCount = 0;
    if (!verts || vertCount <= 0) return;
    const size_t bytes = (size_t)vertCount * 10 * sizeof(float);
    g_vk.terrPending = (float*)malloc(bytes);
    memcpy(g_vk.terrPending, verts, bytes);
    g_vk.terrPendingCount = vertCount;
}

namespace {
void UploadPendingTerrain() {
    if (!g_vk.terrPending) return;
    DFN(vkDestroyBuffer); DFN(vkFreeMemory); DFN(vkDeviceWaitIdle); DFN(vkUnmapMemory);
    if (g_vk.terrBuf) {
        vkDeviceWaitIdle(g_vk.dev);
        vkDestroyBuffer(g_vk.dev, g_vk.terrBuf, NULL);
        vkFreeMemory(g_vk.dev, g_vk.terrMem, NULL);
        g_vk.terrBuf = VK_NULL_HANDLE;
    }
    void* map = nullptr;
    const size_t bytes = (size_t)g_vk.terrPendingCount * 10 * sizeof(float);
    if (MakeBuffer(bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   &g_vk.terrBuf, &g_vk.terrMem, &map)) {
        memcpy(map, g_vk.terrPending, bytes);
        vkUnmapMemory(g_vk.dev, g_vk.terrMem);
        g_vk.terrCount = g_vk.terrPendingCount;
    }
    free(g_vk.terrPending);
    g_vk.terrPending = nullptr;
    g_vk.terrPendingCount = 0;
}
}  // namespace

// V173: upload the combined R8 glyph atlas (linear-tiled, host-visible,
// row-pitch honoured) and point a combined-image-sampler descriptor at it.
void VulkanSetUiAtlas(const unsigned char* r8, int w, int h) {
    if (!g_vk.live || g_vk.atlasSet) return;
    DFN(vkCreateImage); DFN(vkGetImageMemoryRequirements); DFN(vkAllocateMemory);
    DFN(vkBindImageMemory); DFN(vkMapMemory); DFN(vkCreateImageView);
    DFN(vkCreateSampler); DFN(vkGetImageSubresourceLayout);
    DFN(vkCreateDescriptorSetLayout); DFN(vkCreateDescriptorPool);
    DFN(vkAllocateDescriptorSets); DFN(vkUpdateDescriptorSets);

    VkImageCreateInfo aci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    aci.imageType = VK_IMAGE_TYPE_2D;
    aci.format = VK_FORMAT_R8_UNORM;
    aci.extent = { (uint32_t)w, (uint32_t)h, 1 };
    aci.mipLevels = 1;
    aci.arrayLayers = 1;
    aci.samples = VK_SAMPLE_COUNT_1_BIT;
    aci.tiling = VK_IMAGE_TILING_LINEAR;
    aci.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    aci.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
    if (vkCreateImage(g_vk.dev, &aci, NULL, &g_vk.atlasImg) != VK_SUCCESS) return;
    VkMemoryRequirements areq;
    vkGetImageMemoryRequirements(g_vk.dev, g_vk.atlasImg, &areq);
    VkMemoryAllocateInfo aai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    aai.allocationSize = areq.size;
    aai.memoryTypeIndex = MemType(areq.memoryTypeBits,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory amem;
    if (vkAllocateMemory(g_vk.dev, &aai, NULL, &amem) != VK_SUCCESS) return;
    vkBindImageMemory(g_vk.dev, g_vk.atlasImg, amem, 0);
    VkImageSubresource sub = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
    VkSubresourceLayout lay;
    vkGetImageSubresourceLayout(g_vk.dev, g_vk.atlasImg, &sub, &lay);
    void* amap = nullptr;
    vkMapMemory(g_vk.dev, amem, 0, areq.size, 0, &amap);
    for (int y = 0; y < h; ++y)
        memcpy((unsigned char*)amap + lay.offset + (size_t)y * lay.rowPitch,
               r8 + (size_t)y * w, (size_t)w);
    VkImageViewCreateInfo avci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    avci.image = g_vk.atlasImg;
    avci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    avci.format = VK_FORMAT_R8_UNORM;
    avci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCreateImageView(g_vk.dev, &avci, NULL, &g_vk.atlasView);
    VkSamplerCreateInfo smp = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    smp.magFilter = VK_FILTER_LINEAR;
    smp.minFilter = VK_FILTER_LINEAR;
    smp.addressModeU = smp.addressModeV = smp.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(g_vk.dev, &smp, NULL, &g_vk.atlasSamp);

    VkDescriptorSetLayoutBinding b = {};
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dlci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    dlci.bindingCount = 1;
    dlci.pBindings = &b;
    vkCreateDescriptorSetLayout(g_vk.dev, &dlci, NULL, &g_vk.uiDsl);
    VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
    VkDescriptorPoolCreateInfo dpci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &ps;
    VkDescriptorPool dpool;
    vkCreateDescriptorPool(g_vk.dev, &dpci, NULL, &dpool);
    VkDescriptorSetAllocateInfo dsai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    dsai.descriptorPool = dpool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &g_vk.uiDsl;
    vkAllocateDescriptorSets(g_vk.dev, &dsai, &g_vk.uiDset);
    VkDescriptorImageInfo dii = { g_vk.atlasSamp, g_vk.atlasView,
                                  VK_IMAGE_LAYOUT_GENERAL };
    VkWriteDescriptorSet wds = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    wds.dstSet = g_vk.uiDset;
    wds.dstBinding = 0;
    wds.descriptorCount = 1;
    wds.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wds.pImageInfo = &dii;
    vkUpdateDescriptorSets(g_vk.dev, 1, &wds, 0, NULL);
    g_vk.atlasSet = true;
    TraceLog(RL_LOG_INFO, "rdr: VULKAN TEXT ATLAS uploaded (%dx%d R8)", w, h);
}

// V177: register an RGBA texture for textured UI quads; returns an id for
// the segment stream, or -1. Linear-tiled host-visible, like the atlas.
int VulkanRegisterUiTexture(const unsigned char* rgba, int w, int h) {
    if (!g_vk.live || !g_vk.atlasSet || g_vk.texCount >= 16) return -1;
    DFN(vkCreateImage); DFN(vkGetImageMemoryRequirements); DFN(vkAllocateMemory);
    DFN(vkBindImageMemory); DFN(vkMapMemory); DFN(vkCreateImageView);
    DFN(vkGetImageSubresourceLayout); DFN(vkCreateDescriptorPool);
    DFN(vkAllocateDescriptorSets); DFN(vkUpdateDescriptorSets);
    if (!g_vk.texPool) {
        VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16 };
        VkDescriptorPoolCreateInfo dpci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        dpci.maxSets = 16;
        dpci.poolSizeCount = 1;
        dpci.pPoolSizes = &ps;
        vkCreateDescriptorPool(g_vk.dev, &dpci, NULL, &g_vk.texPool);
    }
    VkImageCreateInfo ici = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent = { (uint32_t)w, (uint32_t)h, 1 };
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_LINEAR;
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
    VkImage img;
    if (vkCreateImage(g_vk.dev, &ici, NULL, &img) != VK_SUCCESS) return -1;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(g_vk.dev, img, &req);
    VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = MemType(req.memoryTypeBits,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory mem;
    if (vkAllocateMemory(g_vk.dev, &mai, NULL, &mem) != VK_SUCCESS) return -1;
    vkBindImageMemory(g_vk.dev, img, mem, 0);
    VkImageSubresource sub = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
    VkSubresourceLayout lay;
    vkGetImageSubresourceLayout(g_vk.dev, img, &sub, &lay);
    void* map = nullptr;
    vkMapMemory(g_vk.dev, mem, 0, req.size, 0, &map);
    for (int y = 0; y < h; ++y)
        memcpy((unsigned char*)map + lay.offset + (size_t)y * lay.rowPitch,
               rgba + (size_t)y * w * 4, (size_t)w * 4);
    VkImageViewCreateInfo vci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vci.image = img;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VK_FORMAT_R8G8B8A8_UNORM;
    vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    VkImageView view;
    vkCreateImageView(g_vk.dev, &vci, NULL, &view);
    VkDescriptorSetAllocateInfo dsai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    dsai.descriptorPool = g_vk.texPool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &g_vk.uiDsl;
    VkDescriptorSet set;
    if (vkAllocateDescriptorSets(g_vk.dev, &dsai, &set) != VK_SUCCESS) return -1;
    VkDescriptorImageInfo dii = { g_vk.atlasSamp, view, VK_IMAGE_LAYOUT_GENERAL };
    VkWriteDescriptorSet wds = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    wds.dstSet = set;
    wds.dstBinding = 0;
    wds.descriptorCount = 1;
    wds.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wds.pImageInfo = &dii;
    vkUpdateDescriptorSets(g_vk.dev, 1, &wds, 0, NULL);
    g_vk.texSet[g_vk.texCount] = set;
    g_vk.texMap[g_vk.texCount] = (unsigned char*)map;
    g_vk.texPitch[g_vk.texCount] = lay.rowPitch
                                       ? (size_t)lay.rowPitch + lay.offset * 0
                                       : (size_t)w * 4;
    g_vk.texMap[g_vk.texCount] += lay.offset;
    g_vk.texW[g_vk.texCount] = w;
    g_vk.texH[g_vk.texCount] = h;
    return g_vk.texCount++;
}

// V178: overwrite a registered texture in place (dynamic previews). The
// image stays linear + host-mapped, so this is a straight row copy.
void VulkanUpdateUiTexture(int id, const unsigned char* rgba, int w, int h) {
    if (id < 0 || id >= g_vk.texCount || !g_vk.texMap[id]) return;
    if (w != g_vk.texW[id] || h != g_vk.texH[id]) return;
    // No wait needed (V182): UI textures are only sampled by the UI pass,
    // which is synchronous — PresentVulkanUi fence-waits before returning,
    // so by the time the next frame records here the GPU is done with it.
    // (The pipelined 3D slots never touch UI textures.)
    for (int y = 0; y < h; ++y)
        memcpy(g_vk.texMap[id] + (size_t)y * g_vk.texPitch[id],
               rgba + (size_t)y * w * 4, (size_t)w * 4);
}

namespace {
// Build the UI pipeline against the shared render pass (needs rp to exist).
bool EnsureUiPipeline() {
    if (g_vk.uiPipe) return true;
    if (!g_vk.rp || !g_vk.atlasSet) return false;
    DFN(vkCreateShaderModule); DFN(vkCreatePipelineLayout);
    DFN(vkCreateGraphicsPipelines); DFN(vkAllocateCommandBuffers); DFN(vkCreateFence);
    size_t tvs, tfs;
    void* tvsc = ReadSpv("assets/spv/text.vert.spv", &tvs);
    void* tfsc = ReadSpv("assets/spv/text.frag.spv", &tfs);
    if (!tvsc || !tfsc) return false;
    VkShaderModuleCreateInfo mci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    mci.codeSize = tvs;
    mci.pCode = (const uint32_t*)tvsc;
    VkShaderModule tv, tf;
    vkCreateShaderModule(g_vk.dev, &mci, NULL, &tv);
    mci.codeSize = tfs;
    mci.pCode = (const uint32_t*)tfsc;
    vkCreateShaderModule(g_vk.dev, &mci, NULL, &tf);
    free(tvsc); free(tfsc);

    VkPushConstantRange upcr = { VK_SHADER_STAGE_VERTEX_BIT, 0, 16 };
    VkPipelineLayoutCreateInfo uplci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    uplci.setLayoutCount = 1;
    uplci.pSetLayouts = &g_vk.uiDsl;
    uplci.pushConstantRangeCount = 1;
    uplci.pPushConstantRanges = &upcr;
    vkCreatePipelineLayout(g_vk.dev, &uplci, NULL, &g_vk.uiLayout);

    VkPipelineShaderStageCreateInfo st[2] = {};
    st[0].sType = st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    st[0].module = tv;
    st[0].pName = "main";
    st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    st[1].module = tf;
    st[1].pName = "main";
    VkVertexInputBindingDescription ub = { 0, 8 * sizeof(float),
                                           VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription ua[3] = {
        { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 },
        { 1, 0, VK_FORMAT_R32G32_SFLOAT, 8 },
        { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16 },
    };
    VkPipelineVertexInputStateCreateInfo uvin = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    uvin.vertexBindingDescriptionCount = 1;
    uvin.pVertexBindingDescriptions = &ub;
    uvin.vertexAttributeDescriptionCount = 3;
    uvin.pVertexAttributeDescriptions = ua;
    VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vps = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vps.viewportCount = 1;
    vps.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    VkPipelineColorBlendAttachmentState cba = {};
    cba.blendEnable = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp = VK_BLEND_OP_ADD;
    cba.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dsci = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dsci.dynamicStateCount = 2;
    dsci.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo gp = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    gp.stageCount = 2;
    gp.pStages = st;
    gp.pVertexInputState = &uvin;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vps;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pDepthStencilState = &ds;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &dsci;
    gp.layout = g_vk.uiLayout;
    gp.renderPass = g_vk.rp;
    if (vkCreateGraphicsPipelines(g_vk.dev, NULL, 1, &gp, NULL, &g_vk.uiPipe) != VK_SUCCESS)
        return false;
    // Textured-quad sibling (V177): same layout, image.frag samples RGBA.
    size_t ifs;
    void* ifsc = ReadSpv("assets/spv/image.frag.spv", &ifs);
    if (ifsc) {
        mci.codeSize = ifs;
        mci.pCode = (const uint32_t*)ifsc;
        VkShaderModule imf;
        vkCreateShaderModule(g_vk.dev, &mci, NULL, &imf);
        free(ifsc);
        VkPipelineShaderStageCreateInfo ist[2] = { st[0], st[1] };
        ist[1].module = imf;
        VkGraphicsPipelineCreateInfo igp = gp;
        igp.pStages = ist;
        vkCreateGraphicsPipelines(g_vk.dev, NULL, 1, &igp, NULL, &g_vk.imgPipe);
    }
    VkCommandBufferAllocateInfo cbai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cbai.commandPool = g_vk.pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    vkAllocateCommandBuffers(g_vk.dev, &cbai, &g_vk.uiCmd);
    VkFenceCreateInfo fci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    vkCreateFence(g_vk.dev, &fci, NULL, &g_vk.uiFence);
    TraceLog(RL_LOG_INFO, "rdr: VULKAN TEXT PIPELINE LIVE - HUD renders on %s",
             g_vk.gpuName);
    return true;
}
}  // namespace

// V173: render the recorded HUD quads through the Vulkan text pipeline into
// the shared offscreen target (transparent clear) and read back RGBA.
// V177: the segment arrays interleave atlas/solid runs with textured runs.
const unsigned char* VulkanRenderUi(const void* verts, int vcount,
                                    const int* segTex, const int* segCount,
                                    int nSegs, int w, int h) {
    if (!g_vk.live || g_vk.frameFailed || g_vk.uiFailed || !g_vk.atlasSet ||
        vcount <= 0 || w <= 0 || h <= 0)
        return nullptr;
    if (w != g_vk.width || h != g_vk.height) {
        DFN(vkDeviceWaitIdle);
        if (g_vk.fb) vkDeviceWaitIdle(g_vk.dev);
        if (!BuildFrameResources(w, h)) { g_vk.frameFailed = true; return nullptr; }
    }
    if (!EnsureUiPipeline()) { g_vk.uiFailed = true; return nullptr; }
    DFN(vkDestroyBuffer); DFN(vkUnmapMemory); DFN(vkFreeMemory); DFN(vkDeviceWaitIdle);
    if (vcount > g_vk.uiCap) {
        if (g_vk.uiBuf) {
            vkDeviceWaitIdle(g_vk.dev);
            vkUnmapMemory(g_vk.dev, g_vk.uiMem);
            vkDestroyBuffer(g_vk.dev, g_vk.uiBuf, NULL);
            vkFreeMemory(g_vk.dev, g_vk.uiMem, NULL);
        }
        const int cap = vcount < 16384 ? 16384 : vcount * 2;
        if (!MakeBuffer((VkDeviceSize)cap * 32, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        &g_vk.uiBuf, &g_vk.uiMem, &g_vk.uiMap))
            return nullptr;
        g_vk.uiCap = cap;
    }
    if (g_vk.uiReadSize < w * h * 4) {
        if (g_vk.uiRead) {
            vkDeviceWaitIdle(g_vk.dev);
            vkUnmapMemory(g_vk.dev, g_vk.uiReadMem);
            vkDestroyBuffer(g_vk.dev, g_vk.uiRead, NULL);
            vkFreeMemory(g_vk.dev, g_vk.uiReadMem, NULL);
        }
        VkMemoryPropertyFlags rf = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                   VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        bool cached = false;
        for (uint32_t i = 0; i < g_vk.memProps.memoryTypeCount; ++i)
            if ((g_vk.memProps.memoryTypes[i].propertyFlags & rf) == rf) cached = true;
        if (!cached) rf = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if (!MakeBuffer((VkDeviceSize)w * h * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, rf,
                        &g_vk.uiRead, &g_vk.uiReadMem, &g_vk.uiReadMap))
            return nullptr;
        g_vk.uiReadSize = w * h * 4;
    }
    memcpy(g_vk.uiMap, verts, (size_t)vcount * 32);

    DFN(vkBeginCommandBuffer); DFN(vkCmdBeginRenderPass); DFN(vkCmdBindPipeline);
    DFN(vkCmdSetViewport); DFN(vkCmdSetScissor); DFN(vkCmdBindVertexBuffers);
    DFN(vkCmdPushConstants); DFN(vkCmdDraw); DFN(vkCmdEndRenderPass);
    DFN(vkCmdCopyImageToBuffer); DFN(vkEndCommandBuffer); DFN(vkQueueSubmit);
    DFN(vkWaitForFences); DFN(vkResetFences); DFN(vkResetCommandBuffer);
    DFN(vkCmdBindDescriptorSets);

    vkResetCommandBuffer(g_vk.uiCmd, 0);
    VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(g_vk.uiCmd, &bi);
    VkClearValue clears[2];
    clears[0].color = { { 0, 0, 0, 0 } };
    clears[1].depthStencil = { 1.0f, 0 };
    VkRenderPassBeginInfo rbi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rbi.renderPass = g_vk.rp;
    rbi.framebuffer = g_vk.fb;
    rbi.renderArea = { { 0, 0 }, { (uint32_t)w, (uint32_t)h } };
    rbi.clearValueCount = 2;
    rbi.pClearValues = clears;
    vkCmdBeginRenderPass(g_vk.uiCmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(g_vk.uiCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_vk.uiPipe);
    VkViewport vp = { 0, 0, (float)w, (float)h, 0, 1 };   // y-down = pixel space
    VkRect2D sc = { { 0, 0 }, { (uint32_t)w, (uint32_t)h } };
    vkCmdSetViewport(g_vk.uiCmd, 0, 1, &vp);
    vkCmdSetScissor(g_vk.uiCmd, 0, 1, &sc);
    const float scr[4] = { (float)w, (float)h, 0, 0 };
    vkCmdPushConstants(g_vk.uiCmd, g_vk.uiLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, 16, scr);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(g_vk.uiCmd, 0, 1, &g_vk.uiBuf, &off);
    // Segment stream (V177): -1 = atlas/solid via the text pipeline; >=0 = a
    // registered RGBA texture via the image pipeline. Order preserved.
    if (!segTex || nSegs <= 0) {
        vkCmdBindPipeline(g_vk.uiCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_vk.uiPipe);
        vkCmdBindDescriptorSets(g_vk.uiCmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                g_vk.uiLayout, 0, 1, &g_vk.uiDset, 0, NULL);
        vkCmdDraw(g_vk.uiCmd, (uint32_t)vcount, 1, 0, 0);
    } else {
        int first = 0;
        for (int s = 0; s < nSegs; ++s) {
            const int tex = segTex[s], n = segCount[s];
            if (n <= 0) continue;
            const bool textured = tex >= 0 && tex < g_vk.texCount && g_vk.imgPipe;
            vkCmdBindPipeline(g_vk.uiCmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              textured ? g_vk.imgPipe : g_vk.uiPipe);
            vkCmdBindDescriptorSets(g_vk.uiCmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    g_vk.uiLayout, 0, 1,
                                    textured ? &g_vk.texSet[tex] : &g_vk.uiDset,
                                    0, NULL);
            vkCmdDraw(g_vk.uiCmd, (uint32_t)n, 1, (uint32_t)first, 0);
            first += n;
        }
    }
    vkCmdEndRenderPass(g_vk.uiCmd);
    VkBufferImageCopy region = {};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent = { (uint32_t)w, (uint32_t)h, 1 };
    vkCmdCopyImageToBuffer(g_vk.uiCmd, g_vk.color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           g_vk.uiRead, 1, &region);
    vkEndCommandBuffer(g_vk.uiCmd);
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1;
    si.pCommandBuffers = &g_vk.uiCmd;
    vkQueueSubmit(g_vk.queue, 1, &si, g_vk.uiFence);
    vkWaitForFences(g_vk.dev, 1, &g_vk.uiFence, VK_TRUE, UINT64_MAX);
    vkResetFences(g_vk.dev, 1, &g_vk.uiFence);
    return (const unsigned char*)g_vk.uiReadMap;
}

// Boots the device once (logged); true once the frame executor is usable.
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
    return g_vk.live && !g_vk.frameFailed;
}

// Renders `count` instances (80 bytes each: column-major mat4 + rgba floats)
// through the Vulkan box pipeline into an offscreen target and returns the
// RGBA pixels (w*h*4, row 0 = top), or null on failure or warm-up (caller falls back
// to GL). Pipelined readback (V166): the returned pixels are LAST frame's
// render - one frame of latency buys back the sync-wait stall â€” the present-interop stage; the native
// swapchain replaces it when the window itself moves to Vulkan.
// V178: lightVP16 is the sun's view-proj (already z-fixed to [0,1]) and
// flags bit0 says "shadows on" - both flow from rdr.cpp, which owns the
// raylib math this file cannot include.
const unsigned char* VulkanRenderFrame(const float* viewProj16, const float* sun4,
                                       const float* lightVP16, int flags,
                                       const void* instData, int count,
                                       const void* pillData, int pillCount,
                                       const void* skinBoxData, const int* skinBoxSegSkin,
                                       const int* skinBoxSegCount, int nSkinBoxSegs,
                                       const void* skinPillData, const int* skinPillSegSkin,
                                       const int* skinPillSegCount, int nSkinPillSegs,
                                       const void* cylData, int cylCount,
                                       const void* skinCylData, const int* skinCylSegSkin,
                                       const int* skinCylSegCount, int nSkinCylSegs,
                                       int w, int h) {
    if (!g_vk.live || g_vk.frameFailed || w <= 0 || h <= 0) return nullptr;
    if (w != g_vk.width || h != g_vk.height) {
        DFN(vkDeviceWaitIdle);
        if (g_vk.fb) vkDeviceWaitIdle(g_vk.dev);
        if (!BuildFrameResources(w, h)) {
            g_vk.frameFailed = true;
            TraceLog(RL_LOG_WARNING, "rdr: vulkan frame resources failed; GL fallback");
            return nullptr;
        }
        if (!g_vk.frameReady) {
            g_vk.frameReady = true;
            TraceLog(RL_LOG_INFO,
                     "rdr: VULKAN FRAME EXECUTOR LIVE - the army renders on %s",
                     g_vk.gpuName);
        }
    }
    UploadPendingTerrain();
    BuildShadowResources();   // self-gating; one attempt, logged
    BuildSkinResources();     // V180: needs shadow stack + staged atlas

    // Total skinned instances per stream (the seg arrays partition them).
    int skinBoxCount = 0, skinPillCount = 0, skinCylCount = 0;
    for (int s = 0; s < nSkinBoxSegs; ++s) skinBoxCount += skinBoxSegCount[s];
    for (int s = 0; s < nSkinPillSegs; ++s) skinPillCount += skinPillSegCount[s];
    for (int s = 0; s < nSkinCylSegs; ++s) skinCylCount += skinCylSegCount[s];
    if (!skinBoxData) skinBoxCount = 0;
    if (!skinPillData || !g_vk.sphereBuf) skinPillCount = 0;
    if (!skinCylData || !g_vk.cylBuf) skinCylCount = 0;   // V185

    DFN(vkBeginCommandBuffer); DFN(vkCmdBeginRenderPass); DFN(vkCmdBindPipeline);
    DFN(vkCmdSetViewport); DFN(vkCmdSetScissor); DFN(vkCmdBindVertexBuffers);
    DFN(vkCmdPushConstants); DFN(vkCmdDraw); DFN(vkCmdEndRenderPass);
    DFN(vkCmdCopyImageToBuffer); DFN(vkEndCommandBuffer); DFN(vkQueueSubmit);
    DFN(vkWaitForFences); DFN(vkResetFences); DFN(vkResetCommandBuffer);

    // Slot rotation (V166): reclaim this slot (two frames old â€” the GPU is
    // long done), fill and submit it, then present the OTHER slot, whose
    // work was submitted last frame and has had a whole frame to finish.
    const int cur = g_vk.frame & 1;
    g_vk.frame++;
    if (g_vk.submitted[cur])
        vkWaitForFences(g_vk.dev, 1, &g_vk.fence[cur], VK_TRUE, UINT64_MAX);
    vkResetFences(g_vk.dev, 1, &g_vk.fence[cur]);
    g_vk.submitted[cur] = false;
    if (!EnsureInstCap(cur, count)) { g_vk.frameFailed = true; return nullptr; }
    if (count > 0) memcpy(g_vk.instMap[cur], instData, (size_t)count * 80);
    if (!g_vk.sphereBuf) pillCount = 0;   // sphere upload failed: skip pills
    if (pillCount > 0 && !EnsurePillCap(cur, pillCount)) pillCount = 0;
    if (pillCount > 0) memcpy(g_vk.pillMap[cur], pillData, (size_t)pillCount * 80);
    if (skinBoxCount > 0 &&
        !EnsureStreamCap(g_vk.skinBoxBuf, g_vk.skinBoxMem, g_vk.skinBoxMap,
                         g_vk.skinBoxCap, cur, skinBoxCount))
        skinBoxCount = 0;
    if (skinBoxCount > 0)
        memcpy(g_vk.skinBoxMap[cur], skinBoxData, (size_t)skinBoxCount * 80);
    if (skinPillCount > 0 &&
        !EnsureStreamCap(g_vk.skinPillBuf, g_vk.skinPillMem, g_vk.skinPillMap,
                         g_vk.skinPillCap, cur, skinPillCount))
        skinPillCount = 0;
    if (skinPillCount > 0)
        memcpy(g_vk.skinPillMap[cur], skinPillData, (size_t)skinPillCount * 80);
    // Capsule shafts (V185): plain + skinned cylinder streams.
    if (!g_vk.cylBuf) cylCount = 0;
    if (cylCount > 0 &&
        !EnsureStreamCap(g_vk.cylInst, g_vk.cylInstMem, g_vk.cylInstMap,
                         g_vk.cylInstCap, cur, cylCount))
        cylCount = 0;
    if (cylCount > 0) memcpy(g_vk.cylInstMap[cur], cylData, (size_t)cylCount * 80);
    if (skinCylCount > 0 &&
        !EnsureStreamCap(g_vk.skinCylBuf, g_vk.skinCylMem, g_vk.skinCylMap,
                         g_vk.skinCylCap, cur, skinCylCount))
        skinCylCount = 0;
    if (skinCylCount > 0)
        memcpy(g_vk.skinCylMap[cur], skinCylData, (size_t)skinCylCount * 80);

    vkResetCommandBuffer(g_vk.cmd[cur], 0);
    VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(g_vk.cmd[cur],&bi);
    // Sun-shadow pass (V178): depth-only render of the same casters from
    // the light. Runs (at least as a clear) whenever the stack exists so
    // the map's layout is always valid for the lit pass that samples it.
    if (g_vk.shReady) {
        DFN(vkCmdNextSubpass);
        (void)vkCmdNextSubpass;
        VkClearValue shClear;
        shClear.depthStencil = { 1.0f, 0 };
        VkRenderPassBeginInfo srbi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        srbi.renderPass = g_vk.shRp;
        srbi.framebuffer = g_vk.shFb;
        srbi.renderArea = { { 0, 0 }, { SHADOW_RES_VK, SHADOW_RES_VK } };
        srbi.clearValueCount = 1;
        srbi.pClearValues = &shClear;
        vkCmdBeginRenderPass(g_vk.cmd[cur], &srbi, VK_SUBPASS_CONTENTS_INLINE);
        if ((flags & 1) && lightVP16) {
            VkViewport svp = { 0, (float)SHADOW_RES_VK, (float)SHADOW_RES_VK,
                               -(float)SHADOW_RES_VK, 0, 1 };
            VkRect2D ssc = { { 0, 0 }, { SHADOW_RES_VK, SHADOW_RES_VK } };
            vkCmdSetViewport(g_vk.cmd[cur], 0, 1, &svp);
            vkCmdSetScissor(g_vk.cmd[cur], 0, 1, &ssc);
            vkCmdPushConstants(g_vk.cmd[cur], g_vk.shLayout,
                               VK_SHADER_STAGE_VERTEX_BIT, 0, 64, lightVP16);
            if (g_vk.terrBuf && g_vk.shMeshPipe) {
                vkCmdBindPipeline(g_vk.cmd[cur], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  g_vk.shMeshPipe);
                VkDeviceSize toff = 0;
                vkCmdBindVertexBuffers(g_vk.cmd[cur], 0, 1, &g_vk.terrBuf, &toff);
                vkCmdDraw(g_vk.cmd[cur], (uint32_t)g_vk.terrCount, 1, 0, 0);
            }
            if (count > 0) {
                vkCmdBindPipeline(g_vk.cmd[cur], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  g_vk.shBoxPipe);
                VkBuffer sbufs[2] = { g_vk.cubeBuf, g_vk.instBuf[cur] };
                VkDeviceSize soffs[2] = { 0, 0 };
                vkCmdBindVertexBuffers(g_vk.cmd[cur], 0, 2, sbufs, soffs);
                vkCmdDraw(g_vk.cmd[cur], 36, (uint32_t)count, 0, 0);
            }
            if (pillCount > 0) {   // pills cast too (V179)
                vkCmdBindPipeline(g_vk.cmd[cur], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  g_vk.shBoxPipe);
                VkBuffer pbufs[2] = { g_vk.sphereBuf, g_vk.pillBuf[cur] };
                VkDeviceSize poffs[2] = { 0, 0 };
                vkCmdBindVertexBuffers(g_vk.cmd[cur], 0, 2, pbufs, poffs);
                vkCmdDraw(g_vk.cmd[cur], (uint32_t)g_vk.sphereVerts,
                          (uint32_t)pillCount, 0, 0);
            }
            if (skinBoxCount > 0) {   // skinned bodies cast (V180): depth
                vkCmdBindPipeline(g_vk.cmd[cur], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  g_vk.shBoxPipe);   // needs no texture
                VkBuffer kbufs[2] = { g_vk.cubeBuf, g_vk.skinBoxBuf[cur] };
                VkDeviceSize koffs[2] = { 0, 0 };
                vkCmdBindVertexBuffers(g_vk.cmd[cur], 0, 2, kbufs, koffs);
                vkCmdDraw(g_vk.cmd[cur], 36, (uint32_t)skinBoxCount, 0, 0);
            }
            if (skinPillCount > 0) {
                vkCmdBindPipeline(g_vk.cmd[cur], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  g_vk.shBoxPipe);
                VkBuffer kbufs[2] = { g_vk.sphereBuf, g_vk.skinPillBuf[cur] };
                VkDeviceSize koffs[2] = { 0, 0 };
                vkCmdBindVertexBuffers(g_vk.cmd[cur], 0, 2, kbufs, koffs);
                vkCmdDraw(g_vk.cmd[cur], (uint32_t)g_vk.sphereVerts,
                          (uint32_t)skinPillCount, 0, 0);
            }
            if (cylCount > 0) {   // capsule shafts cast (V185)
                vkCmdBindPipeline(g_vk.cmd[cur], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  g_vk.shBoxPipe);
                VkBuffer cbufs[2] = { g_vk.cylBuf, g_vk.cylInst[cur] };
                VkDeviceSize coffs[2] = { 0, 0 };
                vkCmdBindVertexBuffers(g_vk.cmd[cur], 0, 2, cbufs, coffs);
                vkCmdDraw(g_vk.cmd[cur], (uint32_t)g_vk.cylVerts,
                          (uint32_t)cylCount, 0, 0);
            }
            if (skinCylCount > 0) {
                vkCmdBindPipeline(g_vk.cmd[cur], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  g_vk.shBoxPipe);
                VkBuffer cbufs[2] = { g_vk.cylBuf, g_vk.skinCylBuf[cur] };
                VkDeviceSize coffs[2] = { 0, 0 };
                vkCmdBindVertexBuffers(g_vk.cmd[cur], 0, 2, cbufs, coffs);
                vkCmdDraw(g_vk.cmd[cur], (uint32_t)g_vk.cylVerts,
                          (uint32_t)skinCylCount, 0, 0);
            }
        }
        vkCmdEndRenderPass(g_vk.cmd[cur]);
    }
    VkClearValue clears[2];
    clears[0].color = { { 0, 0, 0, 0 } };            // transparent: GL composites
    clears[1].depthStencil = { 1.0f, 0 };
    VkRenderPassBeginInfo rbi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rbi.renderPass = g_vk.rp;
    rbi.framebuffer = g_vk.fb;
    rbi.renderArea = { { 0, 0 }, { (uint32_t)w, (uint32_t)h } };
    rbi.clearValueCount = 2;
    rbi.pClearValues = clears;
    vkCmdBeginRenderPass(g_vk.cmd[cur],&rbi, VK_SUBPASS_CONTENTS_INLINE);
    {
        // Negative-height viewport (core 1.1) keeps GL clip conventions, so
        // the game's matrices work unmodified and row 0 reads back as top.
        VkViewport vp = { 0, (float)h, (float)w, -(float)h, 0, 1 };
        VkRect2D sc = { { 0, 0 }, { (uint32_t)w, (uint32_t)h } };
        vkCmdSetViewport(g_vk.cmd[cur],0, 1, &vp);
        vkCmdSetScissor(g_vk.cmd[cur],0, 1, &sc);
        float pc[20];
        memcpy(pc, viewProj16, 64);
        memcpy(pc + 16, sun4, 16);
        vkCmdPushConstants(g_vk.cmd[cur],g_vk.layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, 80, pc);
        if (g_vk.terrBuf && g_vk.meshPipe) {   // depth-only hillside occluder
            vkCmdBindPipeline(g_vk.cmd[cur],VK_PIPELINE_BIND_POINT_GRAPHICS, g_vk.meshPipe);
            VkDeviceSize toff = 0;
            vkCmdBindVertexBuffers(g_vk.cmd[cur],0, 1, &g_vk.terrBuf, &toff);
            vkCmdDraw(g_vk.cmd[cur],(uint32_t)g_vk.terrCount, 1, 0, 0);
        }
        if (count > 0) {
            VkBuffer bufs[2] = { g_vk.cubeBuf, g_vk.instBuf[cur] };
            VkDeviceSize offs[2] = { 0, 0 };
            if (g_vk.shReady && lightVP16) {
                // Shadowed road (V178): lit pipeline samples the sun map.
                DFN(vkCmdBindDescriptorSets);
                vkCmdBindPipeline(g_vk.cmd[cur], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  g_vk.litPipe);
                vkCmdBindDescriptorSets(g_vk.cmd[cur], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        g_vk.litLayout, 0, 1, &g_vk.shSet, 0, NULL);
                float lpc[36];
                memcpy(lpc, viewProj16, 64);
                memcpy(lpc + 16, lightVP16, 64);
                lpc[32] = sun4[0];
                lpc[33] = sun4[1];
                lpc[34] = sun4[2];
                lpc[35] = (flags & 1) ? 1.0f : 0.0f;
                vkCmdPushConstants(g_vk.cmd[cur], g_vk.litLayout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, 144, lpc);
            } else {
                vkCmdBindPipeline(g_vk.cmd[cur], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  g_vk.pipe);
            }
            vkCmdBindVertexBuffers(g_vk.cmd[cur],0, 2, bufs, offs);
            vkCmdDraw(g_vk.cmd[cur],36, (uint32_t)count, 0, 0);
        }
        if (pillCount > 0) {   // the rounded bodies (V179), same pipeline
            VkBuffer pbufs[2] = { g_vk.sphereBuf, g_vk.pillBuf[cur] };
            VkDeviceSize poffs[2] = { 0, 0 };
            if (!(count > 0)) {   // nobody bound a pipeline yet this pass
                if (g_vk.shReady && lightVP16) {
                    DFN(vkCmdBindDescriptorSets);
                    vkCmdBindPipeline(g_vk.cmd[cur], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      g_vk.litPipe);
                    vkCmdBindDescriptorSets(g_vk.cmd[cur], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            g_vk.litLayout, 0, 1, &g_vk.shSet, 0, NULL);
                    float lpc[36];
                    memcpy(lpc, viewProj16, 64);
                    memcpy(lpc + 16, lightVP16, 64);
                    lpc[32] = sun4[0];
                    lpc[33] = sun4[1];
                    lpc[34] = sun4[2];
                    lpc[35] = (flags & 1) ? 1.0f : 0.0f;
                    vkCmdPushConstants(g_vk.cmd[cur], g_vk.litLayout,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, 144, lpc);
                } else {
                    vkCmdBindPipeline(g_vk.cmd[cur], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      g_vk.pipe);
                }
            }
            vkCmdBindVertexBuffers(g_vk.cmd[cur], 0, 2, pbufs, poffs);
            vkCmdDraw(g_vk.cmd[cur], (uint32_t)g_vk.sphereVerts,
                      (uint32_t)pillCount, 0, 0);
        }
        if (cylCount > 0) {   // capsule shafts (V185), same pipeline
            VkBuffer cbufs[2] = { g_vk.cylBuf, g_vk.cylInst[cur] };
            VkDeviceSize coffs[2] = { 0, 0 };
            if (count <= 0 && pillCount <= 0) {   // nobody bound yet this pass
                if (g_vk.shReady && lightVP16) {
                    DFN(vkCmdBindDescriptorSets);
                    vkCmdBindPipeline(g_vk.cmd[cur], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      g_vk.litPipe);
                    vkCmdBindDescriptorSets(g_vk.cmd[cur], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            g_vk.litLayout, 0, 1, &g_vk.shSet, 0, NULL);
                    float lpc[36];
                    memcpy(lpc, viewProj16, 64);
                    memcpy(lpc + 16, lightVP16, 64);
                    lpc[32] = sun4[0];
                    lpc[33] = sun4[1];
                    lpc[34] = sun4[2];
                    lpc[35] = (flags & 1) ? 1.0f : 0.0f;
                    vkCmdPushConstants(g_vk.cmd[cur], g_vk.litLayout,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, 144, lpc);
                } else {
                    vkCmdBindPipeline(g_vk.cmd[cur], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      g_vk.pipe);
                }
            }
            vkCmdBindVertexBuffers(g_vk.cmd[cur], 0, 2, cbufs, coffs);
            vkCmdDraw(g_vk.cmd[cur], (uint32_t)g_vk.cylVerts,
                      (uint32_t)cylCount, 0, 0);
        }
        // Skinned streams (V180): per-segment draws with the atlas row in
        // the push constant. Fail-soft: without the skin pipeline they run
        // through the plain lit/no-shadow pipeline, untextured but present.
        if (skinBoxCount > 0 || skinPillCount > 0 || skinCylCount > 0) {
            DFN(vkCmdBindDescriptorSets);
            const bool skinned = g_vk.skinReady && lightVP16;
            float kpc[40];
            memcpy(kpc, viewProj16, 64);
            if (lightVP16) memcpy(kpc + 16, lightVP16, 64);
            kpc[32] = sun4[0];
            kpc[33] = sun4[1];
            kpc[34] = sun4[2];
            kpc[35] = (flags & 1) ? 1.0f : 0.0f;
            auto bindSkinned = [&]() {
                if (skinned) {
                    vkCmdBindPipeline(g_vk.cmd[cur], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      g_vk.skinPipe);
                    vkCmdBindDescriptorSets(g_vk.cmd[cur], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            g_vk.skinLayout, 0, 1, &g_vk.skinSet, 0, NULL);
                } else if (g_vk.shReady && lightVP16) {
                    vkCmdBindPipeline(g_vk.cmd[cur], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      g_vk.litPipe);
                    vkCmdBindDescriptorSets(g_vk.cmd[cur], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            g_vk.litLayout, 0, 1, &g_vk.shSet, 0, NULL);
                    vkCmdPushConstants(g_vk.cmd[cur], g_vk.litLayout,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, 144, kpc);
                } else {
                    vkCmdBindPipeline(g_vk.cmd[cur], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      g_vk.pipe);
                    float ppc[20];
                    memcpy(ppc, viewProj16, 64);
                    memcpy(ppc + 16, sun4, 16);
                    vkCmdPushConstants(g_vk.cmd[cur], g_vk.layout,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, 80, ppc);
                }
            };
            auto drawSegs = [&](VkBuffer meshVB, uint32_t meshVerts, VkBuffer instVB,
                                const int* segSkin, const int* segCount, int nSegs) {
                VkBuffer bufs2[2] = { meshVB, instVB };
                VkDeviceSize offs2[2] = { 0, 0 };
                vkCmdBindVertexBuffers(g_vk.cmd[cur], 0, 2, bufs2, offs2);
                int first = 0;
                for (int s = 0; s < nSegs; ++s) {
                    const int n = segCount[s];
                    if (n <= 0) continue;
                    if (skinned) {
                        const int row = segSkin[s] < 0 ? 0 : segSkin[s] > 3 ? 3 : segSkin[s];
                        kpc[36] = 0.0f;                 // skinRect.x
                        kpc[37] = row * 0.25f;          // skinRect.y
                        kpc[38] = 1.0f;                 // skinRect.z
                        kpc[39] = 0.25f;                // skinRect.w
                        vkCmdPushConstants(g_vk.cmd[cur], g_vk.skinLayout,
                                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                           0, 160, kpc);
                    }
                    vkCmdDraw(g_vk.cmd[cur], meshVerts, (uint32_t)n, 0, (uint32_t)first);
                    first += n;
                }
            };
            if (skinBoxCount > 0) {
                bindSkinned();
                drawSegs(g_vk.cubeBuf, 36, g_vk.skinBoxBuf[cur],
                         skinBoxSegSkin, skinBoxSegCount, nSkinBoxSegs);
            }
            if (skinPillCount > 0) {
                bindSkinned();
                drawSegs(g_vk.sphereBuf, (uint32_t)g_vk.sphereVerts,
                         g_vk.skinPillBuf[cur],
                         skinPillSegSkin, skinPillSegCount, nSkinPillSegs);
            }
            if (skinCylCount > 0) {   // skinned capsule shafts (V185)
                bindSkinned();
                drawSegs(g_vk.cylBuf, (uint32_t)g_vk.cylVerts,
                         g_vk.skinCylBuf[cur],
                         skinCylSegSkin, skinCylSegCount, nSkinCylSegs);
            }
        }
    }
    vkCmdEndRenderPass(g_vk.cmd[cur]);
    VkBufferImageCopy region = {};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent = { (uint32_t)w, (uint32_t)h, 1 };
    vkCmdCopyImageToBuffer(g_vk.cmd[cur], g_vk.color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           g_vk.readBuf[cur], 1, &region);
    vkEndCommandBuffer(g_vk.cmd[cur]);
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1;
    si.pCommandBuffers = &g_vk.cmd[cur];
    vkQueueSubmit(g_vk.queue, 1, &si, g_vk.fence[cur]);
    g_vk.submitted[cur] = true;
    // Present the other slot: its GPU work has had a full frame to land,
    // so this wait is normally instant â€” the pipelining win (V166).
    const int prev = cur ^ 1;
    if (!g_vk.submitted[prev]) return nullptr;   // very first frame: GL covers it
    vkWaitForFences(g_vk.dev, 1, &g_vk.fence[prev], VK_TRUE, UINT64_MAX);
    return (const unsigned char*)g_vk.readMap[prev];
}

}  // namespace rdr

#else
namespace rdr {
bool VulkanExecutorReady() { return false; }
void VulkanSetTerrain(const float*, int) {}
void VulkanSetUiAtlas(const unsigned char*, int, int) {}
int VulkanRegisterUiTexture(const unsigned char*, int, int) { return -1; }
const unsigned char* VulkanRenderUi(const void*, int, const int*, const int*,
                                    int, int, int) { return nullptr; }
void VulkanSetSkinAtlas(const unsigned char*, int, int) {}
const unsigned char* VulkanRenderFrame(const float*, const float*, const float*,
                                       int, const void*, int, const void*, int,
                                       const void*, const int*, const int*, int,
                                       const void*, const int*, const int*, int,
                                       const void*, int,
                                       const void*, const int*, const int*, int,
                                       int, int) { return nullptr; }
}
#endif
