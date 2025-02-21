#include "VltContext.h"

#include "VltBuffer.h"
#include "VltDevice.h"
#include "VltImage.h"
#include "VltGpuEvent.h"
#include "VltDescriptor.h"

LOG_CHANNEL("Graphic.Violet");

namespace sce::vlt
{
	VltContext::VltContext(VltDevice* device) :
		m_device(device),
		m_common(&device->m_objects),
		m_execBarriers(VltCmdType::ExecBuffer),
		m_execAcquires(VltCmdType::ExecBuffer),
		m_transBarriers(VltCmdType::TransferBuffer),
		m_transAcquires(VltCmdType::TransferBuffer),
		m_initBarriers(VltCmdType::InitBuffer),
		m_staging(device)
	{
	}

	VltContext::~VltContext()
	{
	}

	void VltContext::beginRecording(const Rc<VltCommandList>& cmdList)
	{
		m_cmd = cmdList;
		m_cmd->beginRecording();

		// The current state of the internal command buffer is
		// undefined, so we have to bind and set up everything
		// before any draw or dispatch command is recorded.
		m_flags.clr(
			VltContextFlag::GpRenderingActive,
			VltContextFlag::GpXfbActive);

		m_flags.set(
			VltContextFlag::GpDirtyFramebuffer,
			VltContextFlag::GpDirtyFramebufferState,
			VltContextFlag::GpDirtyPipeline,
			VltContextFlag::GpDirtyPipelineState,
			VltContextFlag::GpDirtyResources,
			VltContextFlag::GpDirtyVertexBuffers,
			VltContextFlag::GpDirtyIndexBuffer,
			VltContextFlag::GpDirtyXfbBuffers,
			VltContextFlag::GpDirtyBlendConstants,
			VltContextFlag::GpDirtyStencilRef,
			VltContextFlag::GpDirtyViewport,
			VltContextFlag::GpDirtyDepthBias,
			VltContextFlag::GpDirtyDepthBounds,
			VltContextFlag::CpDirtyPipeline,
			VltContextFlag::CpDirtyPipelineState,
			VltContextFlag::CpDirtyResources,
			VltContextFlag::DirtyDrawBuffer);
	}

	Rc<VltCommandList> VltContext::endRecording()
	{
		this->endRendering();

		m_execBarriers.recordCommands(m_cmd);
		m_transBarriers.recordCommands(m_cmd);
		m_initBarriers.recordCommands(m_cmd);

		m_cmd->endRecording();
		return std::exchange(m_cmd, nullptr);
	}

	void VltContext::flushCommandList()
	{
		auto commandList = this->endRecording();
		auto queueType   = commandList->type();

		m_device->submitCommandList(
			commandList,
			VK_NULL_HANDLE,
			VK_NULL_HANDLE);

		this->beginRecording(
			m_device->createCommandList(queueType));
	}

	void VltContext::bindRenderTarget(
		uint32_t             slot,
		const VltAttachment& target)
	{
		m_state.cb.renderTargets.color[slot] = target;

		resetFramebufferOps();

		m_flags.set(VltContextFlag::GpDirtyFramebuffer);
	}
	
	void VltContext::bindDepthRenderTarget(
		const VltAttachment& depthTarget)
	{
		m_state.cb.renderTargets.depth = depthTarget;

		m_flags.set(VltContextFlag::GpDirtyFramebuffer);
	}

	void VltContext::bindIndexBuffer(
		const VltBufferSlice& buffer,
		VkIndexType           indexType)
	{
		m_state.vi.indexBuffer = buffer;
		m_state.vi.indexType   = indexType;

		m_flags.set(VltContextFlag::GpDirtyIndexBuffer);
	}

	void VltContext::bindVertexBuffer(
		uint32_t              binding,
		const VltBufferSlice& buffer,
		uint32_t              stride)
	{
		m_state.vi.vertexBuffers[binding] = buffer;
		m_flags.set(VltContextFlag::GpDirtyVertexBuffers);

		if (unlikely(!buffer.defined()))
			stride = 0;

		if (unlikely(m_state.vi.vertexStrides[binding] != stride))
		{
			m_state.vi.vertexStrides[binding] = stride;
			m_flags.set(VltContextFlag::GpDirtyPipelineState);
		}
	}

	void VltContext::pushConstants(
		uint32_t    offset,
		uint32_t    size,
		const void* data)
	{
		std::memcpy(&m_state.pc.data[offset], data, size);

		m_flags.set(VltContextFlag::DirtyPushConstants);
	}

	void VltContext::draw(
		uint32_t vertexCount,
		uint32_t instanceCount,
		uint32_t firstVertex,
		uint32_t firstInstance)
	{
		if (this->commitGraphicsState<false, false>())
		{
			m_cmd->cmdDraw(
				vertexCount, instanceCount,
				firstVertex, firstInstance);
		}
	}

	void VltContext::drawIndexed(
		uint32_t indexCount,
		uint32_t instanceCount,
		uint32_t firstIndex,
		uint32_t vertexOffset,
		uint32_t firstInstance)
	{
		if (this->commitGraphicsState<true, false>())
		{
			m_cmd->cmdDrawIndexed(
				indexCount, instanceCount,
				firstIndex, vertexOffset,
				firstInstance);
		}
	}
	
	void VltContext::dispatch(
		uint32_t x,
		uint32_t y,
		uint32_t z)
	{
		if (this->commitComputeState())
		{
			this->commitComputePrevBarriers();

			m_cmd->cmdDispatch(x, y, z);

			this->commitComputePostBarriers();
		}
	}

	void VltContext::changeImageLayout(
		const Rc<VltImage>& image,
		VkImageLayout       layout)
	{
		if (image->info().layout != layout)
		{
			this->endRendering();

			VkImageSubresourceRange subresources;
			subresources.aspectMask     = image->formatInfo()->aspectMask;
			subresources.baseArrayLayer = 0;
			subresources.baseMipLevel   = 0;
			subresources.layerCount     = image->info().numLayers;
			subresources.levelCount     = image->info().mipLevels;

			if (m_execBarriers.isImageDirty(image, subresources, VltAccess::Write))
				m_execBarriers.recordCommands(m_cmd);

			m_execBarriers.accessImage(image, subresources,
									   image->info().layout,
									   image->info().stages,
									   image->info().access,
									   layout,
									   image->info().stages,
									   image->info().access);

			image->updateLayout(layout);
		}
	}

	void VltContext::transformImage(
		const Rc<VltImage>&            dstImage,
		const VkImageSubresourceRange& dstSubresources,
		VkImageLayout                  srcLayout,
		VkImageLayout                  dstLayout)
	{
		this->transformImage(
			dstImage,
			dstSubresources,
			srcLayout,
			dstImage->info().stages,
			dstImage->info().access,
			dstLayout,
			dstImage->info().stages,
			dstImage->info().access);
	}

	void VltContext::transformImage(
		const Rc<VltImage>&            dstImage,
		const VkImageSubresourceRange& dstSubresources,
		VkImageLayout                  srcLayout,
		VkPipelineStageFlags2          srcStages,
		VkAccessFlags2                 srcAccess,
		VkImageLayout                  dstLayout,
		VkPipelineStageFlags2          dstStages,
		VkAccessFlags2                 dstAccess)
	{
		if (srcLayout != dstLayout)
		{
			m_execBarriers.recordCommands(m_cmd);

			m_execBarriers.accessImage(
				dstImage, dstSubresources,
				srcLayout,
				srcStages,
				srcAccess,
				dstLayout,
				dstStages,
				dstAccess);

			m_cmd->trackResource<VltAccess::Write>(dstImage);
		}
	}

	void VltContext::copyBuffer(
		const Rc<VltBuffer>& dstBuffer,
		VkDeviceSize         dstOffset,
		const Rc<VltBuffer>& srcBuffer,
		VkDeviceSize         srcOffset,
		VkDeviceSize         numBytes)
	{
		if (numBytes == 0)
			return;

		this->endRendering();

		auto dstSlice = dstBuffer->getSliceHandle(dstOffset, numBytes);
		auto srcSlice = srcBuffer->getSliceHandle(srcOffset, numBytes);

		if (m_execBarriers.isBufferDirty(srcSlice, VltAccess::Read) || 
			m_execBarriers.isBufferDirty(dstSlice, VltAccess::Write))
			m_execBarriers.recordCommands(m_cmd);

		VkBufferCopy bufferRegion;
		bufferRegion.srcOffset = srcSlice.offset;
		bufferRegion.dstOffset = dstSlice.offset;
		bufferRegion.size      = dstSlice.length;

		m_cmd->cmdCopyBuffer(VltCmdType::ExecBuffer,
							 srcSlice.handle, dstSlice.handle, 1, &bufferRegion);

		m_execBarriers.accessBuffer(srcSlice,
									VK_PIPELINE_STAGE_TRANSFER_BIT,
									VK_ACCESS_TRANSFER_READ_BIT,
									srcBuffer->info().stages,
									srcBuffer->info().access);

		m_execBarriers.accessBuffer(dstSlice,
									VK_PIPELINE_STAGE_TRANSFER_BIT,
									VK_ACCESS_TRANSFER_WRITE_BIT,
									dstBuffer->info().stages,
									dstBuffer->info().access);

		m_cmd->trackResource<VltAccess::Write>(dstBuffer);
		m_cmd->trackResource<VltAccess::Read>(srcBuffer);
	}

