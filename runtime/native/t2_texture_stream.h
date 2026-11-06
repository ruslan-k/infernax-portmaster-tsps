/* Bounded catalogue staging. Files are read forwards even when texture rows
 * are flipped: reverse rows within each slab and upload the slabs at descending
 * Y. No shader, palette, filter, mip or resident texture format changes. */
#ifndef T2_TEXTURE_STREAM_H
#define T2_TEXTURE_STREAM_H
#include <GLES2/gl2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define T2_TEXTURE_STREAM_ROWS 64U
typedef int (*t2_texture_rows_fn)(void *,unsigned,unsigned,const uint8_t *);

/* Does not own FILE. Seek only for the initial exact-size check and rewind;
 * every subsequent read is sequential. The callback may consume/copy the slab
 * only during the call; its storage is reused for the next rows. */
static inline int t2_texture_stream_file(FILE *file,unsigned width,unsigned height,
        unsigned channels,unsigned flipped,t2_texture_rows_fn consume,void *context) {
    if(!file || !width || !height || width>4096 || height>4096 || !channels || channels>4 || !consume)return 0;
    size_t row=(size_t)width*channels,bytes=row*height;
    if(fseek(file,0,SEEK_END) || ftell(file)!=(long)bytes || fseek(file,0,SEEK_SET))return 0;
    unsigned capacity=height<T2_TEXTURE_STREAM_ROWS?height:T2_TEXTURE_STREAM_ROWS;
    uint8_t *data=malloc(row*capacity);if(!data)return 0;
    int ok=1;
    for(unsigned first=0;first<height && ok;first+=capacity) {
        unsigned rows=height-first<capacity?height-first:capacity;
        for(unsigned r=0;r<rows;r++) {
            unsigned destination=flipped?rows-1-r:r;
            if(fread(data+(size_t)destination*row,1,row,file)!=row){ok=0;break;}
        }
        unsigned y=flipped?height-first-rows:first;
        if(ok)ok=consume(context,y,rows,data);
    }
    if(ok && (fgetc(file)!=EOF || ferror(file)))ok=0;
    free(data);return ok;
}

struct t2_texture_stream_gl_upload {unsigned width,height;GLenum format,error;unsigned allocated;};
static inline int t2_texture_stream_gl_rows(void *context,unsigned y,unsigned rows,const uint8_t *pixels) {
    struct t2_texture_stream_gl_upload *u=context;
    if(!u->allocated) {
        glTexImage2D(GL_TEXTURE_2D,0,u->format,(GLsizei)u->width,(GLsizei)u->height,0,u->format,GL_UNSIGNED_BYTE,NULL);
        u->error=glGetError();if(u->error!=GL_NO_ERROR)return 0;
        u->allocated=1;
    }
    glTexSubImage2D(GL_TEXTURE_2D,0,0,(GLint)y,(GLsizei)u->width,(GLsizei)rows,u->format,GL_UNSIGNED_BYTE,pixels);
    u->error=glGetError();return u->error==GL_NO_ERROR;
}
/* Upload into the currently bound texture; leave binding and unpack alignment
 * unchanged on success or failure. A failure may leave an incomplete image:
 * the caller must reject it, never publish or sample it. */
static inline int t2_texture_stream_gl(const char *path,unsigned width,unsigned height,
        unsigned channels,unsigned flipped,GLenum *error) {
    const GLenum formats[]={GL_LUMINANCE,GL_LUMINANCE_ALPHA,GL_RGB,GL_RGBA};
    if(error)*error=GL_NO_ERROR;
    if(!path || !channels || channels>4)return 0;
    FILE *file=fopen(path,"rb");if(!file)return 0;
    GLint alignment;glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    struct t2_texture_stream_gl_upload upload={width,height,formats[channels-1],GL_NO_ERROR,0};
    int ok=t2_texture_stream_file(file,width,height,channels,flipped,t2_texture_stream_gl_rows,&upload);
    if(fclose(file))ok=0;
    glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);
    GLenum restore_error=glGetError();if(restore_error!=GL_NO_ERROR){if(!upload.error)upload.error=restore_error;ok=0;}
    if(error)*error=upload.error;
    return ok;
}
#endif
