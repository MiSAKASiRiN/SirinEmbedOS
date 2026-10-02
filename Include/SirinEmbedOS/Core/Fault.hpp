#ifndef SRNOS_FAULT_HPP
#define SRNOS_FAULT_HPP

#include "SirinEmbedOS/Core/C/Fault.h"

namespace SRNOS::Fault {

using Reason = SRNOS_Fault_Reason;
using Context = SRNOS_Fault_Context;
using Record = SRNOS_Fault_Record;

/** @brief Enter the Panic pipeline; prefer SRNOS_PANIC when caller source is needed. */
[[noreturn]] inline void panic(Reason reason,
                               const char *message = nullptr) noexcept
{
    SRNOS_Fault_Panic(reason, message, __FILE__, __func__, __LINE__);
}

/** @brief Deliberately trigger an explicit Panic; SRNOS_TRIGGER_PANIC keeps caller source. */
[[noreturn]] inline void triggerPanic() noexcept
{
    SRNOS_Fault_TriggerPanic(nullptr, nullptr, 0u);
}

/** @brief Validate and copy the last reset-retained Panic record. */
[[nodiscard]] inline bool lastRecord(Record &record) noexcept
{
    return SRNOS_Fault_LastRecord(&record);
}

/** @brief Explicitly invalidate the last retained Panic record. */
inline void clearLastRecord() noexcept
{
    SRNOS_Fault_ClearLastRecord();
}

} // namespace SRNOS::Fault

#endif
