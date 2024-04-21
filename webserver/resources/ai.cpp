#include "resources.hpp"
#include <iostream>

webserver::resources::AiResource::AiResource() : Resource()
{
    m_default_config = R"(
    {
        "detection": {
            "enabled": true
        },
        "denoise": {
            "enabled": false
        },
        "defog": {
            "enabled": false
        }
    })";
    m_config = nlohmann::json::parse(m_default_config);
    m_denoise_config = nlohmann::json::parse(R"({
        "hailort": {
            "device-id": "device0"
        },
        "denoise": {
            "enabled": false,
            "sensor": "imx678",
            "method": "HIGH_QUALITY",
            "loopback-count": 1,
            "network": {
                "network_path": "/home/root/apps/internals/frontend_pipelines/resources/VD_M2_IMX678.hef",
                "y_channel": "model/input_layer1",
                "uv_channel": "model/input_layer4",
                "feedback_y_channel": "model/input_layer3",
                "feedback_uv_channel": "model/input_layer2",
                "output_y_channel": "model/conv17",
                "output_uv_channel": "model/conv14"
            }
        }
    })");
    m_defog_config = nlohmann::json::parse(R"({
        "hailort": {
            "device-id": "device0"
        },
        "defog": {
            "enabled": false,
            "network": {
                "network_path": "/home/root/apps/internals/frontend_pipelines/resources/dehazenet.hef",
                "y_channel": "dehazenet/input_layer1",
                "uv_channel": "dehazenet/input_layer2",
                "output_y_channel": "dehazenet/conv17",
                "output_uv_channel": "dehazenet/ew_add1"
            }
        }
    })");
}

std::vector<webserver::resources::AiResource::AiApplications> webserver::resources::AiResource::get_enabled_applications()
{
    std::vector<webserver::resources::AiResource::AiApplications> enabled_applications;
    for (const auto &[key, value] : m_config.items())
    {
        if (value["enabled"])
        {
            if (key == "detection")
                enabled_applications.push_back(AiApplications::AI_APPLICATION_DETECTION);
            else if (key == "denoise")
                enabled_applications.push_back(AiApplications::AI_APPLICATION_DENOISE);
            else if (key == "defog")
                enabled_applications.push_back(AiApplications::AI_APPLICATION_DEFOG);
        }
    }
    return enabled_applications;
}

std::shared_ptr<webserver::resources::AiResource::AiResourceState> webserver::resources::AiResource::parse_state(std::vector<webserver::resources::AiResource::AiApplications> current_enabled, std::vector<webserver::resources::AiResource::AiApplications> prev_enabled)
{
    auto state = std::make_shared<AiResourceState>();
    for (const auto &app : current_enabled)
    {
        if (std::find(prev_enabled.begin(), prev_enabled.end(), app) == prev_enabled.end())
        {
            state->enabled.push_back(app);
        }
    }
    for (const auto &app : prev_enabled)
    {
        if (std::find(current_enabled.begin(), current_enabled.end(), app) == current_enabled.end())
        {
            state->disabled.push_back(app);
        }
    }
    return state;
}

void webserver::resources::AiResource::http_patch(nlohmann::json body)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto prev_enabled_apps = this->get_enabled_applications();
    m_config.merge_patch(body);
    auto current_enabled_apps = this->get_enabled_applications();

    // if denoise in current but not in prev, disable defog
    if (std::find(current_enabled_apps.begin(), current_enabled_apps.end(), AiApplications::AI_APPLICATION_DENOISE) != current_enabled_apps.end() &&
        std::find(prev_enabled_apps.begin(), prev_enabled_apps.end(), AiApplications::AI_APPLICATION_DENOISE) == prev_enabled_apps.end())
    {
        m_config["defog"]["enabled"] = false;
    }

    // if defog in current but not in prev, disable denoise
    if (std::find(current_enabled_apps.begin(), current_enabled_apps.end(), AiApplications::AI_APPLICATION_DEFOG) != current_enabled_apps.end() &&
        std::find(prev_enabled_apps.begin(), prev_enabled_apps.end(), AiApplications::AI_APPLICATION_DEFOG) == prev_enabled_apps.end())
    {
        // disable denoise
        m_config["denoise"]["enabled"] = false;
    }

    m_defog_config["defog"]["enabled"] = m_config["defog"]["enabled"];
    m_denoise_config["denoise"]["enabled"] = m_config["denoise"]["enabled"];

    on_resource_change(this->parse_state(this->get_enabled_applications(), prev_enabled_apps));
}

void webserver::resources::AiResource::http_register(std::shared_ptr<httplib::Server> srv)
{
    srv->Get("/ai", [this](const httplib::Request &, httplib::Response &res)
             { res.set_content(this->to_string(), "application/json"); });

    srv->Patch("/ai", [this](const httplib::Request &req, httplib::Response &res)
               { 
                    auto body = nlohmann::json::parse(req.body);
                    http_patch(body);
                    res.set_content(this->to_string(), "application/json"); });
}

std::string webserver::resources::AiResource::get_ai_config(AiApplications app)
{
    if (app == AiApplications::AI_APPLICATION_DENOISE)
        return m_denoise_config.dump();
    if (app == AiApplications::AI_APPLICATION_DEFOG)
        return m_defog_config.dump();
    return "";
}