/* REV6 variable-length splitter section. Protocol fields independently
 * checked against the pinned audio reference and the original SDK packet.
 * Resources persist across updates; a section may update any subset. */
static void zero_aud_splitter_update(struct aud_renderer *r,const uint8_t *in,uint32_t size,uint64_t pc) {
    if(size<32 || memcmp(in,"SNDH",4))stop("ZERO_AUDIO_SPLITTER_HEADER",pc,size,24);
    unsigned count=read32(in+4),destinations=read32(in+8),at=32;
    if(count>r->p.splitters || destinations>r->p.splitter_destinations)stop("ZERO_AUDIO_SPLITTER_COUNTS",pc,count,24);
    uint8_t seen_splitters[1024]={0},seen_destinations[6144]={0};
    for(unsigned i=0;i<count;i++) {
        if(at>size || size-at<28 || memcmp(in+at,"SNDI",4))stop("ZERO_AUDIO_SPLITTER_INFO",pc,at,24);
        unsigned id=read32(in+at+4),rate=read32(in+at+8),n=read32(in+at+12);
        if(id>=r->p.splitters || seen_splitters[id] || n>24 || (n && rate!=r->p.rate) || n*4>size-at-28)
            stop("ZERO_AUDIO_SPLITTER_INFO_RANGE",pc,id,24);
        seen_splitters[id]=1;struct zero_aud_splitter *s=&r->splitters[id];s->rate=rate;s->count=n;
        for(unsigned j=0;j<n;j++) {
            unsigned destination=read32(in+at+16+j*4);
            if(destination>=r->p.splitter_destinations)stop("ZERO_AUDIO_SPLITTER_DESTINATION_ID",pc,destination,24);
            for(unsigned k=0;k<j;k++)if(s->ids[k]==destination)stop("ZERO_AUDIO_SPLITTER_DUPLICATE_ROUTE",pc,destination,24);
            s->ids[j]=destination;
        }
        at+=28+n*4;
    }
    for(unsigned i=0;i<destinations;i++,at+=112) {
        if(at>size || size-at<112 || memcmp(in+at,"SNDD",4))stop("ZERO_AUDIO_SPLITTER_DESTINATION",pc,at,24);
        unsigned id=read32(in+at+4),mix=read32(in+at+104),used=in[at+108];
        if(id>=r->p.splitter_destinations || seen_destinations[id] || used>1 || (used && mix!=UINT32_MAX && mix>r->p.submixes))
            stop("ZERO_AUDIO_SPLITTER_DESTINATION_RANGE",pc,id,24);
        seen_destinations[id]=1;struct zero_aud_destination *d=&r->destinations[id];d->mix=mix;d->used=used;
        memcpy(d->gain,in+at+8,sizeof(d->gain));
        for(unsigned j=0;j<24;j++)if(used && !isfinite(d->gain[j]))stop("ZERO_AUDIO_SPLITTER_GAIN",pc,id,24);
    }
    if(((at+15)&~15U)!=size)stop("ZERO_AUDIO_SPLITTER_SECTION_SIZE",pc,at,24);
    if(!r->updates)printf("ZERO_AUDIO_SPLITTER_UPDATE groups=%u destinations=%u bytes=%u\n",count,destinations,size);
}
