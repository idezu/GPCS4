#pragma once

#include "VltCommon.h"
#include "VltHash.h"
#include "VltPipeLayout.h"
#include "VltShader.h"
#include "VltRenderState.h"
#include "VltRenderTarget.h"

namespace sce::vlt
{
	class VltDevice;
	class VltPipelineManager;

	/**
     * \brief Flags that describe pipeline properties
     */
	enum class VltGraphicsPipelineFlag
	{
		HasTransformFeedback,
		HasStorageDescriptors,
	};

	using VltGraphicsPipelineFlags = util::Flags<VltGraphicsPipelineFlag>;

	/**
     * \brief Shaders used in graphics pipelines
     */
	struct VltGraphicsPipelineShaders
	{
		Rc<VltShader> vs;
		Rc<VltShader> tcs;
		Rc<VltShader> tes;
		Rc<VltShader> gs;
		Rc<VltShader> fs;

		bool eq(const VltGraphicsPipelineShaders& other) const
		{
			return vs == other.vs &&
				   tcs == other.tcs &&
				   tes == other.tes &&
				   gs == other.gs &&
				   fs == other.fs;
		}

		size_t hash() const
		{
			VltHashState state;
			state.add(VltShader::getHash(vs));
			state.add(VltShader::getHash(tcs));
			state.add(VltShader::getHash(tes));
			state.add(VltShader::getHash(gs));
			state.add(VltShader::getHash(fs));
			return state;
		}
	};

	/**
     * \brief Common graphics pipeline state
     * 
     * Non-dynamic pipeline state that cannot
     * be changed dynamically.
     */
	struct VltGraphicsCommonPipelineStateInfo
	{
		bool  msSampleShadingEnable = false;
		float msSampleShadingFactor = 0.0;
	};

	/**
     * \brief Graphics pipeline instance
     * 
     * Stores a state vector and the
     * corresponding pipeline handle.
     */
	class VltGraphicsPipelineInstance
	{

	public:
		VltGraphicsPipelineInstance() :
			m_stateVector(),
			m_pipeline(VK_NULL_HANDLE)
		{
		}

		VltGraphicsPipelineInstance(
			const VltGraphicsPipelineStateInfo& state,
			const VltAttachmentFormat&          format,
			VkPipeline                          pipe) :
			m_stateVector(state),
			m_format(format),
			m_pipeline(pipe)
		{
		}

		/**
         * \brief Checks for matching pipeline state
         * 
         * \param [in] stateVector Graphics pipeline state
         * \returns \c true if the specialization is compatible
         */
		bool isCompatible(
			const VltGraphicsPipelineStateInfo& state,
			const VltAttachmentFormat&          format)
		{
			return m_stateVector == state &&
				   m_format.eq(format);
		}

		/**
         * \brief Retrieves pipeline
         * \returns The pipeline handle
         */
		VkPipeline pipeline() const
		{
			return m_pipeline;
		}

	private:
		VltGraphicsPipelineStateInfo m_stateVector;
		VltAttachmentFormat          m_format;
		VkPipeline                   m_pipeline;
	};

	/**
     * \brief Graphics pipeline
     * 
     * Stores the pipeline layout as well as methods to
     * recompile the graphics pipeline against a given
     * pipeline state vector.
     */
	class VltGraphicsPipeline
	{

	public:
		VltGraphicsPipeline(
			VltPipelineManager*        pipeMgr,
			VltGraphicsPipelineShaders shaders);

		~VltGraphicsPipeline();

		/**
         * \brief Shaders used by the pipeline
         * \returns Shaders used by the pipeline
         */
		const VltGraphicsPipelineShaders& shaders() const
		{
			return m_shaders;
		}

		/**
         * \brief Returns graphics pipeline flags
         * \returns Graphics pipeline property flags
         */
		VltGraphicsPipelineFlags flags() const
		{
			return m_flags;
		}

		/**
         * \brief Pipeline layout
         * 
         * Stores the pipeline layout and the descriptor set
         * layout, as well as information on the resource
         * slots used by the pipeline.
         * \returns Pipeline layout
         */
		VltPipelineLayout* layout() const
		{
			return m_layout.ptr();
		}

		/**
         * \brief Queries shader for a given stage
         * 
         * In case no shader is specified for the
         * given stage, \c nullptr will be returned.
         * \param [in] stage The shader stage
         * \returns Shader of the given stage
         */
		Rc<VltShader> getShader(
			VkShaderStageFlagBits stage) const;

		/**
         * \brief Pipeline handle
         * 
         * Retrieves a pipeline handle for the given pipeline
         * state. If necessary, a new pipeline will be created.
         * \param [in] state Pipeline state vector
         * \param [in] format Attachments' format
         * \returns Pipeline handle
         */
		VkPipeline getPipelineHandle(
			const VltGraphicsPipelineStateInfo& state,
			const VltAttachmentFormat&          format);

		/**
         * \brief Compiles a pipeline
         * 
         * Asynchronously compiles the given pipeline
         * and stores the result for future use.
         * \param [in] state Pipeline state vector
         */
		void compilePipeline(
			const VltGraphicsPipelineStateInfo& state,
			const VltAttachmentFormat&          format);

	private:
		VltDevice*          m_device;
		VltPipelineManager* m_pipeMgr;

		VltGraphicsPipelineShaders m_shaders;
		VltDescriptorSlotMapping   m_slotMapping;

		Rc<VltPipelineLayout> m_layout;

		uint32_t m_vsIn  = 0;
		uint32_t m_fsOut = 0;

		VltGraphicsPipelineFlags           m_flags;
		VltGraphicsCommonPipelineStateInfo m_common = {};

		// List of pipeline instances, shared between threads
		alignas(CACHE_LINE_SIZE) util::sync::Spinlock m_mutex;
		std::vector<VltGraphicsPipelineInstance> m_pipelines;

		VltGraphicsPipelineInstance* createInstance(
			const VltGraphicsPipelineStateInfo& state,
			const VltAttachmentFormat&          format);

		VltGraphicsPipelineInstance* findInstance(
			const VltGraphicsPipelineStateInfo& state,
			const VltAttachmentFormat&          format);

		VkPipeline createPipeline(
			const VltGraphicsPipelineStateInfo& state,
			const VltAttachmentFormat&          format) const;

		void destroyPipeline(
			VkPipeline pipeline) const;

		VltShaderModule createShaderModule(
			const Rc<VltShader>&                shader,
			const VltGraphicsPipelineStateInfo& state) const;

		Rc<VltShader> getPrevStageShader(
			VkShaderStageFlagBits stage) const;

		bool validatePipelineState(
			const VltGraphicsPipelineStateInfo& state) const;

		void logPipelineState(
			LogLevel                            level,
			const VltGraphicsPipelineStateInfo& state) const;
	};

}  // namespace sce::vlt