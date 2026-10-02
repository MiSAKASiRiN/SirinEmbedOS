//
// Created by bismarckkk on 2024/2/17.
//

#ifndef SRNOS_REFEREE_UI_UI_INTERFACE_H
#define SRNOS_REFEREE_UI_UI_INTERFACE_H

#include <stdio.h>
#include "SirinEmbedOS/Services/Referee/UI/Types.h"

extern int SRNOS_RefereeUISelfId;

void SRNOS_RefereeUIPrint(const uint8_t* message, int length);

// User Code Begin
#define SRNOS_REFEREE_UI_SEND_MESSAGE(message, length) SRNOS_RefereeUISend(message, length)
int SRNOS_RefereeUISend(const uint8_t *message, int length);
// User Code End

void SRNOS_RefereeUIProc1Frame(SRNOS_RefereeUI1Frame *msg);
void SRNOS_RefereeUIProc2Frame(SRNOS_RefereeUI2Frame *msg);
void SRNOS_RefereeUIProc5Frame(SRNOS_RefereeUI5Frame *msg);
void SRNOS_RefereeUIProc7Frame(SRNOS_RefereeUI7Frame *msg);
void SRNOS_RefereeUIProcStringFrame(SRNOS_RefereeUIStringFrame *msg);

#endif //SRNOS_REFEREE_UI_UI_INTERFACE_H
