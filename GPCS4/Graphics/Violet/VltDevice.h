#pragma once

#include "VltCommon.h"
#include "UtilSync.h"
#include "VltInstance.h"
#include "VltAdapter.h"
#include "VltContext.h"
#include "VltCmdList.h"
#include "VltRecycler.h"
#include "VltQueue.h"
#include "VltBuffer.h"
#include "VltImage.h"
#include "VltSampler.h"
#include "VltObject.h"
#include "VltSemaphore.h"

namespace sce::gcn
{
	class SpirvCodeBuffer;
}  // namespace sce::vlt

namespace sce::vlt
{
	class VltInstance;

	/**
     * \brief Device queue
     * 
     * Stores a Vulkan queue and the
     * queue family that it belongs to.
     */
	struct VltDeviceQueue
	{
		VkQueue  queueHandle = VK_NULL_HANDLE;
		uint32_t queueFamily = 0;
		uint32_t queueIndex  = 0;
	};

	/**
     * \brief Device queue infos
     */
	struct VltDeviceQueueSet
	{
		VltDeviceQueue graphics;
		VltDeviceQueue compute;
		VltDeviceQueue transfer;
	};

	/**
     * \brief Violet device
     * 
     * Device object. This is responsible for resource creation,
     * memory allocation, command submission and state tracking.
     * Rendering commands are recorded into command lists using
     * contexts. Multiple contexts can be created for a device.
     */
	class VltDevice : public RcObject
	{
		friend class VltSubmissionQueue;
		friend class VltDescriptorPoolTracker;
		friend class VltContext;

	public:
		VltDevice(
			const Rc<VltInstance>&     instance,
			const Rc<VltAdapter>&      adapter,
			VkDevice                   device,
			const VltDeviceExtensions& extensions,
			const VltDeviceFeatures&   features);

		~VltDevice();

		/**
         * \brief Logical device handle
         * \returns The device handle
         */
		VkDevice handle() const
		{
			return m_device;
		}

		/**
         * \brief Queue handles
         * 
         * Handles and queue family indices
         * of all known device queues.
         * \returns Device queue infos
         */
		const VltDeviceQueueSet& queues() const
		{
			return m_queues;
		}

		/**
         * \brief Tests whether a dedicated transfer queue is available
         * \returns \c true if an transfer queue is supported by the device
         */
		bool hasDedicatedTransferQueue() const
		{
			return m_queues.transfer.queueHandle != 
                m_queues.graphics.queueHandle;
		}

		/**
         * \brief Tests whether a dedicated compute queue is available
         * \returns \c true if an compute queue is supported by the device
         */
		bool hasDedicatedComputeQueue() const
		{
			return m_queues.compute.queueHandle !=
				   m_queues.graphics.queueHandle;
		}

		/**
         * \brief The instance
         * 
         * The VLT instance that created this device.
         * \returns Instance
         */
		Rc<VltInstance> instance() const
		{
			return m_instance;
		}

		/**
         * \brief The adapter
         * 
         * The physical device that the
         * device has been created for.
         * \returns Adapter
         */
		Rc<VltAdapter> adapter() const
		{
			return m_adapter;
		}

		/**
         * \brief Enabled device extensions
         * \returns Enabled device extensions
         */
		const VltDeviceExtensions& extensions() const
		{
			return m_extensions;
		}

		/**
         * \brief Enabled device features
         * \returns Enabled features
         */
		const VltDeviceFeatures& features() const
		{
			return m_features;
		}

		/**
         * \brief Device properties
         * \returns Device properties
         */
		const VltDeviceInfo& properties() const
		{
			return m_properties;
		}

		/**
         * \brief Checks whether this is a UMA system
         *
         * Basically tests whether all heaps are device-local.
         * Can be used for various optimizations in client APIs.
         * \returns \c true if the system has unified memory.
         */
		bool isUnifiedMemoryArchitecture() const;

		/**
        * \brief Queries supported shader stages
        * \returns Supported shader pipeline stages
        */
		VkPipelineStageFlags getShaderPipelineStages() const;

		/**
         * \brief Creates framebuffer for a set of render targets
         * 
         * Automatically deduces framebuffer dimensions
         * from the supplied render target views.
         * \param [in] renderTargets Render targets
         * \returns The framebuffer object
         */
		Rc<VltFramebuffer> createFramebuffer(
			const VltRenderTargets& renderTargets);

		/**
         * \brief Creates a buffer object
         * 
         * \param [in] createInfo Buffer create info
         * \param [in] memoryType Memory type flags
         * \returns The buffer object
         */
		Rc<VltBuffer> createBuffer(
			const VltBufferCreateInfo& createInfo,
			VkMemoryPropertyFlags      memoryType);

		/**
         * \brief Creates a buffer view
         * 
         * \param [in] buffer The buffer to view
         * \param [in] createInfo Buffer view properties
         * \returns The buffer view object
         */
		Rc<VltBufferView> createBufferView(
			const Rc<VltBuffer>&           buffer,
			const VltBufferViewCreateInfo& createInfo);

