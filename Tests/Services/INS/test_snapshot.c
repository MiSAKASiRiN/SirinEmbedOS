#include "ins_internal.h"
#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <stdatomic.h>
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static _Thread_local unsigned locked;
static _Atomic unsigned ready,done;
static uint64_t now=100000;
uint32_t test_get_mask(void){return locked;}
void test_disable_irq(void){assert(!locked);pthread_mutex_lock(&mutex);locked=1;}
void test_set_mask(uint32_t mask){assert(!mask&&locked);locked=0;pthread_mutex_unlock(&mutex);}
uint64_t DWT_GetUptimeUs(void){assert(locked);return now;}
static void *writer(void *p){
 (void)p;SRNOS_INSSnapshot s={0};s.status.phase=SRNOS_INS_RUNNING;s.status.updated_us=now;
 s.attitude.attitude_us=now;s.attitude.flags=SRNOS_INS_VALID;
 s.imu[0].header=(SRNOS_SampleHeader){0,19,now-50,now,SRNOS_SENSOR_READY};
 for(unsigned n=1;n<=50000;n++){
  s.attitude.sequence=n;s.attitude.epoch=n;s.imu[0].header.sequence=n;s.magnetic.header.sequence=n;
  for(unsigned i=0;i<4;i++)s.attitude.quaternion_wxyz[i]=(float)n;
  SRNOS_INSPublish(&s);atomic_store(&ready,1);
 }atomic_store(&done,1);return NULL;
}
static void *reader(void *p){
 (void)p;while(!atomic_load(&ready)){}
 unsigned n=0;
 do {
  SRNOS_INSSnapshot s;assert(SRNOS_INSGetSnapshot(&s));
  assert(s.attitude.sequence==s.attitude.epoch&&s.imu[0].header.sequence==s.attitude.sequence&&s.magnetic.header.sequence==s.attitude.sequence);
  for(unsigned i=0;i<4;i++)assert(s.attitude.quaternion_wxyz[i]==(float)s.attitude.sequence);
  SRNOS_INSAttitude a;SRNOS_INSGetAttitude(&a);for(unsigned i=0;i<4;i++)assert(a.quaternion_wxyz[i]==(float)a.sequence);
  SRNOS_INSStatus status;assert(SRNOS_INSGetStatus(&status)&&status.phase==SRNOS_INS_RUNNING);++n;
 }while(!atomic_load(&done)||n<10000);
 return NULL;
}
int main(void){
 assert(!SRNOS_INSGetSnapshot(NULL)&&!SRNOS_INSGetAttitude(NULL)&&!SRNOS_INSGetStatus(NULL));
 pthread_t w,r[4];pthread_create(&w,NULL,writer,NULL);for(unsigned i=0;i<4;i++)pthread_create(&r[i],NULL,reader,NULL);
 pthread_join(w,NULL);for(unsigned i=0;i<4;i++)pthread_join(r[i],NULL);
 now+=6000;SRNOS_INSSnapshot s;SRNOS_INSGetSnapshot(&s);
 assert(!(s.attitude.flags&SRNOS_INS_VALID)&&s.status.phase==SRNOS_INS_OFFLINE&&s.imu[0].header.valid_fields==0);
 assert(s.attitude.attitude_us==100000);puts("INS snapshot: one publisher/four consumers, coherent independent copies and stale invalidation passed");
}
