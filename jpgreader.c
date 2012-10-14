/*
  jpgreader.c: JPEG image reader for VapourSynth

  This file is a part of vsjpgreader

  Copyright (C) 2012  Oka Motofumi

  Author: Oka Motofumi (chikuzen.mo at gmail dot com)

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with Libav; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>

#include "turbojpeg.h"
#include "VapourSynth.h"

#define VS_JPGR_VERSION "0.1.0"
#define INITIAL_SRC_BUFF_SIZE (2 * 1024 * 1024) /* 2MiByte */

typedef struct jpeg_handle {
    const char **src_files;
    unsigned long *src_size;
    unsigned char *src_buff;
    uint32_t buff_size;
    unsigned char *decode_buff;
    tjhandle tjh;
    VSVideoInfo vi;
} jpg_hnd_t;

typedef struct {
    const VSMap *in;
    VSMap *out;
    VSCore *core;
    const VSAPI *vsapi;
} vs_args_t;

typedef struct {
    int width;
    int height;
    int subsample;
} whs_t;


static void close_handler(jpg_hnd_t *jh)
{
    if (!jh) {
        return;
    }
    if (jh->decode_buff) {
        free(jh->decode_buff);
    }
    if (jh->src_buff) {
        free(jh->src_buff);
    }
    if (jh->src_files) {
        free(jh->src_files);
    }
    if (jh->tjh && tjDestroy(jh->tjh)) {
        fprintf(stderr, tjGetErrorStr());
    }
    free(jh);
}


static void VS_CC
vs_init(VSMap *in, VSMap *out, void **instance_data, VSNode *node,
        VSCore *core, const VSAPI *vsapi)
{
    jpg_hnd_t *jh = (jpg_hnd_t *)*instance_data;
    vsapi->setVideoInfo(&jh->vi, node);
}


static void VS_CC
vs_close(void *instance_data, VSCore *core, const VSAPI *vsapi)
{
    jpg_hnd_t *jh = (jpg_hnd_t *)instance_data;
    close_handler(jh);
}


static VSPresetFormat VS_CC tjsamp_to_vspresetformat(enum TJSAMP tjsamp)
{
    const struct {
        enum TJSAMP tjsample_type;
        VSPresetFormat vsformat;
    } table[] = {
        { TJSAMP_444,  pfYUV444P8 },
        { TJSAMP_422,  pfYUV422P8 },
        { TJSAMP_420,  pfYUV420P8 },
        { TJSAMP_GRAY, pfGray8    },
        { TJSAMP_440,  pfYUV440P8 },
        { tjsamp,      pfNone     }
    };

    int i = 0;
    while (table[i].tjsample_type != tjsamp) i++;
    return table[i].vsformat;
}


static char * VS_CC
check_srcs(jpg_hnd_t *jh, struct stat *st, int n, whs_t *whs)
{
    if (stat(jh->src_files[n], st)) {
        return "source file does not exist";
    }

    jh->src_size[n] = st->st_size;
    if (jh->buff_size < st->st_size) {
        jh->buff_size = st->st_size;
        free(jh->src_buff);
        jh->src_buff = malloc(jh->buff_size);
        if (!jh->src_buff) {
            return "failed to allocate read buffer";
        }
    }

    FILE *fp = fopen(jh->src_files[n], "rb");
    if (!fp) {
        return "failed to open file";
    }

    unsigned long read = fread(jh->src_buff, 1, st->st_size, fp);
    fclose(fp);
    if (read < st->st_size) {
        return "failed to read file";
    }

    int width, height, subsample;
    if (tjDecompressHeader2(
            jh->tjh, jh->src_buff, read, &width, &height, &subsample) != 0) {
        return tjGetErrorStr();
    }

    width = ((width + 3) >> 2) << 2;

    if (whs->width != width || whs->height != height) {
        if (n > 0) {
            return "found a file which has diffrent resolution from first file";
        }
        whs->width = width;
        whs->height = height;
    }

    if (whs->subsample != subsample) {
        if (n > 0) {
            return "found a file witch has different sample type from first file";
        }
        whs->subsample = subsample;
    }

    return NULL;
}


static void VS_CC
set_args_int64(int64_t *p, int default_value, const char *arg, vs_args_t *va)
{
    int err;
    *p = va->vsapi->propGetInt(va->in, arg, 0, &err);
    if (err) {
        *p = default_value;
    }
}


static void VS_CC
jpg_bit_blt(VSFrameRef *dst, int plane, const VSAPI *vsapi, uint8_t *srcp,
            int row_size, int height)
{
    uint8_t *dstp = vsapi->getWritePtr(dst, plane);
    int dst_stride = vsapi->getStride(dst, plane);

    if (row_size == dst_stride) {
        memcpy(dstp, srcp, row_size * height);
        return;
    }

    for (int i = 0; i < height; i++) {
        memcpy(dstp, srcp, row_size);
        dstp += dst_stride;
        srcp += row_size;
    }
}


