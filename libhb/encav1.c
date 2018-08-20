/* encav1.c

Copyright (c) 2003-2018 HandBrake Team
This file is part of the HandBrake source code
Homepage: <http://handbrake.fr/>.
It may be used under the terms of the GNU General Public License v2.
For full terms see the file COPYING file or visit http://www.gnu.org/licenses/gpl-2.0.html
*/

#include "hb.h"
#include "encav1.h"

int  encav1Init(hb_work_object_t *, hb_job_t *);
int  encav1Work(hb_work_object_t *, hb_buffer_t **, hb_buffer_t **);
void encav1Close(hb_work_object_t *);

hb_work_object_t hb_encav1 =
{
    WORK_ENCAV1,
    "av1 encoder (libaom)",
    encav1Init,
    encav1Work,
    encav1Close
};

struct hb_work_private_s
{
    hb_job_t    * job;

    char          * filename;
    unsigned char   stat_buf[80];
    int             stat_read;
    int             stat_fill;

    aom_codec_ctx_t * codec;
    aom_image_t * raw;
    aom_fixed_buf_t * twopass_stats;

    int frame_index;
};

int  encav1Init(hb_work_object_t * w, hb_job_t * job)
{
    // priv init
    hb_work_private_t * pv = calloc(1, sizeof(hb_work_private_t));
    w->private_data = pv;

    pv->job = job;

    pv->filename = hb_get_temporary_filename("av1.log");
    // End priv init

    const struct aom_codec_iface *iface = aom_codec_av1_cx();
    aom_codec_caps_t codec_caps = aom_codec_get_caps(iface);
    aom_codec_ctx_t codec;
    aom_codec_enc_cfg_t cfg = { 0 };
    aom_codec_flags_t flags = 0;
    aom_image_t raw;
    aom_img_fmt_t img_fmt = AOM_IMG_FMT_I420;
    aom_codec_err_t res;

    if ((res = aom_codec_enc_config_default(iface, &cfg, 0)) != AOM_CODEC_OK)
    {
        hb_error("Failed to get config: %s\n", aom_codec_err_to_string(res));
    }
    /* set_pix_fmt
    if (set_pix_fmt(avctx, codec_caps, &enccfg, &flags, &img_fmt))
    {
        hb_error("Failed to set pixel format");
    }

    if(!avctx->bit_rate)
    {
        if(avctx->rc_max_rate || avctx->rc_buffer_size || avctx->rc_initial_buffer_occupancy)
        {
            av_log( avctx, AV_LOG_ERROR, "Rate control parameters set without a bitrate\n");
            return AVERROR(EINVAL);
        }
    }
    */
    cfg.g_w = job->width;
    cfg.g_h = job->height;

    /* Some HandBrake-specific defaults; users can override them
    * using the encoder_options string. */
    // cfg.g_timebase.den = job->vrate.den;
    // cfg.g_timebase.num = job->vrate.num;

    if (job->cfr == 1)
    {
        cfg.g_timebase.num = 0;
        cfg.g_timebase.den = 0;
    }
    else
    {
        cfg.g_timebase.num = 1;
        cfg.g_timebase.den = 90000;
    }

    // Maximum number of threads to use
    // cfg.g_threads = avctx->thread_count;
    /* FFmpeg code
    // Allow lagged encoding // NOT FOUND
    if (ctx->lag_in_frames >= 0)
    {
        enccfg.g_lag_in_frames = ctx->lag_in_frames;
    }
    */
    // Multi-pass Encoding Mode
    if (job->pass_id & HB_PASS_ENCODE_1ST)
    {
        cfg.g_pass = AOM_RC_FIRST_PASS;
    }
    else if (job->pass_id & HB_PASS_ENCODE_2ND)
    {
        cfg.g_pass = AOM_RC_LAST_PASS;
    }
    else
    {
        cfg.g_pass = AOM_RC_ONE_PASS;
    }
        

    // Rate control algorithm to use
    // !!! check this out
    cfg.rc_end_usage = AOM_CBR;
    if (job->vquality > HB_INVALID_VIDEO_QUALITY)
    {
        // Constant
        cfg.rc_end_usage = AOM_CQ;
        if (!job->vbitrate)
        {
            cfg.rc_end_usage = AOM_Q;
        }
    }
    else
    {
        cfg.rc_end_usage = AOM_VBR;
        cfg.rc_target_bitrate = job->vbitrate; // ?
    }
    //cfg.rc_end_usage = AOM_Q;
/*	if (avctx->rc_min_rate == avctx->rc_max_rate &&
        avctx->rc_min_rate == avctx->bit_rate && avctx->bit_rate) 
    {
        enccfg.rc_end_usage = AOM_CBR;
    } else if (ctx->crf >= 0) {
        enccfg.rc_end_usage = AOM_CQ;
        if (!avctx->bit_rate)
            enccfg.rc_end_usage = AOM_Q;
    }
*/


/*
    if (avctx->bit_rate) {
    enccfg.rc_target_bitrate = av_rescale_rnd(avctx->bit_rate, 1, 1000,
    AV_ROUND_NEAR_INF);
    } else if (enccfg.rc_end_usage != AOM_Q) {
    if (enccfg.rc_end_usage == AOM_CQ) {
    enccfg.rc_target_bitrate = 1000000;
    } else {
    avctx->bit_rate = enccfg.rc_target_bitrate * 1000;
    av_log(avctx, AV_LOG_WARNING,
    "Neither bitrate nor constrained quality specified, using default bitrate of %dkbit/sec\n",
    enccfg.rc_target_bitrate);
    }
    }

    if (avctx->qmin >= 0)
    enccfg.rc_min_quantizer = avctx->qmin;
    if (avctx->qmax >= 0)
    enccfg.rc_max_quantizer = avctx->qmax;

    if (enccfg.rc_end_usage == AOM_CQ || enccfg.rc_end_usage == AOM_Q) {
    if (ctx->crf < enccfg.rc_min_quantizer || ctx->crf > enccfg.rc_max_quantizer) {
    av_log(avctx, AV_LOG_ERROR,
    "CQ level %d must be between minimum and maximum quantizer value (%d-%d)\n",
    ctx->crf, enccfg.rc_min_quantizer, enccfg.rc_max_quantizer);
    return AVERROR(EINVAL);
    }
    }
    */

    // Enable error resilient modes
    cfg.g_error_resilient = 1;
    // !!! important 
    cfg.g_lag_in_frames = 0;

    cfg.g_bit_depth = AOM_BITS_8;

    if (aom_codec_enc_init(&codec, iface, &cfg, flags))
    {
        hb_error(&codec, "Failed to initialize encoder");
    }



    // img init
    // frame->format           = buf->f.fmt;
    if (!aom_img_alloc(&raw, img_fmt, cfg.g_w, cfg.g_h, 1))
    {
        hb_error("Failed to allocate image.");
    }

    /*
    // provide dummy value to initialize wrapper, values will be updated each _encode()
    aom_img_wrap(&raw, img_fmt, cfg.g_w, cfg.g_h, 1,
        (unsigned char*)1);*/
    // End img init

    pv->codec = &codec;
    pv->raw = &raw;
    pv->frame_index = 0;

    // Headers
    /* для начала их всё равно стоит заполнить :\
    aom_fixed_buf_t* aom_header = aom_codec_get_global_headers(&codec);
    w->config->av1.seq_length = aom_header->sz;
    memcpy(w->config->av1.seq, aom_header->buf, aom_header->sz);
    */

    // w->config initialization
    int frame_cnt = 0;
    
    int header_length = w->config->av1.seq_length = 32;
    char header[header_length];

    header[0] = 'D';
    header[1] = 'K';
    header[2] = 'I';
    header[3] = 'F';

    mem_put_le16(header + 4, 0);                    // version
    mem_put_le16(header + 6, 32);                   // header size
    mem_put_le32(header + 8, AV1_FOURCC);           // fourcc
    mem_put_le16(header + 12, cfg.g_w);             // width
    mem_put_le16(header + 14, cfg.g_h);             // height
    mem_put_le32(header + 16, cfg.g_timebase.den);  // rate
    mem_put_le32(header + 20, cfg.g_timebase.num);  // scale
    mem_put_le32(header + 24, frame_cnt);           // length
    mem_put_le32(header + 28, 0);                   // unused
    
    memcpy(w->config->av1.seq, header, header_length);

    // End Headers

    return 0;
}