	void VltContext::copyBufferToImage(
		const Rc<VltImage>&      dstImage,
		VkImageSubresourceLayers dstSubresource,
		VkOffset3D               dstOffset,
		VkExtent3D               dstExtent,
		const Rc<VltBuffer>&     srcBuffer,
		VkDeviceSize             srcOffset,
		VkExtent2D               srcExtent)
	{
		this->endRendering();

		auto srcSlice = srcBuffer->getSliceHandle(srcOffset, 0);

		// We may copy to only one aspect of a depth-stencil image,
		// but pipeline barriers need to have all aspect bits set
		auto dstFormatInfo = dstImage->formatInfo();

		auto dstSubresourceRange       = vutil::makeSubresourceRange(dstSubresource);
		dstSubresourceRange.aspectMask = dstFormatInfo->aspectMask;

		if (m_execBarriers.isImageDirty(dstImage, dstSubresourceRange, VltAccess::Write) ||
			m_execBarriers.isBufferDirty(srcSlice, VltAccess::Read))
			m_execBarriers.recordCommands(m_cmd);

		// Initialize the image if the entire subresource is covered
		VkImageLayout dstImageLayoutInitial  = dstImage->info().layout;
		VkImageLayout dstImageLayoutTransfer = dstImage->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		if (dstImage->isFullSubresource(dstSubresource, dstExtent))
			dstImageLayoutInitial = VK_IMAGE_LAYOUT_UNDEFINED;

		m_execAcquires.accessImage(
			dstImage, dstSubresourceRange,
			dstImageLayoutInitial, 0, 0,
			dstImageLayoutTransfer,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT);

		m_execAcquires.recordCommands(m_cmd);

		VkBufferImageCopy copyRegion;
		copyRegion.bufferOffset      = srcSlice.offset;
		copyRegion.bufferRowLength   = srcExtent.width;
		copyRegion.bufferImageHeight = srcExtent.height;
		copyRegion.imageSubresource  = dstSubresource;
		copyRegion.imageOffset       = dstOffset;
		copyRegion.imageExtent       = dstExtent;

		m_cmd->cmdCopyBufferToImage(VltCmdType::ExecBuffer,
									srcSlice.handle, dstImage->handle(),
									dstImageLayoutTransfer, 1, &copyRegion);

		m_execBarriers.accessImage(
			dstImage, dstSubresourceRange,
			dstImageLayoutTransfer,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			dstImage->info().layout,
			dstImage->info().stages,
			dstImage->info().access);

		m_execBarriers.accessBuffer(srcSlice,
									VK_PIPELINE_STAGE_TRANSFER_BIT,
									VK_ACCESS_TRANSFER_READ_BIT,
									srcBuffer->info().stages,
									srcBuffer->info().access);

		m_cmd->trackResource<VltAccess::Write>(dstImage);
		m_cmd->trackResource<VltAccess::Read>(srcBuffer);
	}


	void VltContext::updateIndexBufferBinding()
	{
		m_flags.clr(VltContextFlag::GpDirtyIndexBuffer);

		if (m_state.vi.indexBuffer.defined())
		{
			auto bufferInfo = m_state.vi.indexBuffer.getDescriptor();

			m_cmd->cmdBindIndexBuffer(
				bufferInfo.buffer.buffer,
				bufferInfo.buffer.offset,
				m_state.vi.indexType);
		}
		else
		{
			m_cmd->cmdBindIndexBuffer(
				m_common->dummyResources().bufferHandle(),
				0, VK_INDEX_TYPE_UINT32);
		}
	}

	void VltContext::updateVertexBufferBindings()
	{
		m_flags.clr(VltContextFlag::GpDirtyVertexBuffers);

		if (unlikely(!m_state.gp.state.il.bindingCount()))
			return;

		std::array<VkBuffer, MaxNumVertexBindings>     buffers;
		std::array<VkDeviceSize, MaxNumVertexBindings> offsets;

		// Set buffer handles and offsets for active bindings
		for (uint32_t i = 0; i < m_state.gp.state.il.bindingCount(); i++)
		{
			uint32_t binding = m_state.gp.state.ilBindings[i].binding();

			if (likely(m_state.vi.vertexBuffers[binding].defined()))
			{
				auto vbo = m_state.vi.vertexBuffers[binding].getDescriptor();

				buffers[i] = vbo.buffer.buffer;
				offsets[i] = vbo.buffer.offset;
			}
			else
			{
				buffers[i] = m_common->dummyResources().bufferHandle();
				offsets[i] = 0;
			}
		}

		// Vertex bindigs get remapped when compiling the
		// pipeline, so this actually does the right thing
		m_cmd->cmdBindVertexBuffers(
			0, m_state.gp.state.il.bindingCount(),
			buffers.data(), offsets.data());
	}

	
	void VltContext::updateDynamicState()
	{
		if (!m_gpActivePipeline)
			return;

		if (m_flags.test(VltContextFlag::GpDirtyViewport))
		{
			m_flags.clr(VltContextFlag::GpDirtyViewport);

			uint32_t viewportCount = m_state.gp.state.rs.viewportCount();
			m_cmd->cmdSetViewport(0, viewportCount, m_state.vp.viewports.data());
			m_cmd->cmdSetScissor(0, viewportCount, m_state.vp.scissorRects.data());
		}

		if (m_flags.all(VltContextFlag::GpDirtyBlendConstants,
						VltContextFlag::GpDynamicBlendConstants))
		{
			m_flags.clr(VltContextFlag::GpDirtyBlendConstants);
			m_cmd->cmdSetBlendConstants(&m_state.dyn.blendConstants.r);
		}

		if (m_flags.all(VltContextFlag::GpDirtyStencilRef,
						VltContextFlag::GpDynamicStencilRef))
		{
			m_flags.clr(VltContextFlag::GpDirtyStencilRef);

			m_cmd->cmdSetStencilReference(
				VK_STENCIL_FRONT_AND_BACK,
				m_state.dyn.stencilReference);
		}

		if (m_flags.all(VltContextFlag::GpDirtyDepthBias,
						VltContextFlag::GpDynamicDepthBias))
		{
			m_flags.clr(VltContextFlag::GpDirtyDepthBias);

			m_cmd->cmdSetDepthBias(
				m_state.dyn.depthBias.depthBiasConstant,
				m_state.dyn.depthBias.depthBiasClamp,
				m_state.dyn.depthBias.depthBiasSlope);
		}

		if (m_flags.all(VltContextFlag::GpDirtyDepthBounds,
						VltContextFlag::GpDynamicDepthBounds))
		{
			m_flags.clr(VltContextFlag::GpDirtyDepthBounds);

			m_cmd->cmdSetDepthBoundsTestEnable(
				m_state.gp.state.ds.enableDepthBoundsTest());

			m_cmd->cmdSetDepthBounds(
				m_state.dyn.depthBoundsRange.minDepthBounds,
				m_state.dyn.depthBoundsRange.maxDepthBounds);
		}
	}

