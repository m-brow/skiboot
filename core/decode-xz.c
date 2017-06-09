/* Copyright 2017 IBM Corp.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * 	http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <skiboot.h>
#include <platform.h>
#include <libxz/xz.h>


#define HEADER_MAGIC "\3757zXZ"
#define HEADER_MAGIC_SIZE 6
/*
 * decode_resource_xz
 * Decodes any xz compressed memory region.
 *
 * @buf : pointer to memory region pointer
 * @len : pointer to length of compressed region
 * @uncomp_len: pointer to uncompressed length (optional)
 *
 * Returns 1 - successful or 0 - failed 
 */
int decode_resource_xz(void **buf, size_t *len, size_t *uncomp_len)
{
	struct xz_dec *file;
	struct xz_buf bufs;
	char *input_header;
	uint64_t *out_len;
	enum xz_ret rc;
	
	uint8_t *r_buf = (uint8_t *)(*buf);
	size_t   r_len = *len;

	/* Check if input header matched XZ encoding signature */
	input_header = malloc(HEADER_MAGIC_SIZE);
	if (!input_header)
		return 0;

	memcpy(input_header, r_buf, HEADER_MAGIC_SIZE);
	if (strcmp(input_header, HEADER_MAGIC)){
		prlog(PR_PRINTF, "DECODE: resource header magic does not match\
				  xz format\n");
		free(input_header);
		return 0;
	}


	/* Set up decoder */
	file = xz_dec_init(XZ_SINGLE, 0x100000);

	if (!file) {
		/* Allocation failed */
		prlog(PR_PRINTF, "DECODE: xz_dec_init allocation error\n ");
		free(input_header);
		return 0;
	}

	out_len = (uint64_t *)malloc(sizeof(uint64_t));
	if (!uncomp_len)
       		*out_len = (r_len) * 10;
	else
		*out_len = *uncomp_len;

	bufs.in       = (const uint8_t *)r_buf;
	bufs.in_pos   = 0;
	bufs.in_size  = r_len;
	bufs.out      = (uint8_t *)local_alloc(0, *out_len, 0x10000);
	bufs.out_pos  = 0;
	bufs.out_size = *(size_t *)out_len;

	if (bufs.out == NULL) {
		/* Buffer allocation failed */
		prlog(PR_PRINTF, "DECODE: bufs.out allocation error\n ");
		free(out_len);
		free(input_header);
		return 0;
	}

	rc = xz_dec_run(file, &bufs);

	if (rc != XZ_STREAM_END) {
		prlog(PR_ALERT, "DECODE: XZ decompression failed rc:%d\n", rc);
		free(bufs.out);
		free(out_len);
		free(input_header);
		return 0;
	}

	/* Redefine resource base and size */
	*buf = (void *)bufs.out;
	*len = (size_t)*out_len;

	prlog(PR_PRINTF, "DECODE: decode_resource_xz base: %p, len: %llu \
			remote: %p\n", bufs.out, *out_len, *buf);

	xz_dec_end(file);
	free(out_len);
	free(input_header);

	return 1;
}
