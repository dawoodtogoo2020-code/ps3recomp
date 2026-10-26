/* SPDX-License-Identifier: MIT
 * Vulkan sample transfer, implemented from the contract in
 * docs/RENDERER-PROVENANCE.md. Included after vk_eng_translate.
 * Extraction and insertion share pipeline construction and descriptor storage.
 */
#include "rsx_sample_transfer.h"

typedef struct vk_sample_pass {
    VkFormat format;
    u32 samples;
    VkRenderPass render_pass;
    VkPipeline pipeline;
} vk_sample_pass;

/* Three extraction formats and two insertion formats at two sample counts. */
static struct {
    VkDescriptorSetLayout descriptors;
    VkDescriptorPool pool;
    VkPipelineLayout layout;
    VkShaderModule vertex, extract, insert;
    vk_sample_pass passes[7];
    VkDescriptorSet upload;
    VkDescriptorSet recycled[VK_ENG_MAX_OBJ];
    u32 recycle_count;
} s_sample_transfer;

static void vk_sample_drop_pass(vk_sample_pass* pass)
{
    if (pass->pipeline) pvkDestroyPipeline(s_vk.device, pass->pipeline, NULL);
    if (pass->render_pass) pvkDestroyRenderPass(s_vk.device, pass->render_pass, NULL);
    memset(pass, 0, sizeof *pass);
}

static void vk_eng_expansion_shutdown(void)
{
    for (u32 i = 0; i < 7; ++i) vk_sample_drop_pass(&s_sample_transfer.passes[i]);
    VkShaderModule modules[] = {s_sample_transfer.vertex, s_sample_transfer.extract,
                               s_sample_transfer.insert};
    for (u32 i = 0; i < 3; ++i)
        if (modules[i]) pvkDestroyShaderModule(s_vk.device, modules[i], NULL);
    if (s_sample_transfer.layout)
        pvkDestroyPipelineLayout(s_vk.device, s_sample_transfer.layout, NULL);
    if (s_sample_transfer.pool)
        pvkDestroyDescriptorPool(s_vk.device, s_sample_transfer.pool, NULL);
    if (s_sample_transfer.descriptors)
        pvkDestroyDescriptorSetLayout(s_vk.device, s_sample_transfer.descriptors, NULL);
    memset(&s_sample_transfer, 0, sizeof s_sample_transfer);
}

/* Kept for the backend's teardown interface; resources have one owner now. */
static void vk_eng_pack_shutdown(void) {}

static int vk_sample_shader(VkShaderModule* module, const char* text, int stage)
{
    if (*module) return 1;
    VkShaderModule created = VK_NULL_HANDLE;
    if (vk_eng_translate(text, stage, &created) || !created) return 0;
    *module = created;
    return 1;
}

static int vk_sample_init(void)
{
    if (!s_sample_transfer.descriptors) {
        const VkDescriptorSetLayoutBinding binding = {
            .binding = RSX_SPIRV_TEXTURE_BINDING,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT};
        const VkDescriptorSetLayoutCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 1, .pBindings = &binding};
        VkDescriptorSetLayout created = VK_NULL_HANDLE;
        if (pvkCreateDescriptorSetLayout(s_vk.device, &info, NULL, &created) != VK_SUCCESS) return 0;
        s_sample_transfer.descriptors = created;
    }
    if (!s_sample_transfer.layout) {
        const VkPushConstantRange constants = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16};
        const VkPipelineLayoutCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1, .pSetLayouts = &s_sample_transfer.descriptors,
            .pushConstantRangeCount = 1, .pPushConstantRanges = &constants};
        VkPipelineLayout created = VK_NULL_HANDLE;
        if (pvkCreatePipelineLayout(s_vk.device, &info, NULL, &created) != VK_SUCCESS) return 0;
        s_sample_transfer.layout = created;
    }
    if (!s_sample_transfer.pool) {
        const VkDescriptorPoolSize capacity = {
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_ENG_MAX_OBJ + 1};
        const VkDescriptorPoolCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = VK_ENG_MAX_OBJ + 1, .poolSizeCount = 1, .pPoolSizes = &capacity};
        VkDescriptorPool created = VK_NULL_HANDLE;
        if (pvkCreateDescriptorPool(s_vk.device, &info, NULL, &created) != VK_SUCCESS) return 0;
        s_sample_transfer.pool = created;
    }
    return vk_sample_shader(&s_sample_transfer.vertex, rsx_sample_vertex_hlsl,
                             RSX_SHADER_STAGE_VERTEX);
}