	template <bool Indexed, bool Indirect>
	bool VltContext::commitGraphicsState()
	{
		if (m_flags.test(VltContextFlag::GpDirtyFramebuffer) ||
			m_flags.test(VltContextFlag::GpDirtyFramebufferState))
		{
			this->updateFramebuffer();
		}
			
		if (m_flags.test(VltContextFlag::GpDirtyPipeline))
		{
			if (unlikely(!this->updateGraphicsPipeline()))
				return false;
		}

		if (!m_flags.test(VltContextFlag::GpRenderingActive))
		{
			this->beginRendering();
		}

		if (m_flags.test(VltContextFlag::GpDirtyIndexBuffer) && Indexed)
			this->updateIndexBufferBinding();

		if (m_flags.test(VltContextFlag::GpDirtyVertexBuffers))
			this->updateVertexBufferBindings();

		if (m_flags.any(
				VltContextFlag::GpDirtyResources,
				VltContextFlag::GpDirtyDescriptorBinding))
			this->updateGraphicsShaderResources();

		if (m_flags.test(VltContextFlag::GpDirtyPipelineState))
		{
			if (unlikely(!this->updateGraphicsPipelineState()))
				return false;
		}

		if (m_flags.any(
				VltContextFlag::GpDirtyViewport,
				VltContextFlag::GpDirtyBlendConstants,
				VltContextFlag::GpDirtyStencilRef,
				VltContextFlag::GpDirtyDepthBias,
				VltContextFlag::GpDirtyDepthBounds))
			this->updateDynamicState();

		if (m_flags.test(VltContextFlag::DirtyPushConstants))
			this->updatePushConstants<VK_PIPELINE_BIND_POINT_GRAPHICS>();

		return true;
	}

	bool VltContext::updateGraphicsPipeline()
	{
		m_state.gp.pipeline = lookupGraphicsPipeline(m_state.gp.shaders);
		if (unlikely(m_state.gp.pipeline == nullptr))
		{
			m_state.gp.flags = VltGraphicsPipelineFlags();
			return false;
		}

		m_flags.clr(VltContextFlag::GpDirtyPipeline);
		return true;
	}

	bool VltContext::updateGraphicsPipelineState()
	{
		// Set up vertex buffer strides for active bindings
		for (uint32_t i = 0; i < m_state.gp.state.il.bindingCount(); i++)
		{
			const uint32_t binding = m_state.gp.state.ilBindings[i].binding();
			m_state.gp.state.ilBindings[i].setStride(m_state.vi.vertexStrides[binding]);
		}

		// Check which dynamic states need to be active. States that
		// are not dynamic will be invalidated in the command buffer.
		m_flags.clr(VltContextFlag::GpDynamicBlendConstants,
					VltContextFlag::GpDynamicDepthBias,
					VltContextFlag::GpDynamicDepthBounds,
					VltContextFlag::GpDynamicStencilRef);

		m_flags.set(m_state.gp.state.useDynamicBlendConstants()
						? VltContextFlag::GpDynamicBlendConstants
						: VltContextFlag::GpDirtyBlendConstants);

		m_flags.set(m_state.gp.state.useDynamicDepthBias()
						? VltContextFlag::GpDynamicDepthBias
						: VltContextFlag::GpDirtyDepthBias);

		m_flags.set(m_state.gp.state.useDynamicDepthBounds()
						? VltContextFlag::GpDynamicDepthBounds
						: VltContextFlag::GpDirtyDepthBounds);

		m_flags.set(m_state.gp.state.useDynamicStencilRef()
						? VltContextFlag::GpDynamicStencilRef
						: VltContextFlag::GpDirtyStencilRef);

		// Retrieve and bind actual Vulkan pipeline handle
		m_gpActivePipeline = m_state.gp.pipeline->getPipelineHandle(
			m_state.gp.state,
			m_state.cb.renderTargets.generateAttachmentFormat());

		if (unlikely(!m_gpActivePipeline))
			return false;

		m_cmd->cmdBindPipeline(
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			m_gpActivePipeline);

		m_flags.clr(VltContextFlag::GpDirtyPipelineState);
		return true;
	}

	void VltContext::setViewports(
		uint32_t          viewportCount,
		const VkViewport* viewports)
	{
		if (m_state.gp.state.rs.viewportCount() != viewportCount)
		{
			m_state.gp.state.rs.setViewportCount(viewportCount);
			m_flags.set(VltContextFlag::GpDirtyPipelineState);
		}

		for (uint32_t i = 0; i < viewportCount; i++)
		{
			m_state.vp.viewports[i] = viewports[i];

			// Vulkan viewports are not allowed to have a width
			// of zero (but zero height is allowed),
			// so we fall back to a dummy viewport
			// and instead set an empty scissor rect, which is legal.
			if (viewports[i].width == 0.0f)
			{
				m_state.vp.viewports[i] = VkViewport{
					0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f
				};
				m_state.vp.scissorRects[i] = VkRect2D{
					VkOffset2D{ 0, 0 },
					VkExtent2D{ 0, 0 }
				};
			}
		}

		m_flags.set(VltContextFlag::GpDirtyViewport);
	}

	void VltContext::setScissors(
		uint32_t        scissorCount,
		const VkRect2D* scissorRects)
	{
		// Assume count of scissor and viewport are always equal.
		// In fact, these's only one scissor for Gnm
		if (m_state.gp.state.rs.viewportCount() != scissorCount)
		{
			m_state.gp.state.rs.setViewportCount(scissorCount);
			m_flags.set(VltContextFlag::GpDirtyPipelineState);
		}

		for (uint32_t i = 0; i < scissorCount; i++)
		{
			m_state.vp.scissorRects[i] = scissorRects[i];
		}

		m_flags.set(VltContextFlag::GpDirtyScissor);
	}

	
	void VltContext::setBlendConstants(
		VltBlendConstants blendConstants)
	{
		if (m_state.dyn.blendConstants != blendConstants)
		{
			m_state.dyn.blendConstants = blendConstants;
			m_flags.set(VltContextFlag::GpDirtyBlendConstants);
		}
	}

	void VltContext::setDepthBias(
		VltDepthBias depthBias)
	{
		if (m_state.dyn.depthBias != depthBias)
		{
			m_state.dyn.depthBias = depthBias;
			m_flags.set(VltContextFlag::GpDirtyDepthBias);
		}
	}

	void VltContext::setDepthBoundsTestEnable(
		VkBool32 depthBoundsTestEnable)
	{
		if (m_state.gp.state.ds.enableDepthBoundsTest() != depthBoundsTestEnable)
		{
			m_state.gp.state.ds.setEnableDepthBoundsTest(depthBoundsTestEnable);
			m_flags.set(VltContextFlag::GpDirtyPipelineState);
		}
	}

	void VltContext::setDepthBoundsRange(
		VltDepthBoundsRange depthBoundsRange)
	{
		if (m_state.dyn.depthBoundsRange != depthBoundsRange)
		{
			m_state.dyn.depthBoundsRange = depthBoundsRange;
			m_flags.set(VltContextFlag::GpDirtyDepthBounds);
		}

	}

	void VltContext::setStencilReference(uint32_t reference)
	{
		if (m_state.dyn.stencilReference != reference)
		{
			m_state.dyn.stencilReference = reference;
			m_flags.set(VltContextFlag::GpDirtyStencilRef);
		}
	}

	void VltContext::bindResourceBuffer(
		uint32_t              slot,
		const VltBufferSlice& buffer)
	{
		if (likely(!m_rc[slot].bufferSlice.matches(buffer)))
		{
			m_flags.set(
				VltContextFlag::CpDirtyResources,
				VltContextFlag::GpDirtyResources);
		}
		else
		{
			m_flags.set(
				VltContextFlag::CpDirtyDescriptorBinding,
				VltContextFlag::GpDirtyDescriptorBinding);
		}

		m_rc[slot].bufferSlice = buffer;
	}

	void VltContext::bindResourceView(
		uint32_t                 slot,
		const Rc<VltImageView>&  imageView,
		const Rc<VltBufferView>& bufferView)
	{
		m_rc[slot].imageView   = imageView;
		m_rc[slot].bufferView  = bufferView;
		m_rc[slot].bufferSlice = bufferView != nullptr
									 ? bufferView->slice()
									 : VltBufferSlice();
		m_flags.set(
			VltContextFlag::CpDirtyResources,
			VltContextFlag::GpDirtyResources);
	}

	void VltContext::bindResourceSampler(
		uint32_t              slot,
		const Rc<VltSampler>& sampler)
	{
		m_rc[slot].sampler = sampler;
	
		m_flags.set(
			VltContextFlag::CpDirtyResources,
			VltContextFlag::GpDirtyResources);
	}


