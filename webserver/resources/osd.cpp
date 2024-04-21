#include "resources.hpp"
#include <iostream>

webserver::resources::OsdResource::OsdResource() : Resource()
{
    m_osd_configs = {
        nlohmann::json::parse(R"({
                "id": "example_image",
                "image_path": "/home/root/apps/detection/resources/configs/osd_hailo_static_image.png",
                "width": 0.2,
                "height": 0.13,
                "x": 0.78,
                "y": 0.0,
                "z-index": 1,
                "angle": 0,
                "rotation_policy": "CENTER"
            })"),
        nlohmann::json::parse(R"({
                "id": "example_datetime",
                "font_size": 100,
                "line_thickness": 3,
                "rgb": [
                    255,
                    0,
                    0
                ],
                "font_path": "/usr/share/fonts/ttf/LiberationMono-Regular.ttf",
                "x": 0.0,
                "y": 0.95,
                "z-index": 3,
                "angle": 0,
                "rotation_policy": "CENTER"
            })"),
        nlohmann::json::parse(R"({
                "id": "example_text1",
                "label": "HailoAI",
                "font_size": 100,
                "line_thickness": 3,
                "rgb": [
                    255,
                    255,
                    255
                ],
                "x": 0.78,
                "y": 0.12,
                "z-index": 2,
                "font_path": "/usr/share/fonts/ttf/LiberationMono-Regular.ttf",
                "angle": 0,
                "rotation_policy": "CENTER"
            })"),
        nlohmann::json::parse(R"({
                "id": "example_text2",
                "label": "Demo Application",
                "font_size": 100,
                "line_thickness": 3,
                "rgb": [
                    102,
                    0,
                    51
                ],
                "x": 0.0,
                "y": 0.01,
                "z-index": 1,
                "font_path": "/usr/share/fonts/ttf/LiberationMono-Regular.ttf",
                "angle": 0,
                "rotation_policy": "CENTER"
            })"),
    };
    m_default_config = R"(
    [
        {
            "id": "example_image",
            "name": "Image",
            "type": "image",
            "enabled": true
        },
        {
            "id": "example_datetime",
            "name": "Date & Time",
            "type": "datetime",
            "enabled": true
        },
        {
            "id": "example_text1",
            "name": "HailoAI Label",
            "type": "text",
            "enabled": true 
        },
        {
            "id": "example_text2",
            "name": "Demo Label",
            "type": "text",
            "enabled": true 
        }
    ])";
    m_config = nlohmann::json::parse(m_default_config);
}

nlohmann::json webserver::resources::OsdResource::get_osd_config_by_id(std::string id)
{
    for (auto &config : m_osd_configs)
    {
        if (config["id"] == id)
        {
            return config;
        }
    }
    return NULL;
}

nlohmann::json webserver::resources::OsdResource::get_current_osd_config()
{
    std::vector<nlohmann::json> images;
    std::vector<nlohmann::json> texts;
    std::vector<nlohmann::json> dates;

    for (auto &config : m_config)
    {
        if (config["enabled"] == false)
            continue;

        auto j_config = get_osd_config_by_id(config["id"]);

        if (j_config == NULL)
            continue;

        if (config["type"] == "image")
        {
            images.push_back(j_config);
        }
        else if (config["type"] == "text")
        {
            texts.push_back(j_config);
        }
        else if (config["type"] == "datetime")
        {
            dates.push_back(j_config);
        }
    }

    nlohmann::json current_config = {{"image", images}, {"text", texts}, {"dateTime", dates}};

    return current_config;
}

void webserver::resources::OsdResource::http_register(std::shared_ptr<httplib::Server> srv)
{
    srv->Get("/osd", [this](const httplib::Request &, httplib::Response &res)
             { res.set_content(this->to_string(), "application/json"); });

    srv->Patch("/osd", [this](const httplib::Request &req, httplib::Response &res)
               {
                    auto partial_config = nlohmann::json::parse(req.body);
                    m_config.merge_patch(partial_config);
                    res.set_content(this->to_string(), "application/json");
                    auto state = std::make_shared<webserver::resources::ResourceState>(ConfigResourceState(this->get_current_osd_config().dump()));
                    on_resource_change(state); });

    srv->Put("/osd", [this](const httplib::Request &req, httplib::Response &res)
             {
                auto config = nlohmann::json::parse(req.body);
                auto partial_config = nlohmann::json::diff(m_config, config);
                m_config = m_config.patch(partial_config);
                res.set_content(this->to_string(), "application/json");
                auto state = std::make_shared<webserver::resources::ResourceState>(ConfigResourceState(this->get_current_osd_config().dump()));
                on_resource_change(state); });
}