static int vk_sample_set(VkDescriptorSet* set, VkImageView view)
{
    if (!*set) {
        if (s_sample_transfer.recycle_count) {
            *set = s_sample_transfer.recycled[--s_sample_transfer.recycle_count];
        } else {
            const VkDescriptorSetAllocateInfo info = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = s_sample_transfer.pool, .descriptorSetCount = 1,
                .pSetLayouts = &s_sample_transfer.descriptors};
            if (pvkAllocateDescriptorSets(s_vk.device, &info, set) != VK_SUCCESS) {
                *set = VK_NULL_HANDLE;
                return 0;
            }
        }
    }
    const VkDescriptorImageInfo image = {VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL};
    const VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = *set,
        .dstBinding = RSX_SPIRV_TEXTURE_BINDING, .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .pImageInfo = &image};
    pvkUpdateDescriptorSets(s_vk.device, 1, &write, 0, NULL);
    return 1;
}

static vk_sample_pass* vk_sample_pipeline(VkFormat format, u32 samples)
{
    const int insert = samples > 1;
    if ((samples != 1 && samples != 2 && samples != 4) ||
        (format != VK_FORMAT_R8G8B8A8_UNORM && format != VK_FORMAT_R16G16B16A16_SFLOAT &&
         (format != VK_FORMAT_R32_SFLOAT || insert))) return NULL;
    vk_sample_pass* vacant = NULL;
    for (u32 i = 0; i < 7; ++i) {
        vk_sample_pass* p = &s_sample_transfer.passes[i];
        if (p->pipeline && p->format == format && p->samples == samples) return p;
        if (!p->pipeline && !vacant) vacant = p;
    }
    if (!vacant || !vk_sample_init()) return NULL;
    VkShaderModule* fragment = insert ? &s_sample_transfer.insert : &s_sample_transfer.extract;
    if (!vk_sample_shader(fragment, insert ? rsx_sample_insert_hlsl : rsx_sample_extract_hlsl,
                          RSX_SHADER_STAGE_FRAGMENT)) return NULL;
    vk_sample_pass candidate = {.format = format, .samples = samples};
    const VkAttachmentDescription attachment = {
        .format = format, .samples = (VkSampleCountFlagBits)samples,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_GENERAL, .finalLayout = VK_IMAGE_LAYOUT_GENERAL};
    /* Insertion overwrites every sample within ONE render pass. DONT_CARE at
     * its beginning is safe: it does not discard samples between the draws. */
    const VkAttachmentReference output = {0, VK_IMAGE_LAYOUT_GENERAL};
    const VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1, .pColorAttachments = &output};
    const VkRenderPassCreateInfo pass_info = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &attachment, .subpassCount = 1, .pSubpasses = &subpass};
    if (pvkCreateRenderPass(s_vk.device, &pass_info, NULL, &candidate.render_pass) != VK_SUCCESS)
        return NULL;
    const VkPipelineShaderStageCreateInfo shaders[] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = s_sample_transfer.vertex, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = *fragment, .pName = "main"}};
    const VkPipelineVertexInputStateCreateInfo vertices = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    const VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    const VkPipelineViewportStateCreateInfo viewport = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .scissorCount = 1};
    const VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f};
    const VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = (VkSampleCountFlagBits)samples};
    const VkPipelineColorBlendAttachmentState channels = {.colorWriteMask = 0xf};
    const VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &channels};
    const VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamic = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2, .pDynamicStates = states};
    const VkGraphicsPipelineCreateInfo pipeline = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = shaders, .pVertexInputState = &vertices,
        .pInputAssemblyState = &assembly, .pViewportState = &viewport,
        .pRasterizationState = &raster, .pMultisampleState = &multisample,
        .pColorBlendState = &blend, .pDynamicState = &dynamic,
        .layout = s_sample_transfer.layout, .renderPass = candidate.render_pass};
    if (pvkCreateGraphicsPipelines(s_vk.device, vk_pcache(), 1, &pipeline, NULL,
                                   &candidate.pipeline) != VK_SUCCESS) {
        vk_sample_drop_pass(&candidate);
        return NULL;
    }
    *vacant = candidate;
    return vacant;
}