	void VltContext::bindShader(
		VkShaderStageFlagBits stage,
		const Rc<VltShader>&  shader)
	{
		Rc<VltShader>* shaderStage;

		// clang-format off
		switch (stage) 
		{
		  case VK_SHADER_STAGE_VERTEX_BIT:                  shaderStage = &m_state.gp.shaders.vs;  break;
		  case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:    shaderStage = &m_state.gp.shaders.tcs; break;
		  case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: shaderStage = &m_state.gp.shaders.tes; break;
		  case VK_SHADER_STAGE_GEOMETRY_BIT:                shaderStage = &m_state.gp.shaders.gs;  break;
		  case VK_SHADER_STAGE_FRAGMENT_BIT:                shaderStage = &m_state.gp.shaders.fs;  break;
		  case VK_SHADER_STAGE_COMPUTE_BIT:                 shaderStage = &m_state.cp.shaders.cs;  break;
		  default: return;
		}
		// clang-format on

		*shaderStage = shader;

		if (stage == VK_SHADER_STAGE_COMPUTE_BIT)
		{
			m_flags.set(
				VltContextFlag::CpDirtyPipeline,
				VltContextFlag::CpDirtyPipelineState,
				VltContextFlag::CpDirtyResources);
		}
		else
		{
			m_flags.set(
				VltContextFlag::GpDirtyPipeline,
				VltContextFlag::GpDirtyPipelineState,
				VltContextFlag::GpDirtyResources);
		}
	}

	void VltContext::setInputAssemblyState(
		const VltInputAssemblyState& ia)
	{
		m_state.gp.state.ia = VltIaInfo(
			ia.primitiveTopology,
			ia.primitiveRestart,
			ia.patchVertexCount);

		m_flags.set(VltContextFlag::GpDirtyPipelineState);
	}

	void VltContext::setInputLayout(
		uint32_t                  attributeCount,
		const VltVertexAttribute* attributes,
		uint32_t                  bindingCount,
		const VltVertexBinding*   bindings)
	{
		m_flags.set(
			VltContextFlag::GpDirtyPipelineState,
			VltContextFlag::GpDirtyVertexBuffers);

		for (uint32_t i = 0; i < attributeCount; i++)
		{
			m_state.gp.state.ilAttributes[i] = VltIlAttribute(
				attributes[i].location, attributes[i].binding,
				attributes[i].format, attributes[i].offset);
		}

		for (uint32_t i = attributeCount; i < m_state.gp.state.il.attributeCount(); i++)
			m_state.gp.state.ilAttributes[i] = VltIlAttribute();

		for (uint32_t i = 0; i < bindingCount; i++)
		{
			m_state.gp.state.ilBindings[i] = VltIlBinding(
				bindings[i].binding, 0, bindings[i].inputRate,
				bindings[i].fetchRate);
		}

		for (uint32_t i = bindingCount; i < m_state.gp.state.il.bindingCount(); i++)
			m_state.gp.state.ilBindings[i] = VltIlBinding();

		m_state.gp.state.il = VltIlInfo(attributeCount, bindingCount);
	}

	void VltContext::setRasterizerState(
		const VltRasterizerState& rs)
	{
		m_state.gp.state.rs = VltRsInfo(
			rs.depthClipEnable,
			rs.depthBiasEnable,
			rs.polygonMode,
			rs.cullMode,
			rs.frontFace,
			m_state.gp.state.rs.viewportCount(),
			rs.sampleCount);

		m_flags.set(VltContextFlag::GpDirtyPipelineState);
	}

	void VltContext::setMultisampleState(
		const VltMultisampleState& ms)
	{
		m_state.gp.state.ms = VltMsInfo(
			m_state.gp.state.ms.sampleCount(),
			ms.sampleMask,
			ms.enableAlphaToCoverage);

		m_flags.set(VltContextFlag::GpDirtyPipelineState);
	}

	void VltContext::setDepthStencilState(
		const VltDepthStencilState& ds)
	{
		m_state.gp.state.ds = VltDsInfo(
			ds.enableDepthTest,
			ds.enableDepthWrite,
			m_state.gp.state.ds.enableDepthBoundsTest(),
			ds.enableStencilTest,
			ds.depthCompareOp);

		m_state.gp.state.dsFront = VltDsStencilOp(ds.stencilOpFront);
		m_state.gp.state.dsBack  = VltDsStencilOp(ds.stencilOpBack);

		m_flags.set(VltContextFlag::GpDirtyPipelineState);
	}

	void VltContext::setLogicOpState(
		const VltLogicOpState& lo)
	{
		m_state.gp.state.cb = VltCbInfo(
			lo.enableLogicOp,
			lo.logicOp);

		m_flags.set(VltContextFlag::GpDirtyPipelineState);
	}

	void VltContext::setBlendMode(
		uint32_t attachment, const VltBlendMode& blendMode)
	{
		m_state.gp.state.cbBlend[attachment] = VltCbAttachmentBlend(
			blendMode.enableBlending,
			blendMode.colorSrcFactor,
			blendMode.colorDstFactor,
			blendMode.colorBlendOp,
			blendMode.alphaSrcFactor,
			blendMode.alphaDstFactor,
			blendMode.alphaBlendOp,
			blendMode.writeMask);

		m_flags.set(VltContextFlag::GpDirtyPipelineState);
	}

	void VltContext::setBlendMask(
		uint32_t              attachment,
		VkColorComponentFlags writeMask)
	{
		m_state.gp.state.cbBlend[attachment].setColorWriteMask(writeMask);

		m_flags.set(VltContextFlag::GpDirtyPipelineState);
	}

	void VltContext::clearRenderTarget(
		const Rc<VltImageView>& imageView,
		VkImageAspectFlags      clearAspects,
		VkClearValue            clearValue)
	{
		this->updateFramebuffer();

		// Prepare attachment ops
		VltAttachmentOps newOp;
		newOp.loadOp  = VK_ATTACHMENT_LOAD_OP_LOAD;
		newOp.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

		if (clearAspects & VK_IMAGE_ASPECT_COLOR_BIT)
			newOp.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;

		if (clearAspects & VK_IMAGE_ASPECT_DEPTH_BIT)
			newOp.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;

		if (clearAspects & VK_IMAGE_ASPECT_STENCIL_BIT)
			newOp.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;

		// Make sure the color components are ordered correctly
		if (clearAspects & VK_IMAGE_ASPECT_COLOR_BIT)
		{
			clearValue.color = vutil::swizzleClearColor(clearValue.color,
														vutil::invertComponentMapping(imageView->info().swizzle));
		}

		// Check whether the render target view is an attachment
		// of the current framebuffer and is included entirely.
		// If not, we need to create a temporary framebuffer.
		int32_t attachmentIndex = -1;

		if (m_state.cb.framebuffer->isFullSize(imageView))
			attachmentIndex = m_state.cb.framebuffer->findAttachment(imageView);

		if (attachmentIndex < 0)
		{
			LOG_ASSERT(false, "TODO");
		}
		else if (m_flags.test(VltContextFlag::GpRenderingActive))
		{
			// Clear the attachment in quesion. For color images,
			// the attachment index for the current subpass is
			// equal to the render pass attachment index.
			VkClearAttachment clearInfo;
			clearInfo.aspectMask      = clearAspects;
			clearInfo.colorAttachment = attachmentIndex;
			clearInfo.clearValue      = clearValue;

			VkClearRect clearRect;
			clearRect.rect.offset.x      = 0;
			clearRect.rect.offset.y      = 0;
			clearRect.rect.extent.width  = imageView->mipLevelExtent(0).width;
			clearRect.rect.extent.height = imageView->mipLevelExtent(0).height;
			clearRect.baseArrayLayer     = 0;
			clearRect.layerCount         = imageView->info().numLayers;

			m_cmd->cmdClearAttachments(1, &clearInfo, 1, &clearRect);
		}
		else
		{
			// Perform the clear when starting the render pass
			if (clearAspects & VK_IMAGE_ASPECT_COLOR_BIT)
			{
				m_state.cb.attachmentOps.colorOps[attachmentIndex]       = newOp;
				m_state.cb.clearValues.colorValue[attachmentIndex].color = clearValue.color;
			}

			if (clearAspects & VK_IMAGE_ASPECT_DEPTH_BIT)
			{
				m_state.cb.attachmentOps.depthOps = newOp;
				m_state.cb.clearValues.depthValue.depthStencil.depth = clearValue.depthStencil.depth;
			}

			if (clearAspects & VK_IMAGE_ASPECT_STENCIL_BIT)
			{
				m_state.cb.attachmentOps.depthOps                      = newOp;
				m_state.cb.clearValues.depthValue.depthStencil.stencil = clearValue.depthStencil.stencil;
			}

			m_flags.set(VltContextFlag::GpDirtyFramebufferState);
		}
	}

