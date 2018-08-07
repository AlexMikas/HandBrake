/* encav1.h

Copyright (c) 2003-2018 HandBrake Team
This file is part of the HandBrake source code
Homepage: <http://handbrake.fr/>.
It may be used under the terms of the GNU General Public License v2.
For full terms see the file COPYING file or visit http://www.gnu.org/licenses/gpl-2.0.html
*/

#ifndef HB_ENCAV1_H
#define HB_ENCAV1_H

#include "aom/aom_encoder.h"
#include "aom/aomcx.h"

#define AV1_FOURCC 0x31305641

#ifndef MAU_T
/* Minimum Access Unit for this target */
#define MAU_T unsigned char
#endif

#ifndef MEM_VALUE_T
#define MEM_VALUE_T int
#endif

typedef struct AvxInterface {
	const char *const name;
	const uint32_t fourcc;
	aom_codec_iface_t *(*const codec_interface)();
} AvxInterface;

static const AvxInterface aom_encoders[] = {
	{ "av1", AV1_FOURCC, &aom_codec_av1_cx },
};

int get_aom_encoder_count(void) {
	return sizeof(aom_encoders) / sizeof(aom_encoders[0]);
}

const AvxInterface *get_aom_encoder_by_index(int i) { return &aom_encoders[i]; }

const AvxInterface *get_aom_encoder_by_name(const char *name) {
	int i;

	for (i = 0; i < get_aom_encoder_count(); ++i) {
		const AvxInterface *encoder = get_aom_encoder_by_index(i);
		if (strcmp(encoder->name, name) == 0) return encoder;
	}

	return NULL;
}

void mem_put_le16(void *vmem, MEM_VALUE_T val) {
	MAU_T *mem = (MAU_T *)vmem;

	mem[0] = (MAU_T)((val >> 0) & 0xff);
	mem[1] = (MAU_T)((val >> 8) & 0xff);
}

void mem_put_le32(void *vmem, MEM_VALUE_T val) {
	MAU_T *mem = (MAU_T *)vmem;

	mem[0] = (MAU_T)((val >> 0) & 0xff);
	mem[1] = (MAU_T)((val >> 8) & 0xff);
	mem[2] = (MAU_T)((val >> 16) & 0xff);
	mem[3] = (MAU_T)((val >> 24) & 0xff);
}

#endif // HB_ENCAV1_H
