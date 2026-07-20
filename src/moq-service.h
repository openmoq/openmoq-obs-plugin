#pragma once
#include <obs-module.h>
#include <string>

class MOQService {
public:
    std::string server;
    std::string moq_namespace;
    bool skip_verify = true; // todo: add a setting or find a way to set this

    MOQService(obs_data_t *settings, obs_service_t *service);

    void Update(obs_data_t *settings);
    static void ApplyEncoderSettings(obs_data_t *video_settings, obs_data_t *audio_settings);
    static obs_properties_t *Properties();
    const char *GetConnectInfo(enum obs_service_connect_info info);
    bool CanTryToConnect();

};

void register_moq_service();