	void VltContext::setDepthClearValue(VkClearValue clearValue)
	{
		this->updateFramebuffer();

		m_state.cb.clearValues.depthValue.depthStencil.depth = clearValue.depthStencil.depth;

		// TODO:
		// The way for Gnm to clear a depth target is to render a fullscreen
		// quad while setting DeRenderControl to enable depth clear.
		// Vulkan don't have DeRenderControl, we need to find an alternative way.
		m_state.cb.attachmentOps.depthOps.loadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
		m_state.cb.attachmentOps.depthOps.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

		m_flags.set(VltContextFlag::GpDirtyFramebufferState);
	}

	void VltContext::setStencilClearValue(VkClearValue clearValue)
	{
		this->updateFramebuffer();

		m_state.cb.clearValues.depthValue.depthStencil.stencil = clearValue.depthStencil.stencil;

		m_state.cb.attachmentOps.depthOps.loadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
		m_state.cb.attachmentOps.depthOps.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

		m_flags.set(VltContextFlag::GpDirtyFramebufferState);
	}

	void VltContext::emitRenderTargetReadbackBarrier()
	{
		emitMemoryBarrier(VK_DEPENDENCY_BY_REGION_BIT,
						  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
						  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
						  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
						  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						  VK_ACCESS_SHADER_READ_BIT);
	}

	void VltContext::updateBuffer(
		const Rc<VltBuffer>& buffer,
		VkDeviceSize         offset,
		VkDeviceSize         size,
		const void*          data)
	{
		
		this->endRendering();

		VltBufferSliceHandle bufferSlice = buffer->getSliceHandle(offset, size);
		VltCmdType           cmdBuffer   = VltCmdType::ExecBuffer;

		if (m_execBarriers.isBufferDirty(bufferSlice, VltAccess::Write))
			m_execBarriers.recordCommands(m_cmd);

		// Vulkan specifies that small amounts of data (up to 64kB) can
		// be copied to a buffer directly if the size is a multiple of
		// four. Anything else must be copied through a staging buffer.
		// We'll limit the size to 4kB in order to keep command buffers
		// reasonably small, we do not know how much data apps may upload.
		if ((size <= 4096) && ((size & 0x3) == 0) && ((offset & 0x3) == 0))
		{
			m_cmd->cmdUpdateBuffer(
				cmdBuffer,
				bufferSlice.handle,
				bufferSlice.offset,
				bufferSlice.length,
				data);
		}
		else
		{
			auto stagingSlice  = m_staging.alloc(CACHE_LINE_SIZE, size);
			auto stagingHandle = stagingSlice.getSliceHandle();

			std::memcpy(stagingHandle.mapPtr, data, size);

			VkBufferCopy region;
			region.srcOffset = stagingHandle.offset;
			region.dstOffset = bufferSlice.offset;
			region.size      = size;

			m_cmd->cmdCopyBuffer(cmdBuffer,
								 stagingHandle.handle, bufferSlice.handle, 1, &region);

			m_cmd->trackResource<VltAccess::Read>(stagingSlice.buffer());
		}

		m_execBarriers.accessBuffer(
			bufferSlice,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			buffer->info().stages,
			buffer->info().access);

		m_cmd->trackResource<VltAccess::Write>(buffer);
	}

	void VltContext::updateImage(
		const Rc<VltImage>&             image,
		const VkImageSubresourceLayers& subresources,
		VkOffset3D                      imageOffset,
		VkExtent3D                      imageExtent,
		const void*                     data,
		VkDeviceSize                    pitchPerRow,
		VkDeviceSize                    pitchPerLayer)
	{
		this->endRendering();

		// Upload data through a staging buffer. Special care needs to
		// be taken when dealing with compressed image formats: Rather
		// than copying pixels, we'll be copying blocks of pixels.
		const VltFormatInfo* formatInfo = image->formatInfo();

		// Align image extent to a full block. This is necessary in
		// case the image size is not a multiple of the block size.
		VkExtent3D elementCount = vutil::computeBlockCount(
			imageExtent, formatInfo->blockSize);
		elementCount.depth *= subresources.layerCount;

		// Allocate staging buffer memory for the image data. The
		// pixels or blocks will be tightly packed within the buffer.
		auto stagingSlice  = m_staging.alloc(CACHE_LINE_SIZE,
											 formatInfo->elementSize * vutil::flattenImageExtent(elementCount));
		auto stagingHandle = stagingSlice.getSliceHandle();
		vutil::packImageData(stagingHandle.mapPtr, data,
							 elementCount, formatInfo->elementSize,
							 pitchPerRow, pitchPerLayer);

		// Prepare the image layout. If the given extent covers
		// the entire image, we may discard its previous contents.
		auto subresourceRange       = vutil::makeSubresourceRange(subresources);
		subresourceRange.aspectMask = formatInfo->aspectMask;

		if (m_execBarriers.isImageDirty(image, subresourceRange, VltAccess::Write))
			m_execBarriers.recordCommands(m_cmd);

		// Initialize the image if the entire subresource is covered
		VkImageLayout imageLayoutInitial  = image->info().layout;
		VkImageLayout imageLayoutTransfer = image->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		if (image->isFullSubresource(subresources, imageExtent))
			imageLayoutInitial = VK_IMAGE_LAYOUT_UNDEFINED;

		m_execAcquires.accessImage(
			image, subresourceRange,
			imageLayoutInitial, 0, 0,
			imageLayoutTransfer,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT);

		m_execAcquires.recordCommands(m_cmd);

		// Copy contents of the staging buffer into the image.
		// Since our source data is tightly packed, we do not
		// need to specify any strides.
		VkBufferImageCopy region;
		region.bufferOffset      = stagingHandle.offset;
		region.bufferRowLength   = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource  = subresources;
		region.imageOffset       = imageOffset;
		region.imageExtent       = imageExtent;

		m_cmd->cmdCopyBufferToImage(VltCmdType::ExecBuffer,
									stagingHandle.handle, image->handle(),
									imageLayoutTransfer, 1, &region);

		// Transition image back into its optimal layout
		m_execBarriers.accessImage(
			image, subresourceRange,
			imageLayoutTransfer,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			image->info().layout,
			image->info().stages,
			image->info().access);

		m_cmd->trackResource<VltAccess::Write>(image);
		m_cmd->trackResource<VltAccess::Read>(stagingSlice.buffer());
	}

	void VltContext::uploadBuffer(
		const Rc<VltBuffer>& buffer,
		const void*          data)
	{
		auto bufferSlice = buffer->getSliceHandle();

		auto stagingSlice  = m_staging.alloc(bufferSlice.length, CACHE_LINE_SIZE);
		auto stagingHandle = stagingSlice.getSliceHandle();
		std::memcpy(stagingHandle.mapPtr, data, bufferSlice.length);

		VkBufferCopy region;
		region.srcOffset = stagingHandle.offset;
		region.dstOffset = bufferSlice.offset;
		region.size      = bufferSlice.length;

		m_cmd->cmdCopyBuffer(VltCmdType::TransferBuffer,
							 stagingHandle.handle, bufferSlice.handle, 1, &region);

		uint32_t acquireQueueFamily = m_cmd->type() == VltQueueType::Graphics
										 ? m_device->queues().graphics.queueFamily
										 : m_device->queues().compute.queueFamily;

		m_transBarriers.releaseBuffer(
			m_initBarriers, bufferSlice,
			m_device->queues().transfer.queueFamily,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			acquireQueueFamily,
			buffer->info().stages,
			buffer->info().access);

		m_cmd->trackResource<VltAccess::Read>(stagingSlice.buffer());
		m_cmd->trackResource<VltAccess::Write>(buffer);
	}

