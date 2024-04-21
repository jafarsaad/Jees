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
/**
 * @file gsthailoenc.hpp
 * @brief Gstreamer Hailo Encoder parent class module
 **/

#pragma once

#include "media_library/hailo_encoder.hpp"
#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

typedef struct _GstHailoEnc GstHailoEnc;
typedef struct _GstHailoEncClass GstHailoEncClass;
#define GST_TYPE_HAILO_ENC (gst_hailoenc_get_type())
#define GST_HAILO_ENC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HAILO_ENC, GstHailoEnc))
#define GST_HAILO_ENC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HAILO_ENC, GstHailoEncClass))
#define GST_IS_HAILO_ENC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HAILO_ENC))
#define GST_IS_HAILO_ENC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HAILO_ENC))
#define GST_HAILO_ENC_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_HAILO_ENC, GstHailoEncClass))

struct _GstHailoEnc
{
  GstVideoEncoder parent;
  GstVideoCodecState *input_state;
  GstBuffer *header_buffer;
  VCEncApiVersion apiVer;
  VCEncBuild encBuild;
  VCEncGopPicConfig gopPicCfg[MAX_GOP_PIC_CONFIG_NUM];
  EncoderParams enc_params;
  VCEncInst encoder_instance;
  gboolean stream_restart;
  gboolean hard_restart;
  gboolean update_config;
  gboolean update_gop_size;
  GQueue *dts_queue;
};

struct _GstHailoEncClass
{
  GstVideoEncoderClass parent_class;
};

G_GNUC_INTERNAL GType gst_hailoenc_get_type(void);

G_END_DECLS
