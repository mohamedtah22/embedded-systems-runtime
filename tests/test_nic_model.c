#include "nic_model.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
static nic_model nic;
static unsigned groups;
static void roundtrip(unsigned value) {
    unsigned out=0; size_t length=0;
    assert(nic_tx_submit(&nic,&value,sizeof(value))==0);
    assert(nic_device_step(&nic,1)==1);
    assert(nic_tx_reclaim(&nic,NULL)==1);
    assert(nic_rx_receive(&nic,&out,sizeof(out),&length)==0);
    assert(out==value && length==sizeof(value));
}
static void boundaries(void) {
    assert(nic_model_init(&nic,0)==-1);
    assert(nic_model_init(&nic,NIC_MAX_RING+1)==-1);
    assert(nic_model_init(&nic,3)==0);
    char data[NIC_MTU+1]={0}, out[NIC_MTU]; size_t len;
    assert(nic_tx_submit(&nic,data,0)==-1);
    assert(nic_tx_submit(&nic,data,sizeof(data))==-1);
    assert(nic_tx_submit(&nic,NULL,1)==-1);
    assert(nic_device_step(&nic,0)==0);
    assert(nic_rx_receive(&nic,out,sizeof(out),&len)==1);
    assert(nic_tx_submit(&nic,data,NIC_MTU)==0);
    assert(nic_device_step(&nic,100)==1);
    assert(nic_rx_receive(&nic,out,1,&len)==-1);
    assert(nic_rx_receive(&nic,out,sizeof(out),&len)==0 && len==NIC_MTU);
    assert(nic_tx_reclaim(&nic,NULL)==1);
    ++groups;
}
static void backpressure(void) {
    assert(nic_model_init(&nic,3)==0);
    for(unsigned i=0;i<3;i++) assert(nic_tx_submit(&nic,&i,sizeof(i))==0);
    unsigned value=99;
    assert(nic_tx_submit(&nic,&value,sizeof(value))==1);
    assert(nic_device_step(&nic,3)==3);
    assert(nic_tx_submit(&nic,&value,sizeof(value))==1); // Done != reclaimed.
    assert(nic_tx_reclaim(&nic,NULL)==3);
    assert(nic_tx_submit(&nic,&value,sizeof(value))==0);
    assert(nic_device_step(&nic,1)==1); // RX ring full; count drop, finish TX.
    assert(atomic_load(&nic.rx_no_buffer)==1);
    assert(nic_tx_reclaim(&nic,NULL)==1);
    for(unsigned i=0;i<3;i++){
        unsigned out; size_t len;
        assert(nic_rx_receive(&nic,&out,sizeof(out),&len)==0 && out==i);
    }
    roundtrip(123);
    ++groups;
}
static void wraparound(void) {
    assert(nic_model_init(&nic,3)==0);
    for(unsigned i=0;i<10000;i++) roundtrip(i);
    assert(atomic_load(&nic.tx_completed)==10000);
    assert(atomic_load(&nic.rx_completed)==10000);
    assert(atomic_load(&nic.rx_no_buffer)==0);
    ++groups;
}
static void faults_and_reset(void) {
    assert(nic_model_init(&nic,4)==0);
    unsigned value=5, errors=0; size_t len;
    nic_inject_tx_error(&nic);
    assert(nic_tx_submit(&nic,&value,sizeof(value))==0);
    assert(nic_device_step(&nic,1)==1);
    assert(nic_tx_reclaim(&nic,&errors)==1 && errors==1);
    assert(nic_rx_receive(&nic,&value,sizeof(value),&len)==1);
    assert(nic_take_interrupts(&nic)==(NIC_IRQ_TX|NIC_IRQ_ERROR));
    assert(nic_take_interrupts(&nic)==0);
    roundtrip(7);
    assert(nic_take_interrupts(&nic)==(NIC_IRQ_TX|NIC_IRQ_RX));
    assert(nic_model_init(&nic,4)==0); // Quiescent reset.
    assert(atomic_load(&nic.tx_errors)==0);
    roundtrip(8);
    ++groups;
}
static atomic_bool device_stop;
static void *device_worker(void *unused) {
    (void)unused;
    while(!atomic_load(&device_stop)) {
        if(nic_device_step(&nic,8)==0) sched_yield();
    }
    return NULL;
}
static void concurrent_ownership(void) {
    assert(nic_model_init(&nic,8)==0);
    atomic_init(&device_stop,0);
    pthread_t device;
    assert(pthread_create(&device,NULL,device_worker,NULL)==0);
    // One outstanding frame avoids intentional RX drops and verifies ordering.
    for(unsigned i=0;i<5000;i++) {
        unsigned out=0; size_t length=0;
        while(nic_tx_submit(&nic,&i,sizeof(i))==1){
            nic_tx_reclaim(&nic,NULL); sched_yield();
        }
        while(nic_rx_receive(&nic,&out,sizeof(out),&length)==1) sched_yield();
        assert(out==i && length==sizeof(i));
        nic_tx_reclaim(&nic,NULL);
    }
    atomic_store(&device_stop,1);
    pthread_join(device,NULL);
    nic_tx_reclaim(&nic,NULL);
    assert(atomic_load(&nic.tx_completed)==5000);
    assert(atomic_load(&nic.rx_no_buffer)==0);
    ++groups;
}
int main(void) {
    boundaries(); backpressure(); wraparound(); faults_and_reset(); concurrent_ownership();
    printf("test_nic_model: PASS (%u groups; 10000 wraparound transfers + 5000 concurrent frames)\n",groups);
    return 0;
}
