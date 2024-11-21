/*
 * Quram Qmage image format demuxer
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

#include "avformat.h"
#include "demux.h"
#include "internal.h"
#include "libavcodec/get_bits.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"

#define QMAGE_MAGIC        0x514d
#define QVERSION_1_43_LESS 0xb

static int qmage_probe(const AVProbeData *p)
{
    if (AV_RB16(p->buf) != QMAGE_MAGIC || !AV_RL16(p->buf + 6) || !AV_RL16(p->buf + 8))
        return 0;

    return AVPROBE_SCORE_EXTENSION / 4;
}

/* keyframe alpha size is not stored in the bitstream, so we must parse the
 * bitstream to determine size
 */
static int parse_a9ll_alpha_size(AVFormatContext * s, int width, int height)
{
    AVIOContext *pb = s->pb;
    int len1, len2, ret;
    uint8_t * data;
    GetBitContext gb1, gb2;
    int64_t start = avio_tell(pb);

    if ((width & 7) || (height & 3)) {
        avpriv_request_sample(s, "unaligned alpha");
        return AVERROR_PATCHWELCOME;
    }

    len1 = avio_rl32(pb);
    len2 = avio_rl32(pb);
    if (len1 < 8 || len2 < 8 || len1 > len2)
        return AVERROR_INVALIDDATA;

    len1 -= 8;
    len2 -= 8;
    data = av_malloc(len2 + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!data)
        return AVERROR(ENOMEM);
    avio_read(pb, data, len2);
    if ((ret = init_get_bits8(&gb1, data, len1)) < 0)
        return ret;
    if ((ret = init_get_bits8(&gb2, data + len1, len2 - len1)) < 0)
        return ret;

    for (int y = 0; y < height; y += 4) {
        for (int x = 0; x < width; x += 8) {
            int mode = get_bits(&gb1, 2);
            if (mode < 3) {
                int cbp = avio_rl16(pb);
                for (int k = 0; k < 16; k++) {
                    if (!(cbp & (1 << k))) {
                        int nb_bits = get_bits(&gb2, 3);
                        if (nb_bits == 7) {
                            avio_skip(pb, 2);
                        } else {
                            skip_bits(&gb1, nb_bits + 1);
                        }
                    }
                }
            }
        }
    }

    free(data);
    return ((avio_tell(pb) + 3) & ~3) - start;
}

typedef struct {
    int qversion;
    int raw_type;
    int transparency;
    int mode;
    int width;
    int height;
    int alpha_position;
    int total_frame_number;
    int current_frame_number;
    int animation_delay_time;
    int animation_no_repeat;
    int header_size;
} Header;

static int read_header(AVFormatContext * s, Header * h)
{
    AVIOContext *pb = s->pb;

    int magic = avio_rb16(pb);
    if (avio_feof(pb))
        return AVERROR_EOF;
    if (magic != QMAGE_MAGIC) {
        av_log(s, AV_LOG_ERROR, "unexpected magic 0x%x at 0x%" PRIx64 "\n", magic, avio_tell(pb) - 4);
        return AVERROR_INVALIDDATA;
    }

    h->qversion = avio_r8(pb);
    h->raw_type = avio_r8(pb);
    switch (h->raw_type) {
    case 0: //RGB565
        h->transparency = 0;
        break;
    case 3: //RGBA5658
    case 6: //RGBA
        h->transparency = 1;
        break;
    default:
        avpriv_request_sample(s, "raw_type=%d\n", h->raw_type);
        return AVERROR_INVALIDDATA;
    }

    h->mode = !!(avio_r8(pb) & 0x80);
    avio_skip(pb, 1);
    h->width = avio_rl16(pb);
    h->height = avio_rl16(pb);
    avio_skip(pb, 2);

    if (h->qversion == QVERSION_1_43_LESS) {
        if (h->transparency || h->mode)
            h->alpha_position = avio_rl32(pb);
        else
            h->alpha_position = -1;

    } else if (h->qversion > QVERSION_1_43_LESS) {
        h->alpha_position = avio_rl16(pb);
        avio_skip(pb, 2);
    } else {
        avpriv_request_sample(s, "qversion=0x%x", h->qversion);
        return AVERROR_INVALIDDATA;
    }

    if (h->mode) {
        h->total_frame_number = avio_rl16(pb);
        h->current_frame_number = avio_rl16(pb);
        h->animation_delay_time = avio_rl16(pb);
        h->animation_no_repeat = avio_r8(pb);
        avio_skip(pb, 1);
        h->header_size = 24;
    } else {
        h->total_frame_number = h->current_frame_number = 1;
        h->header_size = h->transparency ? 16 : 12;
    }

    if (h->qversion > QVERSION_1_43_LESS) {
        if (!h->mode || h->current_frame_number <= 1)
            h->alpha_position *= 4;
    }

    if (h->mode) {
        if (h->alpha_position <= h->header_size)
            return AVERROR_INVALIDDATA;

        if (h->transparency) {
            int alpha_size;
            avio_seek(pb, h->alpha_position - h->header_size, SEEK_CUR);
            if (h->current_frame_number == 1) {
                alpha_size = parse_a9ll_alpha_size(s, h->width, h->height);
            } else {
                alpha_size = avio_rl32(pb);
                if (alpha_size < 4)
                    return AVERROR_INVALIDDATA;
            }
            return h->alpha_position + alpha_size;
        } else {
            return h->alpha_position;
        }
    }

    return avio_size(pb);
}

static int qmage_read_header(AVFormatContext *s)
{
    Header h;
    int ret;
    AVStream * st;

    ret = read_header(s, &h);
    if (ret < 0)
        return ret;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id   = AV_CODEC_ID_QMAGE;
    st->codecpar->width = h.width;
    st->codecpar->height = h.height;
    st->nb_frames = h.total_frame_number;
    avpriv_set_pts_info(st, 64, 1, 15);

    avio_seek(s->pb, 0, SEEK_SET);
    return 0;
}

static int qmage_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext *pb = s->pb;
    Header h;
    uint64_t pos;
    int size;

    pos = avio_tell(pb);
    size = read_header(s, &h);
    if (size < 0)
        return size;

    avio_seek(pb, pos, SEEK_SET);
    return av_get_packet(pb, pkt, size);
}

const FFInputFormat ff_qmage_demuxer = {
    .p.name         = "qmage",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Quram Qmage"),
    .p.flags        = AVFMT_GENERIC_INDEX,
    .read_probe     = qmage_probe,
    .read_header    = qmage_read_header,
    .read_packet    = qmage_read_packet,
};
