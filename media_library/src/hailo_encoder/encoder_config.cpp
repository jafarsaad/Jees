/*
 * Copyright (c) 2017-2023 Hailo Technologies Ltd. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <iostream>
#include <memory>
#include <unordered_map>
#include <stdexcept>

#include "encoder_config.hpp"
#include "config_manager.hpp"
#include "encoder_class.hpp"
#include "encoder_internal.hpp"
#include "media_library_logger.hpp"

#define MIN_MONITOR_FRAMES (10)
#define MAX_MONITOR_FRAMES (120)

EncoderConfig::EncoderConfig(const std::string &json_string)
    : m_json_string(json_string)
{
    m_config_manager = std::make_shared<ConfigManager>(ConfigSchema::CONFIG_SCHEMA_ENCODER);
    auto ret = m_config_manager->validate_configuration(json_string);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        throw std::runtime_error("JSON config file is not valid");
    }
    m_doc = nlohmann::json::parse(m_json_string);
}
const nlohmann::json &EncoderConfig::get_doc() const
{
    return m_doc;
}
const nlohmann::json &EncoderConfig::get_gop_config() const
{
    return m_doc["gop_config"];
}
const nlohmann::json &EncoderConfig::get_input_stream() const
{
    return m_doc["config"]["input_stream"];
}

const nlohmann::json &EncoderConfig::get_coding_control() const
{
    return m_doc["coding_control"];
}

const nlohmann::json &EncoderConfig::get_rate_control() const
{
    return m_doc["rate_control"];
}

const nlohmann::json &EncoderConfig::get_output_stream() const
{
    return m_doc["config"]["output_stream"];
}

uint32_t Encoder::Impl::get_codec()
{
    auto output_stream = m_config->get_output_stream();
    std::string codec = std::string(output_stream["codec"]);
    if (codec == "h264")
        return 1;
    else if (codec == "hevc")
        return 0;
    else
        return 0;
}

VCEncProfile Encoder::Impl::get_profile()
{
    auto output_stream = m_config->get_output_stream();
    std::string profile = std::string(output_stream["profile"]);
    if (profile == "VCENC_H264_BASE_PROFILE")
        return VCENC_H264_BASE_PROFILE;
    else if (profile == "VCENC_H264_MAIN_PROFILE")
        return VCENC_H264_MAIN_PROFILE;
    else if (profile == "VCENC_H264_HIGH_PROFILE")
        return VCENC_H264_HIGH_PROFILE;
    else if (profile == "VCENC_HEVC_MAIN_STILL_PICTURE_PROFILE")
        return VCENC_HEVC_MAIN_STILL_PICTURE_PROFILE;
    else
        return VCENC_HEVC_MAIN_PROFILE;
}

VCEncPictureType Encoder::Impl::get_input_format(std::string format)
{
    if (input_formats.find(format) != input_formats.end())
        return input_formats.find(format)->second;
    else
        throw std::invalid_argument("Invalid format");
}

VCEncLevel Encoder::Impl::get_level(std::string level, bool codecH264)
{
    if (codecH264)
    {
        if (h264_level.find(level) != h264_level.end())
        {
            return h264_level.find(level)->second;
        }
        else
            throw std::invalid_argument("Invalid level");
    }
    else
    {
        if (h265_level.find(level) != h265_level.end())
        {
            return h265_level.find(level)->second;
        }
        else
            throw std::invalid_argument("Invalid level");
    }
}

void Encoder::Impl::updateArea(nlohmann::json &area, VCEncPictureArea &vc_area)
{
    bool enable = area["enable"];
    if (enable)
    {
        vc_area.enable = 1;
        vc_area.top = area["top"];
        vc_area.left = area["left"];
        vc_area.bottom = area["bottom"];
        vc_area.right = area["right"];
    }
    else
    {
        vc_area.enable = 0;
        vc_area.top = vc_area.left = vc_area.bottom = vc_area.right = -1;
    }
}

void Encoder::Impl::init_gop_config()
{
    LOGGER__DEBUG("Encoder - init_gop_config");
    auto codec = get_codec();
    auto gop_config_json = m_config->get_gop_config();
    int32_t bframe_qp_delta = gop_config_json["b_frame_qp_delta"];
    int32_t gop_size = gop_config_json["gop_size"];
    memset(&m_enc_in.gopConfig, 0, sizeof(VCEncGopConfig));
    m_gop_cfg = std::make_unique<gopConfig>(&(m_enc_in.gopConfig), gop_size, bframe_qp_delta, codec);
}

VCEncRet Encoder::Impl::init_rate_control_config()
{
    LOGGER__DEBUG("Encoder - init_rate_control_config");
    VCEncRet ret = VCENC_OK;

    auto rate_control = m_config->get_rate_control();

    /* Encoder setup: rate control */
    if ((ret = VCEncGetRateCtrl(m_inst, &m_vc_rate_cfg)) != VCENC_OK)
    {
        VCEncRelease(m_inst);
        return ret;
    }
    m_vc_rate_cfg.qpHdr = rate_control["quantization"]["qp_hdr"];
    m_vc_rate_cfg.qpMin = rate_control["quantization"]["qp_min"];
    m_vc_rate_cfg.qpMax = rate_control["quantization"]["qp_max"];
    m_vc_rate_cfg.pictureSkip = rate_control["picture_skip"];
    m_vc_rate_cfg.pictureRc = rate_control["picture_rc"];
    m_vc_rate_cfg.ctbRc = rate_control["ctb_rc"];

    uint32_t block_rc_size = rate_control["block_rc_size"];
    switch (block_rc_size)
    {
    case 64:
        m_vc_rate_cfg.blockRCSize = 0;
        break;
    case 32:
        m_vc_rate_cfg.blockRCSize = 1;
        break;
    case 16:
        m_vc_rate_cfg.blockRCSize = 2;
        break;
    default:
        throw std::invalid_argument("Invalid block_rc_size");
    }

    m_vc_rate_cfg.bitPerSecond = rate_control["bitrate"]["target_bitrate"];
    m_vc_rate_cfg.bitVarRangeI = rate_control["bitrate"]["bit_var_range_i"];
    m_vc_rate_cfg.bitVarRangeP = rate_control["bitrate"]["bit_var_range_p"];
    m_vc_rate_cfg.bitVarRangeB = rate_control["bitrate"]["bit_var_range_b"];
    m_vc_rate_cfg.tolMovingBitRate = rate_control["bitrate"]["tolerance_moving_bitrate"];

    uint32_t monitor_frames = rate_control["monitor_frames"];
    if (monitor_frames != 0)
        m_vc_rate_cfg.monitorFrames = monitor_frames;
    else
        m_vc_rate_cfg.monitorFrames = (m_vc_cfg.frameRateNum + m_vc_cfg.frameRateDenom - 1) / m_vc_cfg.frameRateDenom;

    if (m_vc_rate_cfg.monitorFrames > MAX_MONITOR_FRAMES)
        m_vc_rate_cfg.monitorFrames = MAX_MONITOR_FRAMES;
    if (m_vc_rate_cfg.monitorFrames < MIN_MONITOR_FRAMES)
        m_vc_rate_cfg.monitorFrames = MIN_MONITOR_FRAMES;

    m_vc_rate_cfg.hrd = rate_control["hrd"];
    m_vc_rate_cfg.hrdCpbSize = rate_control["hrd_cpb_size"];

    m_vc_rate_cfg.gopLen = rate_control["gop_length"];
    m_vc_rate_cfg.intraQpDelta = rate_control["quantization"]["intra_qp_delta"];
    m_vc_rate_cfg.fixedIntraQp = rate_control["quantization"]["fixed_intra_qp"];

    if ((ret = VCEncSetRateCtrl(m_inst, &m_vc_rate_cfg)) != VCENC_OK)
    {
        VCEncRelease(m_inst);
    }
    return ret;
}