	void VltContext::uploadImage(
		const Rc<VltImage>&             image,
		const VkImageSubresourceLayers& subresources,
		const void*                     data,
		VkDeviceSize                    pitchPerRow,
		VkDeviceSize                    pitchPerLayer)
	{
		const VltFormatInfo* formatInfo = image->formatInfo();

		VkOffset3D imageOffset = { 0, 0, 0 };
		VkExtent3D imageExtent = image->mipLevelExtent(subresources.mipLevel);

		// Allocate staging buffer slice and copy data to it
		VkExtent3D elementCount = vutil::computeBlockCount(
			imageExtent, formatInfo->blockSize);
		elementCount.depth *= subresources.layerCount;

		auto stagingSlice  = m_staging.alloc(formatInfo->elementSize * vutil::flattenImageExtent(elementCount),
                                            CACHE_LINE_SIZE);
		auto stagingHandle = stagingSlice.getSliceHandle();

		vutil::packImageData(stagingHandle.mapPtr, data,
							 elementCount, formatInfo->elementSize,
							 pitchPerRow, pitchPerLayer);

		// Discard previous subresource contents
		m_transAcquires.accessImage(image,
									vutil::makeSubresourceRange(subresources),
									VK_IMAGE_LAYOUT_UNDEFINED, 0, 0,
									image->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
									VK_PIPELINE_STAGE_TRANSFER_BIT,
									VK_ACCESS_TRANSFER_WRITE_BIT);

		m_transAcquires.recordCommands(m_cmd);

		// Perform copy on the transfer queue
		VkBufferImageCopy region;
		region.bufferOffset      = stagingHandle.offset;
		region.bufferRowLength   = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource  = subresources;
		region.imageOffset       = imageOffset;
		region.imageExtent       = imageExtent;

		m_cmd->cmdCopyBufferToImage(VltCmdType::TransferBuffer,
									stagingHandle.handle, image->handle(),
									image->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
									1, &region);

		uint32_t acquireQueueFamily = m_cmd->type() == VltQueueType::Graphics
										  ? m_device->queues().graphics.queueFamily
										  : m_device->queues().compute.queueFamily;

		// Transfer ownership to graphics queue
		m_transBarriers.releaseImage(m_initBarriers,
									 image, vutil::makeSubresourceRange(subresources),
									 m_device->queues().transfer.queueFamily,
									 image->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
									 VK_PIPELINE_STAGE_TRANSFER_BIT,
									 VK_ACCESS_TRANSFER_WRITE_BIT,
									 acquireQueueFamily,
									 image->info().layout,
									 image->info().stages,
									 image->info().access);

		m_cmd->trackResource<VltAccess::Write>(image);
		m_cmd->trackResource<VltAccess::Read>(stagingSlice.buffer());
	}

	void VltContext::downloadBuffer(
		const Rc<VltBuffer>& buffer,
		void*                data)
	{
		auto bufferSlice = buffer->getSliceHandle();

		auto stagingSlice  = m_staging.alloc(bufferSlice.length, CACHE_LINE_SIZE);
		auto stagingHandle = stagingSlice.getSliceHandle();

		VkBufferCopy region;
		region.srcOffset = bufferSlice.offset;
		region.dstOffset = stagingHandle.offset ;
		region.size      = bufferSlice.length;

		m_cmd->cmdCopyBuffer(VltCmdType::ExecBuffer,
							 bufferSlice.handle, stagingHandle.handle, 1, &region);

		flushCommandList();
		// Wait for copying done before memcpy
		// TODO:
		// This should could be batched.
		m_device->waitForIdle();

		std::memcpy(data, stagingHandle.mapPtr, bufferSlice.length);
	}

	void VltContext::initBuffer(
		const Rc<VltBuffer>& buffer)
	{
		auto slice = buffer->getSliceHandle();

		m_cmd->cmdFillBuffer(VltCmdType::InitBuffer,
							 slice.handle, slice.offset,
							 util::align(slice.length, 4), 0);

		m_initBarriers.accessBuffer(slice,
									VK_PIPELINE_STAGE_TRANSFER_BIT,
									VK_ACCESS_TRANSFER_WRITE_BIT,
									buffer->info().stages,
									buffer->info().access);

		m_cmd->trackResource<VltAccess::Write>(buffer);
	}

