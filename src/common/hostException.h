#ifndef KYTY_COMMON_HOST_EXCEPTION_H_
#define KYTY_COMMON_HOST_EXCEPTION_H_

#include "common/common.h"

namespace Common::HostException {

enum class ExceptionType { Unknown, AccessViolation, IllegalInstruction };

enum class AccessViolationType { Unknown, Read, Write, Execute };

struct ExceptionInfo {
	ExceptionType       type                   = ExceptionType::Unknown;
	AccessViolationType access_violation_type  = AccessViolationType::Unknown;
	uint64_t            access_violation_vaddr = 0;
	uint64_t            exception_address      = 0;
	uint64_t            rax                    = 0;
	uint64_t            rbx                    = 0;
	uint64_t            rcx                    = 0;
	uint64_t            rdx                    = 0;
	uint64_t            rsi                    = 0;
	uint64_t            rdi                    = 0;
	uint64_t            rbp                    = 0;
	uint64_t            rsp                    = 0;
	uint64_t            r8                     = 0;
	uint64_t            r9                     = 0;
	uint64_t            r10                    = 0;
	uint64_t            r11                    = 0;
	uint64_t            r12                    = 0;
	uint64_t            r13                    = 0;
	uint64_t            r14                    = 0;
	uint64_t            r15                    = 0;
	uint32_t            native_code            = 0;
	// Platform-specific mutable context, valid only for the duration of the handler call.
	void* native_context = nullptr;
	// Filled by the platform layer when the faulting instruction resolves to a loaded
	// module, so crash reports can name it instead of a raw address. Stays empty when
	// the faulting address does not belong to any module.
	char     faulting_module[260] {};
	uint64_t faulting_module_base = 0;
	// True on the last-chance dispatch (POSIX signal handler, Windows unhandled-exception
	// filter), where an unresolved fault must terminate with a fatal report. Windows
	// first-chance dispatches leave it false so host __try/__except handlers can recover
	// the fault before the emulator treats it as fatal.
	bool last_chance = false;
};

using Handler = bool (*)(const ExceptionInfo&);

bool InstallHandler(Handler handler);

#if KYTY_PLATFORM == KYTY_PLATFORM_LINUX
bool InitializeThreadSignalStack();
#endif

} // namespace Common::HostException

#endif /* KYTY_COMMON_HOST_EXCEPTION_H_ */
