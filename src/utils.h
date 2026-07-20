#pragma once

#include <obs-avc.h>
#include <vector>

static std::string AvcCodecString(const std::vector<uint8_t> &avcc)
{
	uint8_t profile = 0x42, constraints = 0xe0, level = 0x1f;

	if (avcc.size() >= 4 && avcc[0] == 1) {
		profile = avcc[1];
		constraints = avcc[2];
		level = avcc[3];
	}

	char buf[16];
	snprintf(buf, sizeof(buf), "avc1.%02x%02x%02x", profile, constraints, level);
	return buf;
}

static std::vector<uint8_t> AnnexBToAvcC(const uint8_t *extra, size_t size)
{
	uint8_t *avcc = nullptr;
	size_t avcc_size = obs_parse_avc_header(&avcc, extra, size);
	if (!avcc || avcc_size == 0) {
		bfree(avcc);
		return {};
	}
	std::vector<uint8_t> out(avcc, avcc + avcc_size);
	bfree(avcc);
	return out;
}
