#include "gpu_tiler_standard4_3d.inc"
uvec3 standard64_3d_extent() {
	if (ELEMENT_BYTES == 1) return uvec3(64, 32, 32);
	if (ELEMENT_BYTES == 2) return uvec3(32, 32, 32);
	if (ELEMENT_BYTES == 4) return uvec3(32, 32, 16);
	if (ELEMENT_BYTES == 8) return uvec3(32, 16, 16);
	return uvec3(16, 16, 16);
}
uint standard64_3d_offset(uvec3 p) {
	uint base = standard3d_offset(p);
	// A switch on the spec constant, like the 2D path: a nested ternary building a uvec4
	// from spec constants crashes AMD's Windows shader compiler.
	switch (ELEMENT_BYTES) {
		case 1:
			return base ^ (((p.x >> 4) & 1u) << 12) ^ (((p.z >> 4) & 1u) << 13) ^
			       (((p.y >> 4) & 1u) << 14) ^ (((p.x >> 5) & 1u) << 15);
		case 2:
			return base ^ (((p.x >> 3) & 1u) << 12) ^ (((p.z >> 4) & 1u) << 13) ^
			       (((p.y >> 4) & 1u) << 14) ^ (((p.x >> 4) & 1u) << 15);
		case 4:
			return base ^ (((p.x >> 3) & 1u) << 12) ^ (((p.z >> 3) & 1u) << 13) ^
			       (((p.y >> 4) & 1u) << 14) ^ (((p.x >> 4) & 1u) << 15);
		case 8:
			return base ^ (((p.x >> 3) & 1u) << 12) ^ (((p.z >> 3) & 1u) << 13) ^
			       (((p.y >> 3) & 1u) << 14) ^ (((p.x >> 4) & 1u) << 15);
		default:
			return base ^ (((p.x >> 2) & 1u) << 12) ^ (((p.z >> 3) & 1u) << 13) ^
			       (((p.y >> 3) & 1u) << 14) ^ (((p.x >> 3) & 1u) << 15);
	}
}
