#include "graphics/host_gpu/renderer/pipeline/blendMapping.h"

#include "graphics/guest_gpu/hardwareContext.h"

#include <initializer_list>

namespace Libs::Graphics {

bool BlendFactorIsDualSource(uint8_t factor) {
	return factor >= static_cast<uint8_t>(Prospero::BlendFactor::kSrc1Color) &&
	       factor <= static_cast<uint8_t>(Prospero::BlendFactor::kOneMinusSrc1Alpha);
}

namespace {

bool BlendFactorIsConstantColor(uint8_t factor) {
	return factor == static_cast<uint8_t>(Prospero::BlendFactor::kConstantColor) ||
	       factor == static_cast<uint8_t>(Prospero::BlendFactor::kOneMinusConstantColor);
}

bool WritesOnlyLogicalAlpha(const Prospero::ColorComponentMapping& mapping,
                            uint32_t                               physical_write_mask) {
	physical_write_mask &= 0x0fu;
	if (physical_write_mask == 0) {
		return false;
	}
	for (uint32_t physical_component = 0; physical_component < 4u; physical_component++) {
		if ((physical_write_mask & (1u << physical_component)) != 0 &&
		    mapping.Map(physical_component) != 3u) {
			return false;
		}
	}
	return true;
}

// In the alpha equation every guest factor reduces to a scalar taken from an alpha source.
// Expressed as a color factor, Vulkan reads it from the physical component holding logical alpha.
uint8_t LogicalAlphaFactor(uint8_t factor) {
	using Factor = Prospero::BlendFactor;
	const auto to_u8 = [](Factor f) { return static_cast<uint8_t>(f); };
	switch (static_cast<Factor>(factor)) {
		case Factor::kSrcColor:
		case Factor::kSrcAlpha: return to_u8(Factor::kSrcColor);
		case Factor::kOneMinusSrcColor:
		case Factor::kOneMinusSrcAlpha: return to_u8(Factor::kOneMinusSrcColor);
		case Factor::kDstColor:
		case Factor::kDstAlpha: return to_u8(Factor::kDstColor);
		case Factor::kOneMinusDstColor:
		case Factor::kOneMinusDstAlpha: return to_u8(Factor::kOneMinusDstColor);
		// The alpha component of SRC_ALPHA_SATURATE is one.
		case Factor::kSrcAlphaSaturate: return to_u8(Factor::kOne);
		case Factor::kConstantColor:
		case Factor::kConstantAlpha: return to_u8(Factor::kConstantAlpha);
		case Factor::kOneMinusConstantColor:
		case Factor::kOneMinusConstantAlpha: return to_u8(Factor::kOneMinusConstantAlpha);
		// The second source uses the same export mapping as the first.
		case Factor::kSrc1Color:
		case Factor::kSrc1Alpha: return to_u8(Factor::kSrc1Color);
		case Factor::kOneMinusSrc1Color:
		case Factor::kOneMinusSrc1Alpha: return to_u8(Factor::kOneMinusSrc1Color);
		default: return factor;
	}
}

} // namespace

HW::BlendControl LogicalAlphaBlendControl(const HW::BlendControl& blend) {
	const bool separate = blend.separate_alpha_blend;
	const auto src      = LogicalAlphaFactor(separate ? blend.alpha_srcblend : blend.color_srcblend);
	const auto dst = LogicalAlphaFactor(separate ? blend.alpha_destblend : blend.color_destblend);
	const auto fcn = separate ? blend.alpha_comb_fcn : blend.color_comb_fcn;

	HW::BlendControl r     = blend;
	r.color_srcblend       = src;
	r.color_destblend      = dst;
	r.color_comb_fcn       = fcn;
	r.alpha_srcblend       = src;
	r.alpha_destblend      = dst;
	r.alpha_comb_fcn       = fcn;
	r.separate_alpha_blend = false;
	return r;
}

BlendMappingSupport ClassifyBlendMapping(const HW::BlendControl&                blend,
                                         const Prospero::ColorComponentMapping& mapping,
                                         uint32_t physical_write_mask) {
	// Alpha-only targets, such as single-channel alpha masks, blend with the guest alpha
	// equation on whichever physical component stores logical alpha. Dual-source factors
	// are excluded because dual-source blending is only set up for slot 0 by PrepareProgram.
	if (mapping.Map(3) != 3u && WritesOnlyLogicalAlpha(mapping, physical_write_mask) &&
	    !BlendFactorIsDualSource(blend.separate_alpha_blend ? blend.alpha_srcblend : blend.color_srcblend) &&
	    !BlendFactorIsDualSource(blend.separate_alpha_blend ? blend.alpha_destblend : blend.color_destblend)) {
		return BlendMappingSupport::LogicalAlpha;
	}
	// Color constants are not swizzled with the exports; scalar constant alpha is unaffected.
	if (!mapping.IsIdentity() && (BlendFactorIsConstantColor(blend.color_srcblend) ||
	                              BlendFactorIsConstantColor(blend.color_destblend))) {
		return BlendMappingSupport::Unsupported;
	}
	if (mapping.Map(3) == 3u) {
		return BlendMappingSupport::Direct;
	}
	// Moving alpha requires the same equation for all channels.
	if (blend.separate_alpha_blend && (blend.alpha_srcblend != blend.color_srcblend ||
	                                   blend.alpha_destblend != blend.color_destblend ||
	                                   blend.alpha_comb_fcn != blend.color_comb_fcn)) {
		return BlendMappingSupport::Unsupported;
	}
	auto support = BlendMappingSupport::Direct;
	for (const auto factor: {blend.color_srcblend, blend.color_destblend}) {
		if (BlendFactorIsDualSource(factor)) {
			return BlendMappingSupport::Unsupported;
		}
		switch (static_cast<Prospero::BlendFactor>(factor)) {
			case Prospero::BlendFactor::kSrcAlpha:
			case Prospero::BlendFactor::kOneMinusSrcAlpha:
				support = BlendMappingSupport::SourceAlpha;
				break;
			case Prospero::BlendFactor::kDstAlpha:
			case Prospero::BlendFactor::kOneMinusDstAlpha:
			case Prospero::BlendFactor::kSrcAlphaSaturate: return BlendMappingSupport::Unsupported;
			default: break;
		}
	}
	return support;
}

} // namespace Libs::Graphics