static int vk_sample_framebuffer(VkFramebuffer* framebuffer, const vk_sample_pass* pass,
                                 const vk_eng_obj* target)
{
    const VkFramebufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = pass->render_pass, .attachmentCount = 1, .pAttachments = &target->im.view,
        .width = target->w, .height = target->h, .layers = 1};
    VkFramebuffer created = VK_NULL_HANDLE;
    if (pvkCreateFramebuffer(s_vk.device, &info, NULL, &created) != VK_SUCCESS) return 0;
    *framebuffer = created;
    return 1;
}

/* Records only. Caller owns synchronization, barriers, lifetime and submission. */
static void vk_sample_draw(const vk_sample_pass* pass, VkFramebuffer framebuffer,
                            VkDescriptorSet source, u32 width, u32 height)
{
    const VkRenderPassBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = pass->render_pass, .framebuffer = framebuffer,
        .renderArea = {{0, 0}, {width, height}}};
    const VkViewport viewport = {0, 0, (float)width, (float)height, 0, 1};
    const VkRect2D scissor = {{0, 0}, {width, height}};
    pvkCmdBeginRenderPass(s_vk.cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
    pvkCmdSetViewport(s_vk.cmd, 0, 1, &viewport);
    pvkCmdSetScissor(s_vk.cmd, 0, 1, &scissor);
    pvkCmdBindPipeline(s_vk.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pass->pipeline);
    pvkCmdBindDescriptorSets(s_vk.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            s_sample_transfer.layout, 0, 1, &source, 0, NULL);
    for (u32 slot = 0; slot < pass->samples; ++slot) {
        if (pass->samples > 1) {
            const u32 parameters[] = {slot, pass->samples / 2, 0, 0};
            pvkCmdPushConstants(s_vk.cmd, s_sample_transfer.layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                                0, sizeof parameters, parameters);
        }
        pvkCmdDraw(s_vk.cmd, 3, 1, 0, 0);
    }
    pvkCmdEndRenderPass(s_vk.cmd);
}

static void vk_eng_expansion_forget(vk_eng_obj* target)
{
    if (target->exp_fb || target->exp_view || target->exp_set) {
        vk_flush_pending(1);
        if (target->exp_fb) pvkDestroyFramebuffer(s_vk.device, target->exp_fb, NULL);
        if (target->exp_view) pvkDestroyImageView(s_vk.device, target->exp_view, NULL);
        if (target->exp_set && s_sample_transfer.pool &&
            s_sample_transfer.recycle_count < VK_ENG_MAX_OBJ)
            s_sample_transfer.recycled[s_sample_transfer.recycle_count++] = target->exp_set;
    }
    target->exp_fb = VK_NULL_HANDLE;
    target->exp_view = VK_NULL_HANDLE;
    target->exp_set = VK_NULL_HANDLE;
    target->exp_dst = 0;
    target->expansion_valid = 0;
}

