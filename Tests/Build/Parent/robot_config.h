#define SRNOS_APP_STACK_BYTES 8192U
#define SRNOS_HEAP_DTCM_BYTES (48U * 1024U)

/* Keep a non-default heap while reserving the new 32 KiB application pool
 * and boot-time lifecycle/cycle storage under the conservative budget. */
