#pragma once
#include <obs-module.h>

#include <mutex>
#include <thread>
#include <atomic>
#include <string>
#include <vector>

#include <moq/url.h>
#include <moq/rcbuf.h>
#include <moq/media_object.h>
#include <moq/media_sender.h>

struct video_config {
	uint32_t video_width;
	uint32_t video_height;
	uint32_t fps_num;
	uint32_t fps_den;
	uint64_t bitrate;
};

class MOQOutput {
public:
	MOQOutput(obs_data_t *settings, obs_output_t *output);
	~MOQOutput();

	bool Start();
	void Stop(bool signal = true);
	void Data(struct encoder_packet *packet);

	inline size_t GetTotalBytes() { return total_bytes_sent; }

	inline int GetConnectTime() { return connect_time_ms; }

private:
	void StartThread();
	void SplitNamespace();
	bool LoadVideoEncoderSettings();
	bool CreateVideoTrack(moq_media_sender_t *new_sender);
	bool ResolveServiceConfig();
	bool Connect();

	static void OnReady(void *ctx, moq_media_sender_t *sender);
	static void OnClosed(void *ctx, moq_media_sender_t *sender, bool is_fatal, uint64_t fatal_code);
	static void OnTrackClosed(void *ctx, moq_media_sender_t *sender, moq_media_track_t *track);

	obs_output_t *output;

	std::mutex start_stop_mutex;
	std::mutex sender_mutex;
	std::thread start_stop_thread;

	std::atomic<size_t> total_bytes_sent;
	std::atomic<int> connect_time_ms;
	int64_t start_time_ns = 0;

	std::atomic<bool> stopping;
	std::atomic<bool> running;
	std::atomic<bool> got_ready;

	video_config video_conf;

	std::vector<uint8_t> init_data;
	std::string codec_string;

	std::string url;
	moq_namespace_t namespace_val;

	std::string stream_key;
	std::vector<moq_bytes_t> ns_bytes;

	moq_media_sender_t *sender = nullptr;
	moq_media_track_t *video_track = nullptr;
};

void register_moq_output();