	void VltContext::initImage(
		const Rc<VltImage>&            image,
		const VkImageSubresourceRange& subresources,
		VkImageLayout                  initialLayout)
	{
		if (initialLayout == VK_IMAGE_LAYOUT_PREINITIALIZED)
		{
			m_initBarriers.accessImage(image, subresources,
									   initialLayout, 0, 0,
									   image->info().layout,
									   image->info().stages,
									   image->info().access);

			m_cmd->trackResource<VltAccess::None>(image);
		}
		else
		{
			VkImageLayout clearLayout = image->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

			// transform image to clear layout
			m_execAcquires.accessImage(image, subresources,
									   initialLayout, 0, 0, clearLayout,
									   VK_PIPELINE_STAGE_TRANSFER_BIT,
									   VK_ACCESS_TRANSFER_WRITE_BIT);
			m_execAcquires.recordCommands(m_cmd);

			auto formatInfo = image->formatInfo();

			if (formatInfo->flags.any(VltFormatFlag::BlockCompressed, VltFormatFlag::MultiPlane))
			{
				LOG_FIXME("init compressed or multi plane image is not supported.");
			}
			else
			{
				if (subresources.aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
				{
					VkClearDepthStencilValue value = {};

					m_cmd->cmdClearDepthStencilImage(image->handle(),
													 clearLayout, &value, 1, &subresources);
				}
				else
				{
					VkClearColorValue value = {};

					m_cmd->cmdClearColorImage(image->handle(),
											  clearLayout, &value, 1, &subresources);
				}
			}

			// transform image back to default layout
			m_execBarriers.accessImage(image, subresources,
									   clearLayout,
									   VK_PIPELINE_STAGE_TRANSFER_BIT,
									   VK_ACCESS_TRANSFER_WRITE_BIT,
									   image->info().layout,
									   image->info().stages,
									   image->info().access);

			m_cmd->trackResource<VltAccess::Write>(image);
		}
	}

	void VltContext::setBarrierControl(VltBarrierControlFlags control)
	{
		m_barrierControl = control;
	}

	void VltContext::signalGpuEvent(
		const Rc<VltGpuEvent>&  event,
		const VkDependencyInfo* dependencyInfo)
	{
		this->endRecording();

		VltGpuEventHandle handle = m_common->eventPool().allocEvent();

		m_cmd->cmdSetEvent2(handle.event, dependencyInfo);

		m_cmd->trackGpuEvent(event->reset(handle));
		m_cmd->trackResource<VltAccess::None>(event);
	}

	void VltContext::signal(
		const Rc<util::sync::Signal>& signal,
		uint64_t                      value)
	{
		m_cmd->queueSignal(signal, value);
	}

	void VltContext::signalSemaphore(const VltSemaphoreSubmission& submission)
	{
		m_cmd->signalSemaphore(submission);
	}

	void VltContext::waitSemaphore(const VltSemaphoreSubmission& submission)
	{
		m_cmd->waitSemaphore(submission);
	}

	void VltContext::beginRendering()
	{
		auto& framebuffer = m_state.cb.framebuffer;
		if (!m_flags.test(VltContextFlag::GpRenderingActive) &&
			framebuffer != nullptr)
		{
			m_execBarriers.recordCommands(m_cmd);

			const VltFramebufferSize fbSize = framebuffer->size();

			VkRect2D renderArea;
			renderArea.offset = VkOffset2D{ 0, 0 };
			renderArea.extent = VkExtent2D{ fbSize.width, fbSize.height };

			VkRenderingInfo renderInfo      = {};
			renderInfo.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
			renderInfo.pNext                = nullptr;
			renderInfo.flags                = 0;
			renderInfo.renderArea           = renderArea;
			renderInfo.layerCount           = 1;
			renderInfo.viewMask             = 0;
			renderInfo.colorAttachmentCount = framebuffer->numColorAttachments();
			renderInfo.pColorAttachments    = framebuffer->colorAttachments();
			renderInfo.pDepthAttachment     = framebuffer->depthAttachment();
			renderInfo.pStencilAttachment   = nullptr;

			m_cmd->cmdBeginRendering(&renderInfo);

			// Don't discard image contents if we have
			// to stop rendering
			resetFramebufferOps();

			m_flags.set(VltContextFlag::GpRenderingActive);
		}
	}

	void VltContext::endRendering()
	{
		if (m_flags.test(VltContextFlag::GpRenderingActive))
		{
			m_cmd->cmdEndRendering();

			m_flags.clr(VltContextFlag::GpRenderingActive);
		}
	}

	bool VltContext::commitComputeState()
	{
		if (m_flags.test(VltContextFlag::GpRenderingActive))
			this->endRendering();

		if (m_flags.test(VltContextFlag::CpDirtyPipeline))
		{
			if (unlikely(!this->updateComputePipeline()))
				return false;
		}

		if (m_flags.any(
				VltContextFlag::CpDirtyResources,
				VltContextFlag::CpDirtyDescriptorBinding))
			this->updateComputeShaderResources();

		if (m_flags.test(VltContextFlag::CpDirtyPipelineState))
		{
			if (unlikely(!this->updateComputePipelineState()))
				return false;
		}

		if (m_flags.test(VltContextFlag::DirtyPushConstants))
		{
			this->updatePushConstants<VK_PIPELINE_BIND_POINT_COMPUTE>();
		}	

		return true;
	}

	bool VltContext::updateComputePipeline()
	{
		m_state.cp.pipeline = lookupComputePipeline(m_state.cp.shaders);

		if (unlikely(m_state.cp.pipeline == nullptr))
			return false;

		if (m_state.cp.pipeline->layout()->pushConstRange().size)
			m_flags.set(VltContextFlag::DirtyPushConstants);

		m_flags.clr(VltContextFlag::CpDirtyPipeline);
		return true;
	}

	template <VkPipelineBindPoint BindPoint>
	void VltContext::updateShaderDescriptorSetBinding(
		VkDescriptorSet set, const VltPipelineLayout* layout)
	{
		if (set)
		{
			std::array<uint32_t, MaxNumActiveBindings> offsets;

			for (uint32_t i = 0; i < layout->dynamicBindingCount(); i++)
			{
				const auto& binding = layout->dynamicBinding(i);
				const auto& res     = m_rc[binding.slot];

				offsets[i] = res.bufferSlice.defined()
								 ? res.bufferSlice.getDynamicOffset()
								 : 0;
			}

			m_cmd->cmdBindDescriptorSet(BindPoint,
										layout->pipelineLayout(), set,
										layout->dynamicBindingCount(),
										offsets.data());
		}
	}

	template <VkPipelineBindPoint BindPoint>
	void sce::vlt::VltContext::updatePushConstants()
	{
		m_flags.clr(VltContextFlag::DirtyPushConstants);

		auto layout = BindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS
						  ? m_state.gp.pipeline->layout()
						  : m_state.cp.pipeline->layout();

		if (!layout)
			return;

		VkPushConstantRange pushConstRange = layout->pushConstRange();
		if (!pushConstRange.size)
			return;

		m_cmd->cmdPushConstants(
			layout->pipelineLayout(),
			pushConstRange.stageFlags,
			pushConstRange.offset,
			pushConstRange.size,
			&m_state.pc.data[pushConstRange.offset]);
	}

	template <VkPipelineBindPoint BindPoint>
	void VltContext::updateShaderResources(
		const VltPipelineLayout* layout)
	{
		std::array<VltDescriptorInfo, MaxNumActiveBindings> descriptors;

		// Assume that all bindings are active as a fast path
		VltBindingMask bindMask;
		bindMask.setFirst(layout->bindingCount());

		for (uint32_t i = 0; i < layout->bindingCount(); i++)
		{
			const auto& binding = layout->binding(i);
			const auto& res     = m_rc[binding.slot];

			switch (binding.type)
			{
			case VK_DESCRIPTOR_TYPE_SAMPLER:
				if (res.sampler != nullptr)
				{
					descriptors[i].image.sampler     = res.sampler->handle();
					descriptors[i].image.imageView   = VK_NULL_HANDLE;
					descriptors[i].image.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				}
				else
				{
					descriptors[i].image = m_common->dummyResources().samplerDescriptor();
				}
				break;

			case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
				if (res.imageView != nullptr && res.imageView->handle(binding.view) != VK_NULL_HANDLE)
				{
					descriptors[i].image.sampler     = VK_NULL_HANDLE;
					descriptors[i].image.imageView   = res.imageView->handle(binding.view);
					descriptors[i].image.imageLayout = res.imageView->imageInfo().layout;
				}
				else
				{
					bindMask.clr(i);
					descriptors[i].image = m_common->dummyResources().imageViewDescriptor(binding.view, true);
				}
				break;

			case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
				if (res.imageView != nullptr && res.imageView->handle(binding.view) != VK_NULL_HANDLE)
				{
					descriptors[i].image.sampler     = VK_NULL_HANDLE;
					descriptors[i].image.imageView   = res.imageView->handle(binding.view);
					descriptors[i].image.imageLayout = res.imageView->imageInfo().layout;
				}
				else
				{
					bindMask.clr(i);
					descriptors[i].image = m_common->dummyResources().imageViewDescriptor(binding.view, false);
				}
				break;

			case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
				if (res.sampler != nullptr && res.imageView != nullptr && res.imageView->handle(binding.view) != VK_NULL_HANDLE)
				{
					descriptors[i].image.sampler     = res.sampler->handle();
					descriptors[i].image.imageView   = res.imageView->handle(binding.view);
					descriptors[i].image.imageLayout = res.imageView->imageInfo().layout;
				}
				else
				{
					bindMask.clr(i);
					descriptors[i].image = m_common->dummyResources().imageSamplerDescriptor(binding.view);
				}
				break;

			case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
				if (res.bufferView != nullptr)
				{
					res.bufferView->updateView();
					descriptors[i].texelBuffer = res.bufferView->handle();
				}
				else
				{
					bindMask.clr(i);
					descriptors[i].texelBuffer = m_common->dummyResources().bufferViewDescriptor();
				}
				break;

			case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
				if (res.bufferView != nullptr)
				{
					res.bufferView->updateView();
					descriptors[i].texelBuffer = res.bufferView->handle();
				}
				else
				{
					bindMask.clr(i);
					descriptors[i].texelBuffer = m_common->dummyResources().bufferViewDescriptor();
				}
				break;

			case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
				if (res.bufferSlice.defined())
				{
					descriptors[i] = res.bufferSlice.getDescriptor();
				}
				else
				{
					descriptors[i].buffer = m_common->dummyResources().bufferDescriptor();
				}
				break;

			case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
				if (res.bufferSlice.defined())
				{
					descriptors[i] = res.bufferSlice.getDescriptor();
				}
				else
				{
					bindMask.clr(i);
					descriptors[i].buffer = m_common->dummyResources().bufferDescriptor();
				}
				break;

			case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
				if (res.bufferSlice.defined())
				{
					descriptors[i]               = res.bufferSlice.getDescriptor();
					descriptors[i].buffer.offset = 0;
				}
				else
				{
					descriptors[i].buffer = m_common->dummyResources().bufferDescriptor();
				}
				break;

			default:
				Logger::err(util::str::formatex("VltContext: Unhandled descriptor type: ", binding.type));
			}
		}

		// Allocate and update descriptor set
		auto& set = BindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS ? m_gpSet : m_cpSet;

		if (layout->bindingCount())
		{
			set = allocateDescriptorSet(layout->descriptorSetLayout());

			m_cmd->updateDescriptorSetWithTemplate(set,
												   layout->descriptorTemplate(), descriptors.data());
		}
		else
		{
			set = VK_NULL_HANDLE;
		}

		// Select the active binding mask to update
		auto& refMask = BindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS
							? m_state.gp.state.bsBindingMask
							: m_state.cp.state.bsBindingMask;

		// If some resources are not bound, we may need to
		// update spec constants and rebind the pipeline
		if (refMask != bindMask)
		{
			refMask = bindMask;

			m_flags.set(BindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS
							? VltContextFlag::GpDirtyPipelineState
							: VltContextFlag::CpDirtyPipelineState);
		}
	}

	VkDescriptorSet VltContext::allocateDescriptorSet(VkDescriptorSetLayout layout)
	{
		if (m_descPool == nullptr)
			m_descPool = m_device->createDescriptorPool();

		VkDescriptorSet set = m_descPool->alloc(layout);

		if (set == VK_NULL_HANDLE)
		{
			m_cmd->trackDescriptorPool(std::move(m_descPool));

			m_descPool = m_device->createDescriptorPool();
			set        = m_descPool->alloc(layout);
		}

		return set;
	}

	bool VltContext::updateComputePipelineState()
	{
		m_cpActivePipeline = m_state.cp.pipeline->getPipelineHandle(m_state.cp.state);

		if (unlikely(!m_cpActivePipeline))
			return false;

		m_cmd->cmdBindPipeline(
			VK_PIPELINE_BIND_POINT_COMPUTE,
			m_cpActivePipeline);

		m_flags.clr(VltContextFlag::CpDirtyPipelineState);
		return true;
	}

	void VltContext::updateComputeShaderResources()
	{
		if ((m_flags.test(VltContextFlag::CpDirtyResources)) || 
			(m_state.cp.pipeline->layout()->hasStaticBufferBindings()))
			this->updateShaderResources<VK_PIPELINE_BIND_POINT_COMPUTE>(m_state.cp.pipeline->layout());

		this->updateShaderDescriptorSetBinding<VK_PIPELINE_BIND_POINT_COMPUTE>(
			m_cpSet, m_state.cp.pipeline->layout());

		m_flags.clr(VltContextFlag::CpDirtyResources,
					VltContextFlag::CpDirtyDescriptorBinding);
	}

	void VltContext::updateGraphicsShaderResources()
	{
		if ((m_flags.test(VltContextFlag::GpDirtyResources)) || 
			(m_state.gp.pipeline->layout()->hasStaticBufferBindings()))
			this->updateShaderResources<VK_PIPELINE_BIND_POINT_GRAPHICS>(m_state.gp.pipeline->layout());

		this->updateShaderDescriptorSetBinding<VK_PIPELINE_BIND_POINT_GRAPHICS>(
			m_gpSet, m_state.gp.pipeline->layout());

		m_flags.clr(VltContextFlag::GpDirtyResources,
					VltContextFlag::GpDirtyDescriptorBinding);
	}

	VltGraphicsPipeline* VltContext::lookupGraphicsPipeline(
		const VltGraphicsPipelineShaders& shaders)
	{
		auto idx = shaders.hash() % m_gpLookupCache.size();

		if (unlikely(!m_gpLookupCache[idx] || !shaders.eq(m_gpLookupCache[idx]->shaders())))
			m_gpLookupCache[idx] = m_common->pipelineManager().createGraphicsPipeline(shaders);

		return m_gpLookupCache[idx];
	}

	VltComputePipeline* VltContext::lookupComputePipeline(
		const VltComputePipelineShaders& shaders)
	{
		auto idx = shaders.hash() % m_cpLookupCache.size();

		if (unlikely(!m_cpLookupCache[idx] || !shaders.eq(m_cpLookupCache[idx]->shaders())))
			m_cpLookupCache[idx] = m_common->pipelineManager().createComputePipeline(shaders);

		return m_cpLookupCache[idx];
	}

	void VltContext::commitComputePrevBarriers()
	{
		auto layout = m_state.cp.pipeline->layout();

		bool requiresBarrier = false;

		for (uint32_t i = 0; i < layout->bindingCount() && !requiresBarrier; i++)
		{
			if (m_state.cp.state.bsBindingMask.test(i))
			{
				const VltDescriptorSlot      binding = layout->binding(i);
				const VltShaderResourceSlot& slot    = m_rc[binding.slot];

				VltAccessFlags dstAccess = VltAccess::Read;
				VltAccessFlags srcAccess = 0;

				switch (binding.type)
				{
				case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
					if (binding.access & VK_ACCESS_SHADER_WRITE_BIT)
						dstAccess.set(VltAccess::Write);
					/* fall through */

				case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
				case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
					srcAccess = m_execBarriers.getBufferAccess(
						slot.bufferSlice.getSliceHandle());
					break;

				case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
					if (binding.access & VK_ACCESS_SHADER_WRITE_BIT)
						dstAccess.set(VltAccess::Write);
					/* fall through */

				case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
					srcAccess = m_execBarriers.getBufferAccess(
						slot.bufferView->getSliceHandle());
					break;

				case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
					if (binding.access & VK_ACCESS_SHADER_WRITE_BIT)
						dstAccess.set(VltAccess::Write);
					/* fall through */

				case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
				case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
					srcAccess = m_execBarriers.getImageAccess(
						slot.imageView->image(),
						slot.imageView->imageSubresources());
					break;

				default:
					/* nothing to do */;
				}

				if (srcAccess == 0)
				{
					continue;
				}

				// Skip write-after-write barriers if explicitly requested
				if ((m_barrierControl.test(VltBarrierControl::IgnoreWriteAfterWrite)) &&
					(m_execBarriers.getSrcStages() == VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT) &&
					(srcAccess.test(VltAccess::Write)) &&
					(dstAccess.test(VltAccess::Write)))
				{
					continue;
				}

				requiresBarrier = (srcAccess | dstAccess).test(VltAccess::Write);
			}
		}

		if (requiresBarrier)
			m_execBarriers.recordCommands(m_cmd);
	}

	void VltContext::commitComputePostBarriers()
	{
		auto layout = m_state.cp.pipeline->layout();

		for (uint32_t i = 0; i < layout->bindingCount(); i++)
		{
			if (m_state.cp.state.bsBindingMask.test(i))
			{
				const VltDescriptorSlot      binding = layout->binding(i);
				const VltShaderResourceSlot& slot    = m_rc[binding.slot];

				VkPipelineStageFlags stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
				VkAccessFlags        access = VK_ACCESS_SHADER_READ_BIT;

				switch (binding.type)
				{
				case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
					if (binding.access & VK_ACCESS_SHADER_WRITE_BIT)
						access |= VK_ACCESS_SHADER_WRITE_BIT;
					/* fall through */

				case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
				case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
					m_execBarriers.accessBuffer(
						slot.bufferSlice.getSliceHandle(),
						stages, access,
						slot.bufferSlice.bufferInfo().stages,
						slot.bufferSlice.bufferInfo().access);
					break;

				case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
					if (binding.access & VK_ACCESS_SHADER_WRITE_BIT)
						access |= VK_ACCESS_SHADER_WRITE_BIT;
					/* fall through */

				case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
					m_execBarriers.accessBuffer(
						slot.bufferView->getSliceHandle(),
						stages, access,
						slot.bufferView->bufferInfo().stages,
						slot.bufferView->bufferInfo().access);
					break;

				case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
					if (binding.access & VK_ACCESS_SHADER_WRITE_BIT)
						access |= VK_ACCESS_SHADER_WRITE_BIT;
					/* fall through */

				case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
				case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
					m_execBarriers.accessImage(
						slot.imageView->image(),
						slot.imageView->imageSubresources(),
						slot.imageView->imageInfo().layout,
						stages, access,
						slot.imageView->imageInfo().layout,
						slot.imageView->imageInfo().stages,
						slot.imageView->imageInfo().access);
					break;

				default:
					/* nothing to do */;
				}
			}
		}
	}

	void VltContext::updateFramebuffer()
	{
		this->endRendering();

		auto& framebuffer = m_state.cb.framebuffer;

		if (framebuffer == nullptr ||
			!framebuffer->matchTargets(m_state.cb.renderTargets))
		{
			framebuffer = m_device->createFramebuffer(
				m_state.cb.renderTargets);
			m_flags.set(VltContextFlag::GpDirtyFramebufferState);
		}

		if (m_flags.test(VltContextFlag::GpDirtyFramebufferState))
		{
			framebuffer->setAttachmentOps(m_state.cb.attachmentOps);
			framebuffer->setAttachmentClearValues(m_state.cb.clearValues);
			m_flags.clr(VltContextFlag::GpDirtyFramebufferState);
		}

		framebuffer->prepareRenderingLayout(m_execAcquires);
		m_execAcquires.recordCommands(m_cmd);

		m_flags.clr(VltContextFlag::GpDirtyFramebuffer);
	}

	void VltContext::resetFramebufferOps()
	{
		VltFrameBufferOps ops;
		ops.depthOps = m_state.cb.renderTargets.depth.view != nullptr
						   ? VltAttachmentOps{
								 VK_ATTACHMENT_LOAD_OP_LOAD,
								 VK_ATTACHMENT_STORE_OP_STORE
							 }
						   : VltAttachmentOps{};

		for (uint32_t i = 0; i < MaxNumRenderTargets; i++)
		{
			ops.colorOps[i] = m_state.cb.renderTargets.color[i].view != nullptr
								  ? VltAttachmentOps{
										VK_ATTACHMENT_LOAD_OP_LOAD,
										VK_ATTACHMENT_STORE_OP_STORE
									}
								  : VltAttachmentOps{};
		}
		m_state.cb.attachmentOps = ops;
	}

	void VltContext::emitMemoryBarrier(
		VkDependencyFlags     flags,
		VkPipelineStageFlags2 srcStages,
		VkAccessFlags2        srcAccess,
		VkPipelineStageFlags2 dstStages,
		VkAccessFlags2        dstAccess)
	{
		this->endRendering();

		VkMemoryBarrier2 barrier;
		barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
		barrier.pNext         = nullptr;
		barrier.srcStageMask  = srcStages;
		barrier.srcAccessMask = srcAccess;
		barrier.dstStageMask  = dstStages;
		barrier.dstAccessMask = dstAccess;

		VkDependencyInfo info;
		info.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		info.pNext                    = nullptr;
		info.dependencyFlags          = flags;
		info.memoryBarrierCount       = 1;
		info.pMemoryBarriers          = &barrier;
		info.bufferMemoryBarrierCount = 0;
		info.pBufferMemoryBarriers    = nullptr;
		info.imageMemoryBarrierCount  = 0;
		info.pImageMemoryBarriers     = nullptr;

		m_cmd->cmdPipelineBarrier2(
			VltCmdType::ExecBuffer, &info);
	}


}  // namespace sce::vlt