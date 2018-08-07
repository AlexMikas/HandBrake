/* encav1.c

Copyright (c) 2003-2018 HandBrake Team
This file is part of the HandBrake source code
Homepage: <http://handbrake.fr/>.
It may be used under the terms of the GNU General Public License v2.
For full terms see the file COPYING file or visit http://www.gnu.org/licenses/gpl-2.0.html
*/

#include "hb.h"
#include "encav1.h"
//#include "tools_common.h"
//#include "aom/common/video_writer.h"

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

	FILE          * filename;
	unsigned char   stat_buf[80];
	int             stat_read;
	int             stat_fill;

	aom_codec_ctx_t * codec;
	aom_image_t * raw;
	int frame_index;
};

int  encav1Init( hb_work_object_t * w, hb_job_t * job )
{
	hb_log(">>>>>>>>>>>>>>>>>>> encav1Init");

	hb_work_private_t * pv = calloc(1, sizeof(hb_work_private_t));
	w->private_data = pv;

	pv->job = job;

	pv->filename = hb_get_temporary_filename("av1.log");

	aom_codec_ctx_t codec;
	aom_codec_enc_cfg_t cfg;
	int frame_count = 0;
	aom_image_t raw;
	aom_codec_err_t res;
	//AvxVideoInfo info;
	//AvxVideoWriter *writer = NULL;
	const AvxInterface *encoder = NULL;
	const int fps = 30;
	const int bitrate = 200;
	int keyframe_interval = 0;
	//int max_frames = 0;
	int frames_encoded = 0;
	const char *codec_arg = NULL;
	int frame_width = NULL;
	int frame_height = NULL;
	const char *keyframe_interval_arg = NULL;

	codec_arg = w->name; // enc name
	frame_width = job->width;
	frame_height = job->height;

	hb_log(codec_arg);

	if (codec_arg == "av1 encoder (libaom)")
	{
		encoder = get_aom_encoder_by_name("av1");
	}
	if (!encoder)
	{
		hb_error("Unsupported codec.");
	}

	if (frame_width <= 0 || frame_height <= 0 ||
		(frame_width % 2) != 0 || (frame_height % 2) != 0)
	{
		hb_error("Invalid frame size: %dx%d", frame_width, frame_height);
	}
	// !!! - AOM_IMG_FMT_I420 - one of supported img formats. Mb here must be a choice of it?
	// mb smth like "findImgFormatByNane"
	if (!aom_img_alloc(&raw, AOM_IMG_FMT_I420, frame_width,
		frame_height, 1))
	{
		hb_error("Failed to allocate image.");
	}

	//keyframe_interval = (int)strtol(keyframe_interval_arg, NULL, 0);
	//if (keyframe_interval < 0) die("Invalid keyframe interval value.");

	printf("Using %s\n", aom_codec_iface_name(encoder->codec_interface()));

	res = aom_codec_enc_config_default(encoder->codec_interface(), &cfg, 0);
	if (res)
	{
		hb_error(&codec, "Failed to get default codec config.");
	}

	cfg.g_w = frame_width;
	cfg.g_h = frame_height;
	cfg.g_timebase.num = 1;
	cfg.g_timebase.den = fps;
	cfg.rc_target_bitrate = bitrate;

	if (aom_codec_enc_init(&codec, encoder->codec_interface(), &cfg, 0))
	{
		hb_error(&codec, "Failed to initialize encoder");
	}

	pv->codec = &codec;
	pv->raw = &raw;
	pv->frame_index = 0;
	// w->config initialization
	int frame_cnt = 0;
	int header_length = w->config->av1.seq_length = 32;

	char header[header_length];

	header[0] = 'D';
	header[1] = 'K';
	header[2] = 'I';
	header[3] = 'F';

	mem_put_le16(header + 4, 0);                     // version
	mem_put_le16(header + 6, 32);                    // header size
	mem_put_le32(header + 8, encoder->fourcc);       // fourcc
	mem_put_le16(header + 12, cfg.g_w);             // width
	mem_put_le16(header + 14, cfg.g_h);             // height
	mem_put_le32(header + 16, cfg.g_timebase.den);  // rate
	mem_put_le32(header + 20, cfg.g_timebase.num);  // scale
	mem_put_le32(header + 24, frame_cnt);            // length
	mem_put_le32(header + 28, 0);                    // unused
	
	memcpy(w->config->av1.seq, header, header_length);

	return 0;
}

