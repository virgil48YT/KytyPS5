#include "graphics/host_gpu/renderer/masterSemaphore.h"

#include "common/assert.h"
#include "common/logging/log.h"
#include "graphics/host_gpu/graphicContext.h"

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <vector>

namespace Libs::Graphics {

namespace {

// A host wait this long is worth naming in the log: it is long enough to notice.
constexpr int64_t SlowWaitMs = 1000;

} // namespace

MasterSemaphore::MasterSemaphore(GraphicContext& graphics): m_graphics(graphics) {
	vk::SemaphoreTypeCreateInfo type_info {};
	type_info.semaphoreType = vk::SemaphoreType::eTimeline;
	type_info.initialValue  = 0;

	vk::SemaphoreCreateInfo create_info {};
	create_info.pNext = &type_info;

	const auto result = m_graphics.device.createSemaphore(&create_info, nullptr, &m_semaphore);
	EXIT_NOT_IMPLEMENTED(result != vk::Result::eSuccess || m_semaphore == nullptr);
}

MasterSemaphore::~MasterSemaphore() {
	if (m_semaphore != nullptr) {
		m_graphics.device.destroySemaphore(m_semaphore, nullptr);
	}
}

void MasterSemaphore::Refresh() {
	uint64_t   counter = 0;
	const auto result  = m_graphics.device.getSemaphoreCounterValue(m_semaphore, &counter);
	if (result != vk::Result::eSuccess) {
		const auto known_tick = m_gpu_tick.load(std::memory_order_acquire);
		EXIT("MasterSemaphore::Refresh failed: %s (gpu tick %" PRIu64 ")\n",
		     vk::to_string(result).c_str(), known_tick);
	}

	// Only Submit signals this semaphore, with a tick NextTick handed out first, so the payload
	// stays below the next unallocated tick. A lost device need not say so: vkWaitSemaphores may
	// return VK_SUCCESS on device loss, and a removed device can make a Windows driver report
	// every timeline as fully signalled. Accepted, such a value would mark every tick free and
	// leave the loss to whichever later call checks the device, far from the work that faulted.
	if (const auto next_tick = CurrentTick(); counter >= next_tick) {
		const auto known_tick = m_gpu_tick.load(std::memory_order_acquire);
		EXIT("MasterSemaphore: device lost: timeline reads %" PRIu64 " but the next unallocated "
		     "tick is %" PRIu64 "; tick %" PRIu64 " was the last seen complete\n",
		     counter, next_tick, known_tick);
	}

	auto known = m_gpu_tick.load(std::memory_order_acquire);
	while (known < counter &&
	       !m_gpu_tick.compare_exchange_weak(known, counter, std::memory_order_release,
	                                         std::memory_order_relaxed)) {
	}
}

void MasterSemaphore::Wait(uint64_t tick) {
	if (IsFree(tick)) {
		return;
	}
	Refresh();
	if (IsFree(tick)) {
		return;
	}

	vk::SemaphoreWaitInfo wait_info {};
	wait_info.semaphoreCount = 1;
	wait_info.pSemaphores    = &m_semaphore;
	wait_info.pValues        = &tick;

	const auto begin     = std::chrono::steady_clock::now();
	const auto gpu_before = m_gpu_tick.load(std::memory_order_acquire);
	const auto result     = m_graphics.device.waitSemaphores(&wait_info, UINT64_MAX);
	const auto waited_ms =
	    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
	                                                          begin)
	        .count();
	// Name the result: with an infinite timeout this cannot be eTimeout, so a failure here is a
	// lost device or an allocation failure, and those need completely different investigations.
	if (result != vk::Result::eSuccess) {
		const auto known_tick = m_gpu_tick.load(std::memory_order_acquire);
		EXIT("MasterSemaphore::Wait failed: %s (tick %" PRIu64 ", gpu tick %" PRIu64
		     ", waited %" PRId64 " ms)\n",
		     vk::to_string(result).c_str(), tick, known_tick, static_cast<int64_t>(waited_ms));
	}
	// A success may still hide a lost device (see Refresh); naming long waits localizes where the
	// GPU was made to stop before Refresh decides.
	if (waited_ms >= SlowWaitMs) {
		LOGF("MasterSemaphore::Wait: tick %" PRIu64 " took %" PRId64 " ms (gpu tick %" PRIu64
		     " before the wait)\n",
		     tick, static_cast<int64_t>(waited_ms), gpu_before);
	}
	Refresh();
}

} // namespace Libs::Graphics
