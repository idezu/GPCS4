#pragma once

#include "GnmCommon.h"

namespace sce::Gnm
{
// The "correct" way to build a pm4 packet should following the official standard,
// for example:
// using IT_DRAW_INDEX_2 to implement sceGnmDrawIndex
// But that will make things more complicated.
// So I defined our own private format for functions in gnm driver.
// This will make our dispatch routines more easier.
#define PM4_HEADER_BUILD(lenDw, op, priv) (((uint32_t)(((((uint16_t)lenDw) << 16) + 0x3FFE0000) | 0xC0000000)) | ((uint8_t)(op)) << 8 | ((uint8_t)(priv)))

#define PM4_PRIV(token) ((IT_OpCodePriv)(((uint32_t)(token)) & 0xFF))

#define PM4_TYPE(token) (((uint32_t)(token) >> 30) & 3)

#define PM4_LENGTH_DW(token) ((((uint32_t)(token) >> 16) & 0x3FFF) + 2)

	//////////////////////////////////////////////////////////////////////////
	//--------------------General PM4 HEADER--------------------
	typedef union PM4_HEADER
	{
		struct
		{
			uint32_t reserved : 16;
			uint32_t count : 14;
			uint32_t type : 2;  // PM4_TYPE
		};
		uint32_t u32All;

	} PM4_HEADER, *PPM4_HEADER;

	//--------------------TYPE_0_HEADER--------------------
	typedef union PM4_TYPE_0_HEADER
	{
		struct
		{
			uint32_t baseIndex : 16;
			uint32_t count : 14;
			uint32_t type : 2;  // PM4_TYPE
		};
		uint32_t u32All;

	} PM4_TYPE_0_HEADER, *PPM4_TYPE_0_HEADER;

	//--------------------TYPE_3_HEADER--------------------
	typedef union PM4_TYPE_3_HEADER
	{
		struct
		{
			uint32_t predicate : 1;
			uint32_t shaderType : 1;  // 0: Graphics, 1: Compute Shader
			uint32_t reserved : 6;
			uint32_t opcode : 8;  // IT_OpCodeType
			uint32_t count : 14;
			uint32_t type : 2;  // PM4_TYPE
		};
		uint32_t u32All;

	} PM4_TYPE_3_HEADER, *PPM4_TYPE_3_HEADER;

	constexpr unsigned int PM4_TYPE_0 = 0;
	constexpr unsigned int PM4_TYPE_2 = 2;
	constexpr unsigned int PM4_TYPE_3 = 3;

