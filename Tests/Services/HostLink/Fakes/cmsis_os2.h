#ifndef TEST_CMSIS_OS2_H
#define TEST_CMSIS_OS2_H

#include <stdint.h>

typedef struct test_message_queue *osMessageQueueId_t;
typedef int32_t osStatus_t;

#define osOK             ((osStatus_t)0)
#define osErrorResource  ((osStatus_t)-3)
#define osWaitForever    UINT32_MAX

osMessageQueueId_t osMessageQueueNew(uint32_t msg_count,
                                     uint32_t msg_size,
                                     const void *attr);
osStatus_t osMessageQueuePut(osMessageQueueId_t queue,
                             const void *msg,
                             uint8_t priority,
                             uint32_t timeout);
osStatus_t osMessageQueueGet(osMessageQueueId_t queue,
                             void *msg,
                             uint8_t *priority,
                             uint32_t timeout);
uint32_t osMessageQueueGetCount(osMessageQueueId_t queue);

#endif /* TEST_CMSIS_OS2_H */