static u32 vk_eng_expand_samples(u32 surface)
{
    if (!surface || surface >= VK_ENG_MAX_OBJ) return 0;
    vk_eng_obj* target = &s_eobj[surface];
    if (target->kind != VK_ENG_COLOR && target->kind != VK_ENG_DEPTH) return 0;
    u32 width, height;
    if (!rsx_sample_extent(target->w, target->h, target->samples, &width, &height) ||
        width > s_vk.max_dim || height > s_vk.max_dim) return 0;
    if (target->expansion_valid && vk_eng_get(target->expanded, VK_ENG_TEXTURE))
        return target->expanded;
    const int depth = target->kind == VK_ENG_DEPTH;
    const VkFormat format = depth ? VK_FORMAT_R32_SFLOAT : target->fmt;
    vk_sample_pass* pass = vk_sample_pipeline(format, 1);
    if (!pass) goto failed;
    vk_eng_obj* storage = vk_eng_get(target->expanded, VK_ENG_TEXTURE);
    if (!storage) {
        target->expanded = 0;
        const u32 handle = vk_eng_alloc();
        if (!handle) goto failed;
        storage = &s_eobj[handle];
        if (vk_create_image(&storage->im, format, width, height,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT)) {
            vk_destroy_image(&storage->im);
            memset(storage, 0, sizeof *storage);
            goto failed;
        }
        /* Initialize layout before publishing ownership, so a failed begin
         * cannot leave a retry seeing an image still in UNDEFINED. */
        if (vk_begin()) {
            vk_destroy_image(&storage->im);
            memset(storage, 0, sizeof *storage);
            goto failed;
        }
        vk_barrier_image(storage->im.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED);
        if (vk_submit_or_defer()) {
            vk_destroy_image(&storage->im);
            memset(storage, 0, sizeof *storage);
            goto failed;
        }
        storage->kind = VK_ENG_TEXTURE;
        storage->fmt = format;
        storage->bpp = depth ? 4 : target->bpp;
        storage->w = width; storage->h = height;
        storage->scale = target->scale;
        storage->grid_x = 2; storage->grid_y = target->samples / 2;
        if (target->guest_w && !rsx_sample_extent(target->guest_w, target->guest_h,
                target->samples, &storage->guest_w, &storage->guest_h)) {
            /* Physical extent was checked above. A malformed guest extent
             * must not wrap into apparently valid metadata. */
            vk_flush_pending(1);
            vk_destroy_image(&storage->im);
            memset(storage, 0, sizeof *storage);
            goto failed;
        }
        target->expanded = handle;
    }
    if (!target->exp_fb || target->exp_dst != target->expanded) {
        vk_eng_expansion_forget(target);
        const VkImageViewCreateInfo info = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = target->im.img, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = target->fmt,
            .subresourceRange = {depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
                                 0, 1, 0, 1}};
        VkImageView created = VK_NULL_HANDLE;
        if (pvkCreateImageView(s_vk.device, &info, NULL, &created) != VK_SUCCESS) goto failed;
        target->exp_view = created;
        if (!vk_sample_framebuffer(&target->exp_fb, pass, storage) ||
            !vk_sample_set(&target->exp_set, target->exp_view)) goto failed;
        target->exp_dst = target->expanded;
    }
    if (vk_begin()) goto failed;
    vk_barrier_image(target->im.img, depth ? s_vk.depth_aspect : VK_IMAGE_ASPECT_COLOR_BIT,
                     VK_IMAGE_LAYOUT_GENERAL);
    vk_barrier_image(storage->im.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
    vk_sample_draw(pass, target->exp_fb, target->exp_set, width, height);
    if (vk_submit_or_defer()) goto failed;
    target->expansion_valid = 1;
    return target->expanded;
failed:
    vk_eng_expansion_forget(target);
    ps3_strict_boot_guard("vulkan-msaa-expand", "sample extraction failed surface=%u", surface);
    return 0;
}

static int vk_eng_pack_samples(u32 surface, u32 expanded)
{
    vk_eng_obj* target = vk_eng_get(surface, VK_ENG_COLOR);
    vk_eng_obj* storage = vk_eng_get(expanded, VK_ENG_TEXTURE);
    u32 width, height;
    if (!target || !storage || surface == expanded || storage->samples > 1 ||
        !rsx_sample_extent(target->w, target->h, target->samples, &width, &height) ||
        storage->w != width || storage->h != height || storage->fmt != target->fmt ||
        storage->bpp != target->bpp || !target->im.img || !target->im.view ||
        !storage->im.img || !storage->im.view) return 0;
    vk_sample_pass* pass = vk_sample_pipeline(target->fmt, target->samples);
    if (!pass) return 0;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    /* Updating this descriptor is legal only after all uses have completed. */
    if (s_batch.recording && (s_batch.pending || s_batch.deferred) && vk_submit_and_wait())
        return 0;
    if (!vk_sample_framebuffer(&framebuffer, pass, target) ||
        !vk_sample_set(&s_sample_transfer.upload, storage->im.view)) goto finished;
    if (vk_begin()) goto finished;
    vk_barrier_image(storage->im.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
    vk_barrier_image(target->im.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
    vk_sample_draw(pass, framebuffer, s_sample_transfer.upload, target->w, target->h);
    {
        const int success = vk_submit_and_wait() == 0;
        pvkDestroyFramebuffer(s_vk.device, framebuffer, NULL);
        target->expansion_valid = success && expanded == target->expanded;
        return success;
    }
finished:
    if (framebuffer) pvkDestroyFramebuffer(s_vk.device, framebuffer, NULL);
    return 0;
}