	// Some of the following opcode type is not used by PS4 Gnm.
	enum IT_OpCodeType : uint8_t
	{
		IT_NOP                                = 0x00000010,
		IT_SET_BASE                           = 0x00000011,
		IT_CLEAR_STATE                        = 0x00000012,
		IT_INDEX_BUFFER_SIZE                  = 0x00000013,
		IT_DISPATCH_DIRECT                    = 0x00000015,
		IT_DISPATCH_INDIRECT                  = 0x00000016,
		IT_INDIRECT_BUFFER_END                = 0x00000017,
		IT_INDIRECT_BUFFER_CNST_END           = 0x00000019,
		IT_ATOMIC_GDS                         = 0x0000001d,
		IT_ATOMIC_MEM                         = 0x0000001e,
		IT_OCCLUSION_QUERY                    = 0x0000001f,
		IT_SET_PREDICATION                    = 0x00000020,
		IT_REG_RMW                            = 0x00000021,
		IT_COND_EXEC                          = 0x00000022,
		IT_PRED_EXEC                          = 0x00000023,
		IT_DRAW_INDIRECT                      = 0x00000024,
		IT_DRAW_INDEX_INDIRECT                = 0x00000025,
		IT_INDEX_BASE                         = 0x00000026,
		IT_DRAW_INDEX_2                       = 0x00000027,
		IT_CONTEXT_CONTROL                    = 0x00000028,
		IT_INDEX_TYPE                         = 0x0000002a,
		IT_DRAW_INDIRECT_MULTI                = 0x0000002c,
		IT_DRAW_INDEX_AUTO                    = 0x0000002d,
		IT_NUM_INSTANCES                      = 0x0000002f,
		IT_DRAW_INDEX_MULTI_AUTO              = 0x00000030,
		IT_INDIRECT_BUFFER_PRIV               = 0x00000032,
		IT_INDIRECT_BUFFER_CNST               = 0x00000033,
		IT_COND_INDIRECT_BUFFER_CNST          = 0x00000033,
		IT_STRMOUT_BUFFER_UPDATE              = 0x00000034,
		IT_DRAW_INDEX_OFFSET_2                = 0x00000035,
		IT_DRAW_PREAMBLE                      = 0x00000036,
		IT_WRITE_DATA                         = 0x00000037,
		IT_DRAW_INDEX_INDIRECT_MULTI          = 0x00000038,
		IT_MEM_SEMAPHORE                      = 0x00000039,
		IT_DRAW_INDEX_MULTI_INST              = 0x0000003a,
		IT_COPY_DW                            = 0x0000003b,
		IT_WAIT_REG_MEM                       = 0x0000003c,
		IT_INDIRECT_BUFFER                    = 0x0000003f,
		IT_COND_INDIRECT_BUFFER               = 0x0000003f,
		IT_COPY_DATA                          = 0x00000040,
		IT_CP_DMA                             = 0x00000041,
		IT_PFP_SYNC_ME                        = 0x00000042,
		IT_SURFACE_SYNC                       = 0x00000043,
		IT_ME_INITIALIZE                      = 0x00000044,
		IT_COND_WRITE                         = 0x00000045,
		IT_EVENT_WRITE                        = 0x00000046,
		IT_EVENT_WRITE_EOP                    = 0x00000047,
		IT_EVENT_WRITE_EOS                    = 0x00000048,
		IT_RELEASE_MEM                        = 0x00000049,
		IT_PREAMBLE_CNTL                      = 0x0000004a,
		IT_DRAW_RESERVED0                     = 0x0000004c,
		IT_DRAW_RESERVED1                     = 0x0000004d,
		IT_DRAW_RESERVED2                     = 0x0000004e,
		IT_DRAW_RESERVED3                     = 0x0000004f,
		IT_DMA_DATA                           = 0x00000050,
		IT_CONTEXT_REG_RMW                    = 0x00000051,
		IT_GFX_CNTX_UPDATE                    = 0x00000052,
		IT_BLK_CNTX_UPDATE                    = 0x00000053,
		IT_INCR_UPDT_STATE                    = 0x00000055,
		IT_ACQUIRE_MEM                        = 0x00000058,
		IT_REWIND                             = 0x00000059,
		IT_INTERRUPT                          = 0x0000005a,
		IT_GEN_PDEPTE                         = 0x0000005b,
		IT_INDIRECT_BUFFER_PASID              = 0x0000005c,
		IT_PRIME_UTCL2                        = 0x0000005d,
		IT_LOAD_UCONFIG_REG                   = 0x0000005e,
		IT_LOAD_SH_REG                        = 0x0000005f,
		IT_LOAD_CONFIG_REG                    = 0x00000060,
		IT_LOAD_CONTEXT_REG                   = 0x00000061,
		IT_LOAD_COMPUTE_STATE                 = 0x00000062,
		IT_LOAD_SH_REG_INDEX                  = 0x00000063,
		IT_SET_CONFIG_REG                     = 0x00000068,
		IT_SET_CONTEXT_REG                    = 0x00000069,
		IT_SET_CONTEXT_REG_INDEX              = 0x0000006a,
		IT_SET_VGPR_REG_DI_MULTI              = 0x00000071,
		IT_SET_SH_REG_DI                      = 0x00000072,
		IT_SET_CONTEXT_REG_INDIRECT           = 0x00000073,
		IT_SET_SH_REG_DI_MULTI                = 0x00000074,
		IT_GFX_PIPE_LOCK                      = 0x00000075,
		IT_SET_SH_REG                         = 0x00000076,
		IT_SET_SH_REG_OFFSET                  = 0x00000077,
		IT_SET_QUEUE_REG                      = 0x00000078,
		IT_SET_UCONFIG_REG                    = 0x00000079,
		IT_SET_UCONFIG_REG_INDEX              = 0x0000007a,
		IT_FORWARD_HEADER                     = 0x0000007c,
		IT_SCRATCH_RAM_WRITE                  = 0x0000007d,
		IT_SCRATCH_RAM_READ                   = 0x0000007e,
		IT_LOAD_CONST_RAM                     = 0x00000080,
		IT_WRITE_CONST_RAM                    = 0x00000081,
		IT_DUMP_CONST_RAM                     = 0x00000083,
		IT_INCREMENT_CE_COUNTER               = 0x00000084,
		IT_INCREMENT_DE_COUNTER               = 0x00000085,
		IT_WAIT_ON_CE_COUNTER                 = 0x00000086,
		IT_WAIT_ON_DE_COUNTER_DIFF            = 0x00000088,
		IT_SWITCH_BUFFER                      = 0x0000008b,
		IT_FRAME_CONTROL                      = 0x00000090,
		IT_INDEX_ATTRIBUTES_INDIRECT          = 0x00000091,
		IT_WAIT_REG_MEM64                     = 0x00000093,
		IT_COND_PREEMPT                       = 0x00000094,
		IT_HDP_FLUSH                          = 0x00000095,
		IT_INVALIDATE_TLBS                    = 0x00000098,
		IT_DMA_DATA_FILL_MULTI                = 0x0000009a,
		IT_SET_SH_REG_INDEX                   = 0x0000009b,
		IT_DRAW_INDIRECT_COUNT_MULTI          = 0x0000009c,
		IT_DRAW_INDEX_INDIRECT_COUNT_MULTI    = 0x0000009d,
		IT_DUMP_CONST_RAM_OFFSET              = 0x0000009e,
		IT_LOAD_CONTEXT_REG_INDEX             = 0x0000009f,
		IT_SET_RESOURCES                      = 0x000000a0,
		IT_MAP_PROCESS                        = 0x000000a1,
		IT_MAP_QUEUES                         = 0x000000a2,
		IT_UNMAP_QUEUES                       = 0x000000a3,
		IT_QUERY_STATUS                       = 0x000000a4,
		IT_RUN_LIST                           = 0x000000a5,
		IT_MAP_PROCESS_VM                     = 0x000000a6,
		IT_DISPATCH_DRAW_PREAMBLE__GFX09      = 0x0000008c,
		IT_DISPATCH_DRAW_PREAMBLE_ACE__GFX09  = 0x0000008c,
		IT_DISPATCH_DRAW__GFX09               = 0x0000008d,
		IT_DISPATCH_DRAW_ACE__GFX09           = 0x0000008d,
		IT_GET_LOD_STATS__GFX09               = 0x0000008e,
		IT_DRAW_MULTI_PREAMBLE__GFX09         = 0x0000008f,
		IT_AQL_PACKET__GFX09                  = 0x00000099,
		IT_DISPATCH_DRAW_PREAMBLE__GFX101     = 0x0000008c,
		IT_DISPATCH_DRAW_PREAMBLE_ACE__GFX101 = 0x0000008c,
		IT_DISPATCH_DRAW__GFX101              = 0x0000008d,
		IT_DISPATCH_DRAW_ACE__GFX101          = 0x0000008d,
		IT_DRAW_MULTI_PREAMBLE__GFX101        = 0x0000008f,
		IT_AQL_PACKET__GFX101                 = 0x00000099,

