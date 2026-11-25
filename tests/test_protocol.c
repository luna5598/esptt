#undef NDEBUG
#include <assert.h>
#include <string.h>
#include "limb_protocol.h"

int main(void)
{
    const uint8_t bind[] = {0x20,3,0,0x78,0x56,0x34,0x12,0xd2};
    const uint8_t release[] = {0x21,3,0,0x78,0x56,0x34,0x12,0x0d};
    const uint8_t cmd[] = {0x30,0x78,0x56,0x34,0x12,1,0,3,0,1,0xc8,0,0,0xbb};
    uint8_t ack[] = {0x78,0x56,0x34,0x12,1,0,3,0,2,0,0xa3};
    uint8_t frame[14];
    assert(limb_bind_frame(LIMB_BIND, 3, 0x12345678, frame) && !memcmp(bind,frame,sizeof bind));
    assert(limb_bind_frame(LIMB_RELEASE, 3, 0x12345678, frame) && !memcmp(release,frame,sizeof release));
    assert(limb_cmd_frame(0x12345678,1,3,1,200,frame) && !memcmp(cmd,frame,sizeof cmd));
    assert(!limb_cmd_frame(1,1,1,1,501,frame));
    assert(!limb_bind_frame(LIMB_BIND,0,0,frame));
    limb_ack a;
    assert(limb_decode_ack(ack,sizeof ack,&a) && a.status == 2 && a.session == 0x12345678);
    ack[2] ^= 1;
    assert(!limb_decode_ack(ack,sizeof ack,&a));
    uint8_t info[] = {0x4d,1,1,2,3,2,0,0xab,0xcd,0,7,0};
    info[11] = limb_crc(LIMB_INFO,info,11);
    limb_info i;
    assert(limb_decode_info(info,sizeof info,&i) && i.tip_gen == 0xcdab && i.capabilities == 2);
    info[2] = 2;
    info[11] = limb_crc(LIMB_INFO,info,11);
    assert(!limb_decode_info(info,sizeof info,&i));
    uint8_t sample[] = {3,0,1,0,1,2,0xf4,0xff,0x88,0x13,50,0,0,0,0,0};
    sample[15] = limb_crc(LIMB_SAMPLE,sample,15);
    limb_sample s;
    assert(limb_decode_sample(sample,16,&s) && s.a == -12 && s.b == 5000 && s.age_ms == 50);
    assert(!limb_decode_sample(sample,15,&s));
    sample[0] ^= 1;
    assert(!limb_decode_sample(sample,16,&s));
    return 0;
}
