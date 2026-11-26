#undef NDEBUG
#include <assert.h>
#include <string.h>
#include "limb_reader.h"

typedef struct {
    uint8_t flags, type;
    uint16_t gen, seq;
    uint32_t session;
    unsigned writes;
    bool drop_ack, corrupt;
} node;

static bool write_reg(void *ctx,const uint8_t *data,size_t n)
{
    node *s=ctx;
    assert(n==8 && data[0]==LIMB_BIND && limb_u16(data+1)==s->gen &&
           data[7]==limb_crc(LIMB_BIND,data+1,6));
    s->session=limb_u16(data+3) | (uint32_t)limb_u16(data+5)<<16;
    s->flags |= LIMB_BOUND;
    s->writes++;
    return true;
}

static bool read_reg(void *ctx,uint8_t reg,uint8_t *p,size_t n)
{
    node *s=ctx;
    memset(p,0,n);
    switch(reg){
    case LIMB_INFO:
        assert(n==LIMB_INFO_BYTES);
        p[0]=0x4d; p[1]=1; p[2]=1; p[3]=s->type; p[4]=s->flags; p[5]=2;
        limb_put16(p+7,s->gen);
        break;
    case LIMB_ACK:
        assert(n==LIMB_ACK_BYTES);
        if(s->drop_ack){s->drop_ack=false;return false;}
        limb_put32(p,s->session);limb_put16(p+6,s->gen);
        break;
    case LIMB_SAMPLE:
        assert(n==LIMB_SAMPLE_BYTES);
        limb_put16(p,s->gen);limb_put16(p+2,s->seq);
        p[4]=1; p[5]=2; limb_put16(p+6,2150);limb_put16(p+8,5000);
        break;
    default: return false;
    }
    p[n-1]=limb_crc(reg,p,n-1);
    if(s->corrupt && reg==LIMB_SAMPLE)p[n-1]^=1;
    return true;
}

int main(void)
{
    node n={.type=LIMB_TEMP_HUM,.flags=LIMB_PRESENT|LIMB_READY,.gen=3,.seq=65535,.drop_ack=true};
    limb_transport io={.ctx=&n,.read=read_reg,.write=write_reg};
    limb_reader r={0};
    assert(limb_poll(&r,&io,100).result==LIMB_LINK_FAULT);
    assert(limb_poll(&r,&io,101).result==LIMB_NEW_SAMPLE && n.writes==1);
    assert(limb_poll(&r,&io,102).result==LIMB_UNCHANGED);
    n.seq=0;
    assert(limb_poll(&r,&io,103).result==LIMB_NEW_SAMPLE);
    n.seq=65535;
    assert(limb_poll(&r,&io,104).result==LIMB_UNCHANGED);
    n.corrupt=true;
    assert(limb_poll(&r,&io,105).result==LIMB_BAD_FRAME);
    n.corrupt=false;
    n.flags=LIMB_PRESENT|LIMB_READY;
    limb_poll_result reset=limb_poll(&r,&io,106);
    assert(reset.replaced && reset.result==LIMB_NEW_SAMPLE && n.session==106);
    n.flags=LIMB_PRESENT|LIMB_READY;n.gen++;
    reset=limb_poll(&r,&io,107);
    assert(reset.replaced && reset.result==LIMB_NEW_SAMPLE);
    n.session=999;
    assert(limb_poll(&r,&io,108).result==LIMB_FOREIGN_BINDING);
    n.flags=0;
    assert(limb_poll(&r,&io,109).result==LIMB_EMPTY_SLOT && !r.session);
    n.flags = LIMB_PRESENT;
    assert(limb_poll(&r,&io,110).result == LIMB_PENDING);
    n.type = 255;
    assert(limb_poll(&r,&io,111).result == LIMB_UNSUPPORTED);
    return 0;
}
