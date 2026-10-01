#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_PIPELINE_BLENDMAPPING_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_PIPELINE_BLENDMAPPING_H_

#include <cstdint>

namespace Libs::Graphics {

namespace HW {
struct BlendControl;
}
namespace Prospero {
struct ColorComponentMapping;
}

enum class BlendMappingSupport {
	Direct,
	SourceAlpha,  // Requires logical alpha in the second blend source.
	LogicalAlpha, // Every written physical component holds logical alpha.
	Unsupported,
};

bool BlendFactorIsDualSource(uint8_t factor);
// physical_write_mask selects the physical components that exist in the attachment and are
// written by the draw. The default assumes all four.
BlendMappingSupport ClassifyBlendMapping(const HW::BlendControl&                blend,
                                         const Prospero::ColorComponentMapping& mapping,
                                         uint32_t physical_write_mask = 0x0fu);
// Rewrites the guest alpha equation for physical components that hold logical alpha. The result
// uses one equation for all host channels and only color factors, so Vulkan reads the physical
// component that holds logical alpha.
HW::BlendControl LogicalAlphaBlendControl(const HW::BlendControl& blend);

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_PIPELINE_BLENDMAPPING_H_