int get_aom_img(aom_image_t *img, hb_buffer_t *in)
{
    int plane;

    if (in)
    {
        /*
        img->planes[AOM_PLANE_Y] = in->plane[0].data;
        img->planes[AOM_PLANE_U] = in->plane[1].data;
        img->planes[AOM_PLANE_V] = in->plane[2].data;
        */
        for (plane = 0; plane < 3; ++plane)
        {
            uint8_t * data = in->plane[plane].data;
            unsigned char *buf = img->planes[plane];
            const int stride = img->stride[plane];
            // !!! - AOM_IMG_FMT_HIGHBITDEPTH - one of supported types. Mb must be more then one?
            const int w = aom_img_plane_width(img, plane) *
                ((img->fmt & AOM_IMG_FMT_HIGHBITDEPTH) ? 2 : 1);
            const int h = aom_img_plane_height(img, plane);

            int y;
            for (y = 0; y < h; ++y)
            {
                memcpy(buf, data, w);
                data += w;
                buf += stride;
            }
        }
        img->range = AOM_CR_STUDIO_RANGE;
        /*
        switch (frame->color_range)
        {
        case AVCOL_RANGE_MPEG:
            img->range = AOM_CR_STUDIO_RANGE;
            break;
        case AVCOL_RANGE_JPEG:
            img->range = AOM_CR_FULL_RANGE;
            break;
        }
        */
    }
    return 0;
}

