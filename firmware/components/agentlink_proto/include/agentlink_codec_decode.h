/*
 * Generated using zcbor version 0.9.1
 * https://github.com/NordicSemiconductor/zcbor
 * Generated with a --default-max-qty of 3
 */

#ifndef AGENTLINK_CODEC_DECODE_H__
#define AGENTLINK_CODEC_DECODE_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "agentlink_codec_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#if DEFAULT_MAX_QTY != 3
#error "The type file was generated with a different default_max_qty than this file"
#endif


int cbor_decode_envelope(
		const uint8_t *payload, size_t payload_len,
		struct envelope_r *result,
		size_t *payload_len_out);


#ifdef __cplusplus
}
#endif

#endif /* AGENTLINK_CODEC_DECODE_H__ */