		/**
         * \brief Creates an image object
         * 
         * \param [in] createInfo Image create info
         * \param [in] memoryType Memory type flags
         * \returns The image object
         */
		Rc<VltImage> createImage(
			const VltImageCreateInfo& createInfo,
			VkMemoryPropertyFlags     memoryType);

		/**
         * \brief Creates an image object for an existing VkImage
         * 
         * \param [in] createInfo Image create info
         * \param [in] image Vulkan image to wrap
         * \returns The image object
         */
		Rc<VltImage> createImageFromVkImage(
			const VltImageCreateInfo& createInfo,
			VkImage                   image);

		/**
         * \brief Creates an image view
         * 
         * \param [in] image The image to create a view for
         * \param [in] createInfo Image view create info
         * \returns The image view
         */
		Rc<VltImageView> createImageView(
			const Rc<VltImage>&           image,
			const VltImageViewCreateInfo& createInfo);


		/**
         * \brief Creates a sampler object
         * 
         * \param [in] createInfo Sampler parameters
         * \returns Newly created sampler object
         */
		Rc<VltSampler> createSampler(
			const VltSamplerCreateInfo& createInfo);


		/**
         * \brief Creates a shader module
         * 
         * \param [in] stage Shader stage
         * \param [in] slotCount Resource slot count
         * \param [in] slotInfos Resource slot descriptions
         * \param [in] iface Inter-stage interface slots
         * \param [in] code Shader code
         * \returns New shader module
         */
		Rc<VltShader> createShader(
			VkShaderStageFlagBits       stage,
			uint32_t                    slotCount,
			const VltResourceSlot*      slotInfos,
			const VltInterfaceSlots&    iface,
			const gcn::SpirvCodeBuffer& code);


		/**
        * \brief Creates a command list
        * \returns The command list
        */
		Rc<VltCommandList> createCommandList(
			VltQueueType queueType);


		/**
         * \brief Creates a descriptor pool
         * 
         * Returns a previously recycled pool, or creates
         * a new one if necessary. The context should take
         * ownership of the returned pool.
         * \returns Descriptor pool
         */
		Rc<VltDescriptorPool> createDescriptorPool();

		/**
        * \brief Creates a context
        * 
        * Creates a context object that can
        * be used to record command buffers.
        * \returns The context object
        */
		Rc<VltContext> createContext();

		/**
         * \brief Creates a GPU event
         * \returns New GPU event
         */
		Rc<VltGpuEvent> createGpuEvent();

		/**
		 * \brief Creates a semaphore
		 * \returns New semaphore
		 */
		Rc<VltSemaphore> createSemaphore(
			const VltSemaphoreCreateInfo& info);

		/**
		 * \brief Submits a command list
		 * 
		 * Submits the given command list to the device using
		 * the given set of optional synchronization primitives.
		 * \param [in] commandList The command list to submit
		 * \param [in] waitSync (Optional) Semaphore to wait on
		 * \param [in] wakeSync (Optional) Semaphore to notify
		 */
		void submitCommandList(
			const Rc<VltCommandList>& commandList,
			VkSemaphore               waitSync,
			VkSemaphore               wakeSync);

		/**
         * \brief Presents a swap chain image
         * 
         * Invokes the presenter's \c presentImage method on
         * the submission thread. The status of this operation
         * can be retrieved with \ref waitForSubmission.
         * \param [in] presenter The presenter
         */
		void presentImage(
			const Rc<sce::ScePresenter>& presenter);

		/**
         * \brief Waits for all submission works done.
         * 
         */
		void syncSubmission();

		/**
        * \brief Waits until the device becomes idle
        * 
        * Waits for the GPU to complete the execution of all
        * previously submitted command buffers. This may be
        * used to ensure that resources that were previously
        * used by the GPU can be safely destroyed.
        */
		void waitForIdle();

	private:
		void recycleCommandList(
			const Rc<VltCommandList>& cmdList);

		void recycleDescriptorPool(
			const Rc<VltDescriptorPool>& pool);

		VltDeviceQueue getQueue(
			uint32_t family,
			uint32_t index) const;

	private:
		VkDevice m_device;

		Rc<VltInstance>     m_instance;
		Rc<VltAdapter>      m_adapter;
		VltDeviceExtensions m_extensions;

		VltDeviceFeatures m_features;
		VltDeviceInfo     m_properties;

		VltObjects m_objects;

		VltDeviceQueueSet m_queues;

		VltSubmissionQueue m_submissionQueue;

		VltRecycler<VltCommandList, 16>    m_recycledCommandListsGraphics;
		VltRecycler<VltCommandList, 16>    m_recycledCommandListsCompute;
		VltRecycler<VltDescriptorPool, 16> m_recycledDescriptorPools;
	};

}  // namespace sce::vlt