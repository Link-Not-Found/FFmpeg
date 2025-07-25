/*
 * Copyright (c) 2005 Francois Revol
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

#include "config.h"
#include <limits.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_IO_H
#include <io.h>
#endif

#include "libavutil/avstring.h"
#include "libavutil/time.h"
#include "libavformat/avformat.h"

#define FILENAME_BUF_SIZE 4096
#define PKTFILESUFF "_%08" PRId64 "_%02d_%010" PRId64 "_%06d_%c.bin"
#define EXTRADATAFILESUFF "_extradata_%02d_%06d.bin"

static int usage(int ret)
{
    fprintf(stderr, "Dump (up to maxpkts) AVPackets as they are demuxed by libavformat.\n");
    fprintf(stderr, "Each packet is dumped in its own file named like\n");
    fprintf(stderr, "$(basename file.ext)_$PKTNUM_$STREAMINDEX_$STAMP_$SIZE_$FLAGS.bin\n");
    fprintf(stderr, "pktdumper [-nw] file [maxpkts]\n");
    fprintf(stderr, "-n\twrite No file at all, only demux.\n");
    fprintf(stderr, "-w\tWait at end of processing instead of quitting.\n");
    return ret;
}

int main(int argc, char **argv)
{
    char fntemplate[FILENAME_BUF_SIZE];
    char fntemplate2[FILENAME_BUF_SIZE];
    char pktfilename[FILENAME_BUF_SIZE];
    AVFormatContext *fctx = NULL;
    AVPacket *pkt;
    int64_t pktnum  = 0;
    int64_t maxpkts = 0;
    int donotquit   = 0;
    int nowrite     = 0;
    int err;

    if ((argc > 1) && !strncmp(argv[1], "-", 1)) {
        if (strchr(argv[1], 'w'))
            donotquit = 1;
        if (strchr(argv[1], 'n'))
            nowrite = 1;
        argv++;
        argc--;
    }
    if (argc < 2)
        return usage(1);
    if (argc > 2)
        maxpkts = atoi(argv[2]);
    av_strlcpy(fntemplate, argv[1], sizeof(fntemplate));
    if (strrchr(argv[1], '/'))
        av_strlcpy(fntemplate, strrchr(argv[1], '/') + 1, sizeof(fntemplate));
    if (strrchr(fntemplate, '.'))
        *strrchr(fntemplate, '.') = '\0';
    if (strchr(fntemplate, '%')) {
        fprintf(stderr, "cannot use filenames containing '%%'\n");
        return usage(1);
    }
    if (strlen(fntemplate) + sizeof(PKTFILESUFF) >= sizeof(fntemplate) - 1) {
        fprintf(stderr, "filename too long\n");
        return usage(1);
    }

    err = avformat_open_input(&fctx, argv[1], NULL, NULL);
    if (err < 0) {
        fprintf(stderr, "cannot open input: error %d\n", err);
        return 1;
    }

    err = avformat_find_stream_info(fctx, NULL);
    if (err < 0) {
        fprintf(stderr, "avformat_find_stream_info: error %d\n", err);
        return 1;
    }

    pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "av_packet_alloc: error %d\n", AVERROR(ENOMEM));
        return 1;
    }

    strcpy(fntemplate2, fntemplate);
    strcat(fntemplate2, EXTRADATAFILESUFF);

    for (int i = 0; i < fctx->nb_streams; i++) {
        AVCodecParameters * par = fctx->streams[i]->codecpar;
        int fd;
        if (par->extradata_size) {
            snprintf(pktfilename, sizeof(pktfilename), fntemplate2, i, par->extradata_size);
            printf(EXTRADATAFILESUFF "\n", i, par->extradata_size);
            if (!nowrite) {
                fd  = open(pktfilename, O_WRONLY | O_CREAT, 0644);
                err = write(fd, par->extradata, par->extradata_size);
                if (err < 0) {
                    fprintf(stderr, "write: error %d\n", err);
                    return 1;
                }
                close(fd);
            }
        }
    }

    strcat(fntemplate, PKTFILESUFF);
    printf("FNTEMPLATE: '%s'\n", fntemplate);

    while ((err = av_read_frame(fctx, pkt)) >= 0) {
        int fd;
        snprintf(pktfilename, sizeof(pktfilename), fntemplate, pktnum,
                 pkt->stream_index, pkt->pts, pkt->size,
                 (pkt->flags & AV_PKT_FLAG_KEY) ? 'K' : '_');
        printf(PKTFILESUFF "\n", pktnum, pkt->stream_index, pkt->pts, pkt->size,
               (pkt->flags & AV_PKT_FLAG_KEY) ? 'K' : '_');
        if (!nowrite) {
            fd  = open(pktfilename, O_WRONLY | O_CREAT, 0644);
            err = write(fd, pkt->data, pkt->size);
            if (err < 0) {
                fprintf(stderr, "write: error %d\n", err);
                return 1;
            }
            close(fd);
        }
        av_packet_unref(pkt);
        pktnum++;
        if (maxpkts && (pktnum >= maxpkts))
            break;
    }

    av_packet_free(&pkt);
    avformat_close_input(&fctx);

    while (donotquit)
        av_usleep(60 * 1000000);

    return 0;
}
