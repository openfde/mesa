/*
 * Copyright 2017 Google
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <hardware/gralloc.h>
#include <hardware/hardware.h>
#include <hardware/hwvulkan.h>
#include <vulkan/vk_android_native_buffer.h>
#include <vulkan/vk_icd.h>
#include <sync/sync.h>

#include "anv_private.h"

static int anv_hal_open(const struct hw_module_t* mod, const char* id, struct hw_device_t** dev);
static int anv_hal_close(struct hw_device_t *dev);

static void UNUSED
static_asserts(void)
{
   STATIC_ASSERT(HWVULKAN_DISPATCH_MAGIC == ICD_LOADER_MAGIC);
}

PUBLIC struct hwvulkan_module_t HAL_MODULE_INFO_SYM = {
   .common = {
      .tag = HARDWARE_MODULE_TAG,
      .module_api_version = HWVULKAN_MODULE_API_VERSION_0_1,
      .hal_api_version = HARDWARE_MAKE_API_VERSION(1, 0),
      .id = HWVULKAN_HARDWARE_MODULE_ID,
      .name = "Intel Vulkan HAL",
      .author = "Intel",
      .methods = &(hw_module_methods_t) {
         .open = anv_hal_open,
      },
   },
};

static int
anv_hal_open(const struct hw_module_t* mod, const char* id,
             struct hw_device_t** dev)
{
   assert(mod == &HAL_MODULE_INFO_SYM.common);
   assert(strcmp(id, HWVULKAN_DEVICE_0) == 0);

   hwvulkan_device_t *vkdev = malloc(sizeof(*vkdev));
   if (!vkdev)
      return -1;

   *vkdev = (hwvulkan_device_t) {
      .common = {
         .tag = HARDWARE_DEVICE_TAG,
         .version = HWVULKAN_DEVICE_API_VERSION_0_1,
         .module = &HAL_MODULE_INFO_SYM.common,
         .close = anv_hal_close,
      },
     .EnumerateInstanceExtensionProperties = anv_EnumerateInstanceExtensionProperties,
     .CreateInstance = anv_CreateInstance,
     .GetInstanceProcAddr = anv_GetInstanceProcAddr,
   };

   *dev = &vkdev->common;
   return 0;
}

static int
anv_hal_close(struct hw_device_t *dev)
{
   /* hwvulkan.h claims that hw_device_t::close() is never called. */
   return -1;
}

VkResult anv_GetSwapchainGrallocUsageANDROID(
    VkDevice            device_h,
    VkFormat            format,
    VkImageUsageFlags   imageUsage,
    int*                grallocUsage)
{
   ANV_FROM_HANDLE(anv_device, device, device_h);
   struct anv_physical_device *phys_dev = &device->instance->physicalDevice;
   VkPhysicalDevice phys_dev_h = anv_physical_device_to_handle(phys_dev);
   VkFormatProperties props;

   /* The below formats support GRALLOC_USAGE_HW_FB (that is, display
    * scanout). This short list of formats is univserally supported on Intel
    * but is incomplete.  The full set of supported formats is dependent on
    * kernel and hardware.
    */
   static const VkFormat fb_formats[] = {
      VK_FORMAT_B8G8R8A8_UNORM,
      VK_FORMAT_B5G6R5_UNORM_PACK16,
   };

   *grallocUsage = 0;
   anv_GetPhysicalDeviceFormatProperties(phys_dev_h, format, &props);

   for (uint32_t i = 0; i < ARRAY_SIZE(fb_formats); ++i) {
      if (format == fb_formats[i]) {
          *grallocUsage |= GRALLOC_USAGE_HW_FB |
                           GRALLOC_USAGE_HW_COMPOSER |
                           GRALLOC_USAGE_EXTERNAL_DISP;
          break;
      }
   }

  if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
     *grallocUsage |= GRALLOC_USAGE_HW_TEXTURE;

  if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT))
     *grallocUsage |= GRALLOC_USAGE_HW_RENDER;

  if (*grallocUsage == 0)
     return VK_ERROR_FORMAT_NOT_SUPPORTED;

  return VK_SUCCESS;
}