VCEncRet Encoder::Impl::init_coding_control_config()
{
    LOGGER__DEBUG("Encoder - init_coding_control_config");
    VCEncRet ret = VCENC_OK;
    auto coding_control = m_config->get_coding_control();

    /* Encoder setup: coding control */
    if ((ret = VCEncGetCodingCtrl(m_inst, &m_vc_coding_cfg)) != VCENC_OK)
    {
        VCEncRelease(m_inst);
        return ret;
    }

    m_vc_coding_cfg.sliceSize = 0;
    m_vc_coding_cfg.disableDeblockingFilter = 0;
    m_vc_coding_cfg.tc_Offset = -2;
    m_vc_coding_cfg.beta_Offset = 5;
    m_vc_coding_cfg.enableSao = 1;
    m_vc_coding_cfg.enableDeblockOverride = 0;
    m_vc_coding_cfg.deblockOverride = 0;
    m_vc_coding_cfg.enableCabac = 1;
    m_vc_coding_cfg.cabacInitFlag = 0;
    m_vc_coding_cfg.videoFullRange = 0;

    /* Disabled */
    m_vc_coding_cfg.seiMessages = 0;
    m_vc_coding_cfg.gdrDuration = 0;
    m_vc_coding_cfg.fieldOrder = 0;

    m_vc_coding_cfg.cirStart = 0;
    m_vc_coding_cfg.cirInterval = 0;

    m_vc_coding_cfg.pcm_loop_filter_disabled_flag = 0;

    updateArea(coding_control["roi_area1"], m_vc_coding_cfg.roi1Area);
    updateArea(coding_control["roi_area2"], m_vc_coding_cfg.roi2Area);
    updateArea(coding_control["intra_area"], m_vc_coding_cfg.intraArea);
    updateArea(coding_control["ipcm_area1"], m_vc_coding_cfg.ipcm1Area);
    updateArea(coding_control["ipcm_area2"], m_vc_coding_cfg.ipcm2Area);

    // TODO: check if need to be changed.
    m_vc_coding_cfg.ipcmMapEnable = 0;
    m_vc_coding_cfg.pcm_enabled_flag = 0;

    m_vc_coding_cfg.codecH264 = m_vc_cfg.codecH264;

    m_vc_coding_cfg.roiMapDeltaQpEnable = 0;
    m_vc_coding_cfg.roiMapDeltaQpBlockUnit = 0;

    m_vc_coding_cfg.enableScalingList = 0;
    m_vc_coding_cfg.chroma_qp_offset = 0;

    /* low latency */
    m_vc_coding_cfg.inputLineBufEn = 0;
    m_vc_coding_cfg.inputLineBufLoopBackEn = 0;
    m_vc_coding_cfg.inputLineBufDepth = 0;
    m_vc_coding_cfg.inputLineBufHwModeEn = 0;
    m_vc_coding_cfg.inputLineBufCbFunc = VCEncInputLineBufDone;
    m_vc_coding_cfg.inputLineBufCbData = NULL;

    /* denoise */
    m_vc_coding_cfg.noiseReductionEnable = 0;
    m_vc_coding_cfg.noiseLow = 10;
    m_vc_coding_cfg.firstFrameSigma = 11;

    if ((ret = VCEncSetCodingCtrl(m_inst, &m_vc_coding_cfg)) != VCENC_OK)
    {
        VCEncRelease(m_inst);
    }
    return ret;
}

