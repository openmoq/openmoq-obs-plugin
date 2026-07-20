#include "moq-output.h"
#include "utils.h"

#include <util/platform.h>
#include <obs.hpp>

#define VIDEO_TIMESCALE 1000000u

MOQOutput::MOQOutput(obs_data_t *settings, obs_output_t *output) : output(output)
{
	blog(LOG_INFO, "[obs-moq] output created");
}

MOQOutput::~MOQOutput()
{
	blog(LOG_INFO, "[obs-moq] output destroying");
	Stop(false);

	std::lock_guard<std::mutex> lock(start_stop_mutex);
	if (start_stop_thread.joinable()) {
		start_stop_thread.join();
	}
}

void MOQOutput::SplitNamespace()
{
	ns_bytes.clear();
	const char *base = stream_key.data();
	size_t start = 0;
	for (size_t i = 0; i <= stream_key.size(); ++i) {
		bool sep = (i == stream_key.size()) || stream_key[i] == '-';
		if (sep) {
			if (i > start)
				ns_bytes.push_back({(const uint8_t *)(base + start), i - start});
			start = i + 1;
		}
	}
	namespace_val.parts = ns_bytes.data();
	namespace_val.count = ns_bytes.size();
}

bool MOQOutput::LoadVideoEncoderSettings()
{
	obs_encoder_t *venc = obs_output_get_video_encoder(output);
	if (!venc) {
		blog(LOG_WARNING, "[obs-moq] no video encoder assigned");
		obs_output_set_last_error(output, obs_module_text("Error.NoEncoder"));
		return false;
	}

	OBSDataAutoRelease settings = obs_encoder_get_settings(venc);
	video_conf.bitrate = (uint64_t)obs_data_get_int(settings, "bitrate") * 1000;

	const char *codec = obs_encoder_get_codec(venc);
	//todo: add codec validation here

	video_conf.video_width = obs_encoder_get_width(venc);
	video_conf.video_height = obs_encoder_get_height(venc);

	struct obs_video_info ovi = {};
	obs_get_video_info(&ovi);
	video_conf.fps_num = ovi.fps_num;
	video_conf.fps_den = ovi.fps_den;

	//initialize init_data
	uint8_t *extra = nullptr;
	size_t extra_size = 0;
	obs_encoder_get_extra_data(venc, &extra, &extra_size);

	init_data = AnnexBToAvcC(extra, extra_size);
	codec_string = AvcCodecString(init_data);
	return true;
}

bool MOQOutput::ResolveServiceConfig()
{
	url.clear();
	stream_key.clear();

	obs_service_t *service = obs_output_get_service(output);
	if (!service) {
		obs_output_signal_stop(output, OBS_OUTPUT_ERROR);
		return false;
	}

	const char *server = obs_service_get_connect_info(service, OBS_SERVICE_CONNECT_INFO_SERVER_URL);
	if (server && *server) {
		url = server;
		blog(LOG_INFO, "[obs-moq] using URL from service: %s", url.c_str());
	} else {
		blog(LOG_WARNING, "[obs-moq] no relay URL configured in the service");
		obs_output_set_last_error(output, obs_module_text("Error.NoURL"));
		obs_output_signal_stop(output, OBS_OUTPUT_BAD_PATH);
		return false;
	}

	const char *key = obs_service_get_connect_info(service, OBS_SERVICE_CONNECT_INFO_STREAM_KEY);
	if (key && *key) {
		// TODO: erase this log line after testing
		blog(LOG_INFO, "[obs-moq] using Namespace from service: %s", key);
		stream_key = key;
	} else {
		blog(LOG_WARNING, "[obs-moq] no stream key configured in the service");
		obs_output_set_last_error(output, obs_module_text("Error.NoKey"));
		obs_output_signal_stop(output, OBS_OUTPUT_BAD_PATH);
		return false;
	}

	SplitNamespace();

	return true;
}

bool MOQOutput::CreateVideoTrack(moq_media_sender_t *new_sender)
{
	moq_media_track_cfg_t tcfg;
	moq_media_track_cfg_init(&tcfg);
	tcfg.name = {(const uint8_t *)"video", 5};
	tcfg.media_type = MOQ_MEDIA_TYPE_VIDEO;
	// todo: make this configurable and add CMAF support
	tcfg.packaging = MOQ_MEDIA_PACKAGING_RAW;
	tcfg.codec = {(const uint8_t *)codec_string.c_str(), codec_string.size()};
	tcfg.timescale = VIDEO_TIMESCALE;
	// todo: analyze actual need for this and how it fits w/other codecs
	tcfg.init_data = {init_data.data(), init_data.size()};
	tcfg.is_live = true;
	tcfg.width = video_conf.video_width;
	tcfg.height = video_conf.video_height;
	tcfg.framerate_millis = video_conf.fps_num * 1000 / video_conf.fps_den;
	tcfg.bitrate = video_conf.bitrate;

	moq_media_track_t *new_track = nullptr;
	moq_result_t result = moq_media_sender_add_track(new_sender, &tcfg, &new_track);
	if (result != MOQ_OK) {
		blog(LOG_WARNING, "[obs-moq] moq_media_sender_add_track failed");
		obs_output_signal_stop(output, OBS_OUTPUT_ERROR);
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(sender_mutex);
		sender = new_sender;
		video_track = new_track;
	}

	return true;
}