VkResult
anv_AcquireImageANDROID(
      VkDevice            device_h,
      VkImage             image_h,
      int                 nativeFenceFd,
      VkSemaphore         semaphore_h,
      VkFence             fence_h)
{
   ANV_FROM_HANDLE(anv_device, device, device_h);
   ANV_FROM_HANDLE(anv_semaphore, semaphore, semaphore_h);
   ANV_FROM_HANDLE(anv_fence, fence, fence_h);
   VkResult result = VK_SUCCESS;

   if (nativeFenceFd != -1) {
      /* As a simple, firstpass implementation of VK_ANDROID_native_buffer, we
       * block on the nativeFenceFd. This may introduce latency and is
       * definitiely inefficient, yet it's correct.
       *
       * FINISHME(chadv): Import the nativeFenceFd into the VkSemaphore and
       * VkFence.
       */
      if (sync_wait(nativeFenceFd, /*timeout*/ -1) < 0) {
         result = vk_errorf(device->instance, device, VK_ERROR_DEVICE_LOST,
                            "%s: failed to wait on nativeFenceFd=%d",
                            __func__, nativeFenceFd);
         goto done;
      }
   }

   if (semaphore_h || fence_h) {
      /* Thanks to implicit sync, the image is ready for GPU access.  But we
       * must still put the semaphore into the "submit" state; otherwise the
       * client may get unexpected behavior if the client later uses it as
       * a wait semaphore.
       *
       * Because we blocked above on the nativeFenceFd, the image is also
       * ready for foreign-device access (including CPU access). But we must
       * still signal the fence; otherwise the client may get unexpected
       * behavior if the client later waits on it.
       *
       * For some values of anv_semaphore_type, we must submit the semaphore
       * to execbuf in order to signal it.  Likewise for anv_fence_type.
       * Instead of open-coding here the signal operation for each
       * anv_semaphore_type and anv_fence_type, we piggy-back on
       * vkQueueSubmit.
       */
      const VkSubmitInfo submit = {
         .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .waitSemaphoreCount = 0,
         .commandBufferCount = 0,
         .signalSemaphoreCount = (semaphore_h ? 1 : 0),
         .pSignalSemaphores = &semaphore_h,
      };

      result = anv_QueueSubmit(anv_queue_to_handle(&device->queue), 1,
                               &submit, fence_h);
      if (result != VK_SUCCESS) {
         intel_loge("anv_QueueSubmit failed inside %s", __func__);
         goto done;
      }
   }

 done:
   if (nativeFenceFd != -1) {
      /* From VK_ANDROID_native_buffer's pseudo spec
       * (https://source.android.com/devices/graphics/implement-vulkan):
       *
       *    The driver takes ownership of the fence fd and is responsible for
       *    closing it [...] even if vkAcquireImageANDROID fails and returns
       *    an error.
       */
      close(nativeFenceFd);
   }

   return result;
}

VkResult
anv_QueueSignalReleaseImageANDROID(
      VkQueue             queue,
      uint32_t            waitSemaphoreCount,
      const VkSemaphore*  pWaitSemaphores,
      VkImage             image,
      int*                pNativeFenceFd)
{
   VkResult result;

   if (waitSemaphoreCount == 0)
      goto done;

   result = anv_QueueSubmit(queue, 1,
      &(VkSubmitInfo) {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = pWaitSemaphores,
      },
      (VkFence) VK_NULL_HANDLE);
   if (result != VK_SUCCESS)
      return result;

 done:
   if (pNativeFenceFd) {
      /* We can rely implicit on sync because above we submitted all
       * semaphores to the queue.
       */
      *pNativeFenceFd = -1;
   }

   return VK_SUCCESS;
}
