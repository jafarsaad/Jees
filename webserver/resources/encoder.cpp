#include "resources.hpp"
#include <iostream>

webserver::resources::EncoderResource::EncoderResource() : Resource()
{
    m_type = ResourceType::RESOURCE_ENCODER;
    m_default_config = R"({
        "config": {
            "input_stream": {
                "width": 1920,
                "height": 1080,
                "framerate": 30,
                "format": "NV12"
            },
            "output_stream": {
                "codec": "h264",
                "profile": "VCENC_H264_MAIN_PROFILE",
                "level": "5.0",
                "bit_depth_luma": 8,
                "bit_depth_chroma": 8,
                "stream_type": "bytestream"
            }
        },
        "gop_config": {
            "gop_size": 1,
            "b_frame_qp_delta": 0
        },
        "coding_control": {
            "sei_messages": true,
            "deblocking_filter": {
                "type": "disabled",
                "tc_offset": 12,
                "beta_offset": 23,
                "deblock_override": false
            },
            "intra_area": {
                "enable": false,
                "top": 0,
                "left": 0,
                "bottom": 0,
                "right": 0
            },
            "ipcm_area1": {
                "enable": false,
                "top": 0,
                "left": 0,
                "bottom": 0,
                "right": 0
            },
            "ipcm_area2": {
                "enable": false,
                "top": 0,
                "left": 0,
                "bottom": 0,
                "right": 0
            },
            "roi_area1": {
                "enable": false,
                "top": 0,
                "left": 0,
                "bottom": 0,
                "right": 0,
                "qp_delta": 0
            },
            "roi_area2": {
                "enable": false,
                "top": 0,
                "left": 0,
                "bottom": 0,
                "right": 0,
                "qp_delta": 0
            }
        },
        "rate_control": {
            "picture_rc": true,
            "picture_skip": false,
            "ctb_rc": false,
            "block_rc_size": 64,
            "hrd": false,
            "hrd_cpb_size": 0,
            "monitor_frames": 10,
            "gop_length": 30,
            "quantization": {
                "qp_min": 10,
                "qp_max": 50,
                "qp_hdr": 11,
                "intra_qp_delta": 0,
                "fixed_intra_qp": 0
            },
            "bitrate": {
                "target_bitrate": 10000000,
                "bit_var_range_i": 0,
                "bit_var_range_p": 0,
                "bit_var_range_b": 0,
                "tolerance_moving_bitrate": 0
            }
        }
    })";
    m_config = nlohmann::json::parse(m_default_config);
}

void webserver::resources::EncoderResource::http_register(std::shared_ptr<httplib::Server> srv)
{
    srv->Get("/encoder", [this](const httplib::Request &, httplib::Response &res)
             { res.set_content(this->to_string(), "application/json"); });

    srv->Patch("/encoder", [this](const httplib::Request &req, httplib::Response &res)
               {
                    auto partial_config = nlohmann::json::parse(req.body);
                    m_config.merge_patch(partial_config);
                    res.set_content(this->to_string(), "application/json");
                    on_resource_change(std::make_shared<webserver::resources::ResourceState>(ConfigResourceState(this->to_string()))); });

    srv->Put("/encoder", [this](const httplib::Request &req, httplib::Response &res)
             {
                auto config = nlohmann::json::parse(req.body);
                auto partial_config = nlohmann::json::diff(m_config, config);
                m_config = m_config.patch(partial_config);
                res.set_content(this->to_string(), "application/json");
                on_resource_change(std::make_shared<webserver::resources::ResourceState>(ConfigResourceState(this->to_string()))); });
}