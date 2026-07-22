#include "moq-service.h"

const char *audio_codecs[] = {"aac", "opus", nullptr};
const char *video_codecs[] = {"h264", nullptr};

MOQService::MOQService(obs_data_t *settings, obs_service_t *service)
{
	Update(settings);
}

void MOQService::Update(obs_data_t *settings)
{
	server = obs_data_get_string(settings, "server");
	// todo: verify that this is the correct key for the namespace
	moq_namespace = obs_data_get_string(settings, "key");
	blog(LOG_INFO, "[obs-moq] service updated: server='%s' namespace='%s'", server.c_str(), moq_namespace.c_str());
}

obs_properties_t *MOQService::Properties()
{
	obs_properties_t *ppts = obs_properties_create();

	obs_properties_add_text(ppts, "server", obs_module_text("Service.Server"), OBS_TEXT_DEFAULT);
	obs_properties_add_text(ppts, "key", obs_module_text("Service.Namespace"), OBS_TEXT_DEFAULT);

	return ppts;
}

// todo: validate if we need custom encoder settings
void MOQService::ApplyEncoderSettings(obs_data_t *video_settings, obs_data_t *audio_settings)
{
	blog(LOG_INFO, "[obs-moq] apply encoder settings");
	if (video_settings) {
		obs_data_set_int(video_settings, "bf", 0);
		//todo: check if this is needed
		obs_data_set_bool(video_settings, "repeat_headers", true);
	}
}

const char *MOQService::GetConnectInfo(enum obs_service_connect_info info)
{
	switch (info) {
	case OBS_SERVICE_CONNECT_INFO_SERVER_URL:
		return server.c_str();
	case OBS_SERVICE_CONNECT_INFO_STREAM_KEY:
		return moq_namespace.c_str();
	default:
		return nullptr;
	}
}

bool MOQService::CanTryToConnect()
{
	return !server.empty();
}

void register_moq_service()
{
	struct obs_service_info info = {};

	info.id = "MOQ";
	info.get_name = [](void *) -> const char * {
		return obs_module_text("Service.Name");
	};
	info.create = [](obs_data_t *settings, obs_service_t *service) -> void * {
		return new MOQService(settings, service);
	};
	info.destroy = [](void *priv) {
		delete static_cast<MOQService *>(priv);
	};
	info.update = [](void *priv, obs_data_t *settings) {
		static_cast<MOQService *>(priv)->Update(settings);
	};
	info.get_properties = [](void *) -> obs_properties_t * {
		return MOQService::Properties();
	};
	info.get_protocol = [](void *) -> const char * {
		return "MOQ";
	};
	info.get_url = [](void *priv) -> const char * {
		return static_cast<MOQService *>(priv)->server.c_str();
	};
	info.get_output_type = [](void *) -> const char * {
		return "moq_output";
	};
	info.apply_encoder_settings = [](void *, obs_data_t *video_settings, obs_data_t *audio_settings) {
		MOQService::ApplyEncoderSettings(video_settings, audio_settings);
	};
	info.get_supported_video_codecs = [](void *) -> const char ** {
		return video_codecs;
	};
	info.get_supported_audio_codecs = [](void *) -> const char ** {
		return audio_codecs;
	};
	info.can_try_to_connect = [](void *priv_data) -> bool {
		return static_cast<MOQService *>(priv_data)->CanTryToConnect();
	};
	info.get_connect_info = [](void *priv_data, uint32_t type) -> const char * {
		return static_cast<MOQService *>(priv_data)->GetConnectInfo((enum obs_service_connect_info)type);
	};

	obs_register_service(&info);
	blog(LOG_INFO, "[obs-moq] registered service '%s' (protocol MOQ -> output moq_output)", info.id);
}