VCEncRet Encoder::Impl::init_preprocessing_config()
{
    LOGGER__DEBUG("Encoder - init_preprocessing_config");
    VCEncRet ret;
    /* PreP setup */
    if ((ret = VCEncGetPreProcessing(m_inst, &m_vc_pre_proc_cfg)) != VCENC_OK)
    {
        VCEncRelease(m_inst);
        return ret;
    }
    auto input_stream = m_config->get_input_stream();

    m_vc_pre_proc_cfg.inputType = get_input_format(std::string(input_stream["format"]));
    // No Rotation
    m_vc_pre_proc_cfg.rotation = (VCEncPictureRotation)0;

    m_vc_pre_proc_cfg.origWidth = input_stream["width"];
    m_vc_pre_proc_cfg.origHeight = input_stream["height"];

    m_vc_pre_proc_cfg.xOffset = 0;
    m_vc_pre_proc_cfg.yOffset = 0;
    m_vc_pre_proc_cfg.colorConversion.type = (VCEncColorConversionType)0;

    // For the future RGB to YUV
    if (m_vc_pre_proc_cfg.colorConversion.type == VCENC_RGBTOYUV_USER_DEFINED)
    {
        m_vc_pre_proc_cfg.colorConversion.coeffA = 20000;
        m_vc_pre_proc_cfg.colorConversion.coeffB = 44000;
        m_vc_pre_proc_cfg.colorConversion.coeffC = 5000;
        m_vc_pre_proc_cfg.colorConversion.coeffE = 35000;
        m_vc_pre_proc_cfg.colorConversion.coeffF = 38000;
    }

    m_vc_pre_proc_cfg.scaledWidth = 0;
    m_vc_pre_proc_cfg.scaledHeight = 0;

    m_vc_pre_proc_cfg.busAddressScaledBuff = 0;
    m_vc_pre_proc_cfg.virtualAddressScaledBuff = NULL;
    m_vc_pre_proc_cfg.sizeScaledBuff = 0;
    m_vc_pre_proc_cfg.alignment = 0;

    /* Set overlay area*/
    for (int i = 0; i < MAX_OVERLAY_NUM; i++)
    {
        m_vc_pre_proc_cfg.overlayArea[i].xoffset = 0;
        m_vc_pre_proc_cfg.overlayArea[i].cropXoffset = 0;
        m_vc_pre_proc_cfg.overlayArea[i].yoffset = 0;
        m_vc_pre_proc_cfg.overlayArea[i].cropYoffset = 0;
        m_vc_pre_proc_cfg.overlayArea[i].width = 0;
        m_vc_pre_proc_cfg.overlayArea[i].cropWidth = 0;
        m_vc_pre_proc_cfg.overlayArea[i].height = 0;
        m_vc_pre_proc_cfg.overlayArea[i].cropHeight = 0;
        m_vc_pre_proc_cfg.overlayArea[i].format = 0;
        m_vc_pre_proc_cfg.overlayArea[i].alpha = 0;
        m_vc_pre_proc_cfg.overlayArea[i].enable = 0;
        m_vc_pre_proc_cfg.overlayArea[i].Ystride = 0;
        m_vc_pre_proc_cfg.overlayArea[i].UVstride = 0;
        m_vc_pre_proc_cfg.overlayArea[i].bitmapY = 0;
        m_vc_pre_proc_cfg.overlayArea[i].bitmapU = 0;
        m_vc_pre_proc_cfg.overlayArea[i].bitmapV = 0;
    }

    if ((ret = VCEncSetPreProcessing(m_inst, &m_vc_pre_proc_cfg)) != VCENC_OK)
    {
        VCEncRelease(m_inst);
        return ret;
    }
    return ret;
}

