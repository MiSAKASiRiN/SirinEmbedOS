#include "can_controller_internal.h"
#include "Router/can_router_core_internal.h"
#include "FD/can_fd_internal.h"
#include <string.h>

#define SRNOS_CAN_DEFAULT_CONFIG { SRNOS_CAN_MODE_CLASSIC, 1000000u, 0u, false }
static SRNOS_CANControllerConfig saved[SRNOS_CAN_CONTROLLER_COUNT] = {
    SRNOS_CAN_DEFAULT_CONFIG, SRNOS_CAN_DEFAULT_CONFIG,
    SRNOS_CAN_DEFAULT_CONFIG,
#if SRNOS_CAN_CONTROLLER_COUNT == 4
    SRNOS_CAN_DEFAULT_CONFIG,
#endif
};
static bool prepared;

static bool nominal_rate(uint32_t rate)
{
    return rate==125000u || rate==250000u || rate==500000u || rate==1000000u;
}
bool SRNOS_CANControllerConfigValid(uint8_t channel, const SRNOS_CANControllerConfig *c)
{
    if(channel<1u || channel>SRNOS_CAN_CONTROLLER_COUNT || !c || !nominal_rate(c->nominal_bitrate))return false;
#ifdef SRNOS_MP257_M33
    if(c->mode!=SRNOS_CAN_MODE_CLASSIC || c->nominal_bitrate!=1000000u)return false;
#endif
    if(c->mode==SRNOS_CAN_MODE_CLASSIC)return !c->brs && c->data_bitrate==0u;
    if(c->mode!=SRNOS_CAN_MODE_FD)return false;
    if(!c->brs)return c->data_bitrate==c->nominal_bitrate;
    return c->data_bitrate>=c->nominal_bitrate &&
        (c->data_bitrate==500000u || c->data_bitrate==1000000u ||
         c->data_bitrate==2000000u || c->data_bitrate==4000000u ||
         c->data_bitrate==5000000u || c->data_bitrate==8000000u);
}
bool SRNOS_CANControllerPrepare(const SRNOS_CANControllerConfig configs[SRNOS_CAN_CONTROLLER_COUNT])
{
    if(!configs || prepared || SRNOS_CAN_ConfigurationFrozen())return false;
    for(uint8_t ch=1u;ch<=SRNOS_CAN_CONTROLLER_COUNT;++ch) {
        const SRNOS_CANControllerConfig *c=&configs[ch-1u];
        if(!SRNOS_CANControllerConfigValid(ch,c) ||
           !SRNOS_CANRouterCoreConfigurationCompatible(ch,c) ||
           (c->mode==SRNOS_CAN_MODE_CLASSIC && SRNOS_CANFDCoreHasRegistrations(ch)))return false;
    }
    memcpy(saved,configs,sizeof(saved));
    prepared=true;
    return true;
}
const SRNOS_CANControllerConfig *SRNOS_CANControllerConfigOf(uint8_t channel)
{
    return channel>=1u && channel<=SRNOS_CAN_CONTROLLER_COUNT?&saved[channel-1u]:NULL;
}
bool SRNOS_CANControllerGetConfig(uint8_t channel, SRNOS_CANControllerConfig *out)
{
    const SRNOS_CANControllerConfig *c=SRNOS_CANControllerConfigOf(channel);
    if(!out || !c || !SRNOS_CAN_ConfigurationFrozen())return false;
    *out=*c;return true;
}
bool SRNOS_CANControllerIsClassic(uint8_t channel)
{
    const SRNOS_CANControllerConfig *c=SRNOS_CANControllerConfigOf(channel);
    return c && c->mode==SRNOS_CAN_MODE_CLASSIC;
}
bool SRNOS_CANControllerIsFD(uint8_t channel)
{
    const SRNOS_CANControllerConfig *c=SRNOS_CANControllerConfigOf(channel);
    return c && c->mode==SRNOS_CAN_MODE_FD;
}
bool SRNOS_CANControllerAdapterAllowed(uint8_t channel)
{
    const SRNOS_CANControllerConfig *c=SRNOS_CANControllerConfigOf(channel);
    return c && c->mode==SRNOS_CAN_MODE_CLASSIC && c->nominal_bitrate==1000000u;
}
uint8_t SRNOS_CANFDLengthToDlc(uint8_t length)
{
    if(length<=8u)return length;
    switch(length) {
    case 12:return 9;case 16:return 10;case 20:return 11;case 24:return 12;
    case 32:return 13;case 48:return 14;case 64:return 15;default:return 0xff;
    }
}
uint8_t SRNOS_CANFDDlcToLength(uint8_t dlc)
{
    static const uint8_t lengths[16]={0,1,2,3,4,5,6,7,8,12,16,20,24,32,48,64};
    return dlc<16u?lengths[dlc]:0xff;
}
