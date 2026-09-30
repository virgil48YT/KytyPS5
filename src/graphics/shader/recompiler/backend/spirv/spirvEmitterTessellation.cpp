#include "graphics/shader/recompiler/backend/spirv/spirvEmitterInstructions.h"

#include <algorithm>

namespace Libs::Graphics::ShaderRecompiler::Spirv::Emitter {
namespace {

uint32_t TessellationPointer(ValueEmitContext& ctx, const IR::Inst& inst) {
	auto& state          = ctx.state;
	using Attribute      = IR::TessellationAttribute;
	const auto  kind     = static_cast<Attribute>(inst.Arg(0).U32());
	const auto& tess     = state.input_info.vertex->tess;
	const auto  variable = state.tess_variables.at(static_cast<uint32_t>(kind));
	EXIT_IF(variable == 0);
	const auto input   = kind == Attribute::ControlInput || kind == Attribute::EvaluationInput;
	const auto storage = input ? spv::StorageClassInput : spv::StorageClassOutput;
	const auto pointer = state.builder.AllocateId();
	if (kind == Attribute::Factor) {
		EXIT_NOT_IMPLEMENTED(!inst.Arg(1).IsImmediate());
		const auto index = inst.Arg(1).U32() / 4u;
		const auto outer = index < 3u;
		EXIT_NOT_IMPLEMENTED(index >= 4u);
		state.builder.AddFunction(spv::OpAccessChain, TypePointer(state, storage, TypeF32(state)),
		                          pointer, outer ? variable : state.tess_inner_variable,
		                          ConstantU32(state, outer ? index : index - 3u));
		return pointer;
	}
	auto address = ctx.Arg(inst, 1);
	if (kind == Attribute::PatchOutput) {
		address =
		    EmitBinaryU32(state, spv::OpISub, address, ConstantU32(state, state.tess_patch_base));
	}
	const bool local  = kind == Attribute::LocalOutput || kind == Attribute::ControlInput;
	const auto stride = local ? tess.ls_stride : tess.hs_stride;
	const auto offset = kind == Attribute::PatchOutput ? address
	                                                   : EmitBinaryU32(state, spv::OpUMod, address,
	                                                                   ConstantU32(state, stride));
	const auto attribute =
	    EmitBinaryU32(state, spv::OpShiftRightLogical, offset, ConstantU32(state, 4));
	const auto component =
	    EmitBinaryU32(state, spv::OpBitwiseAnd,
	                  EmitBinaryU32(state, spv::OpShiftRightLogical, offset, ConstantU32(state, 2)),
	                  ConstantU32(state, 3));
	const auto type = TypePointer(state, storage, TypeU32(state));
	if (kind == Attribute::LocalOutput || kind == Attribute::PatchOutput) {
		state.builder.AddFunction(spv::OpAccessChain, type, pointer, variable, attribute,
		                          component);
	} else {
		const auto vertex =
		    kind == Attribute::ControlOutput
		        ? EmitLaneId(state)
		        : EmitBinaryU32(state, spv::OpUDiv, address, ConstantU32(state, stride));
		state.builder.AddFunction(spv::OpAccessChain, type, pointer, variable, vertex, attribute,
		                          component);
	}
	return pointer;
}

// Handles the non-immediate Factor tessellation attribute case by branching on
// whether the runtime index selects an outer (index < 3) or inner (index >= 3)
// tessellation factor. The callback is invoked in each branch with the
// appropriate OpAccessChain pointer, so the caller performs OpLoad or OpStore
// inside the branch — avoiding OpPhi on pointer types (which would require the
// VariablePointers capability).
template <typename Fn>
uint32_t TessellationFactorAccess(ValueEmitContext& ctx, const IR::Inst& inst, Fn&& fn) {
	auto& state          = ctx.state;
	using Attribute      = IR::TessellationAttribute;
	const auto  kind     = static_cast<Attribute>(inst.Arg(0).U32());
	const auto  variable = state.tess_variables.at(static_cast<uint32_t>(kind));
	EXIT_IF(variable == 0);
	const auto input   = kind == Attribute::ControlInput || kind == Attribute::EvaluationInput;
	const auto storage = input ? spv::StorageClassInput : spv::StorageClassOutput;
	const auto ptr_type = TypePointer(state, storage, TypeF32(state));

	const auto address  = ctx.Arg(inst, 1);
	const auto index    = EmitBinaryU32(state, spv::OpUDiv, address, ConstantU32(state, 4u));
	const auto is_outer = EmitULessThan32(state, index, ConstantU32(state, 3u));

	const auto then_label  = state.builder.AllocateId();
	const auto else_label  = state.builder.AllocateId();
	const auto merge_label = state.builder.AllocateId();
	state.builder.AddFunction(spv::OpSelectionMerge, merge_label, spv::SelectionControlMaskNone);
	state.builder.AddFunction(spv::OpBranchConditional, is_outer, then_label, else_label);

	EmitLabel(state, then_label);
	const auto outer_ptr = state.builder.AllocateId();
	state.builder.AddFunction(spv::OpAccessChain, ptr_type, outer_ptr, variable, index);
	const auto outer_result = fn(outer_ptr);
	const auto then_exit    = state.current_label;
	state.builder.AddFunction(spv::OpBranch, merge_label);

	EmitLabel(state, else_label);
	const auto inner_index = EmitBinaryU32(state, spv::OpISub, index, ConstantU32(state, 3u));
	const auto inner_ptr = state.builder.AllocateId();
	state.builder.AddFunction(spv::OpAccessChain, ptr_type, inner_ptr,
	                          state.tess_inner_variable, inner_index);
	const auto inner_result = fn(inner_ptr);
	const auto else_exit    = state.current_label;
	state.builder.AddFunction(spv::OpBranch, merge_label);

	EmitLabel(state, merge_label);
	const auto result = state.builder.AllocateId();
	state.builder.AddFunction(spv::OpPhi, TypeU32(state), result, outer_result, then_exit,
	                          inner_result, else_exit);
	return result;
}

} // namespace

void DefineTessellationInterfaces(EmitterState& state) {
	using Attribute = IR::TessellationAttribute;
	std::array<bool, 6> used {};
	uint32_t            patch_begin = UINT32_MAX, patch_end = 0;
	for (const auto* block: state.program.blocks) {
		for (const auto& inst: *block) {
			if (inst.GetOpcode() != IR::ValueOpcode::GetTessellationAttribute &&
			    inst.GetOpcode() != IR::ValueOpcode::SetTessellationAttribute)
				continue;
			const auto kind = inst.Arg(0).U32();
			used.at(kind)   = true;
			if (kind == static_cast<uint32_t>(Attribute::PatchOutput)) {
				EXIT_NOT_IMPLEMENTED(!inst.Arg(1).IsImmediate());
				patch_begin = std::min(patch_begin, inst.Arg(1).U32());
				patch_end   = std::max(patch_end, inst.Arg(1).U32() + 4u);
			}
		}
	}
	if (std::ranges::none_of(used, [](bool value) { return value; })) return;
	const auto& tess  = state.input_info.vertex->tess;
	const auto  array = [&](uint32_t type, uint32_t count) {
		return state.builder.Type(spv::OpTypeArray, type, ConstantU32(state, count));
	};
	for (uint32_t index = 0; index < used.size(); index++) {
		if (!used[index]) continue;
		const auto kind    = static_cast<Attribute>(index);
		const bool input   = kind == Attribute::ControlInput || kind == Attribute::EvaluationInput;
		const auto storage = input ? spv::StorageClassInput : spv::StorageClassOutput;
		uint32_t   type;
		if (kind == Attribute::Factor) {
			type = array(TypeF32(state), 4u);
		} else if (kind == Attribute::PatchOutput) {
			state.tess_patch_base = patch_begin;
			type = array(TypeU32Vector(state, 4), (patch_end - patch_begin + 15u) / 16u);
		} else {
			const bool local  = kind == Attribute::LocalOutput || kind == Attribute::ControlInput;
			const auto stride = local ? tess.ls_stride : tess.hs_stride;
			type              = array(TypeU32Vector(state, 4), (stride + 15u) / 16u);
			if (kind != Attribute::LocalOutput) {
				type = array(type, local ? tess.input_control_points : tess.output_control_points);
			}
		}
		auto& variable = state.tess_variables[index];
		variable       = DefineInterfaceVariable(state, type, storage, "tess_attributes");
		if (kind == Attribute::Factor) {
			state.builder.AddAnnotation(spv::OpDecorate, variable, spv::DecorationBuiltIn,
			                            spv::BuiltInTessLevelOuter);
			state.tess_inner_variable =
			    DefineInterfaceVariable(state, array(TypeF32(state), 2u), storage, "tess_inner");
			state.builder.AddAnnotation(spv::OpDecorate, state.tess_inner_variable,
			                            spv::DecorationBuiltIn, spv::BuiltInTessLevelInner);
			state.builder.AddAnnotation(spv::OpDecorate, state.tess_inner_variable,
			                            spv::DecorationPatch);
		} else {
			state.builder.AddAnnotation(
			    spv::OpDecorate, variable, spv::DecorationLocation,
			    kind == Attribute::PatchOutput ? (tess.hs_stride + 15u) / 16u : 0u);
		}
		if (kind == Attribute::Factor || kind == Attribute::PatchOutput) {
			state.builder.AddAnnotation(spv::OpDecorate, variable, spv::DecorationPatch);
		}
	}
}

void DefineTessellationExecutionModes(EmitterState& state) {
	const auto& tess = state.input_info.vertex->tess;
	EXIT_NOT_IMPLEMENTED(tess.domain != 1u || tess.partitioning != 2u ||
	                     tess.output_topology != 2u);
	state.builder.RequireCapability(spv::CapabilityTessellation);
	if (state.program.stage == ShaderType::TessellationControl) {
		state.builder.AddExecutionMode(state.main_func, spv::ExecutionModeOutputVertices,
		                               tess.output_control_points);
	} else {
		state.builder.AddExecutionMode(state.main_func, spv::ExecutionModeTriangles);
		state.builder.AddExecutionMode(state.main_func, spv::ExecutionModeSpacingFractionalOdd);
		state.builder.AddExecutionMode(state.main_func, spv::ExecutionModeVertexOrderCw);
	}
}

uint32_t EmitGetTessellationAttribute(ValueEmitContext& ctx, const IR::Inst& inst) {
	return EmitValueOrZeroIfCondition(ctx.state, ctx.Arg(inst, 2), [&] {
		const auto kind = static_cast<IR::TessellationAttribute>(inst.Arg(0).U32());
		if (kind == IR::TessellationAttribute::Factor && !inst.Arg(1).IsImmediate()) {
			return TessellationFactorAccess(ctx, inst, [&](uint32_t ptr) {
				const auto value = ctx.state.builder.AllocateId();
				ctx.state.builder.AddFunction(spv::OpLoad, TypeU32(ctx.state), value, ptr);
				return value;
			});
		}
		const auto value = ctx.state.builder.AllocateId();
		ctx.state.builder.AddFunction(spv::OpLoad, TypeU32(ctx.state), value,
		                              TessellationPointer(ctx, inst));
		return value;
	});
}

void EmitSetTessellationAttribute(ValueEmitContext& ctx, const IR::Inst& inst) {
	EmitIfCondition(ctx.state, ctx.Arg(inst, 3), [&] {
		auto value = ctx.Arg(inst, 2);
		const auto kind = static_cast<IR::TessellationAttribute>(inst.Arg(0).U32());
		if (kind == IR::TessellationAttribute::Factor) {
			const auto floating = ctx.state.builder.AllocateId();
			ctx.state.builder.AddFunction(spv::OpBitcast, TypeF32(ctx.state), floating, value);
			value = floating;
			if (!inst.Arg(1).IsImmediate()) {
				TessellationFactorAccess(ctx, inst, [&](uint32_t ptr) {
					ctx.state.builder.AddFunction(spv::OpStore, ptr, value);
					return 0u;
				});
				return;
			}
		}
		ctx.state.builder.AddFunction(spv::OpStore, TessellationPointer(ctx, inst), value);
	});
}

} // namespace Libs::Graphics::ShaderRecompiler::Spirv::Emitter