int get_aom_img(aom_image_t *img, hb_buffer_t *in)
{
	int plane;

	for (plane = 0; plane < 3; ++plane)
	{
		unsigned char *buf = img->planes[plane];
		const int stride = img->stride[plane];
		// !!! - AOM_IMG_FMT_HIGHBITDEPTH - one of supported types. Mb must be more then one?
		const int w = aom_img_plane_width(img, plane) *
			((img->fmt & AOM_IMG_FMT_HIGHBITDEPTH) ? 2 : 1);
		const int h = aom_img_plane_height(img, plane);
		/*
		int y;

		for (y = 0; y < h; ++y)
		{
		//if (fread(buf, 1, w, file) != (size_t)w) return 0;

		buf += stride;
		}
		*/
		memcpy(buf, *in->plane[plane].data, w*h); // just thinks
	}

	return 0;
}

static hb_buffer_t * encode_frame(aom_codec_ctx_t *codec, aom_image_t *img,
	int frame_index, int flags)
{
	aom_codec_iter_t iter = NULL;
	const aom_codec_cx_pkt_t *pkt = NULL;
	const aom_codec_err_t res = aom_codec_encode(codec, img, frame_index, 1, flags);

	if (res != AOM_CODEC_OK)
	{
		hb_error(codec, "Failed to encode frame");
	}

	hb_buffer_t buf_res;
	while ((pkt = aom_codec_get_cx_data(codec, &iter)) != NULL)
	{
		
		// !!! - and another one place with this img type 
		if (pkt->kind == AOM_CODEC_CX_FRAME_PKT)
		{
			const int keyframe = (pkt->data.frame.flags & AOM_FRAME_IS_KEY) != 0;
			// here must be converter to hb_buffer_t
			// UPD0: pkt->data.frame.buf - must be our buf. Need we write down header of frame?
			// UPD1: if we have "while" we need do smth like this: buf_res += pkt->data.frame.buf
			// cuz it looks like we compute buffer in parts.

			/*
			if (!aom_video_writer_write_frame(writer, pkt->data.frame.buf,
				pkt->data.frame.sz,
				pkt->data.frame.pts)) 
			{
				die_codec(codec, "Failed to write compressed frame");
			}
			*/
			*buf_res.data += *(uint8_t *)pkt->data.frame.buf;

		}
	}
	return &buf_res;
}


/***********************************************************************
* Close
***********************************************************************
*
**********************************************************************/
void encav1Close( hb_work_object_t * w )
{
	hb_log(">>>>>>>>>>>>>>>>>>> encav1Close");

	hb_work_private_t * pv = w->private_data;

	if (pv == NULL)
	{
		// Not initialized
		return;
	}

	// Flush encoder.
	while (encode_frame(&pv->codec, NULL, -1, 0)) continue;

	aom_img_free(&pv->raw);
	if (aom_codec_destroy(&pv->codec))
	{
		hb_log("Failed to destroy codec.");
	}

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

	// hb_buffer_t -> aom_image_t
	get_aom_img(pv->raw, in);
	// encode aom_image_t
	int flags = 0;
	
	flags |= AOM_EFLAG_FORCE_KF;
	
	buf_out = encode_frame(pv->codec, pv->raw, pv->frame_index++, flags);
	// aom_image_t -> hb_buffer_t

	return HB_WORK_OK;
}
