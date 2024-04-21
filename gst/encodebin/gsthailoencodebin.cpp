/*
 * Copyright (c) 2017-2024 Hailo Technologies Ltd. All rights reserved.
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

#include "gsthailoencodebin.hpp"
#include <gst/gst.h>
#include <gst/video/video.h>
#include <dlfcn.h>
#include <unistd.h>

GST_DEBUG_CATEGORY_STATIC(gst_hailoencodebin_debug_category);
#define GST_CAT_DEFAULT gst_hailoencodebin_debug_category

static void gst_hailoencodebin_set_property(GObject *object,
                                            guint property_id, const GValue *value, GParamSpec *pspec);
static void gst_hailoencodebin_get_property(GObject *object,
                                            guint property_id, GValue *value, GParamSpec *pspec);
static void gst_hailoencodebin_init_ghost_sink(GstHailoEncodeBin *hailoencodebin);
static void gst_hailoencodebin_init_ghost_src(GstHailoEncodeBin *hailoencodebin);
static gboolean gst_hailoencodebin_link_elements(GstElement *element);
static void gst_hailoencodebin_dispose(GObject *object);
static void gst_hailoencodebin_reset(GstHailoEncodeBin *self);

#define MIN_QUEUE_SIZE 1
#define DEFAULT_QUEUE_SIZE 2

typedef enum
{
    PROP_0,
    PROP_CONFIG_FILE_PATH,
    PROP_CONFIG_STRING,
    PROP_CONFIG,
    PROP_WAIT_FOR_WRITABLE_BUFFER,
    PROP_BLENDER,
    PROP_QUEUE_SIZE,
    PROP_ENFORCE_CAPS,
} hailoencodebin_prop_t;

// Pad Templates
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE("sink",
                                                                    GST_PAD_SINK,
                                                                    GST_PAD_ALWAYS,
                                                                    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE("src",
                                                                   GST_PAD_SRC,
                                                                   GST_PAD_ALWAYS,
                                                                   GST_STATIC_CAPS_ANY);

G_DEFINE_TYPE_WITH_CODE(GstHailoEncodeBin, gst_hailoencodebin, GST_TYPE_BIN,
                        GST_DEBUG_CATEGORY_INIT(gst_hailoencodebin_debug_category, "hailoencodebin", 0,
                                                "debug category for hailoencodebin element"));

static void
gst_hailoencodebin_class_init(GstHailoEncodeBinClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    gst_element_class_add_static_pad_template(element_class, &src_template);
    gst_element_class_add_static_pad_template(element_class, &sink_template);

    gst_element_class_set_static_metadata(element_class,
                                          "Hailo Encode Bin", "Hailo/Media-Library", "Encode Bin for vision pipelines.",
                                          "hailo.ai <contact@hailo.ai>");

    gobject_class->set_property = GST_DEBUG_FUNCPTR(gst_hailoencodebin_set_property);
    gobject_class->get_property = GST_DEBUG_FUNCPTR(gst_hailoencodebin_get_property);
    gobject_class->dispose = GST_DEBUG_FUNCPTR(gst_hailoencodebin_dispose);

    g_object_class_install_property(gobject_class, PROP_CONFIG_FILE_PATH,
                                    g_param_spec_string("config-file-path", "Config file path, cannot be used with other properties",
                                                        "JSON config file path to load",
                                                        "",
                                                        (GParamFlags)(GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(gobject_class, PROP_CONFIG_STRING,
                                    g_param_spec_string("config-string", "Config string, cannot be used with other properties",
                                                        "JSON config string to load",
                                                        "",
                                                        (GParamFlags)(GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(gobject_class, PROP_WAIT_FOR_WRITABLE_BUFFER,
                                    g_param_spec_boolean("wait-for-writable-buffer", "Wait for writable buffer",
                                                         "Enables the element thread to wait until incomming buffer is writable",
                                                         FALSE,
                                                         (GParamFlags)(GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(gobject_class, PROP_BLENDER,
                                    g_param_spec_pointer("blender", "Blender object",
                                                         "Pointer to blender object",
                                                         (GParamFlags)(GST_PARAM_CONTROLLABLE | G_PARAM_READABLE)));

    g_object_class_install_property(gobject_class, PROP_QUEUE_SIZE,
                                    g_param_spec_uint("queue-size", "Queue size",
                                                      "Size of the queues in the bin, there are 2 queues.",
                                                      MIN_QUEUE_SIZE, G_MAXUINT, DEFAULT_QUEUE_SIZE,
                                                      (GParamFlags)(GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(gobject_class, PROP_CONFIG,
                                    g_param_spec_pointer("config", "Config",
                                                         "Pointer to the config object",
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(gobject_class, PROP_ENFORCE_CAPS,
                                    g_param_spec_boolean("enforce-caps", "Enforece caps",
                                                         "Enforce caps on the input/output pad of the bin",
                                                         TRUE,
                                                         (GParamFlags)(GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
}

static void
gst_hailoencodebin_init(GstHailoEncodeBin *hailoencodebin)
{
    // Default values
    hailoencodebin->config_file_path = NULL;
    hailoencodebin->m_elements_linked = FALSE;
    hailoencodebin->queue_size = DEFAULT_QUEUE_SIZE;

    // Prepare internal elements
    // osd
    hailoencodebin->m_osd = gst_element_factory_make("hailoosd", NULL);
    if (nullptr == hailoencodebin->m_osd)
    {
        GST_ELEMENT_ERROR(hailoencodebin, RESOURCE, FAILED, ("Failed creating hailoosd element in bin!"), (NULL));
    }

    // queue between osd and videorate
    hailoencodebin->m_queue_videorate = gst_element_factory_make("queue", NULL);
    if (nullptr == hailoencodebin->m_queue_videorate)
    {
        GST_ELEMENT_ERROR(hailoencodebin, RESOURCE, FAILED, ("Failed creating queue element in bin!"), (NULL));
    }
    // Passing 0 disables the features here
    g_object_set(hailoencodebin->m_queue_videorate, "max-size-time", (guint64)0, NULL);
    g_object_set(hailoencodebin->m_queue_videorate, "max-size-bytes", (guint)0, NULL);
    g_object_set(hailoencodebin->m_queue_videorate, "max-size-buffers", (guint)hailoencodebin->queue_size, NULL);

    // Prepare internal elements
    // osd
    hailoencodebin->m_videorate = gst_element_factory_make("videorate", NULL);
    if (nullptr == hailoencodebin->m_videorate)
    {
        GST_ELEMENT_ERROR(hailoencodebin, RESOURCE, FAILED, ("Failed creating videorate element in bin!"), (NULL));
    }
    g_object_set(hailoencodebin->m_videorate, "drop-only", TRUE, NULL);

    // queue between osd and videorate
    hailoencodebin->m_queue_encoder = gst_element_factory_make("queue", NULL);
    if (nullptr == hailoencodebin->m_queue_encoder)
    {
        GST_ELEMENT_ERROR(hailoencodebin, RESOURCE, FAILED, ("Failed creating queue element in bin!"), (NULL));
    }
    // Passing 0 disables the features here
    g_object_set(hailoencodebin->m_queue_encoder, "max-size-time", (guint64)0, NULL);
    g_object_set(hailoencodebin->m_queue_encoder, "max-size-bytes", (guint)0, NULL);
    g_object_set(hailoencodebin->m_queue_encoder, "max-size-buffers", (guint)hailoencodebin->queue_size, NULL);

    // encoder
    hailoencodebin->m_encoder = gst_element_factory_make("hailoencoder", NULL);
    if (nullptr == hailoencodebin->m_encoder)
    {
        GST_ELEMENT_ERROR(hailoencodebin, RESOURCE, FAILED, ("Failed creating hailoencoder element in bin!"), (NULL));
    }

    // Add elements and pads in the bin
    gst_bin_add_many(GST_BIN(hailoencodebin),
                     hailoencodebin->m_osd,
                     hailoencodebin->m_queue_videorate,
                     hailoencodebin->m_videorate,
                     hailoencodebin->m_queue_encoder,
                     hailoencodebin->m_encoder,
                     NULL);
    gst_hailoencodebin_init_ghost_sink(hailoencodebin);
    gst_hailoencodebin_init_ghost_src(hailoencodebin);
}

void gst_hailoencodebin_set_property(GObject *object, guint property_id,
                                     const GValue *value, GParamSpec *pspec)
{
    GstHailoEncodeBin *hailoencodebin = GST_HAILO_ENCODE_BIN(object);
    GST_DEBUG_OBJECT(hailoencodebin, "set_property");

    switch (property_id)
    {
    // Handle property assignments here
    case PROP_CONFIG_FILE_PATH:
    {
        hailoencodebin->config_file_path = g_value_dup_string(value);
        GST_DEBUG_OBJECT(hailoencodebin, "config_file_path: %s", hailoencodebin->config_file_path);

        // set params for sub elements here
        g_object_set(hailoencodebin->m_osd, "config-file-path", g_value_get_string(value), NULL);
        g_object_set(hailoencodebin->m_encoder, "config-file-path", g_value_get_string(value), NULL);

        // Now that configuration is known, link the elements
        if (hailoencodebin->m_elements_linked == FALSE)
        {
            gst_hailoencodebin_link_elements(GST_ELEMENT(hailoencodebin));
            hailoencodebin->m_elements_linked = TRUE;
        }
        break;
    }
    case PROP_CONFIG_STRING:
    {
        hailoencodebin->config_string = g_strdup(g_value_get_string(value));
        GST_DEBUG_OBJECT(hailoencodebin, "config-string: %s", hailoencodebin->config_string.c_str());

        // set params for sub elements here
        g_object_set(hailoencodebin->m_osd, "config-string", g_value_get_string(value), NULL);
        g_object_set(hailoencodebin->m_encoder, "config-string", g_value_get_string(value), NULL);

        // Now that configuration is known, link the elements
        if (hailoencodebin->m_elements_linked == FALSE)
        {
            gst_hailoencodebin_link_elements(GST_ELEMENT(hailoencodebin));
            hailoencodebin->m_elements_linked = TRUE;
        }
        break;
    }
    case PROP_CONFIG:
    {
        gpointer config = g_value_get_pointer(value);
        g_object_set(hailoencodebin->m_encoder, "config", config, NULL);
        break;
    }
    case PROP_WAIT_FOR_WRITABLE_BUFFER:
    {
        gboolean wait_for_writable_buffer = g_value_get_boolean(value);
        g_object_set(hailoencodebin->m_osd, "wait-for-writable-buffer", wait_for_writable_buffer, NULL);
        break;
    }
    case PROP_ENFORCE_CAPS:
    {
        gboolean enforce_caps = g_value_get_boolean(value);
        g_object_set(hailoencodebin->m_encoder, "enforce-caps", enforce_caps, NULL);
        break;
    }
    case PROP_QUEUE_SIZE:
    {
        hailoencodebin->queue_size = g_value_get_uint(value);
        g_object_set(hailoencodebin->m_queue_videorate, "max-size-buffers", hailoencodebin->queue_size, NULL);
        g_object_set(hailoencodebin->m_queue_encoder, "max-size-buffers", hailoencodebin->queue_size, NULL);
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gst_hailoencodebin_get_property(GObject *object, guint property_id,
                                     GValue *value, GParamSpec *pspec)
{
    GstHailoEncodeBin *hailoencodebin = GST_HAILO_ENCODE_BIN(object);
    GST_DEBUG_OBJECT(hailoencodebin, "get_property");
    switch (property_id)
    {
    // Handle property retrievals here
    case PROP_CONFIG_FILE_PATH:
    {
        g_value_set_string(value, hailoencodebin->config_file_path);
        break;
    }
    case PROP_CONFIG_STRING:
    {
        g_value_set_string(value, hailoencodebin->config_string.c_str());
        break;
    }
    case PROP_CONFIG:
    {
        gpointer config;
        g_object_get(hailoencodebin->m_encoder, "config", &config, NULL);
        g_value_set_pointer(value, config);
        break;
    }
    case PROP_WAIT_FOR_WRITABLE_BUFFER:
    {
        gboolean wait_for_writable_buffer;
        g_object_get(hailoencodebin->m_osd, "wait-for-writable-buffer", &wait_for_writable_buffer, NULL);
        g_value_set_boolean(value, wait_for_writable_buffer);
        break;
    }
    case PROP_BLENDER:
    {
        gpointer blender;
        g_object_get(hailoencodebin->m_osd, "blender", &blender, NULL);
        g_value_set_pointer(value, blender);
        break;
    }
    case PROP_QUEUE_SIZE:
    {
        g_value_set_uint(value, hailoencodebin->queue_size);
        break;
    }
    case PROP_ENFORCE_CAPS:
    {
        gboolean enforce;
        g_object_get(hailoencodebin->m_encoder, "enforce-caps", &enforce, NULL);
        g_value_set_boolean(value, enforce);
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gst_hailoencodebin_init_ghost_sink(GstHailoEncodeBin *hailoencodebin)
{
    // Get the connecting pad
    const gchar *pad_name = "sink";
    GstPad *pad = gst_element_get_static_pad(hailoencodebin->m_osd, pad_name);

    // Create a ghostpad and connect it to the bin
    GstPadTemplate *pad_tmpl = gst_static_pad_template_get(&sink_template);
    hailoencodebin->sinkpad = gst_ghost_pad_new_from_template(pad_name, pad, pad_tmpl);
    gst_pad_set_active(hailoencodebin->sinkpad, TRUE);
    gst_element_add_pad(GST_ELEMENT(hailoencodebin), hailoencodebin->sinkpad);

    // Cleanup
    gst_object_unref(pad_tmpl);
    gst_object_unref(pad);
}

void gst_hailoencodebin_init_ghost_src(GstHailoEncodeBin *hailoencodebin)
{
    // Get the connecting pad
    const gchar *pad_name = "src";
    GstPad *pad = gst_element_get_static_pad(hailoencodebin->m_encoder, pad_name);

    // Create a ghostpad and connect it to the bin
    GstPadTemplate *pad_tmpl = gst_static_pad_template_get(&src_template);
    hailoencodebin->srcpad = gst_ghost_pad_new_from_template(pad_name, pad, pad_tmpl);
    gst_pad_set_active(hailoencodebin->srcpad, TRUE);
    gst_element_add_pad(GST_ELEMENT(hailoencodebin), hailoencodebin->srcpad);

    // Cleanup
    gst_object_unref(pad_tmpl);
    gst_object_unref(pad);
}

static gboolean
gst_hailoencodebin_link_elements(GstElement *element)
{
    GstHailoEncodeBin *self = GST_HAILO_ENCODE_BIN(element);

    // Link the elements
    gboolean link_status = gst_element_link_many(self->m_osd,
                                                 self->m_queue_videorate,
                                                 self->m_videorate,
                                                 self->m_queue_encoder,
                                                 self->m_encoder, NULL);
    if (!link_status)
    {
        GST_ERROR_OBJECT(self, "Failed to link elements in bin!");
        return FALSE;
    }

    return TRUE;
}

static void
gst_hailoencodebin_dispose(GObject *object)
{
    GstHailoEncodeBin *self = GST_HAILO_ENCODE_BIN(object);
    GST_DEBUG_OBJECT(self, "dispose");

    gst_hailoencodebin_reset(self);

    G_OBJECT_CLASS(gst_hailoencodebin_parent_class)->dispose(object);
}

static void
gst_hailoencodebin_reset(GstHailoEncodeBin *self)
{
    GST_DEBUG_OBJECT(self, "reset");
    if (self->sinkpad != NULL)
    {
        self->sinkpad = NULL;
    }
    if (self->srcpad != NULL)
    {
        self->srcpad = NULL;
    }
}