		// Private IT OP TYPE, used for functions in libGnmDriver
		IT_GNM_PRIVATE = 0x000000FF,
	};

	const char* opcodeName(uint32_t header);

	//////////////////////////////////////////////////////////////////////////
	// For private opcode type (IT_GNM_PRIVATE),
	// we tread the lower 16 bits as IT_OpCodePriv
	// used for PM4_HEADER_BUILD
	enum IT_OpCodePriv : uint8_t
	{
		OP_PRIV_INITIALIZE_DEFAULT_HARDWARE_STATE   = 0x00,
		OP_PRIV_INITIALIZE_TO_DEFAULT_CONTEXT_STATE = 0x01,
		OP_PRIV_SET_EMBEDDED_VS_SHADER              = 0x02,
		OP_PRIV_SET_EMBEDDED_PS_SHADER              = 0x03,
		OP_PRIV_SET_VS_SHADER                       = 0x04,
		OP_PRIV_SET_PS_SHADER                       = 0x05,
		OP_PRIV_SET_ES_SHADER                       = 0x06,
		OP_PRIV_SET_GS_SHADER                       = 0x07,
		OP_PRIV_SET_HS_SHADER                       = 0x08,
		OP_PRIV_SET_LS_SHADER                       = 0x09,
		OP_PRIV_UPDATE_GS_SHADER                    = 0x0A,
		OP_PRIV_UPDATE_HS_SHADER                    = 0x0B,
		OP_PRIV_UPDATE_PS_SHADER                    = 0x0C,
		OP_PRIV_UPDATE_VS_SHADER                    = 0x0D,
		OP_PRIV_SET_VGT_CONTROL                     = 0x0E,
		OP_PRIV_RESET_VGT_CONTROL                   = 0x0F,
		OP_PRIV_DRAW_INDEX                          = 0x10,
		OP_PRIV_DRAW_INDEX_AUTO                     = 0x11,
		OP_PRIV_DRAW_INDEX_INDIRECT                 = 0x12,
		OP_PRIV_DRAW_INDEX_INDIRECT_COUNT_MULTI     = 0x13,
		OP_PRIV_DRAW_INDEX_MULTI_INSTANCED          = 0x14,
		OP_PRIV_DRAW_INDEX_OFFSET                   = 0x15,
		OP_PRIV_DRAW_INDIRECT                       = 0x16,
		OP_PRIV_DRAW_INDIRECT_COUNT_MULTI           = 0x17,
		OP_PRIV_DRAW_OPAQUE_AUTO                    = 0x18,
		OP_PRIV_WAIT_UNTIL_SAFE_FOR_RENDERING       = 0x19,
		OP_PRIV_PUSH_MARKER                         = 0x1A,
		OP_PRIV_PUSH_COLOR_MARKER                   = 0x1B,
		OP_PRIV_POP_MARKER                          = 0x1C,
		OP_PRIV_SET_MARKER                          = 0x1D,
		OP_PRIV_SET_CS_SHADER                       = 0x1E,
		OP_PRIV_DISPATCH_DIRECT                     = 0x1F,
		OP_PRIV_DISPATCH_INDIRECT                   = 0x20,
		OP_PRIV_COMPUTE_WAIT_ON_ADDRESS             = 0x21,
	};

