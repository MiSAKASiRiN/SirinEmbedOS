#include "boot_diag.h"
#include "stm32h7xx.h"
/* Boot lines are immutable once first replay starts. No allocation or UART. */
static uint8_t data[2048];
static size_t used, cursor;
static bool sealed;
void SRNOS_LogCaptureBoot(const void *src, size_t n)
{
    const uint32_t key=__get_PRIMASK(); __disable_irq();
    if (!sealed && src && n <= sizeof(data)-used) {
        const uint8_t *p=src;
        for (size_t i=0;i<n;i++) data[used++]=p[i];
    }
    __set_PRIMASK(key);
}
void SRNOS_BootDiagReplayReset(void) { sealed=true; cursor=0; }
bool SRNOS_BootDiagReplayPeek(const uint8_t **out, size_t *length)
{
    if (!out || !length || cursor>=used) return false;
    *out=&data[cursor]; *length=used-cursor;
    if (*length>384u) *length=384u;
    return true;
}
bool SRNOS_BootDiagReplayCommit(size_t length)
{
    if (length>used-cursor) return false;
    cursor+=length; return true;
}
