#ifndef KYTY_COMMON_DEBUG_BREADCRUMB_H_
#define KYTY_COMMON_DEBUG_BREADCRUMB_H_

namespace Common::DebugBreadcrumb {

// Records the host step the current thread is executing so a fatal exception report can name it.
// Static string literals only: the fault path must not allocate, format or take locks.
inline thread_local const char* g_last_step = "";

inline void        Set(const char* step) { g_last_step = step; }
inline const char* LastStep() { return g_last_step; }

} // namespace Common::DebugBreadcrumb

#endif /* KYTY_COMMON_DEBUG_BREADCRUMB_H_ */