	// OpCode hints
	// Usually the second dword of a PM4 packet. (But not all though.)
	enum OpCodeHint
	{
		OP_HINT_BASE_ALLOCATE_FROM_COMMAND_BUFFER         = 0x68750000,
		OP_HINT_BASE_MARK_DISPATCH_DRAW_ACB_ADDRESS       = 0x68750012,
		OP_HINT_GS_MODE_ENABLE                            = 0x00000290,
		OP_HINT_GS_MODE_ENABLE_ON_CHIP                    = 0x00000291,
		OP_HINT_GS_MODE_DISABLE                           = 0x000002E5,
		OP_HINT_WRITE_GPU_PREFETCH_INTO_L2                = 0x60000000,
		OP_HINT_PREPARE_FLIP_VOID                         = 0x68750777,
		OP_HINT_PREPARE_FLIP_LABEL                        = 0x68750778,
		OP_HINT_PREPARE_FLIP_WITH_EOP_INTERRUPT_VOID      = 0x68750780,
		OP_HINT_PREPARE_FLIP_WITH_EOP_INTERRUPT_LABEL     = 0x68750781,
		OP_HINT_SET_AA_SAMPLE_COUNT                       = 0x2F8,
		OP_HINT_SET_AA_SAMPLE_MASK1                       = 0x30E,
		OP_HINT_SET_AA_SAMPLE_MASK2                       = 0x30F,
		OP_HINT_SET_ACTIVE_SHADER_STAGES                  = 0x2D5,
		OP_HINT_SET_ALPHA_TO_MASK_CONTROL                 = 0x2DC,
		OP_HINT_SET_BORDER_COLOR_TABLE_ADDR               = 0x20,
		OP_HINT_SET_CB_CONTROL                            = 0x202,
		OP_HINT_SET_CLIP_CONTROL                          = 0x204,
		OP_HINT_SET_CLIP_RECTANGLE_RULE                   = 0x83,
		OP_HINT_SET_COMPUTE_SHADER_CONTROL                = 0x215,
		OP_HINT_SET_COMPUTE_SCRATCH_SIZE                  = 0x218,
		OP_HINT_SET_DB_COUNT_CONTROL                      = 0x1,
		OP_HINT_SET_DB_RENDER_CONTROL                     = 0x0,
		OP_HINT_SET_DEPTH_BOUNDS_RANGE                    = 0x8,
		OP_HINT_SET_DEPTH_CLEAR_VALUE                     = 0xB,
		OP_HINT_SET_DEPTH_EQAA_CONTROL                    = 0x201,
		OP_HINT_SET_DEPTH_RENDER_TARGET                   = 0x10,
		OP_HINT_SET_DEPTH_STENCIL_CONTROL                 = 0x200,
		OP_HINT_SET_DEPTH_STENCIL_DISABLE                 = 0x200,
		OP_HINT_SET_DISPATCH_DRAW_INDEX_DEALLOCATION_MASK = 0x2DD,
		OP_HINT_SET_DRAW_PAYLOAD_CONTROL                  = 0x2A6,
		OP_HINT_SET_FOVEATED_WINDOW                       = 0xEB,
		OP_HINT_SET_RESET_FOVEATED_WINDOW                 = 0xEB,
		OP_HINT_SET_GENERIC_SCISSOR                       = 0x90,
		OP_HINT_SET_GRAPHICS_SCRATCH_SIZE                 = 0x1BA,
		OP_HINT_SET_GS_MODE                               = 0x290,
		OP_HINT_SET_GS_MODE_DISABLE                       = 0x2E5,
		OP_HINT_SET_GS_ON_CHIP_CONTROL                    = 0x291,
		OP_HINT_SET_GUARD_BANDS                           = 0x2FA,
		OP_HINT_SET_HARDWARE_SCREEN_OFFSET                = 0x8D,
		OP_HINT_SET_HTILE_STENCIL0                        = 0x2B0,
		OP_HINT_SET_HTILE_STENCIL1                        = 0x2B1,
		OP_HINT_SET_INDEX_OFFSET                          = 0x102,
		OP_HINT_SET_INSTANCE_STEP_RATE                    = 0x2A8,
		OP_HINT_SET_LINE_WIDTH                            = 0x282,
		OP_HINT_SET_OBJECT_ID_MODE                        = 0x20D,
		OP_HINT_SET_PERF_COUNTER_CONTROL_PA               = 0x1808,
		OP_HINT_SET_PERFMON_ENABLE                        = 0xD8,
		OP_HINT_SET_POINT_MIN_MAX                         = 0x281,
		OP_HINT_SET_POINT_SIZE                            = 0x280,
		OP_HINT_SET_POLYGON_OFFSET_BACK                   = 0x2E2,
		OP_HINT_SET_POLYGON_OFFSET_CLAMP                  = 0x2DF,
		OP_HINT_SET_POLYGON_OFFSET_FRONT                  = 0x2E0,
		OP_HINT_SET_POLYGON_OFFSET_Z_FORMAT               = 0x2DE,
		OP_HINT_SET_PRIMITIVE_ID_ENABLE                   = 0x2A1,
		OP_HINT_SET_PRIMITIVE_RESET_INDEX                 = 0x103,
		OP_HINT_SET_PRIMITIVE_RESET_INDEX_ENABLE          = 0x2A5,
		OP_HINT_SET_PRIMITIVE_SETUP                       = 0x205,
		OP_HINT_SET_PRIMITIVE_TYPE_BASE                   = 0x242,
		OP_HINT_SET_PRIMITIVE_TYPE_NEO                    = 0x10000242,
		OP_HINT_SET_PS_SHADER_RATE                        = 0x293,
		OP_HINT_SET_PS_SHADER_SAMPLE_EXCLUSION_MASK       = 0x6,
		OP_HINT_SET_PS_SHADER_USAGE                       = 0x191,
		OP_HINT_SET_RENDER_OVERRIDE2CONTROL               = 0x4,
		OP_HINT_SET_RENDER_OVERRIDE_CONTROL               = 0x3,
		OP_HINT_SET_RENDER_TARGET_MASK                    = 0x8E,
		OP_HINT_SET_SCALED_RESOLUTION_GRID                = 0xE8,
		OP_HINT_SET_SCAN_MODE_CONTROL                     = 0x292,
		OP_HINT_SET_SCREEN_SCISSOR                        = 0xC,
		OP_HINT_SET_SSHARP_IN_USER_DATA                   = 0x68750006,
		OP_HINT_SET_STENCIL                               = 0x10C,
		OP_HINT_SET_STENCIL_CLEAR_VALUE                   = 0xA,
		OP_HINT_SET_STENCIL_OP_CONTROL                    = 0x10B,
		OP_HINT_SET_STENCIL_SEPARATE                      = 0x10C,
		OP_HINT_SET_STREAMOUT_MAPPING                     = 0x2E6,
		OP_HINT_SET_TESSELLATION_DISTRIBUTION_THRESHOLDS  = 0x2D4,
		OP_HINT_SET_TEXTURE_GRADIENT_FACTORS              = 0x382,
		OP_HINT_SET_TSHARP_IN_USER_DATA                   = 0x68750005,
		OP_HINT_SETUP_DRAW_OPAQUE_PARAMETERS_0            = 0x2CC,
		OP_HINT_SETUP_DRAW_OPAQUE_PARAMETERS_1            = 0x2CA,
		OP_HINT_SETUP_ES_GS_RING_REGISTERS                = 0x2AB,
		OP_HINT_SETUP_GS_VS_RING_REGISTERS                = 0x2D7,
		OP_HINT_SET_VERTEX_QUANTIZATION                   = 0x2F9,
		OP_HINT_SET_VERTEX_REUSE_ENABLE                   = 0x2AD,
		OP_HINT_SET_VIEWPORT_TRANSFORM_CONTROL            = 0x206,
		OP_HINT_SET_VSHARP_IN_USER_DATA                   = 0x68750004,
		OP_HINT_SET_VS_SHADER_STREAMOUT_ENABLE            = 0x2E5,
		OP_HINT_SET_WINDOW_OFFSET                         = 0x80,
		OP_HINT_SET_WINDOW_SCISSOR                        = 0x81,
		OP_HINT_SET_USER_DATA_REGION                      = 0x6875000D,
	};