static hb_buffer_t * encode_frame(aom_codec_ctx_t *codec, aom_image_t *img,
    int frame_index, int flags, int *pkts, hb_buffer_t *in)
{
    aom_codec_iter_t iter = NULL;
    const aom_codec_cx_pkt_t *pkt = NULL;
    const aom_codec_err_t res = aom_codec_encode(codec, img, frame_index, 1, flags);

    if (res != AOM_CODEC_OK)
    {
        hb_error(codec, "Failed to encode frame");
    }

    // buffer init
    hb_buffer_t * buf = NULL;
    buf = hb_video_buffer_init(codec->config.enc->g_w, codec->config.enc->g_h);
    buf->size = 0;
    buf->s.frametype = HB_FRAME_I;

    buf->s.start = in->s.start;
    buf->s.stop = in->s.stop;
    buf->s.duration = in->s.stop - in->s.start;
    
    buf->s.flags = HB_FLAG_FRAMETYPE_REF;
    if (flags == AOM_EFLAG_FORCE_KF)
    {
        buf->s.flags |= HB_FLAG_FRAMETYPE_KEY;
    }
    buf->s.renderOffset = in->s.start;

    while ((pkt = aom_codec_get_cx_data(codec, &iter)) != NULL)
    {
        (*pkts)++;
        switch (pkt->kind) {
        case AOM_CODEC_CX_FRAME_PKT:
        {
            memcpy(buf->data + buf->size, (uint8_t *)pkt->data.frame.buf, pkt->data.frame.sz);
            buf->size += pkt->data.frame.sz;

            break;
        }
        case AOM_CODEC_STATS_PKT:
        {
            break;
        }
        case AOM_CODEC_PSNR_PKT: // FIXME add support for AV_CODEC_FLAG_PSNR
        case AOM_CODEC_CUSTOM_PKT:
            // ignore unsupported/unrecognized packet types
            break;
        }

    }

    return buf;
}


/***********************************************************************
* Close
***********************************************************************
*
**********************************************************************/
void encav1Close( hb_work_object_t * w )
{
    hb_work_private_t * pv = w->private_data;

    if (pv == NULL)
    {
        // Not initialized
        return;
    }
    /*
    aom_img_free(pv->raw);
    if (aom_codec_destroy(pv->codec))
    {
        hb_log("Failed to destroy codec.");
    }
    */
    free(pv->filename);
    free(pv);
    w->private_data = NULL;
}

/***********************************************************************
* Work
***********************************************************************
*
**********************************************************************/


int  encav1Work(hb_work_object_t * w, hb_buffer_t ** buf_in,
    hb_buffer_t ** buf_out)
{
    hb_log(">>>>>>>>>>>>>>>>>>> encav1Work");
    
    hb_work_private_t *pv = w->private_data;
    hb_buffer_t *in = *buf_in;

    *buf_out = NULL;

    int got_pkts = 0;

    if (in->s.flags & HB_BUF_FLAG_EOF)
    {
        hb_buffer_list_t list;
        hb_buffer_list_clear(&list);

        // flush
        do
        {
            encode_frame(pv->codec, NULL, -1, 0, &got_pkts, in);
        } while (got_pkts != 0);

        // add the EOF to the end of the chain
        hb_buffer_list_append(&list, in);

        *buf_out = hb_buffer_list_clear(&list);
        *buf_in = NULL;
        return HB_WORK_DONE;
    }
    // hb_buffer_t -> aom_image_t
    get_aom_img(pv->raw, in);

    // encode aom_image_t
    int flags = 0;
    // AV_PICTURE_TYPE_I
    if (in->s.frametype == HB_FRAME_I)
    {
        flags |= AOM_EFLAG_FORCE_KF;
    }

    *buf_out = encode_frame(pv->codec, pv->raw, pv->frame_index++, flags, &got_pkts, in);
    return HB_WORK_OK;
}