void MOQOutput::OnReady(void *ctx, moq_media_sender_t *sender)
{
	MOQOutput *self = static_cast<MOQOutput *>(ctx);

	if (self->stopping.load()) {
		blog(LOG_INFO, "[obs-moq] session ready ignored: stop in progress");
		return;
	}

	self->connect_time_ms.store((int)((os_gettime_ns() - self->start_time_ns) / 1000000.0));
	if (!obs_output_begin_data_capture(self->output, 0)) {
		blog(LOG_WARNING, "[obs-moq] obs_output_begin_data_capture failed");
		return;
	}
	self->running.store(true);
	self->got_ready.store(true);
}

void MOQOutput::OnClosed(void *ctx, moq_media_sender_t *sender, bool is_fatal, uint64_t fatal_code)
{
	MOQOutput *self = static_cast<MOQOutput *>(ctx);
	if (self->running.exchange(false)) {
		obs_output_signal_stop(self->output, OBS_OUTPUT_DISCONNECTED);
	} else if (!self->got_ready.load() && !self->stopping.load()) {
		obs_output_set_last_error(self->output, obs_module_text("Error.Connect"));
		obs_output_signal_stop(self->output, OBS_OUTPUT_CONNECT_FAILED);
	}
}

void MOQOutput::OnTrackClosed(void *ctx, moq_media_sender_t *sender, moq_media_track_t *track)
{
	MOQOutput *self = static_cast<MOQOutput *>(ctx);
	if (self->running.exchange(false)) {
		obs_output_signal_stop(self->output, OBS_OUTPUT_DISCONNECTED);
	}
}

bool MOQOutput::Connect()
{
	moq_endpoint_cfg_t ecfg;
	moq_endpoint_cfg_init(&ecfg);
	ecfg.url.data = (const uint8_t *)url.c_str();
	ecfg.url.len = url.size();
	//todo: dynamically resolve from config
	ecfg.insecure_skip_verify = true;

	//todo: revisit if we want to set the version policy to exact or leave it auto
	static const moq_version_t kVersions[] = {MOQ_VERSION_DRAFT_16};
	ecfg.versions.policy = MOQ_VERSION_POLICY_EXACT;
	ecfg.versions.versions = kVersions;
	ecfg.versions.version_count = 1;

	moq_media_sender_cfg_t scfg;
	moq_media_sender_cfg_init_live(&scfg);
	scfg.endpoint = &ecfg;
	scfg.namespace_ = namespace_val;
	scfg.publish_tracks = true;
	scfg.drop_without_demand = true;

	moq_media_sender_callbacks_init(&scfg.callbacks);
	scfg.callbacks.ctx = this;
	scfg.callbacks.on_ready = &MOQOutput::OnReady;
	scfg.callbacks.on_closed = &MOQOutput::OnClosed;
	scfg.callbacks.on_track_closed = &MOQOutput::OnTrackClosed;

	moq_media_sender_t *media_sender = nullptr;
	if (moq_media_sender_create(&scfg, &media_sender) != MOQ_OK) {
		blog(LOG_WARNING, "[obs-moq] moq_media_sender_create failed");
		obs_output_set_last_error(output, obs_module_text("Error.Connect"));
		obs_output_signal_stop(output, OBS_OUTPUT_CONNECT_FAILED);
		return false;
	}

	if (!CreateVideoTrack(media_sender)) {
		blog(LOG_WARNING, "[obs-moq] failed to create video track");
		moq_media_sender_destroy(media_sender);
		return false;
	}

	return true;
}

bool MOQOutput::Start()
{
	std::lock_guard<std::mutex> lock(start_stop_mutex);

	blog(LOG_INFO, "[obs-moq] Start() requested");

	if (!obs_output_can_begin_data_capture(output, 0)) {
		blog(LOG_WARNING, "[obs-moq] cannot begin data capture");
		return false;
	}
	if (!obs_output_initialize_encoders(output, 0)) {
		blog(LOG_WARNING, "[obs-moq] failed to initialize encoders");
		return false;
	}

	running.store(false);
	got_ready.store(false);
	stopping.store(false);
	total_bytes_sent.store(0);
	connect_time_ms.store(0);
	start_time_ns = os_gettime_ns();

	if (start_stop_thread.joinable())
		start_stop_thread.join();

	start_stop_thread = std::thread(&MOQOutput::StartThread, this);

	return true;
}