	/*

	enum OpCodeDraw : uint32_t
	{
		OP_CODE_BASE = 0xC0001000,
		OP_CODE_CHAIN_COMMAND_BUFFER = 0xC0023F00,
		OP_CODE_GS_MODE = 0xC0016900,
		OP_CODE_ORDERED_APPEND_ALLOCATION_COUNTER = 0xC0037900,
		OP_CODE_DISPATCH_DRAW = 0xC0028D00,
		OP_CODE_WRITE_GPU = 0xC0055000,
		OP_CODE_FLUSH_SHADER_CACHES_AND_WAIT = 0xC0055800,
		OP_CODE_FLUSH_SHADER_CACHES_AND_WAIT_DBCACHE = 0xC0004600,
		OP_CODE_FLUSH_STREAMOUT_0 = 0xC0004600,
		OP_CODE_FLUSH_STREAMOUT_1 = 0xC0055800,
		OP_CODE_FLUSH_STREAMOUT_2 = 0xC0017900,
		OP_CODE_FLUSH_STREAMOUT_3 = 0xC0053C00,
		OP_CODE_INCREMENT_DE_COUNTER = 0xC0008500,
		OP_CODE_PAUSE = 0xC0005900,
		OP_CODE_PREPARE_FLIP = 0xC03E1000,
		OP_CODE_READ_PERF_COUNTER_EG01 = 0xC0055000,
		OP_CODE_READ_PERF_COUNTER_EG23 = 0xC0091000,
		OP_CODE_READ_DATA_FROM_GDS_CS = 0xC0034802,
		OP_CODE_READ_DATA_FROM_GDS_PS = 0xC0034800,
		OP_CODE_REQUEST_MIP_STATS_REPORT_AND_RESET = 0xC0028E00,
		OP_CODE_SELECT_PERF_COUNTER_SLOT01 = 0xC0017900,
		OP_CODE_SELECT_PERF_COUNTER_SLOT23 = 0xC0051000,
		OP_CODE_SET = 0xC0016900,
		OP_CODE_SET_2 = 0xC0026900,
		OP_CODE_SET_3 = 0xC0036900,
		OP_CODE_SET_4 = 0xC0046900,
		OP_CODE_SET_AA_SAMPLE_LOCATION_CONTROL = 0xC0106900,
		OP_CODE_SET_BASE_INDIRECT_ARGS_GRAPHICS = 0xC0021100,
		OP_CODE_SET_BASE_INDIRECT_ARGS_COMPUTE = 0xC0021102,
		OP_CODE_SET_BLEND_COLOR = 0xC0046900,
		OP_CODE_SET_COMPUTE = 0xC0017602,
		OP_CODE_SET_CONFIG_REGISTER = 0xC0016800,
		OP_CODE_SET_CONFIG_REGISTER_RANGE = 0xC0006800, // note: not real value
		OP_CODE_SET_DEPTH_RENDER_TARGET = 0xC0086900,
		OP_CODE_SET_GRAPHICS_SHADER_CONTROL = 0xC0017600,
		OP_CODE_SET_INDEX_BUFFER = 0xC0012600,
		OP_CODE_SET_INDEX_COUNT = 0xC0001300,
		OP_CODE_SET_INDEX_SIZE = 0xC0002A00,
		OP_CODE_SET_NUM_INSTANCES = 0xC0002F00,
		OP_CODE_SET_OBJECT_ID = 0xC0017900,
		OP_CODE_SET_PERF_COUNTER_CONTROL_PA = 0xC0017900,
		OP_CODE_SET_PERF_COUNTER_CONTROL_PN = 0xC0011000,
		OP_CODE_SET_PERSISTENT_REGISTER_GRAPHICS = 0xC0017600,
		OP_CODE_SET_PERSISTENT_REGISTER_COMPUTE = 0xC0017602,
		OP_CODE_SET_POINTER_IN_USER_DATA_GRAPHICS = 0xC0027600,
		OP_CODE_SET_POINTER_IN_USER_DATA_COMPUTE = 0xC0027602,
		OP_CODE_SET_PREDICATION = 0xC0032200,
		OP_CODE_SET_PRIMITIVE_TYPE = 0xC0017900,
		OP_CODE_SET_RENDER_TARGET = 0xC00E6900,
		OP_CODE_SET_TEXTURE_GRADIENT_FACTORS = 0xC0017900,
		OP_CODE_SET_USER_CONFIG_REGISTER = 0xC0017900,
		OP_CODE_SET_USER_CONFIG_REGISTER_RANGE = 0xC0007900,  // not real value
		OP_CODE_SET_USER_CONFIG_REGISTER_WITH_INDEX = 0xC0017900,
		OP_CODE_SET_USER_DATA_CS = 0xC0017602,
		OP_CODE_SET_USER_DATA_NOT_CS = 0xC0017600,
		OP_CODE_SET_USER_DATA_REGION = 0xC0007600,
		OP_CODE_SET_Z_PASS_PREDICATION = 0xC0012000,
		OP_CODE_SIGNAL_SEMAPHORE = 0xC0013900,
		OP_CODE_STALL_COMMAND_BUFFER_PARSER = 0xC0004200,
		OP_CODE_TRIGGER_END_OF_PIPE_INTERRUPT = 0xC0044700,
		OP_CODE_TRIGGER_EVENT = 0xC0004600,
		OP_CODE_WAIT_FOR_GRAPHICS_WRITES = 0xC0055800,
		OP_CODE_WAIT_FOR_SETUP_DISPATCH_DRAW_KICK_RING_BUFFER_PRED_ENABLE = 0xC0048C01,
		OP_CODE_WAIT_FOR_SETUP_DISPATCH_DRAW_KICK_RING_BUFFER_PRED_DISABLE = 0xC0048C00,
		OP_CODE_WAIT_ON_ADDRESS = 0xC0053C00,
		OP_CODE_WAIT_ON_ADDRESS_AND_STALL_COMMAND_BUFFER_PARSER = 0xC0053C00,
		OP_CODE_WAIT_ON_CE = 0xC0008600,
		OP_CODE_WAIT_ON_REGISTER = 0xC0053C00,
		OP_CODE_WAIT_SEMAPHORE = 0xC0013900,
		OP_CODE_WRITE_AT_END_OF_PIPE = 0xC0044700,
		OP_CODE_WRITE_AT_END_OF_PIPE_WITH_INTERRUPT = 0xC0044700,
		OP_CODE_WRITE_AT_END_OF_SHADER_CS = 0xC0034802,
		OP_CODE_WRITE_AT_END_OF_SHADER_PS = 0xC0034800,
		OP_CODE_WRITE_DATA_INLINE = 0xC0003700,  // not real value
		OP_CODE_WRITE_DATA_INLINE_THROUGH_L2 = 0xC0003700,  // not real value
		OP_CODE_WRITE_EVENT_STATS = 0xC0024600,
		OP_CODE_WRITE_OCCLUSION_QUERY = 0xC0055000,
		OP_CODE_WRITE_STREAMOUT_BUFFER_OFFSET = 0xC0043400,
		OP_CODE_WRITE_STREAMOUT_BUFFER_UPDATE = 0xC0043400,
		OP_CODE_WRITE_WAIT_MEM_CMD = 0xC0053C00
		//OP_SET_REGISTER_RANGE =,  // can be set to a arbitrary PM4 header(opcode)
	};


*/

}  // namespace sce::Gnm
