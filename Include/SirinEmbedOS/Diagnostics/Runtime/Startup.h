#ifndef SRNOS_DIAGNOSTICS_STARTUP_H
#define SRNOS_DIAGNOSTICS_STARTUP_H
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/** Once before kernel initialization, after clocks. Sampling worker is optional;
 * Fault task metadata and the monotonic runtime counter remain active. */
bool SRNOS_DiagnosticsPrepare(bool enable_sampling);
/** Once after kernel initialization and before scheduler startup. */
bool SRNOS_DiagnosticsStart(void);
#ifdef __cplusplus
}
#endif
#endif
