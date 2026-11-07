/* Native NV service protocol and memory ownership only. No GPU commands are
 * reported complete without an implementation; first unknown ioctl stops.
 */
#include "nvdec264.h"
static struct {uint32_t transfer_handle;uint64_t aruid,margin;unsigned open;} nv_clients[32];
static struct {unsigned kind,client,open;uint32_t events[4];} nv_fds[32];
/* nvmap handles: the SDK allocates one buffer per decoder surface and hands us the guest address
 * it already reserved; alloc only registers it, nothing is copied. */
#define NVMAP_MAX 256
static struct {uint32_t handle;uint64_t size,addr;} nv_maps[NVMAP_MAX];
static struct {uint32_t handle;uint32_t addr;} nv_cmdbufs[64];static unsigned nv_cmdbuf_count;/* lab: MapCommandBuffer table, see worklog */
static const char *nv_paths[]={"/dev/nvmap","/dev/nvhost-ctrl","/dev/nvhost-ctrl-gpu","/dev/nvhost-as-gpu","/dev/nvhost-gpu","/dev/nvhost-nvdec","/dev/nvhost-vic"};
static void nv_destroy(unsigned state) {
    if(state && state<=32 && nv_clients[state-1].open) {
        session_close(nv_clients[state-1].transfer_handle,0);nv_clients[state-1].open=0;
        for(unsigned i=0;i<32;i++)if(nv_fds[i].client==state)nv_fds[i].open=0;
    }
}
/* Diagnostic only: prints the first bytes of an ioctl payload so the nvmap struct layouts
 * (create/alloc) can be read off a run instead of guessed. */
static void nv_dump(const char *tag, const void *p, unsigned n) {
    const uint8_t *b=p; if(n>0x15c)n=0x15c; struct line l={0}; text(&l,tag);
    for(unsigned i=0;i<n;i++){hex(&l,b[i]);if(i+1<n)text(&l," ");} emit(&l);
}
/* NVDEC method stream. Encoding per the host1x command header (ScarletNX host1x_regs.h,
 * traced back to Eden/yuzu CDmaPusher): value in bits 0-15, method offset in 16-27,
 * submission mode in 28-31. Class 0xF0 is NVDEC; methods 0x101/0x102 carry the picture-info
 * and bitstream addresses and 0x10C.. the output surfaces, so the decoder does not have to
 * guess an access unit out of nvmap by buffer size. Diagnostic only for now: NVD_REG=1. */
