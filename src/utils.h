#pragma once

#include <moq/codec_string.h>
#include <string>
#include <vector>

/* Build a codec's init_data (decoder configuration record, e.g. an avcC from
 * Annex B SPS/PPS, or an AudioSpecificConfig passed through) via moq5's
 * codec utility into `out`. Returns MOQ_OK or the underlying error. */
static moq_result_t BuildInitData(const moq_codec_init_data_cfg_t *cfg, std::vector<uint8_t> &out)
{
	size_t need = 0;
	moq_result_t rc = moq_codec_init_data_build(cfg, nullptr, 0, &need);
	if (rc != MOQ_ERR_BUFFER)
		return rc; /* MOQ_ERR_INVAL / MOQ_ERR_PROTO / MOQ_ERR_UNSUPPORTED */

	out.resize(need);
	return moq_codec_init_data_build(cfg, out.data(), out.size(), &need);
}

/* Format an MSF/WebCodecs codec string via moq5's codec utility into `out`.
 * Returns MOQ_OK or the underlying error. */
static moq_result_t CodecString(const moq_codec_string_cfg_t *cfg, std::string &out)
{
	size_t need = 0;
	moq_result_t rc = moq_codec_string_format(cfg, nullptr, 0, &need);
	if (rc != MOQ_ERR_BUFFER)
		return rc; /* MOQ_ERR_INVAL / MOQ_ERR_PROTO / MOQ_ERR_UNSUPPORTED */

	out.resize(need);
	return moq_codec_string_format(cfg, reinterpret_cast<uint8_t *>(out.data()), out.size(), &need);
}