static const VSFrameRef * VS_CC
jpg_get_frame(int n, int activation_reason, void **instance_data,
              void **frame_data, VSFrameContext *frame_ctx, VSCore *core,
              const VSAPI *vsapi)
{
    if (activation_reason != arInitial) {
        return NULL;
    }

    jpg_hnd_t *jh = (jpg_hnd_t *)*instance_data;

    int frame_number = n;
    if (n >= jh->vi.numFrames) {
        frame_number = jh->vi.numFrames - 1;
    }

    FILE *fp = fopen(jh->src_files[frame_number], "rb");
    if (!fp) {
        return NULL;
    }

    unsigned long read = fread(jh->src_buff, 1, jh->src_size[frame_number], fp);
    fclose(fp);
    if (read < jh->src_size[frame_number]) {
        return NULL;
    }

    if (tjDecompressToYUV(jh->tjh, jh->src_buff, read, jh->decode_buff, 0)) {
        return NULL;
    }

    VSFrameRef *dst = vsapi->newVideoFrame(jh->vi.format, jh->vi.width,
                                           jh->vi.height, NULL, core);

    VSMap *props = vsapi->getFramePropsRW(dst);
    vsapi->propSetInt(props, "_DurationNum", jh->vi.fpsDen, 0);
    vsapi->propSetInt(props, "_DurationDen", jh->vi.fpsNum, 0);

    uint8_t *srcp = jh->decode_buff;
    for (int i = 0, time = jh->vi.format->numPlanes; i < time; i++) {
        int row_size = vsapi->getFrameWidth(dst, i);
        int height = vsapi->getFrameHeight(dst, i);
        jpg_bit_blt(dst, i, vsapi, srcp, row_size, height);
        srcp += row_size * height;
    }

    return dst;
}


#define RET_IF_ERR(cond, ...) \
{\
    if (cond) {\
        close_handler(jh);\
        snprintf(msg, 240, __VA_ARGS__);\
        vsapi->setError(out, msg_buff);\
        return;\
    }\
}

static void VS_CC
create_source(const VSMap *in, VSMap *out, void *user_data, VSCore *core,
              const VSAPI *vsapi)
{
    char msg_buff[256] = "jpgs: ";
    char *msg = msg_buff + strlen(msg_buff);
    vs_args_t va = {in, out, core, vsapi};

    jpg_hnd_t *jh = (jpg_hnd_t *)calloc(sizeof(jpg_hnd_t), 1);
    RET_IF_ERR(!jh, "failed to create handler");

    int num_srcs = vsapi->propNumElements(in, "files");
    RET_IF_ERR(num_srcs < 1, "no source file");

    jh->src_files = (const char **)calloc(sizeof(char *), num_srcs);
    RET_IF_ERR(!jh->src_files, "failed to allocate array of src files");

    int err;
    for (int i = 0; i < num_srcs; i++) {
        jh->src_files[i] = vsapi->propGetData(in, "files", i, &err);
        RET_IF_ERR(err || strlen(jh->src_files[i]) == 0,
                   "zero length file name was found");
    }

    jh->src_size = (unsigned long *)malloc(sizeof(unsigned long) * num_srcs);
    RET_IF_ERR(!jh->src_size, "failed to allocate array of src file size");

    jh->src_buff = malloc(INITIAL_SRC_BUFF_SIZE);
    RET_IF_ERR(!jh->src_buff, "failed to allocate read buffer");
    jh->buff_size = INITIAL_SRC_BUFF_SIZE;

    struct stat st;
    whs_t whs = { 0 };
    jh->tjh = tjInitDecompress();
    RET_IF_ERR(!jh->tjh, "%s\n", tjGetErrorStr());

    for (int i = 0; i < num_srcs; i++) {
        char *cs = check_srcs(jh, &st, i, &whs);
        RET_IF_ERR(cs, "%s : %s", cs, jh->src_files[i]);
    }

    jh->vi.width = whs.width;
    jh->vi.height = whs.height;
    jh->vi.numFrames = num_srcs;
    jh->vi.format = vsapi->getFormatPreset(
        tjsamp_to_vspresetformat(whs.subsample), core);
    set_args_int64(&jh->vi.fpsNum, 24, "fpsnum", &va);
    set_args_int64(&jh->vi.fpsDen, 1, "fpsden", &va);
    RET_IF_ERR(jh->vi.format->id == pfNone, "unsupported format");

    jh->decode_buff = malloc(tjBufSizeYUV(whs.width, whs.height, whs.subsample));
    RET_IF_ERR(!jh->decode_buff, "failed to allocate decode buffer");

    const VSNodeRef *node =
        vsapi->createFilter(in, out, "Read", vs_init, jpg_get_frame, vs_close,
                            fmSerial, 0, jh, core);

    vsapi->propSetNode(out, "clip", node, 0);
}


VS_EXTERNAL_API(void) VapourSynthPluginInit(
    VSConfigPlugin f_config, VSRegisterFunction f_register, VSPlugin *plugin)
{
    f_config("chikuzen.does.not.have.his.own.domain.jpgr", "jpgr",
             "JPEG image reader for VapourSynth " VS_JPGR_VERSION,
             VAPOURSYNTH_API_VERSION, 1, plugin);
    f_register("Read", "files:data[];fpsnum:int:opt;fpsden:int:opt",
               create_source, NULL, plugin);
}
