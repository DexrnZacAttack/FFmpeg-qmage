/*
 * Quram Qmage image format decoder
 * Copyright (c) 2024 Peter Ross
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <inttypes.h>

#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "copy_block.h"
#include "decode.h"
#include "get_bits.h"
#include "libavutil/mem.h"
#include "qmagedata.h"

#define QMAGE_MAGIC 0x514d
#define QVERSION_1_43_LESS 0xb
#define QCODEC_V16_SHORT_INDEX  0
#define QCODEC_W2_PASS 1

typedef struct {
    AVFrame * last_frame;

    int qversion;

    int raw_type;
    int transparency;

    int qp;
    int not_comp;
    int use_chroma_key;
    int mode;

    int encoder_mode;
    int is_dynamic_table;
    int alpha_depth;
    int depth;
    int use_extra_exception;

    int width;
    int height;

    int near_lossless;

    int android_support;
    int is_gray_type;
    int use_index_color;
    int pre_multiplied;
    int not_alpha_comp;
    int is_opaque;
    int nine_patched;

    int alpha_position;
    int alpha_encoder_mode;

    int total_frame_number;
    int current_frame_number;
    int animation_delay_time;
    int animation_no_repeat;

    int header_size;

    int color_count;
} Context;

static void dump(AVCodecContext *avctx)
{
    const Context * ctx = avctx->priv_data;
    av_log(avctx, AV_LOG_DEBUG, "qversion: 0x%x\n", ctx->qversion);
    av_log(avctx, AV_LOG_DEBUG, "raw_type: %d\n", ctx->raw_type);
    av_log(avctx, AV_LOG_DEBUG, "transparency: %d\n", ctx->transparency);
    av_log(avctx, AV_LOG_DEBUG, "qp: %d\n", ctx->qp);
    av_log(avctx, AV_LOG_DEBUG, "not_comp: %d\n", ctx->not_comp);
    av_log(avctx, AV_LOG_DEBUG, "use_chroma_key: %d\n", ctx->use_chroma_key);
    av_log(avctx, AV_LOG_DEBUG, "mode: %d\n", ctx->mode);
    av_log(avctx, AV_LOG_DEBUG, "encoder_mode: %d\n", ctx->encoder_mode);
    av_log(avctx, AV_LOG_DEBUG, "is_dynamic_table: %d\n", ctx->is_dynamic_table);
    av_log(avctx, AV_LOG_DEBUG, "alpha_depth: %d\n", ctx->alpha_depth);
    av_log(avctx, AV_LOG_DEBUG, "depth: %d\n", ctx->depth);
    av_log(avctx, AV_LOG_DEBUG, "use_extra_exception: %d\n", ctx->use_extra_exception);
    av_log(avctx, AV_LOG_DEBUG, "width: %d\n", ctx->width);
    av_log(avctx, AV_LOG_DEBUG, "height: %d\n", ctx->height);
    av_log(avctx, AV_LOG_DEBUG, "near_lossless: %d\n", ctx->near_lossless);
    av_log(avctx, AV_LOG_DEBUG, "android_support: %d\n", ctx->android_support);
    av_log(avctx, AV_LOG_DEBUG, "is_gray_type: %d\n", ctx->is_gray_type);
    av_log(avctx, AV_LOG_DEBUG, "use_index_color: %d\n", ctx->use_index_color);
    av_log(avctx, AV_LOG_DEBUG, "pre_multiplied: %d\n", ctx->pre_multiplied);
    av_log(avctx, AV_LOG_DEBUG, "not_alpha_comp: %d\n", ctx->not_alpha_comp);
    av_log(avctx, AV_LOG_DEBUG, "is_opaque: %d\n", ctx->is_opaque);
    av_log(avctx, AV_LOG_DEBUG, "nine_patched: %d\n", ctx->nine_patched);
    av_log(avctx, AV_LOG_DEBUG, "alpha_position: 0x%x\n", ctx->alpha_position);
    av_log(avctx, AV_LOG_DEBUG, "total_frame_number: %d\n", ctx->total_frame_number);
    av_log(avctx, AV_LOG_DEBUG, "current_frame_number: %d\n", ctx->current_frame_number);
    av_log(avctx, AV_LOG_DEBUG, "animation_delay_time: %d\n", ctx->animation_delay_time);
    av_log(avctx, AV_LOG_DEBUG, "animation_no_repeat: %d\n", ctx->animation_no_repeat);
    av_log(avctx, AV_LOG_DEBUG, "header_size: %d\n", ctx->header_size);
    av_log(avctx, AV_LOG_DEBUG, "color_count: %d\n", ctx->color_count);
}

static int decode_header(AVCodecContext *avctx, AVPacket *avpkt)
{
    Context * ctx = avctx->priv_data;
    GetByteContext gb;
    int flags4, flags5, flags10, flags11;

    if (avpkt->size < 12)
        return AVERROR_INVALIDDATA;

    bytestream2_init(&gb, avpkt->data, avpkt->size);

    if (bytestream2_get_be16(&gb) != QMAGE_MAGIC) {
        av_log(avctx, AV_LOG_ERROR, "bad magic number\n");
        return AVERROR_INVALIDDATA;
    }

    ctx->qversion = bytestream2_get_byte(&gb);

    ctx->raw_type = bytestream2_get_byte(&gb);
    switch (ctx->raw_type) {
    case 0: //RGB565
        ctx->transparency = 0;
        break;
    case 3: //RGBA5658
    case 6: //RGBA
        ctx->transparency = 1;
        break;
    default:
        avpriv_request_sample(avctx, "raw_type=%d", ctx->raw_type);
        return AVERROR_PATCHWELCOME;
    }

    flags4 = bytestream2_get_byte(&gb);
    ctx->qp = flags4 & 0x1f;
    ctx->not_comp = !!(flags4 & 0x20);
    ctx->use_chroma_key = !!(flags4 & 0x40);
    ctx->mode = !!(flags4 & 0x80);

    flags5 = bytestream2_get_byte(&gb);
    if (ctx->qversion == QVERSION_1_43_LESS)
        ctx->encoder_mode = flags5 & 0x7;
    else if (ctx->qversion > QVERSION_1_43_LESS)
        ctx->encoder_mode = flags5 & 0xf;
    else
        ctx->encoder_mode = 0;
    if (ctx->qversion > QVERSION_1_43_LESS)
        ctx->is_dynamic_table = !!(flags5 & 0x10);
    else
        ctx->is_dynamic_table = 0;
    ctx->alpha_depth = (flags5 & 0x20) ? 2 : 1;
    ctx->depth = (flags5 & 0x40) ? 2 : 1;
    ctx->use_extra_exception = !!(flags5 & 0x80);

    ctx->width = bytestream2_get_le16(&gb);
    ctx->height = bytestream2_get_le16(&gb);

    flags10 = bytestream2_get_byte(&gb);
    ctx->near_lossless = !!(flags10 & 0x40);

    flags11 = bytestream2_get_byte(&gb);
    ctx->android_support = !!(flags11 & 0x4);
    ctx->is_gray_type = !!(flags11 & 0x4);
    ctx->use_index_color = !!(flags11 & 0x8);
    ctx->pre_multiplied = !!(flags11 & 0x10);
    ctx->not_alpha_comp  = !!(flags11 & 0x40);
    ctx->is_opaque = !!(flags11 & 0x20);
    ctx->nine_patched = !!(flags11 & 0x80);

    if (ctx->qversion == QVERSION_1_43_LESS) {
        if (ctx->transparency || ctx->mode)
            ctx->alpha_position = bytestream2_get_le32(&gb);
        ctx->alpha_encoder_mode = ctx->encoder_mode;
    } else if (ctx->qversion > QVERSION_1_43_LESS) {
        int flags14;
        ctx->alpha_position = bytestream2_get_le16(&gb);
        flags14 = bytestream2_get_byte(&gb);
        ctx->alpha_encoder_mode = flags14 & 0xf;
        bytestream2_skip(&gb, 1);
    }

    if (ctx->mode) {
        ctx->total_frame_number = bytestream2_get_le16(&gb);
        ctx->current_frame_number = bytestream2_get_le16(&gb);
        ctx->animation_delay_time = bytestream2_get_le16(&gb);
        ctx->animation_no_repeat = bytestream2_get_byte(&gb);
        bytestream2_skip(&gb, 1);
    } else {
        ctx->total_frame_number = ctx->current_frame_number = 1;
    }

    if (ctx->qversion > QVERSION_1_43_LESS) {
        if (!ctx->mode || ctx->current_frame_number <= 1)
            ctx->alpha_position *= 4;
    }

    if (ctx->mode) {
        ctx->header_size = 24;
    } else {
        ctx->header_size = ctx->transparency ? 16 : 12;
    }

    if (ctx->use_index_color) {
        if (ctx->nine_patched)
            bytestream2_skip(&gb, 4);
        ctx->color_count = bytestream2_get_le32(&gb);
    }

    dump(avctx);

    return 0;
}

static uint16_t get_pixel(AVCodecContext * avctx, const uint8_t * src, int linesize, int x, int y)
{
    if (x >= 0 && x < avctx->width && y >= 0 && y < avctx->height)
        return AV_RN16A(src + y*linesize + x*2);
    return 0;
}

static void decode_pixel_inter(AVCodecContext * avctx, int copy, const uint16_t * ori_delta, GetBitContext * gb1, GetBitContext *gb2, GetByteContext * gb3, uint8_t * dst, const uint8_t * ref, int ref_linesize, int ref_x, int ref_y)
{
    if (copy) {
        AV_WN16A(dst, get_pixel(avctx, ref, ref_linesize, ref_x, ref_y));
    } else {
        int nb_bits = get_bits(gb2, 3);
        if (nb_bits == 7) {
            AV_WN16A(dst, bytestream2_get_le16(gb3));
        } else {
            int idx = get_bits(gb1, nb_bits + 1);
            uint16_t delta = ori_delta[idx + (2 << nb_bits) - 2];
            AV_WN16A(dst, get_pixel(avctx, ref, ref_linesize, ref_x, ref_y) + delta);
        }
    }
}

static void copy_edge(uint8_t * dst, int linesize, int width, int height)
{
    for (int j = 0; j < height; j++)
        for (int i = 0; i < width; i++)
            AV_WN16A(dst + j*linesize + i*2, AV_RN16A(dst + j*linesize - 2));
}

static int decode_a9ll(AVCodecContext *avctx, uint8_t * data, int size, uint8_t * dst, int dst_linesize)
{
    Context * s = avctx->priv_data;
    GetBitContext gb1, gb2;
    GetByteContext gb3;
    int gb1_start, gb3_start, ret;
    const uint16_t * ori_delta;
    uint16_t ori_delta_local[512];

    if (size < s->header_size + 8)
        return AVERROR_INVALIDDATA;
    gb1_start = AV_RL32(data + s->header_size);
    gb3_start = AV_RL32(data + s->header_size + 4);
    if (gb1_start < s->header_size + 8 || gb1_start > size || gb3_start < s->header_size + 8 || gb3_start > size)
        return AVERROR_INVALIDDATA;
    if ((ret = init_get_bits8(&gb1, data + s->header_size + 8, size - s->header_size - 8)) < 0)
        return ret;
    if ((ret = init_get_bits8(&gb2, data + gb1_start, size - gb1_start)) < 0)
        return ret;
    bytestream2_init(&gb3, data + gb3_start, size - gb3_start);

    if (s->is_dynamic_table) {
        uint8_t sign[512];
        for (int i = 0; i < 512; i++)
            sign[i] = bytestream2_get_byte(&gb3);
        for (int i = 0; i < 512; i++) {
            int v = bytestream2_get_le16(&gb3);
            ori_delta_local[i] = sign[i] ? v : -v;
        }
        ori_delta = ori_delta_local + 1;
    } else
        ori_delta = qmage_ori_delta[s->qversion != QVERSION_1_43_LESS];

    if (s->use_extra_exception) {
        avpriv_request_sample(avctx, "use_extra_exception");
        return AVERROR_INVALIDDATA;
    }

    for (int y = 0; y < avctx->height; y += 4) {
        for (int x = 0; x < avctx->width; x += 4) {
            int mode = get_bits(&gb1, 2);
            if (mode < 3) {
                int cbp = bytestream2_get_le16(&gb3);
                int k = 0;
                for (int j = 0; j < 4; j++) {
                    for (int i = 0; i < 4; i++) {
                        if (x + i < avctx->width && y + j < avctx->height) {
                            decode_pixel_inter(avctx, cbp & (1 << k), ori_delta, &gb1, &gb2, &gb3,
                                               dst + (y+j)*dst_linesize + (x+i)*2,
                                               dst, dst_linesize,
                                               x+i+qmage_dir[mode].x, y+j+qmage_dir[mode].y);
                            k++;
                        }
                    }
                }
            } else {
                if (x > 0)
                    copy_edge(dst + y*dst_linesize + x*2, dst_linesize,
                              FFMIN(avctx->width - x, 4), FFMIN(avctx->height - y, 4));
            }
        }
    }

    return 0;
}

static void copy_block4x4(uint8_t * dst, int dst_linesize, const uint8_t * ref, int ref_linesize)
{
    copy_block8(dst, ref, dst_linesize, ref_linesize, 4);
}

static void copy_block16x16(uint8_t * dst, int dst_linesize, const uint8_t * ref, int ref_linesize)
{
    copy_block16(dst, ref, dst_linesize, ref_linesize, 16);
    copy_block16(dst + 16, ref + 16, dst_linesize, ref_linesize, 16);
}

static void decode_pixel(AVCodecContext * avctx, GetBitContext * gb1, GetByteContext * gb2, const uint16_t * ori_delta, uint8_t * dst, const uint8_t * ref, int ref_linesize, int ref_x, int ref_y)
{
    int skip = get_bits1(gb1);
    if (skip) {
        AV_WN16A(dst, get_pixel(avctx, ref, ref_linesize, ref_x, ref_y));
    } else {
        int nb_bits = get_bits(gb1, 3);
        if (nb_bits == 7) {
            AV_WN16A(dst, bytestream2_get_le16(gb2));
        } else {
            int idx = get_bits(gb1, nb_bits + 1);
            uint16_t delta = ori_delta[idx + (2 << nb_bits) - 2];
            AV_WN16A(dst, get_pixel(avctx, ref, ref_linesize, ref_x, ref_y) + delta);
        }
    }
}

static void decode_block3_ani(AVCodecContext *avctx, GetBitContext * gb1, GetByteContext * gb2, int x, int y, uint8_t * dst, int linesize, const uint8_t * ref, int ref_linesize, int mv_x, int mv_y, const uint16_t * ori_delta)
{
    Context * s = avctx->priv_data;
    int mode = get_bits(gb1, 3);
    if (s->qp == 0 || get_bits1(gb1)) {
        if (mode < 3) {
            for (int j = 0; j < 4; j++)
                for (int i = 0; i < 4; i++)
                    decode_pixel(avctx, gb1, gb2, ori_delta,
                                 dst + (y+j)*linesize + (x+i)*2,
                                 dst, linesize, x+i+qmage_dir[mode].x, y+j+qmage_dir[mode].y);
        } else if (mode == 3) {
            if (x > 0)
                copy_edge(dst + y*linesize + x*2, linesize, 4, 4);
        } else if (mode == 4) {
            for (int j = 0; j < 4; j++)
                for (int i = 0; i < 4; i++)
                    decode_pixel(avctx, gb1, gb2, ori_delta,
                                 dst + (y+j)*linesize + (x+i)*2,
                                 ref, ref_linesize, x+i, y+j);
        } else if (mode == 5) {
            copy_block4x4(dst + y*linesize + x*2, linesize,
                          ref + y*ref_linesize + x*2, ref_linesize);
        } else if (mode == 6) {
            for (int j = 0; j < 4; j++)
                for (int i = 0; i < 4; i++)
                    decode_pixel(avctx, gb1, gb2, ori_delta,
                                 dst + (y+j)*linesize + (x+i)*2,
                                 ref, ref_linesize, x+i+mv_x, y+j+mv_y);
        } else {
            if (x+mv_x < 0 || x+mv_x+4 > avctx->width ||
                y+mv_y < 0 || y+mv_y+4 > avctx->height) {
                av_log(avctx, AV_LOG_WARNING, "offscreen mv");
                return;
            }
            copy_block4x4(dst + y*linesize + x*2, linesize,
                          ref + (y+mv_y)*ref_linesize + (x+mv_x)*2, ref_linesize);
        }
    } else {
        avpriv_request_sample(avctx, "qp");
    }
}

static void decode_block2_ani(AVCodecContext *avctx, GetBitContext * gb1, GetByteContext * gb2, int x, int y, uint8_t * dst, int linesize, const uint16_t * ori_delta)
{
    Context * s = avctx->priv_data;
    int mode = get_bits(gb1, 2);
    if (s->qp == 0 || get_bits1(gb1)) {
        if (mode < 3) {
            for (int j = 0; j < 4; j++)
                for (int i = 0; i < 4; i++)
                    decode_pixel(avctx, gb1, gb2, ori_delta,
                                 dst + (y+j)*linesize + (x+i)*2,
                                 dst, linesize, x+i+qmage_dir[mode].x, y+j+qmage_dir[mode].y);
        } else {
            if (x > 0)
                copy_edge(dst + y*linesize + x*2, linesize, 4, 4);
        }
    } else {
        avpriv_request_sample(avctx, "qp");
    }
}

static int decode_mb_ani(AVCodecContext *avctx, GetBitContext * gb1, GetByteContext * gb2, int x, int y, uint8_t * dst, int linesize, const uint8_t * ref, int ref_linesize, const uint16_t * ori_delta)
{
   if (get_bits1(gb1)) {
       if (get_bits1(gb1)) {
           copy_block16x16(dst + y*linesize + x*2, linesize, ref + y*ref_linesize + x*2, ref_linesize);
       } else {
           int mv_x, mv_y;
           if (!get_bits1(gb1)) {
               mv_x = get_bits(gb1, 8) - 0x7f;
               mv_y = get_bits(gb1, 7) - 0x3f;
               if (x+mv_x < 0 || x+mv_x+16 > avctx->width ||
                   y+mv_y < 0 || y+mv_y+16 > avctx->height) {
                   av_log(avctx, AV_LOG_WARNING, "offscreen mv");
                   return AVERROR_INVALIDDATA;
               }

               if (get_bits1(gb1)) {
                   copy_block16x16(dst + y*linesize + x*2, linesize,
                                   ref + (y+mv_y)*ref_linesize + (x+mv_x)*2, ref_linesize);
                   return 0;
               }
           } else {
               mv_x = mv_y = 0;
           }
           for (int j = 0; j < 16; j += 4)
               for (int i = 0; i < 16; i += 4)
                   decode_block3_ani(avctx, gb1, gb2, x+i, y+j, dst, linesize, ref, ref_linesize, mv_x, mv_y, ori_delta);
       }
   } else {
       for (int j = 0; j < 16; j += 4)
           for (int i = 0; i < 16; i += 4)
               decode_block2_ani(avctx, gb1, gb2, x+i, y+j, dst, linesize, ori_delta);
   }
   return 0;
}

static int decode_mbedge_ani(AVCodecContext *avctx, GetBitContext * gb1, GetByteContext * gb2, int xpos, int ypos,
    uint8_t * dst, int linesize, const uint8_t * ref, int ref_linesize, const uint16_t * ori_delta)
{
    if (get_bits1(gb1)) {
        avpriv_request_sample(avctx, "skip edge");
        return AVERROR_INVALIDDATA;
    }

    for (int y = ypos; y < FFMIN(ypos + 16, avctx->height); y += 4) {
        for (int x = xpos; x < FFMIN(xpos + 16, avctx->width); x += 4) {
            if (x + 4 <= avctx->width && y + 4 <= avctx->height) {
                int mode = get_bits(gb1, 2);
                if (mode < 3) {
                    for (int j = 0; j < 4; j++)
                        for (int i = 0; i < 4; i++)
                            if (x + i < avctx->width && y + j < avctx->height) {
                                decode_pixel(avctx, gb1, gb2, ori_delta,
                                             dst + (y+j)*linesize + (x+i)*2,
                                             dst, linesize, x+i+qmage_dir[mode].x, y+j+qmage_dir[mode].y);
                            }
                } else {
                   if (x > 0)
                       copy_edge(dst + y*linesize + x*2, linesize, FFMIN(avctx->width - x, 4), FFMIN(avctx->height - y, 4));
                }
            } else {
                 for (int j = 0; j < 4; j++)
                      for (int i = 0; i < 4; i++)
                          if (x + i < avctx->width && y + j < avctx->height)
                              AV_WN16A(dst + (y+j)*linesize + (x+i)*2, bytestream2_get_le16(gb2));
            }
        }
    }
    return 0;
}

static int decode_a9ll_ani(AVCodecContext *avctx, const uint8_t * data, int size, uint8_t * dst, int dst_linesize, const uint8_t * ref, int ref_linesize)
{
    Context * s = avctx->priv_data;
    GetBitContext gb1;
    GetByteContext gb2;
    int gb1_start;
    const uint16_t * ori_delta;
    int ret;

    if (size < s->header_size + 8)
        return AVERROR_INVALIDDATA;

    gb1_start = AV_RL32(data + s->header_size);
    if (gb1_start < s->header_size + 8 || gb1_start > size)
        return AVERROR_INVALIDDATA;
    if ((ret = init_get_bits8(&gb1, data + s->header_size + 8, size - s->header_size - 8)) < 0)
        return ret;
    bytestream2_init(&gb2, data + gb1_start, size - gb1_start);

    ori_delta = qmage_ori_delta[s->qversion != QVERSION_1_43_LESS];

    for (int y = 0; y < avctx->height; y += 16) {
        for (int x = 0; x < avctx->width; x += 16) {
            if (avctx->width - x >= 16 && avctx->height - y >= 16) {
                if (decode_mb_ani(avctx, &gb1, &gb2, x, y, dst, dst_linesize, ref, ref_linesize, ori_delta) < 0)
                    return AVERROR_INVALIDDATA;
            } else {
                if (decode_mbedge_ani(avctx, &gb1, &gb2, x, y, dst, dst_linesize, ref, ref_linesize, ori_delta) < 0)
                    return AVERROR_INVALIDDATA;
            }
        }
    }
    return 0;
}

static void memset32(uint8_t * dst, uint32_t v, int count)
{
    for (int i = 0; i < count; i++)
        AV_WN32A(dst + i * 4, v);
}

static int read_value(GetByteContext * gb)
{
    int v = 0;
    while (bytestream2_peek_byte(gb) == 0xff) {
        bytestream2_skip(gb, 1);
        v += 0xff;
    }
    return v + bytestream2_get_byte(gb);
}

static int decode_w2_aligned(AVCodecContext * avctx, GetByteContext * gb1, GetByteContext * gb2, GetByteContext * gb3, const uint8_t * data, int size, uint8_t * dst)
{
    int counter = 0, idx, run;
    uint32_t val;
    int dim = avctx->width * avctx->height * 2;
    do {
        idx = read_value(gb1);
        if (!idx) {
            val = bytestream2_get_le32(gb3);
            AV_WN32A(dst + counter, val);
            counter += 4;
        } else {
            idx--;
            if (idx * 4 + 4 > size - 16)
                return AVERROR_INVALIDDATA;
            val = AV_RL32(data + 16 + idx * 4);
            run = read_value(gb2) + 1;

            memset32(dst + counter, val, FFMIN(run, (dim - counter) / 4));
            counter += 4 * run;
        }
    } while (counter < dim);
    return 0;
}

static int decode_w2_unaligned(AVCodecContext * avctx, GetByteContext * gb1, GetByteContext * gb2, GetByteContext * gb3, const uint8_t * data, int size, uint8_t * dst, int dst_linesize)
{
    int x = 0, y = 0, idx, run;
    uint16_t v1, v2;

    while (1) {
        idx = read_value(gb1);
        if (!idx) {
#define WRITE_PIXEL(v) \
            AV_WN16A(dst + y*dst_linesize + x*2, v); \
            x++; \
            if (x >= avctx->width) { \
                x = 0; \
                y++; \
                if (y >= avctx->height) \
                    return 0; \
            }
            v1 = bytestream2_get_le16(gb3);
            v2 = bytestream2_get_le16(gb3);
            WRITE_PIXEL(v1)
            WRITE_PIXEL(v2)
        } else {
            idx--;
            if (idx * 4 + 4 > size - 16)
                return AVERROR_INVALIDDATA;
            v1 = AV_RL16(data + 16 + idx * 4);
            v2 = AV_RL16(data + 16 + idx * 4 + 2);
            run = read_value(gb2) + 1;
            for (int i = 0; i < run; i++) {
                WRITE_PIXEL(v1)
                WRITE_PIXEL(v2)
            }
        }
    }
#undef WRITE_PIXEL
    return 0;
}

static int decode_w2_pass_depth1(AVCodecContext *avctx, const uint8_t * data, int size, uint8_t * dst, int dst_linesize)
{
    int cnt_table, size_idx, size_run;
    int start1, start2, start3;
    GetByteContext gb1, gb2, gb3;

    if (size < 16)
        return AVERROR_INVALIDDATA;

    cnt_table = AV_RL32(data);
    size_idx = AV_RL32(data + 4);
    size_run = AV_RL32(data + 8);

    start1 = 16 + cnt_table*4;
    start2 = start1 + size_idx;
    start3 = start2 + size_run;

    if (start1 >= size || start2 >= size || start3 > size)
        return AVERROR_INVALIDDATA;

    bytestream2_init(&gb1, data + start1, size - start1);
    bytestream2_init(&gb2, data + start2, size - start2);
    bytestream2_init(&gb3, data + start3, size - start3);

    if (dst_linesize == avctx->width * 2)
        return decode_w2_aligned(avctx, &gb1, &gb2, &gb3, data, size, dst);

    return decode_w2_unaligned(avctx, &gb1, &gb2, &gb3, data, size, dst, dst_linesize);
}

static int strip1(GetBitContext * gb1, GetByteContext * gb2, GetByteContext * gb3, int * rel, uint8_t * dst, int d_pos)
{
    AV_WN32A(dst + d_pos, bytestream2_get_le32(gb3));
    d_pos += 4;
    for (int i = 0; i < 6; i++) {
        uint16_t v;
        if (!(i & 1)) {
            if (!get_bits1(gb1))
                *rel = get_bits1(gb1) ? bytestream2_get_byte(gb2)
                                      : bytestream2_get_le16(gb3);
        }
        if (!get_bits1(gb1)) {
            if (!get_bits1(gb1)) {
                int pos = d_pos - *rel*2;
                if (pos < 0)
                    return -1;
                v = AV_RN16A(dst + pos) ^ qmage_diff[bytestream2_get_byte(gb2)];
            } else {
                v = bytestream2_get_ne16(gb3);
            }
        } else {
            int pos = d_pos - *rel*2;
            if (pos < 0)
                return -1;
            v = AV_RN16A(dst + d_pos - *rel*2);
        }
        AV_WN16A(dst + d_pos, v);
        d_pos += 2;
    }
    return 0;
}

static int strip2(GetBitContext * gb1, GetByteContext * gb2, GetByteContext * gb3, int * rel, uint8_t * dst, int d_pos)
{
    int mask = bytestream2_get_byte(gb2);
    for (int i = 0; i < 8; i++) {
        uint16_t v;
        if (!(i & 1)) {
            if (!get_bits1(gb1)) {
                *rel = get_bits1(gb1) ? bytestream2_get_byte(gb2)
                                      : bytestream2_get_le16(gb3);
            }
        }
        if (!(mask & (1 << (7 - i)))) {
            if (!get_bits1(gb1)) {
                int pos = d_pos - *rel*2;
                if (pos < 0)
                    return -1;
                v = AV_RN16A(dst + pos) ^ qmage_diff[bytestream2_get_byte(gb2)];
            } else {
                v = bytestream2_get_ne16(gb3);
            }
        } else {
            int pos = d_pos - *rel*2;
            if (pos < 0)
                return -1;
            v = AV_RN16A(dst + d_pos - *rel* 2);
        }
        AV_WN16A(dst + d_pos, v);
        d_pos += 2;
    }
    return 0;
}

static int decode_w2_pass_depth2(AVCodecContext *avctx, const uint8_t * data, int size, uint8_t * dst, int dst_linesize)
{
    int bsize, ret;
    uint8_t * bdata;
    int len1, len2, rel = 1, d_pos;
    GetBitContext gb1;
    GetByteContext gb2, gb3;

    if (size < 12)
        return AVERROR_INVALIDDATA;

    bsize = AV_RL32(data);
    if (bsize < 16)
        return AVERROR_INVALIDDATA;

    bdata = av_malloc(bsize);
    if (!bdata)
        return AVERROR(ENOMEM);

    len1 = AV_RL32(data + 4);
    len2 = AV_RL32(data + 8);
    if ((ret = init_get_bits8(&gb1, data + 12, size - 12)) < 0) {
        free(bdata);
        return ret;
    }
    bytestream2_init(&gb2, data + 12 + len1, size - 12 - len1);
    bytestream2_init(&gb3, data + 12 + len1 + len2, size - 12 - len1 - len2);

    if (strip1(&gb1, &gb2, &gb3, &rel, bdata, 0) < 0) {
        free(bdata);
        return AVERROR_INVALIDDATA;
    }

    for (d_pos = 16; d_pos < (bsize & ~15); d_pos += 16) {
        if (!get_bits1(&gb1)) {
            if (!get_bits1(&gb1)) {
                bytestream2_get_buffer(&gb3, bdata + d_pos, 16);
            } else {
                if (d_pos - rel*2 < 0) {
                    free(bdata);
                    return AVERROR_INVALIDDATA;
                }
                for (int j = 0; j < 8; j++)
                    AV_WN16A(bdata + d_pos + j*2, AV_RN16A(bdata + d_pos - rel*2 + j*2));
            }
        } else {
            if (strip2(&gb1, &gb2, &gb3, &rel, bdata, d_pos) < 0) {
                free(bdata);
                return AVERROR_INVALIDDATA;
            }
        }
    }

    if (bsize & 15)
        bytestream2_get_buffer(&gb2, bdata + d_pos, bsize & 15);

    ret = decode_w2_pass_depth1(avctx, bdata, bsize, dst, dst_linesize);
    av_free(bdata);
    return ret;
}

static av_cold int qmage_decode_init(AVCodecContext *avctx)
{
    Context * s = avctx->priv_data;

    avctx->pix_fmt = AV_PIX_FMT_RGB565;

    s->last_frame = av_frame_alloc();
    if (!s->last_frame)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold int qmage_decode_close(AVCodecContext *avctx)
{
    Context *s = avctx->priv_data;
    av_frame_free(&s->last_frame);
    return 0;
}

static int qmage_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                            int *got_frame, AVPacket *avpkt)
{
    Context * s = avctx->priv_data;
    int ret;

    ret = decode_header(avctx, avpkt);
    if (ret < 0)
        return ret;

    ret = ff_get_buffer(avctx, frame, 0);
    if (ret < 0)
        return ret;

    if (s->mode) {
        if (s->current_frame_number == 1) {
            frame->flags |= AV_FRAME_FLAG_KEY;
            decode_a9ll(avctx, avpkt->data, avpkt->size, frame->data[0], frame->linesize[0]);
        } else {
            decode_a9ll_ani(avctx, avpkt->data, avpkt->size, frame->data[0], frame->linesize[0],
                            s->last_frame->data[0], s->last_frame->linesize[0]);
        }
    } else {
        frame->flags |= AV_FRAME_FLAG_KEY;
        switch(s->encoder_mode) {
        case QCODEC_W2_PASS: break;
        default:
            avpriv_request_sample(avctx, "encoder_mode=%d", s->encoder_mode);
            return AVERROR_INVALIDDATA;
        }
        switch (s->depth) {
        case 1:
            decode_w2_pass_depth1(avctx, avpkt->data + s->header_size, avpkt->size - s->header_size, frame->data[0], frame->linesize[0]);
            break;
        case 2:
            decode_w2_pass_depth2(avctx, avpkt->data + s->header_size, avpkt->size - s->header_size, frame->data[0], frame->linesize[0]);
            break;
        default:
            return AVERROR_INVALIDDATA;
        }
    }

    if ((ret = av_frame_replace(s->last_frame, frame)) < 0)
        return ret;

    *got_frame = 1;

    return avpkt->size;
}

const FFCodec ff_qmage_decoder = {
    .p.name         = "qmage",
    CODEC_LONG_NAME("Quram Qmage"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_QMAGE,
    .p.capabilities = AV_CODEC_CAP_DR1,
    .priv_data_size = sizeof(Context),
    .init           = qmage_decode_init,
    .close          = qmage_decode_close,
    FF_CODEC_DECODE_CB(qmage_decode_frame),
};