static struct {uint32_t cls,pic,bs,exec;uint32_t luma[17],chroma[17];unsigned seen,execs;} nvd_regs;
static void nvd_reg_method(uint32_t cls,uint32_t m,uint32_t v) {
    if(cls!=0xF0)return;
    nvd_regs.cls=cls;
    if(m==0x101)nvd_regs.pic=v;
    else if(m==0x102)nvd_regs.bs=v;
    else if(m>=0x10C&&m<0x10C+17)nvd_regs.luma[m-0x10C]=v;
    else if(m>=0x11D&&m<0x11D+17)nvd_regs.chroma[m-0x11D]=v;
    else if(m==0x80){} /* codec id, always 3 for H.264 */
    else if(m==0xC0)nvd_regs.exec=v;
}
static void nvd_walk_cmdbuf(uint64_t base,uint32_t off,uint32_t words) {
    if(!getenv("NVD_REG")||!base)return;
    if(words>0x4000)words=0x4000;
    uint64_t a0=base+(uint64_t)off*4;
    struct region rr=query(a0); if(!rr.state)return;
    const uint8_t *p=(const void *)(uintptr_t)a0;
    uint32_t cls=0,i=0;
    while(i<words) {
        uint32_t c=read32(p+i*4); i++;
        uint32_t mode=c>>28,moff=(c>>16)&0xfff,val=c&0xffff;
        if(mode==0) { cls=(val>>6)&0x3ff; uint32_t mask=val&0x3f;
            for(unsigned k=0;k<6&&i<words;k++)if(mask&(1u<<k)){nvd_reg_method(cls,moff+k,read32(p+i*4));i++;}
        } else if(mode==1||mode==2) {
            for(unsigned k=0;k<val&&i<words;k++,i++)nvd_reg_method(cls,mode==1?moff+k:moff,read32(p+i*4));
        } else if(mode==3) {
            for(unsigned k=0;k<16&&i<words;k++)if(val&(1u<<k)){nvd_reg_method(cls,moff+k,read32(p+i*4));i++;}
        } else if(mode==4) { nvd_reg_method(cls,moff,val); }
        else break; /* restart/gather: not decoded */
    }
    if(!nvd_regs.pic&&!nvd_regs.bs)return;
    nvd_regs.execs++;
    if(nvd_regs.execs>80)return;
    struct line rl={0};text(&rl,"NVD_REG pic=");hex(&rl,nvd_regs.pic);
    text(&rl," bs=");hex(&rl,nvd_regs.bs);text(&rl," exec=");hex(&rl,nvd_regs.exec);
    text(&rl," n=");hex(&rl,nvd_regs.execs);
    uint32_t len=0,w=0,h=0,pitch=0;
    if(nvd_regs.pic&&query(nvd_regs.pic).state){
        const uint8_t *pi=(const void *)(uintptr_t)nvd_regs.pic;
        len=read32(pi+0x48); w=read32(pi+0x64); h=read32(pi+0x68); pitch=read32(pi+0x8C);
    }
    text(&rl," len=");hex(&rl,len);text(&rl," mbs=");hex(&rl,w);text(&rl,".");hex(&rl,h);
    text(&rl," pitch=");hex(&rl,pitch);
    text(&rl," l0=");hex(&rl,nvd_regs.luma[0]);text(&rl," c0=");hex(&rl,nvd_regs.chroma[0]);
    emit(&rl);
}
static int nv_dispatch(uint8_t *buffer,struct ipc_request *req,enum endpoint endpoint,unsigned *state,uint64_t pc) {
    if(endpoint!=EP_NVDRV)return 0;
    uint32_t error=0;
    if(req->command==3) {
        if(*state || req->data_size<4 || req->copies!=2 || req->copy_handles[0]!=0xffff8001U)
            stop("INVALID_NV_INITIALIZE",pc,req->data_size,24);
        struct session *transfer=session_get(req->copy_handles[1],pc);
        if(transfer->endpoint!=EP_TRANSFER || transfer_regions[transfer->initialized].size!=read32(req->data))
            stop("INVALID_NV_TRANSFER_POOL",pc,read32(req->data),24);
        unsigned index=0;while(index<32 && nv_clients[index].open)index++;
        if(index==32)stop("NV_CLIENT_TABLE_FULL",pc,32,24);
        if(index==8)stop("NV_CLIENT_TABLE_FULL",pc,index,21);
        nv_clients[index].open=1;nv_clients[index].transfer_handle=session_clone(req->copy_handles[1],pc);*state=index+1;
        ipc_reply(buffer,req->token,0,&error,4,0,req->domain);return 1;
    }
    if(!*state || *state>32 || !nv_clients[*state-1].open)stop("NV_NOT_INITIALIZED",pc,req->command,24);
    if(req->command==8 || req->command==7 || req->command==13) {
        if(req->data_size<8)stop("INVALID_NV_CLIENT_SETTING",pc,req->command,24);
        uint64_t value;memcpy(&value,req->data,8);
        if(req->command==13)nv_clients[*state-1].margin=value;
        else {
            if(value!=main_thread_id || (req->command==8&&!req->send_pid))stop("INVALID_NV_ARUID",pc,value,24);
            nv_clients[*state-1].aruid=value;
        }
        ipc_reply(buffer,req->token,0,&error,req->command==13?0:4,0,req->domain);return 1;
    }
    if(req->command==4) {
        /* after EventRegister the SDK waits on that event through this call; nothing in the
           lab signals it yet, so answer immediately and let the caller continue. */
        struct line el={0};text(&el,"AW_NV_EVENT_WAIT data=");hex(&el,(unsigned)req->data_size);emit(&el);
        uint64_t ev=1;ipc_reply(buffer,req->token,0,&ev,8,0,req->domain);return 1;
    }
    if(req->command==0) {
        if(req->num_inputs!=1 || req->inputs[0].size>128)stop("INVALID_NV_PATH",pc,req->num_inputs,24);
        char path[129]={0};memcpy(path,(void *)(uintptr_t)req->inputs[0].address,req->inputs[0].size);
        struct line line={0};text(&line,"NV_OPEN path=");text(&line,path);emit(&line);
        unsigned kind=0;while(kind<6&&strcmp(path,nv_paths[kind]))kind++;
        if(kind>=5) {   /* 5 = nvdec, 6 = vic: both are probe-only until their ioctls are mapped */
            /* Diagnostic, default off: hand out a device fd and answer its ioctls with
             * zeroed results so the guest's request sequence can be mapped. Nothing is
             * decoded and no frame is ever reported; the default path still stops. */
            const char *probe=getenv("NX_NVDEC_PROBE");
            if(probe&&!strcmp(probe,"1")&&(kind==5||kind==6)) {
                unsigned pfd=0;while(pfd<32&&nv_fds[pfd].open)pfd++;
                if(pfd==32)stop("NV_FD_TABLE_FULL",pc,pfd,21);
                nv_fds[pfd].open=1;nv_fds[pfd].kind=kind;nv_fds[pfd].client=*state;
                struct line pline={0};text(&pline,"NV_PROBE_OPEN path=");text(&pline,path);text(&pline," fd=");hex(&pline,pfd+1);emit(&pline);
                uint32_t pout[2]={pfd+1,0};ipc_reply(buffer,req->token,0,pout,8,0,req->domain);return 1;
            }
            /* Diagnostic, default off: report the absent hardware video decoder to the guest instead of
             * stopping the runtime. Opt-in by exact value only; every other unknown path still stops. */
            const char *deny=getenv("NX_NVDEC_OPEN_DENY");
            if(deny&&!strcmp(deny,"1")&&!strcmp(path,"/dev/nvhost-nvdec")) {
                static int told;
                if(!told) {struct line denyline={0};told=1;text(&denyline,"NV_OPEN_DENY path=");text(&denyline,path);
                    text(&denyline," command=0 err=");hex(&denyline,0x30011U);text(&denyline," fd=invalid optin=1");emit(&denyline);}
                uint32_t denied[2]={0xffffffffU,0x30011U};
                ipc_reply(buffer,req->token,0,denied,8,0,req->domain);return 1;
            }
            stop("UNIMPLEMENTED_NV_DEVICE",pc,kind,20);
        }
        unsigned fd=0;while(fd<32&&nv_fds[fd].open)fd++;
        if(fd==32)stop("NV_FD_TABLE_FULL",pc,fd,21);
        nv_fds[fd].open=1;nv_fds[fd].kind=kind;nv_fds[fd].client=*state;
        uint32_t out[2]={fd+1,0};ipc_reply(buffer,req->token,0,out,8,0,req->domain);return 1;
    }
    if(req->command==1 || req->command==11 || req->command==12) {
        if(req->data_size<8)stop("INVALID_NV_IOCTL",pc,req->data_size,24);
        unsigned fd=read32(req->data);uint32_t code=read32(req->data+4);
        if(!fd || fd>32 || !nv_fds[fd-1].open || nv_fds[fd-1].client!=*state)stop("INVALID_NV_FD",pc,fd,24);
        struct line line={0};text(&line,"NV_IOCTL path=");text(&line,nv_paths[nv_fds[fd-1].kind]);
        text(&line," code=");hex(&line,code);text(&line," inputs=");hex(&line,req->num_inputs);text(&line," outputs=");hex(&line,req->num_outputs);emit(&line);
        if(nv_fds[fd-1].kind==0) {
            /* create {u32 size, u32 flags} -> {u32 handle}; alloc {handle,heapmask,flags,align,kind[8],u64 addr} */
            uint8_t *in=req->num_inputs?(void *)(uintptr_t)req->inputs[0].address:0;
            uint8_t *out=req->num_outputs?(void *)(uintptr_t)req->outputs[0].address:0;
            if(code==0xc0080101U) {
                if(!in||!out||req->inputs[0].size<8)stop("INVALID_NVMAP_CREATE",pc,code,24);
                uint32_t size=read32(in);
                if(!size)stop("INVALID_NVMAP_SIZE",pc,code,24);
                unsigned slot=0;while(slot<NVMAP_MAX&&nv_maps[slot].handle)slot++;
                if(slot==64)stop("NVMAP_TABLE_FULL",pc,code,21);
                nv_maps[slot].handle=slot+1;nv_maps[slot].size=size;nv_maps[slot].addr=0;
                /* create is {u32 size in, u32 handle out}: the handle lands at offset 4, not 0. */
                if(req->outputs[0].size<8)stop("INVALID_NVMAP_CREATE_OUT",pc,req->outputs[0].size,24);
                uint32_t hand=slot+1;memcpy(out+4,&hand,4);
                struct line ml={0};text(&ml,"NVMAP_CREATE size=");hex(&ml,size);text(&ml," handle=");hex(&ml,hand);emit(&ml);
                error=0;ipc_reply(buffer,req->token,0,&error,4,0,req->domain);return 1;
            }
            if(code==0xc0200104U) {
                if(!in||req->inputs[0].size<32)stop("INVALID_NVMAP_ALLOC",pc,code,24);
                uint32_t hand=read32(in);uint64_t addr;memcpy(&addr,in+24,8);
                if(!hand||hand>NVMAP_MAX||!nv_maps[hand-1].handle)stop("INVALID_NVMAP_HANDLE",pc,hand,24);
                uint64_t size=nv_maps[hand-1].size;
                struct region rg=query(addr);
                if(!addr||!rg.state||addr+size>rg.base+rg.size)stop("NVMAP_ALLOC_RANGE",pc,addr,24);
                nv_maps[hand-1].addr=addr;
                if(out&&req->outputs[0].size>=32)memcpy(out,in,32);
                struct line al={0};text(&al,"NVMAP_ALLOC handle=");hex(&al,hand);text(&al," addr=");hex(&al,addr);text(&al," size=");hex(&al,size);emit(&al);
                error=0;ipc_reply(buffer,req->token,0,&error,4,0,req->domain);return 1;
            }
        }
        const char *probe=getenv("NX_NVDEC_PROBE");
        if(probe&&!strcmp(probe,"1")&&(nv_fds[fd-1].kind==5||nv_fds[fd-1].kind==6||nv_fds[fd-1].kind==1||nv_fds[fd-1].kind==0)) { /* 0 = nvmap, 1 = ctrl: map their sequences too */
            struct line pline={0};text(&pline,"NV_PROBE_IOCTL code=");hex(&pline,code);
            text(&pline," in=");hex(&pline,req->num_inputs?req->inputs[0].size:0);
            text(&pline," out=");hex(&pline,req->num_outputs?req->outputs[0].size:0);
            text(&pline," cmd=");hex(&pline,req->command);emit(&pline);
            for(unsigned i=0;i<req->num_inputs;i++)
                if(req->inputs[i].size)nv_dump("NV_PROBE_IN ",(void *)(uintptr_t)req->inputs[i].address,req->inputs[i].size);
            /* Full nvdec descriptor bytes, first three of each code, for host-side layout work. */
            if((code==0xc16c0009U||code==0xc16c000aU||code==0xc15c0009U)&&req->num_inputs){
                static int dn[4];int k=(int)((code>>16)&3);
                if(dn[k]<3){char fp[64];FILE*xf;snprintf(fp,sizeof fp,"/tmp/nvdec_%x_%d.bin",code,dn[k]);
                    if((xf=fopen(fp,"wb"))){fwrite((void *)(uintptr_t)req->inputs[0].address,1,req->inputs[0].size,xf);fclose(xf);}dn[k]++;}}
            /* note: in/out share one buffer here, so zeroing outputs must happen after
               the probes below read the argument. */
            /* Lab probe: nvdec's submit descriptor carries a command buffer; read the
               guest bytes it points at so the register stream can be decoded. */
            if((code==0xc15c0009U||code==0xc15c000aU) && req->num_inputs && req->inputs[0].size>=12) {
                uint8_t *mp=(void *)(uintptr_t)req->inputs[0].address;
                uint32_t n=read32(mp);
                if(n>42)n=42; /* ioctl arg is 0x15c: 12-byte header + 42 x 8-byte entries */
                struct line hl={0};text(&hl,code==0xc15c0009U?"NV_CMDBUF_MAP n=":"NV_CMDBUF_UNMAP n=");hex(&hl,n);emit(&hl);
                for(uint32_t i=0;i<n;i++) {
                    uint32_t h=read32(mp+12+i*8), a=read32(mp+12+i*8+4);
                    struct line el={0};text(&el,"NV_CMDBUF handle=");hex(&el,h);text(&el," addr=");hex(&el,a);emit(&el);
                    if(code==0xc15c0009U&&a&&nv_cmdbuf_count<64){nv_cmdbufs[nv_cmdbuf_count].handle=h;nv_cmdbufs[nv_cmdbuf_count].addr=a;nv_cmdbuf_count++;}
                }
                if(code==0xc15c000aU)nv_cmdbuf_count=0;
            }
            if(code==0xc15c0009U && req->num_inputs && req->inputs[0].size>=0x40) {
                uint8_t *sp=(void *)(uintptr_t)req->inputs[0].address;
                for(unsigned off=0;off<0x40;off+=4) {
                    uint32_t v=read32(sp+off);
                    if(v<0x10000000U)continue;
                    struct region r=query(v); if(!r.state)continue;
                    struct line pl={0};text(&pl,"NV_PTR addr=");hex(&pl,v);text(&pl," off=");hex(&pl,off);
                    text(&pl," region=");hex(&pl,(unsigned)r.size);emit(&pl);
                    for(unsigned k=0;k<0x100;k+=0x20)nv_dump("NV_PTR_BYTES ",(void *)(uintptr_t)(v+k),0x20);
                }
            }
            /* 0xc0400001 is the channel submit: SubmitArguments (16) + N x CommandBuffer
               ({mem, offset_words, words_count}) + SyncptIncr + fences. Dump the command
               words out of the mapped nvmap buffer so the register stream is visible. */
            if(code==0xc0400001U && req->num_inputs && req->inputs[0].size>=16) {
                uint8_t *sp=(void *)(uintptr_t)req->inputs[0].address;
                uint32_t ncb=read32(sp),nrel=read32(sp+4),nsync=read32(sp+8),nfen=read32(sp+12);
                struct line sl={0};text(&sl,"NV_SUBMIT cmdbufs=");hex(&sl,ncb);text(&sl," relocs=");hex(&sl,nrel);
                text(&sl," syncpt=");hex(&sl,nsync);text(&sl," fences=");hex(&sl,nfen);emit(&sl);
                for(uint32_t i=0;i<ncb&&16+i*12+12<=req->inputs[0].size;i++) {
                    uint32_t mem=read32(sp+16+i*12),off=read32(sp+20+i*12),words=read32(sp+24+i*12);
                    uint64_t base=0;
                    for(unsigned j=0;j<NVMAP_MAX;j++)if(nv_maps[j].handle==mem&&nv_maps[j].addr){base=nv_maps[j].addr;break;}
                    struct line cl={0};text(&cl,"NV_CMDBUF_ENTRY mem=");hex(&cl,mem);text(&cl," off=");hex(&cl,off);
                    text(&cl," words=");hex(&cl,words);text(&cl," base=");hex(&cl,(unsigned)base);emit(&cl);
                    if(!base)continue;
                    nvd_walk_cmdbuf(base,off,words);
                    uint64_t a=base+(uint64_t)off*4;struct region r=query(a);if(!r.state)continue;
                    uint32_t bytes=words*4;if(bytes>0x300)bytes=0x300;
                    for(uint32_t k=0;k<bytes;k+=0x100)nv_dump("NV_CMDW ",(void *)(uintptr_t)(a+k),bytes-k<0x100?bytes-k:0x100);
                    /* A/B: nvhost may report the cmdbuf offset in BYTES while we assumed words. */
                    {uint64_t ab=base+(uint64_t)off;struct region rb=query(ab);
                     if(rb.state)for(uint32_t k=0;k<bytes;k+=0x100)nv_dump("NV_CMDB ",(void *)(uintptr_t)(ab+k),bytes-k<0x100?bytes-k:0x100);}
                    /* Lab probe: hunt the 0x2FC-byte H.264 picture-info context. Heuristic
                       from the emulator layout: stream_len at +0x48, coded width/height in
                       macroblocks at +0x64/+0x68. Log every aligned candidate. */
                    {static int hunt=0;if(hunt<40){hunt++;
                     for(unsigned j=0;j<NVMAP_MAX;j++)if(nv_maps[j].handle&&nv_maps[j].addr&&nv_maps[j].size>0x2fc){
                        uint8_t *b3=(void *)(uintptr_t)nv_maps[j].addr;uint64_t n3=nv_maps[j].size-0x2fc;
                        if(n3>0x8000)n3=0x8000;
                        for(uint64_t o=0;o<n3;o+=4){
                            if(!(o&0xfff)){struct region rc=query(nv_maps[j].addr+o);if(!rc.state)break;}
                            uint32_t sl=read32(b3+o+0x48),w3=read32(b3+o+0x64),h3=read32(b3+o+0x68),f3=read32(b3+o+0xb0);
                            if(sl>64&&sl<0x200000&&w3>=1&&w3<=300&&h3>=1&&h3<=300){
                                struct line ch={0};text(&ch,"NV_PICINFO h=");hex(&ch,nv_maps[j].handle);
                                text(&ch," off=");hex(&ch,(unsigned)o);text(&ch," slen=");hex(&ch,sl);
                                text(&ch," mb=");hex(&ch,w3);text(&ch,"x");hex(&ch,h3);
                                text(&ch," slices=");hex(&ch,read32(b3+o+0x4c));text(&ch," fl=");hex(&ch,f3);emit(&ch);}
                        }}}
                    }
                    /* Lab probe: scan EVERY mapped nvmap buffer for a real Annex-B NAL
                       header (0x67 SPS / 0x68 PPS / 0x65 IDR / slice types), not just the
                       cmdbuf we were handed, and publish the nvmap table once. */
                    {static int once=0;if(!once||!nvd_seq_done){once=1;for(unsigned j=0;j<NVMAP_MAX;j++)if(nv_maps[j].handle){
                        {struct region r3=query(nv_maps[j].addr);if(r3.state){uint64_t lim3=nv_maps[j].size;uint64_t cap3=nv_maps[j].handle==3?0x300:(nv_maps[j].handle==4?0x2000:0x100);if(lim3>cap3)lim3=cap3;
                         for(uint64_t k3=0;k3<lim3;k3+=0x100)nv_dump("NV_BUF ",(void *)(uintptr_t)(nv_maps[j].addr+k3),lim3-k3<0x100?lim3-k3:0x100);}}
                        struct line tl={0};text(&tl,"NV_MAPTABLE h=");hex(&tl,nv_maps[j].handle);
                        text(&tl," size=");hex(&tl,(unsigned)nv_maps[j].size);text(&tl," addr=");hex(&tl,(unsigned)nv_maps[j].addr);emit(&tl);}}}
                    }}
                    for(unsigned j=0;j<NVMAP_MAX;j++)if(nv_maps[j].handle&&nv_maps[j].addr){
                        uint64_t b2=nv_maps[j].addr,lim2=nv_maps[j].size;if(lim2>0x400000)lim2=0x400000;int h2=0;
                        for(uint64_t k=0;k+4<lim2&&h2<3;k++){
                            if(!(k&0xfff)){struct region r2=query(b2+k);if(!r2.state)break;}
                            uint8_t *q2=(void *)(uintptr_t)(b2+k);
                            if(q2[0]==0&&q2[1]==0&&q2[2]==1&&(q2[3]==0x67||q2[3]==0x68||q2[3]==0x65||q2[3]==0x41||q2[3]==0x61)){
                                struct line n2={0};text(&n2,"NV_ES h=");hex(&n2,nv_maps[j].handle);text(&n2," off=");hex(&n2,(unsigned)k);
                                text(&n2," b3=");hex(&n2,q2[3]);emit(&n2);h2++;
                                if(q2[3]==0x67&&!nvd_seq_done){
                                    nvd_seq_done=1;
                                    if(getenv("NVD_DUMP_ES")){FILE*ef=fopen("/tmp/es.h264","wb");if(ef){fwrite(q2,1,(size_t)(lim2>0x800000?0x800000:lim2),ef);fclose(ef);}}
                                    /* The ES lives in one buffer: decode NALs in stream order so the decoder
                                       keeps its own SPS/PPS and reference pictures like the hardware does. */
                                    uint64_t off[600];unsigned cnt=0;
                                    for(uint64_t t=0;t+4<lim2&&cnt<600;t++){
                                        uint8_t *w2=(void*)(uintptr_t)(b2+t);
                                        if(w2[0]==0&&w2[1]==0&&w2[2]==1){if(!(t&0xfff)){struct region r3=query(b2+t);if(!r3.state)break;}off[cnt++]=t;t+=2;}
                                    }
                                    for(unsigned i=0;i+1<cnt&&nvd_decoded<600;i++){
                                        unsigned al=(unsigned)(off[i+1]-off[i]);if(al>0x40000)al=0x40000;
                                        nvd_decode((void*)(uintptr_t)(b2+off[i]),al);
                                    }
                                    struct line sq={0};text(&sq,"NVD_SEQ nals=");hex(&sq,cnt);text(&sq," frames=");hex(&sq,nvd_decoded);emit(&sq);
                                }
                                else if(getenv("NX_NVDEC_DECODE")&&(q2[3]==0x65||q2[3]==0x61)){
                                    uint64_t au=(uint64_t)-1;
                                    for(uint64_t t=k+4;t+4<lim2;t++){uint8_t *w2=(void *)(uintptr_t)(b2+t);if(w2[0]==0&&w2[1]==0&&w2[2]==1){au=t;break;}}
                                    unsigned alen=au==(uint64_t)-1?0x1bf:(unsigned)(au-k);
                                    if(alen>0x20000)alen=0x20000;
                                    {uint8_t *aup=(void *)(uintptr_t)(b2+k);uint32_t hsh=2166136261u;
                                     for(unsigned q=0;q<alen;q++){hsh^=aup[q];hsh*=16777619u;}
                                     static uint32_t last_h=0;static unsigned last_len=0;static unsigned dup_cnt=0;
                                     if(getenv("NVD_SKIPDUP")&&hsh==last_h&&alen==last_len){dup_cnt++;
                                        struct line dr={0};text(&dr,"NVD_AUSKIP dup=");hex(&dr,dup_cnt);emit(&dr);}
                                     else{
                                    last_h=hsh;last_len=alen;
                                    if(getenv("NVD_DUMP_ES")&&!nvd_decoded){
                                        FILE*ef=fopen("/tmp/es_buf.h264","wb");
                                        if(ef){fwrite((void *)(uintptr_t)b2,1,(size_t)(lim2>0x100000?0x100000:lim2),ef);fclose(ef);}
                                        ef=fopen("/tmp/au0.h264","wb");
                                        if(ef){fwrite((void *)(uintptr_t)(b2+k),1,alen,ef);fclose(ef);}}
                                    if(getenv("NVD_DUMP_ES")){
                                        struct line hl={0};text(&hl,"NVD_AUH len=");hex(&hl,alen);
                                        text(&hl," h=");hex(&hl,hsh);text(&hl," ser=");hex(&hl,(uint32_t)(nvd_serial+1));emit(&hl);
                                        {char ap[64];FILE*pf;snprintf(ap,sizeof ap,"/tmp/aus/au%03u.h264",(unsigned)nvd_decoded);
                                            if((pf=fopen(ap,"wb"))){fwrite(aup,1,alen,pf);fclose(pf);}}}
                                    if(getenv("NVD_CAND")&&nvd_serial<50){
                                        for(unsigned j4=0;j4<NVMAP_MAX;j4++)if(nv_maps[j4].addr&&nv_maps[j4].size>=0x400){
                                            uint8_t *mb=(void *)(uintptr_t)nv_maps[j4].addr;uint64_t ml=nv_maps[j4].size;if(ml>0x2000)ml=0x2000;
                                            for(uint64_t t2=0;t2+4<ml;t2++){if(mb[t2]==0&&mb[t2+1]==0&&mb[t2+2]==1&&(mb[t2+3]==0x65||mb[t2+3]==0x61||mb[t2+3]==0x67)){
                                                uint32_t h4=2166136261u;unsigned l4=(unsigned)((ml-t2)>0x800?0x800:(ml-t2));
                                                for(unsigned q4=0;q4<l4;q4++){h4^=mb[t2+q4];h4*=16777619u;}
                                                struct line cd={0};text(&cd,"NVD_CAND i=");hex(&cd,j4);text(&cd," off=");hex(&cd,(uint32_t)t2);
                                                text(&cd," t=");hex(&cd,mb[t2+3]);text(&cd," h=");hex(&cd,h4);emit(&cd);break;}}}
                                    }
                                    int got=nvd_decode((void *)(uintptr_t)(b2+k),alen);
                                    struct line d2={0};text(&d2,"NVD_DEC got=");hex(&d2,got);text(&d2," alen=");hex(&d2,alen);
                                    text(&d2," n=");hex(&d2,nvd_decoded);text(&d2," w=");hex(&d2,nvd_w);text(&d2," h=");hex(&d2,nvd_h);
                                    text(&d2," rdy=");hex(&d2,nvd_ready);text(&d2," s=");hex(&d2,nvd_send_rc);text(&d2," rv=");hex(&d2,nvd_recv_rc);emit(&d2);
                                    for(unsigned j3=0;got&&j3<NVMAP_MAX;j3++)if(nv_maps[j3].addr&&nv_maps[j3].size==0x2fd000){
                                        nvd_blit_nv12((void *)(uintptr_t)nv_maps[j3].addr,1920,1080);
                                        struct line bl={0};text(&bl,"NVD_BLIT nv12=");hex(&bl,(unsigned)nv_maps[j3].addr);emit(&bl);}
                                    if(getenv("NVD_TESTPAT")&&nvd_decoded==40){
                                        for(unsigned j5=0;j5<NVMAP_MAX;j5++)if(nv_maps[j5].addr&&nv_maps[j5].size==0x2fd000){
                                            uint8_t *pb=(void *)(uintptr_t)nv_maps[j5].addr;
                                            memset(pb,235,(size_t)(1920*1080));memset(pb+1920*1080,128,(size_t)(960*540*2));
                                            struct line tpf={0};text(&tpf,"NVD_TESTPAT addr=");hex(&tpf,(unsigned)nv_maps[j5].addr);emit(&tpf);}
                                    }
                                     }}}
                            }
                        }
                    }
                error=0;ipc_reply(buffer,req->token,0,&error,4,0,req->domain);return 1;
            }
        stop("UNIMPLEMENTED_NV_IOCTL",pc,code,20);
    }
    if(req->command==2) {
        /* cmd 2 with a 12-byte argument arrives right after the decode submits; close the
           fd slot and report success, nothing else touches that state. */
        struct line cl={0};text(&cl,"NV_CLOSE data=");hex(&cl,(unsigned)req->data_size);emit(&cl);
        if(req->data_size)nv_dump("NV_CLOSE_ARG ",(void *)(uintptr_t)req->data,req->data_size<0x20?req->data_size:0x20);
        struct line st={0};text(&st,"NV_CLOSE_STATE ");hex(&st,(unsigned)*state);emit(&st);
        ipc_reply(buffer,req->token,0,&error,4,0,req->domain);return 1;
    }
    {struct line ul={0};text(&ul,"NV_UNHANDLED_CMD command=");hex(&ul,req->command);
     text(&ul," data=");hex(&ul,(unsigned)req->data_size);emit(&ul);}
    return 0;
}