VCEncRet Encoder::Impl::init_encoder_config()
{
    LOGGER__DEBUG("Encoder - init_encoder_config");
    VCEncRet ret = VCENC_OK;
    auto input_stream = m_config->get_input_stream();
    auto output_stream = m_config->get_output_stream();

    m_vc_cfg.width = input_stream["width"];
    m_vc_cfg.height = input_stream["height"];
    m_vc_cfg.frameRateNum = input_stream["framerate"];
    m_vc_cfg.frameRateDenom = 1;

    /* intra tools in sps and pps */
    m_vc_cfg.strongIntraSmoothing = 1;
    m_vc_cfg.streamType = VCENC_BYTE_STREAM;

    m_vc_cfg.codecH264 = get_codec();
    m_vc_cfg.profile = get_profile();
    std::string level = std::string(output_stream["level"]);
    m_vc_cfg.level = get_level(level, m_vc_cfg.codecH264);

    m_vc_cfg.bitDepthLuma = 8;
    m_vc_cfg.bitDepthChroma = 8;

    // default maxTLayer
    m_vc_cfg.maxTLayers = 1;

    m_vc_cfg.interlacedFrame = 0;
    /* Find the max number of reference frame */
    u32 maxRefPics = 0;
    i32 maxTemporalId = 0;
    for (int idx = 0; idx < m_enc_in.gopConfig.size; idx++)
    {
        VCEncGopPicConfig *cfg = &(m_enc_in.gopConfig.pGopPicCfg[idx]);
        if (cfg->codingType != VCENC_INTRA_FRAME)
        {
            if (maxRefPics < cfg->numRefPics)
                maxRefPics = cfg->numRefPics;

            if (maxTemporalId < cfg->temporalId)
                maxTemporalId = cfg->temporalId;
        }
    }
    m_vc_cfg.refFrameAmount = maxRefPics + m_vc_cfg.interlacedFrame + (m_enc_in.gopConfig.ltrInterval > 0);
    m_vc_cfg.maxTLayers = maxTemporalId + 1;
    // TODO: Change compressor from json
    m_vc_cfg.compressor = 3;
    m_vc_cfg.enableOutputCuInfo = 0;
    m_vc_cfg.exp_of_alignment = 0;
    m_vc_cfg.refAlignmentExp = 0;
    m_vc_cfg.AXIAlignment = 0;
    m_vc_cfg.AXIreadOutstandingNum = 64;  // ENCH2_ASIC_AXI_READ_OUTSTANDING_NUM;
    m_vc_cfg.AXIwriteOutstandingNum = 64; // ENCH2_ASIC_AXI_WRITE_OUTSTANDING_NUM;
    if ((ret = VCEncInit(&m_vc_cfg, &m_inst)) != VCENC_OK)
    {
        // PrintErrorValue("VCEncInit() failed.", ret);
        return ret;
    }
    return ret;
}