void MOQOutput::Stop(bool signal)
{
	stopping.store(true);

	std::lock_guard<std::mutex> lock(start_stop_mutex);
	if (start_stop_thread.joinable())
		start_stop_thread.join();

	bool was_running = running.exchange(false);

	if (was_running) {
		obs_output_end_data_capture(output);
	}

	moq_media_sender_t *doomed = nullptr;
	{
		std::lock_guard<std::mutex> slock(sender_mutex);
		doomed = sender;
		sender = nullptr;
		video_track = nullptr;
	}
	if (doomed) {
		moq_media_sender_destroy(doomed);
	}

	if (signal) {
		obs_output_signal_stop(output, OBS_OUTPUT_SUCCESS);
	}

	total_bytes_sent.store(0);
	connect_time_ms.store(0);
	start_time_ns = os_gettime_ns();
}

void MOQOutput::Data(struct encoder_packet *packet)
{
	if (!packet) {
		Stop(false);
		obs_output_signal_stop(output, OBS_OUTPUT_ENCODE_ERROR);
		return;
	}

	if (!running.load() || packet->type != OBS_ENCODER_VIDEO) {
		return;
	}

	moq_rcbuf_t *payload = nullptr;
	// moq_rcbuf_create will copy the data into a new rcbuf, and increment the refcount. We will need to decref it after sending, or if we don't send it.
	if (moq_rcbuf_create(moq_alloc_default(), packet->data, packet->size, &payload) != MOQ_OK) {
		blog(LOG_WARNING, "[obs-moq] rcbuf alloc failed");
		return;
	}

	uint64_t pts_usec = 0;
	pts_usec = util_mul_div64((uint64_t)packet->pts, 1000000ull * (uint64_t)packet->timebase_num,
				  (uint64_t)packet->timebase_den);

	moq_media_send_object_t obj = {};
	obj.struct_size = sizeof(obj);
	obj.payload = payload;
	obj.properties = nullptr;
	obj.is_sync = packet->keyframe;
	obj.starts_group = packet->keyframe;
	obj.ends_group = false;
	obj.presentation_time_us = pts_usec;
	obj.decode_time_us = (uint64_t)packet->dts_usec;

	moq_result_t res;
	{
		std::lock_guard<std::mutex> lock(sender_mutex);
		if (!sender || !video_track) {
			// release the rcbuf since we won't be sending it
			moq_rcbuf_decref(payload);
			return;
		}
		res = moq_media_sender_write(sender, video_track, &obj);
	}

	if (res != MOQ_OK) {
		moq_rcbuf_decref(payload);
		return;
	}

	total_bytes_sent.fetch_add(packet->size);
}

void MOQOutput::StartThread()
{
	blog(LOG_INFO, "[obs-moq] Starting");

	if (!ResolveServiceConfig()) {
		blog(LOG_WARNING, "[obs-moq] failed to resolve service config");
		return;
	}

	if (!LoadVideoEncoderSettings()) {
		blog(LOG_WARNING, "[obs-moq] failed to configure video track");
		return;
	}

	if (!Connect()) {
		blog(LOG_WARNING, "[obs-moq] failed to connect");
		return;
	}
}

void register_moq_output()
{
	struct obs_output_info info = {};
	info.id = "moq_output";
	// todo: change to OBS_OUTPUT_AV when audio is supported
	info.flags = OBS_OUTPUT_VIDEO | OBS_OUTPUT_ENCODED | OBS_OUTPUT_SERVICE;
	info.protocols = "MOQ";
	// todo: add support for hevc and av1
	info.encoded_video_codecs = "h264";
	// todo: add support for audio
	// info.encoded_audio_codecs = "aac;opus";

	info.get_name = [](void *) -> const char * {
		return obs_module_text("Output.Name");
	};
	info.create = [](obs_data_t *settings, obs_output_t *output) -> void * {
		return new MOQOutput(settings, output);
	};
	info.destroy = [](void *priv_data) {
		delete static_cast<MOQOutput *>(priv_data);
	};
	info.start = [](void *priv_data) -> bool {
		return static_cast<MOQOutput *>(priv_data)->Start();
	};
	info.stop = [](void *priv_data, uint64_t) {
		static_cast<MOQOutput *>(priv_data)->Stop(true);
	};
	info.encoded_packet = [](void *priv_data, struct encoder_packet *packet) {
		static_cast<MOQOutput *>(priv_data)->Data(packet);
	};

	obs_register_output(&info);
	blog(LOG_INFO, "[obs-moq] registered output '%s' (protocol MOQ)", info.id);
}