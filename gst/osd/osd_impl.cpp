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

#include "osd_impl.hpp"
#include "buffer_utils/buffer_utils.hpp"
#include <algorithm>
#include <chrono>
#include <thread>

#define WIDTH_PADDING 10

cv::Mat OverlayImpl::resize_mat(cv::Mat mat, int width, int height)
{
    // ensure even dimensions, round up not to clip image
    width += (width % 2);
    height += (height % 2);

    // resize the mat image to target size
    cv::Mat resized_image;
    cv::resize(mat, resized_image, cv::Size(width, height), 0, 0, cv::INTER_AREA);
    return resized_image;
}

cv::Mat OverlayImpl::rotate_mat(cv::Mat mat, uint angle, osd::rotation_alignment_policy_t alignment_policy, cv::Point *center_drift)
{
    *center_drift = cv::Point(0, 0);
    if (angle == 0)
    {
        return mat;
    }

    // Get the image's center
    cv::Point2f center(mat.cols / 2.0, mat.rows / 2.0);

    // Get the rotation matrix
    cv::Mat rot = cv::getRotationMatrix2D(center, angle, 1.0);

    // Define the destination image
    cv::Rect bbox = cv::RotatedRect(center, mat.size(), angle).boundingRect();

    // Ensure the bounding box dimensions are even
    bbox.width += bbox.width % 2;
    bbox.height += bbox.height % 2;

    // Adjust transformation matrix to rotate around center and not top-left corner
    rot.at<double>(0, 2) += bbox.width / 2.0 - center.x;
    rot.at<double>(1, 2) += bbox.height / 2.0 - center.y;

    cv::Mat result;
    cv::warpAffine(mat, result, rot, bbox.size());

    if (alignment_policy == osd::rotation_alignment_policy_t::CENTER)
    {
        // adjust top left corner because center of image may move during rotation
        cv::Point2f new_center(result.cols / 2.0, result.rows / 2.0);
        *center_drift = center - new_center;
    }
    return result;
}

GstVideoFrame OverlayImpl::gst_video_frame_from_mat_bgra(cv::Mat mat)
{
    // Create caps at BGRA format and required size
    GstCaps *caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "BGRA",
                                        "width", G_TYPE_INT, mat.cols,
                                        "height", G_TYPE_INT, mat.rows,
                                        NULL);
    // Create GstVideoInfo meta from those caps
    GstVideoInfo *image_info = gst_video_info_new();
    gst_video_info_from_caps(image_info, caps);
    // Create a GstBuffer from the cv::mat, allowing for contiguous memory
    GstBuffer *buffer = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY,
                                                    mat.data,
                                                    mat.total() * mat.elemSize(),
                                                    0, mat.total() * mat.elemSize(),
                                                    NULL, NULL);
    // Create and map a GstVideoFrame from the GstVideoInfo and GstBuffer
    GstVideoFrame frame;
    gst_video_frame_map(&frame, image_info, buffer, GST_MAP_READ);
    gst_video_info_free(image_info);
    gst_caps_unref(caps);
    return frame;
}

media_library_return OverlayImpl::create_gst_video_frame(uint width, uint height, std::string format, GstVideoFrame *frame)
{
    media_library_return ret = MEDIA_LIBRARY_SUCCESS;
    // Create caps at format and required size
    GstCaps *caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, format.c_str(),
                                        "width", G_TYPE_INT, width,
                                        "height", G_TYPE_INT, height,
                                        NULL);

    void *buffer_ptr = NULL;

    // Create GstVideoInfo meta from those caps
    GstVideoInfo *image_info = gst_video_info_new();
    gst_video_info_from_caps(image_info, caps);
    uint buffer_size = image_info->size;
    dsp_status buffer_status = dsp_utils::create_hailo_dsp_buffer(buffer_size, &buffer_ptr);

    if (buffer_status != DSP_SUCCESS)
    {
        gst_caps_unref(caps);
        LOGGER__ERROR("Error: create_hailo_dsp_buffer - failed to create buffer");
        return MEDIA_LIBRARY_DSP_OPERATION_ERROR;
    }

    // Create a GstBuffer from the cv::mat, allowing for contiguous memory
    GstBuffer *buffer = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY,
                                                    buffer_ptr,
                                                    buffer_size,
                                                    0, buffer_size,
                                                    buffer_ptr, GDestroyNotify(dsp_utils::release_hailo_dsp_buffer));

    // Create and map a GstVideoFrame from the GstVideoInfo and GstBuffer
    gst_video_frame_map(frame, image_info, buffer, GST_MAP_READ);
    gst_buffer_unref(buffer);
    gst_video_info_free(image_info);
    gst_caps_unref(caps);
    return ret;
}

media_library_return OverlayImpl::convert_2_dsp_video_frame(GstVideoFrame *src_frame, GstVideoFrame *dest_frame, GstVideoFormat dest_format)
{
    media_library_return ret = MEDIA_LIBRARY_SUCCESS;

    // Prepare the video info and set the new format
    GstVideoInfo *dest_info = gst_video_info_new();
    gst_video_info_set_format(dest_info, dest_format, src_frame->info.width, src_frame->info.height);
    // Prepare the GstVideoConverter that will facilitate the conversion
    GstVideoConverter *converter = gst_video_converter_new(&src_frame->info, dest_info, NULL);

    void *buffer_ptr = NULL;
    dsp_status buffer_status = dsp_utils::create_hailo_dsp_buffer(dest_info->size, &buffer_ptr);
    if (buffer_status != DSP_SUCCESS)
    {
        LOGGER__ERROR("Error: create_hailo_dsp_buffer - failed to create buffer");
        ret = MEDIA_LIBRARY_DSP_OPERATION_ERROR;
    }
    else
    {
        GstBuffer *buffer = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS,
                                                        buffer_ptr,
                                                        dest_info->size, 0, dest_info->size, buffer_ptr, GDestroyNotify(dsp_utils::release_hailo_dsp_buffer));

        // Prepare the destination buffer and frame
        gst_video_frame_map(dest_frame, dest_info, buffer, GST_MAP_WRITE);
        gst_buffer_unref(buffer);
        // Make the conversion
        gst_video_converter_frame(converter, src_frame, dest_frame);
    }

    gst_video_converter_free(converter);

    return ret;
}

tl::expected<std::tuple<int, int>, media_library_return> OverlayImpl::calc_xy_offsets(std::string id, float x_norm, float y_norm, int overlay_width, int overlay_height, int image_width, int image_height, int x_drift, int y_drift)
{
    int x_offset = x_norm * image_width;
    int y_offset = y_norm * image_height;

    x_offset += x_drift;
    y_offset += y_drift;

    if (x_offset + overlay_width > image_width)
    {
        LOGGER__ERROR("overlay {} too wide to fit in frame! Adjust width or x offset. (x_offset: {}, frame_width: {})", id, x_offset + overlay_width, image_width);
        return tl::make_unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
    }

    if (y_offset + overlay_height > image_height)
    {
        LOGGER__ERROR("overlay {} too tall to fit in frame! Adjust height or y offset. (y_offset: {}, frame_height: {})", id, y_offset + overlay_height, image_height);
        return tl::make_unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
    }

    if (x_offset < 0)
    {
        LOGGER__ERROR("overlay {} can't fit in frame! Adjust x offset. ({})", id, x_offset);
        return tl::make_unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
    }

    if (y_offset < 0)
    {
        LOGGER__ERROR("overlay {} can't fit in frame! Adjust y offset. ({})", id, y_offset);
        return tl::make_unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
    }

    std::tuple ret = {x_offset, y_offset};
    return ret;
}

OverlayImpl::OverlayImpl(std::string id, float x, float y, float width, float height, unsigned int z_index, unsigned int angle, osd::rotation_alignment_policy_t rotation_policy, bool ready_to_blend = true) : m_id(id), m_x(x), m_y(y), m_width(width), m_height(height), m_z_index(z_index), m_angle(angle), m_rotation_policy(rotation_policy), m_ready_to_blend(ready_to_blend)
{
}

bool OverlayImpl::get_ready_to_blend()
{
    /*
    no need to lock this field because it can only be
    changed once and the only possible direction is from false to true,
    so there is no way to get a true when it is false.
    */
    return m_ready_to_blend;
}

OverlayImpl::~OverlayImpl()
{
    // free all resources
    free_resources();
}

void OverlayImpl::free_resources()
{
    for (auto &video_frame : m_video_frames)
    {
        gst_video_frame_unmap(&video_frame);
    }
    for (auto &dsp_overlay : m_dsp_overlays)
    {
        dsp_utils::free_overlay_property_planes(&dsp_overlay);
    }

    m_video_frames.clear();
    m_dsp_overlays.clear();
}

bool OverlayImpl::operator<(const OverlayImpl &other)
{
    return m_z_index < other.m_z_index;
}

std::set<OverlayImplPtr>::iterator OverlayImpl::get_priority_iterator()
{
    return m_priority_iterator;
}

void OverlayImpl::set_priority_iterator(std::set<OverlayImplPtr>::iterator priority_iterator)
{
    m_priority_iterator = priority_iterator;
}

ImageOverlayImpl::ImageOverlayImpl(const osd::ImageOverlay &overlay, media_library_return &status) : OverlayImpl(overlay.id, overlay.x, overlay.y, overlay.width, overlay.height, overlay.z_index, overlay.angle, overlay.rotation_alignment_policy, false),
                                                                                                     m_path(overlay.image_path)
{
    status = MEDIA_LIBRARY_SUCCESS;
}

TextOverlayImpl::TextOverlayImpl(const osd::TextOverlay &overlay, media_library_return &status) : OverlayImpl(overlay.id, overlay.x, overlay.y, 0, 0, overlay.z_index, overlay.angle, overlay.rotation_alignment_policy, false),
                                                                                                  m_label(overlay.label), m_rgb{overlay.rgb.red, overlay.rgb.green, overlay.rgb.blue}, m_font_size(overlay.font_size), m_line_thickness(overlay.line_thickness), m_font_path(overlay.font_path)
{
    if (m_label.empty())
    {
        LOGGER__ERROR("m_label is empty");
        status = MEDIA_LIBRARY_ERROR;
        return;
    }

    // check if the font file exists
    if (!cv::utils::fs::exists(m_font_path))
    {
        LOGGER__ERROR("Error: file {} does not exist", m_font_path);
        status = MEDIA_LIBRARY_INVALID_ARGUMENT;
        return;
    }

    cv::Ptr<cv::freetype::FreeType2> ft2;
    ft2 = cv::freetype::createFreeType2();
    ft2->loadFontData(m_font_path, 0);

    // calculate the size of the text
    int baseline = 0;
    cv::Size text_size = ft2->getTextSize(m_label,
                                          m_font_size,
                                          m_line_thickness,
                                          &baseline);

    // ensure even dimensions, round up not to clip text
    text_size.width += text_size.width % 2;
    text_size.height += text_size.height % 2;

    baseline += baseline % 2;
    m_width = text_size.width;
    m_height = text_size.height + baseline;

    cv::Scalar background_color_rgb(255, 255, 255);
    cv::Scalar background_color_rgba(255, 255, 255, 0); // transparent background

    // check if the desired font color is black, if so, use white background
    if (m_rgb[0] == 255 && m_rgb[1] == 255 && m_rgb[2] == 255)
    {
        background_color_rgb = cv::Scalar(0, 0, 0);
        background_color_rgba = cv::Scalar(0, 0, 0, 0);
    }

    cv::Mat rgb_mat = cv::Mat(text_size.height + baseline, text_size.width + WIDTH_PADDING, CV_8UC3, background_color_rgb);

    m_image_mat = cv::Mat(text_size.height + baseline, text_size.width + WIDTH_PADDING, CV_8UC4, background_color_rgba);

    auto text_position = cv::Point(0, text_size.height);
    cv::Scalar text_color(m_rgb[2], m_rgb[1], m_rgb[0], 255); // The input is expected RGB, but we draw as BGRA
    cv::Scalar rgb_text_color(m_rgb[0], m_rgb[1], m_rgb[2]);  // The input is expected RGB, but we draw as BGRA

    ft2->putText(rgb_mat, m_label, text_position, m_font_size, rgb_text_color, cv::FILLED, 8, true);

    for (int i = 0; i < rgb_mat.rows; i++)
    {
        for (int j = 0; j < rgb_mat.cols; j++)
        {
            if (rgb_mat.at<cv::Vec3b>(i, j)[0] != background_color_rgb[0] || rgb_mat.at<cv::Vec3b>(i, j)[1] != background_color_rgb[1] || rgb_mat.at<cv::Vec3b>(i, j)[2] != background_color_rgb[2])
            {
                m_image_mat.at<cv::Vec4b>(i, j)[0] = rgb_mat.at<cv::Vec3b>(i, j)[2];
                m_image_mat.at<cv::Vec4b>(i, j)[1] = rgb_mat.at<cv::Vec3b>(i, j)[1];
                m_image_mat.at<cv::Vec4b>(i, j)[2] = rgb_mat.at<cv::Vec3b>(i, j)[0];
                m_image_mat.at<cv::Vec4b>(i, j)[3] = 255;
            }
        }
    }

    status = MEDIA_LIBRARY_SUCCESS;
}

DateTimeOverlayImpl::DateTimeOverlayImpl(const osd::DateTimeOverlay &overlay, media_library_return &status) : OverlayImpl(overlay.id, overlay.x, overlay.y, 0, 0, overlay.z_index, overlay.angle, overlay.rotation_alignment_policy, false),
                                                                                                              m_rgb{overlay.rgb.red, overlay.rgb.green, overlay.rgb.blue}, m_font_size(overlay.font_size), m_line_thickness(overlay.line_thickness)
{
    status = MEDIA_LIBRARY_SUCCESS;
}

CustomOverlayImpl::CustomOverlayImpl(const osd::CustomOverlay &overlay, media_library_return &status) : OverlayImpl(overlay.id, overlay.x, overlay.y, overlay.width, overlay.height, overlay.z_index, overlay.angle, overlay.rotation_alignment_policy, false)
{
    status = MEDIA_LIBRARY_SUCCESS;
}

tl::expected<ImageOverlayImplPtr, media_library_return> ImageOverlayImpl::create(const osd::ImageOverlay &overlay)
{
    media_library_return status = MEDIA_LIBRARY_UNINITIALIZED;
    auto osd_overlay = std::make_shared<ImageOverlayImpl>(overlay, status);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return osd_overlay;
}

tl::expected<TextOverlayImplPtr, media_library_return> TextOverlayImpl::create(const osd::TextOverlay &overlay)
{
    media_library_return status = MEDIA_LIBRARY_UNINITIALIZED;
    auto osd_overlay = std::make_shared<TextOverlayImpl>(overlay, status);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return osd_overlay;
}

tl::expected<DateTimeOverlayImplPtr, media_library_return> DateTimeOverlayImpl::create(const osd::DateTimeOverlay &overlay)
{
    media_library_return status = MEDIA_LIBRARY_UNINITIALIZED;
    auto osd_overlay = std::make_shared<DateTimeOverlayImpl>(overlay, status);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return osd_overlay;
}

tl::expected<CustomOverlayImplPtr, media_library_return> CustomOverlayImpl::create(const osd::CustomOverlay &overlay)
{
    media_library_return status = MEDIA_LIBRARY_UNINITIALIZED;
    auto osd_overlay = std::make_shared<CustomOverlayImpl>(overlay, status);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return osd_overlay;
}

std::shared_future<tl::expected<ImageOverlayImplPtr, media_library_return>> ImageOverlayImpl::create_async(const osd::ImageOverlay &overlay)
{
    return std::async(std::launch::async, [overlay]()
                      { return create(overlay); })
        .share();
}

std::shared_future<tl::expected<TextOverlayImplPtr, media_library_return>> TextOverlayImpl::create_async(const osd::TextOverlay &overlay)
{
    return std::async(std::launch::async, [overlay]()
                      { return create(overlay); })
        .share();
}

std::shared_future<tl::expected<DateTimeOverlayImplPtr, media_library_return>> DateTimeOverlayImpl::create_async(const osd::DateTimeOverlay &overlay)
{
    return std::async(std::launch::async, [overlay]()
                      { return create(overlay); })
        .share();
}

std::shared_future<tl::expected<CustomOverlayImplPtr, media_library_return>> CustomOverlayImpl::create_async(const osd::CustomOverlay &overlay)
{
    return std::async(std::launch::async, [overlay]()
                      { return create(overlay); })
        .share();
}

tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> OverlayImpl::create_dsp_overlays(int frame_width, int frame_height)
{
    if (frame_width == 0 || frame_height == 0)
    {
        return tl::make_unexpected(MEDIA_LIBRARY_UNINITIALIZED);
    }

    media_library_return status;
    m_dsp_overlays.clear();

    GstVideoFrame dest_frame;
    if (m_image_mat.empty())
    {
        LOGGER__ERROR("m_image_mat is empty");
        status = MEDIA_LIBRARY_ERROR;
        return tl::make_unexpected(status);
    }

    cv::Mat mat = m_image_mat;
    cv::Point center_drift{0, 0};
    if (m_angle != 0)
    {
        mat = OverlayImpl::rotate_mat(m_image_mat, m_angle, m_rotation_policy, &center_drift);
        LOGGER__DEBUG("Rotated OSD by {} degrees, center drifted by {} pixels, around {}", m_angle, center_drift, m_rotation_policy);
    }

    GstVideoFrame gst_bgra_image = gst_video_frame_from_mat_bgra(mat);
    status = convert_2_dsp_video_frame(&gst_bgra_image, &dest_frame, GST_VIDEO_FORMAT_A420);

    gst_buffer_unref(gst_bgra_image.buffer);
    gst_video_frame_unmap(&gst_bgra_image);

    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(status);
    }

    dsp_image_properties_t dsp_image;
    create_dsp_buffer_from_video_frame(&dest_frame, dsp_image);
    m_video_frames.push_back(dest_frame);
    auto offsets_expected = calc_xy_offsets(m_id, m_x, m_y, dsp_image.width, dsp_image.height, frame_width, frame_height, center_drift.x, center_drift.y);
    if (!offsets_expected.has_value())
    {
        return tl::make_unexpected(offsets_expected.error());
    }

    auto [x_offset, y_offset] = offsets_expected.value();
    dsp_overlay_properties_t dsp_overlay =
        {
            .overlay = dsp_image,
            .x_offset = (size_t)x_offset,
            .y_offset = (size_t)y_offset,
        };

    m_dsp_overlays.push_back(dsp_overlay);
    m_ready_to_blend = true;
    return m_dsp_overlays;
}

tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> ImageOverlayImpl::create_dsp_overlays(int frame_width, int frame_height)
{
    if (frame_width == 0 || frame_height == 0)
    {
        return tl::make_unexpected(MEDIA_LIBRARY_UNINITIALIZED);
    }

    // check if the file exists
    if (!cv::utils::fs::exists(m_path))
    {
        LOGGER__ERROR("Error: file {} does not exist", m_path);
        return tl::make_unexpected(MEDIA_LIBRARY_INVALID_ARGUMENT);
    }

    // read the image from file, keeping alpha channel
    m_image_mat = cv::imread(m_path, cv::IMREAD_UNCHANGED);

    // check if the image was read successfully
    if (m_image_mat.empty())
    {
        LOGGER__ERROR("Error: failed to read image file {}", m_path);
        return tl::make_unexpected(MEDIA_LIBRARY_INVALID_ARGUMENT);
    }

    // convert image to 4-channel RGBA format if necessary
    if (m_image_mat.channels() != 4)
    {
        LOGGER__INFO("READ IMAGE THAT WAS NOT 4 channels");
        cv::cvtColor(m_image_mat, m_image_mat, cv::COLOR_BGR2BGRA);
    }

    m_image_mat = resize_mat(m_image_mat, m_width * frame_width, m_height * frame_height);

    return OverlayImpl::create_dsp_overlays(frame_width, frame_height);
}

tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> CustomOverlayImpl::create_dsp_overlays(int frame_width, int frame_height)
{
    if (frame_width == 0 || frame_height == 0)
    {
        return tl::make_unexpected(MEDIA_LIBRARY_UNINITIALIZED);
    }

    media_library_return status;
    if (!m_dsp_overlays.empty())
    {
        return m_dsp_overlays;
    }

    GstVideoFrame dest_frame;
    status = create_gst_video_frame(m_width * frame_width, m_height * frame_height, "A420", &dest_frame);

    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(status);
    }

    dsp_image_properties_t dsp_image;
    create_dsp_buffer_from_video_frame(&dest_frame, dsp_image);
    m_video_frames.push_back(dest_frame);
    auto offsets_expected = calc_xy_offsets(m_id, m_x, m_y, dsp_image.width, dsp_image.height, frame_width, frame_height, 0, 0);
    if (!offsets_expected.has_value())
    {
        return tl::make_unexpected(offsets_expected.error());
    }

    auto [x_offset, y_offset] = offsets_expected.value();
    dsp_overlay_properties_t dsp_overlay =
        {
            .overlay = dsp_image,
            .x_offset = (size_t)x_offset,
            .y_offset = (size_t)y_offset,
        };

    m_dsp_overlays.push_back(dsp_overlay);
    m_ready_to_blend = true;
    return m_dsp_overlays;
}

tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> DateTimeOverlayImpl::create_dsp_overlays(int frame_width, int frame_height)
{
    if (frame_width == 0 || frame_height == 0)
    {
        return tl::make_unexpected(MEDIA_LIBRARY_UNINITIALIZED);
    }

    std::string datetime = select_chars_for_timestamp();
    if (datetime == m_datetime_str)
    {
        return m_dsp_overlays;
    }

    m_ready_to_blend = false; // temporarily change status to not ready.

    m_datetime_str = datetime;
    m_frame_width = frame_width;
    m_frame_height = frame_height;

    OverlayImpl::free_resources();

    // calculate the size of the text
    int baseline = 0;
    cv::Size text_size = cv::getTextSize(m_datetime_str, cv::FONT_HERSHEY_SIMPLEX, m_font_size, m_line_thickness, &baseline);

    // ensure even dimensions, round up not to clip text
    text_size.width += text_size.width % 2;
    text_size.height += text_size.height % 2;

    m_width = text_size.width;
    m_height = text_size.height + baseline;

    // create a transparent BGRA image
    m_image_mat = cv::Mat(text_size.height + baseline, text_size.width, CV_8UC4, cv::Scalar(0, 0, 0, 0));

    auto text_position = cv::Point(0, text_size.height);
    cv::Scalar text_color(m_rgb[2], m_rgb[1], m_rgb[0], 255); // The input is expected RGB, but we draw as BGRA

    // draw the text
    cv::putText(m_image_mat, m_datetime_str, text_position, cv::FONT_HERSHEY_SIMPLEX, m_font_size, text_color, m_line_thickness);

    return OverlayImpl::create_dsp_overlays(frame_width, frame_height);
}

tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> OverlayImpl::get_dsp_overlays()
{
    if (!m_ready_to_blend)
    {
        LOGGER__ERROR("overlay not ready to blend");
        return tl::make_unexpected(MEDIA_LIBRARY_UNINITIALIZED);
    }

    return m_dsp_overlays;
}

tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> DateTimeOverlayImpl::get_dsp_overlays()
{
    if (!m_ready_to_blend)
    {
        LOGGER__ERROR("overlay not ready to blend");
        return tl::make_unexpected(MEDIA_LIBRARY_UNINITIALIZED);
    }

    // in case of DateTime, overlay needs to be refreshed with the current time
    create_dsp_overlays(m_frame_width, m_frame_height);

    return m_dsp_overlays;
}

std::string DateTimeOverlayImpl::select_chars_for_timestamp()
{
    auto system_time = std::chrono::system_clock::now();
    std::time_t ttime = std::chrono::system_clock::to_time_t(system_time);
    std::tm *time_now = std::gmtime(&ttime);

    // std::vector<char> char_indices(11);

    std::string chars;

    chars += std::to_string(time_now->tm_mday);
    chars += '-';
    chars += std::to_string(time_now->tm_mon);
    chars += '-';
    chars += std::to_string(1900 + time_now->tm_year);
    chars += ' ';
    chars += std::to_string(time_now->tm_hour);
    chars += ':';
    chars += std::to_string(time_now->tm_min);
    chars += ':';
    chars += std::to_string(time_now->tm_sec);

    return chars;
}

std::shared_ptr<osd::Overlay> ImageOverlayImpl::get_metadata()
{
    return std::make_shared<osd::ImageOverlay>(m_id, m_x, m_y, m_width, m_height, m_path, m_z_index, m_angle, m_rotation_policy);
}

std::shared_ptr<osd::Overlay> TextOverlayImpl::get_metadata()
{
    return std::make_shared<osd::TextOverlay>(m_id, m_x, m_y, m_label, osd::rgb_color_t{m_rgb[0], m_rgb[1], m_rgb[2]}, m_font_size, m_line_thickness, m_z_index, m_font_path, m_angle, m_rotation_policy);
}

std::shared_ptr<osd::Overlay> DateTimeOverlayImpl::get_metadata()
{
    return std::make_shared<osd::DateTimeOverlay>(m_id, m_x, m_y, osd::rgb_color_t{m_rgb[0], m_rgb[1], m_rgb[2]}, m_font_size, m_line_thickness, m_z_index, m_angle, m_rotation_policy);
}

std::shared_ptr<osd::Overlay> CustomOverlayImpl::get_metadata()
{
    DspImagePropertiesPtr dsp_image = std::make_shared<dsp_image_properties_t>(m_dsp_overlays[0].overlay);
    return std::make_shared<osd::CustomOverlay>(m_id, m_x, m_y, m_width, m_height, dsp_image, m_z_index);
}

namespace osd
{
    tl::expected<std::unique_ptr<Blender::Impl>, media_library_return> Blender::Impl::create(const std::string &config)
    {
        media_library_return status = MEDIA_LIBRARY_UNINITIALIZED;
        auto blender = std::unique_ptr<Blender::Impl>(new Blender::Impl(config, status));

        if (status != MEDIA_LIBRARY_SUCCESS)
        {
            return tl::make_unexpected(status);
        }
        return blender;
    }

    std::shared_future<tl::expected<std::unique_ptr<Blender::Impl>, media_library_return>> Blender::Impl::create_async(const std::string &config)
    {
        return std::async(std::launch::async, [&config]()
                          { return create(config); })
            .share();
    }

    Blender::Impl::Impl(const std::string &config, media_library_return &status)
    {
        m_frame_width = 0;
        m_frame_height = 0;
        m_frame_size_set = false;
        m_config_manager = std::make_shared<ConfigManager>(ConfigSchema::CONFIG_SCHEMA_OSD);
        // check if the config has ' at the beginning and end of the string. if so, remove them
        std::string clean_config = config; // config is const, so we need to copy it
        if (clean_config[0] == '\'' && clean_config[clean_config.size() - 1] == '\'')
        {
            clean_config = clean_config.substr(1, config.size() - 2);
        }
        m_config_manager->validate_configuration(clean_config);
        m_config = nlohmann::json::parse(clean_config);

        // Acquire DSP device
        dsp_status dsp_result = dsp_utils::acquire_device();
        if (dsp_result != DSP_SUCCESS)
        {
            status = MEDIA_LIBRARY_DSP_OPERATION_ERROR;
            LOGGER__ERROR("Accuire DSP device failed with status code {}", status);
            return;
        }

        if (m_config.contains("image"))
        {
            for (auto &image_json : m_config["image"])
            {
                auto overlay = image_json.template get<ImageOverlay>();
                add_overlay(overlay);
            }
        }
        if (m_config.contains("text"))
        {
            for (auto &text_json : m_config["text"])
            {
                auto overlay = text_json.template get<TextOverlay>();
                add_overlay(overlay);
            }
        }

        if (m_config.contains("dateTime"))
        {
            for (auto &datetime_json : m_config["dateTime"])
            {
                auto overlay = datetime_json.template get<DateTimeOverlay>();
                add_overlay(overlay);
            }
        }

        if (m_config.contains("custom"))
        {
            for (auto &custom_json : m_config["custom"])
            {
                auto overlay = custom_json.template get<CustomOverlay>();
                add_overlay(overlay);
            }
        }

        status = MEDIA_LIBRARY_SUCCESS;
    }

    Blender::Impl::~Impl()
    {
        m_prioritized_overlays.clear();
        m_overlays.clear();

        dsp_status dsp_result = dsp_utils::release_device();
        if (dsp_result != DSP_SUCCESS)
        {
            LOGGER__ERROR("Release DSP device failed with status code {}", dsp_result);
        }
    }

    std::shared_future<media_library_return> Blender::Impl::add_overlay_async(const DateTimeOverlay &overlay)
    {
        return std::async(std::launch::async, [this, overlay]()
                          { 
                            auto overlay_result = DateTimeOverlayImpl::create_async(overlay);
                            auto overlay_expected = overlay_result.get();
                            if (!overlay_expected.has_value())
                            {
                                LOGGER__ERROR("Failed to create datetime overlay {}", overlay.id);
                                return overlay_expected.error();
                            }

                            return add_overlay(overlay_expected.value()); })
            .share();
    }

    std::shared_future<media_library_return> Blender::Impl::add_overlay_async(const ImageOverlay &overlay)
    {
        return std::async(std::launch::async, [this, overlay]()
                          { 
                            auto overlay_result = ImageOverlayImpl::create_async(overlay);
                            auto overlay_expected = overlay_result.get();
                            if (!overlay_expected.has_value())
                            {
                                LOGGER__ERROR("Failed to create image overlay {}", overlay.id);
                                return overlay_expected.error();
                            }

                            return add_overlay(overlay_expected.value()); })
            .share();
    }

    std::shared_future<media_library_return> Blender::Impl::add_overlay_async(const TextOverlay &overlay)
    {
        return std::async(std::launch::async, [this, overlay]()
                          { 
                            auto overlay_result = TextOverlayImpl::create_async(overlay);
                            auto overlay_expected = overlay_result.get();
                            if (!overlay_expected.has_value())
                            {
                                LOGGER__ERROR("Failed to create text overlay {}", overlay.id);
                                return overlay_expected.error();
                            }

                            return add_overlay(overlay_expected.value()); })
            .share();
    }

    std::shared_future<media_library_return> Blender::Impl::add_overlay_async(const CustomOverlay &overlay)
    {
        return std::async(std::launch::async, [this, overlay]()
                          { 
                            auto overlay_result = CustomOverlayImpl::create_async(overlay);
                            auto overlay_expected = overlay_result.get();
                            if (!overlay_expected.has_value())
                            {
                                LOGGER__ERROR("Failed to create custom overlay {}", overlay.id);
                                return overlay_expected.error();
                            }

                            return add_overlay(overlay_expected.value()); })
            .share();
    }

    media_library_return Blender::Impl::add_overlay(const ImageOverlay &overlay)
    {
        auto overlay_expected = ImageOverlayImpl::create(overlay);
        if (!overlay_expected.has_value())
        {
            LOGGER__ERROR("Failed to create image overlay {}", overlay.id);
            return overlay_expected.error();
        }

        return add_overlay(overlay_expected.value());
    }

    media_library_return Blender::Impl::add_overlay(const TextOverlay &overlay)
    {
        auto overlay_expected = TextOverlayImpl::create(overlay);
        if (!overlay_expected.has_value())
        {
            LOGGER__ERROR("Failed to create text overlay {}", overlay.id);
            return overlay_expected.error();
        }

        return add_overlay(overlay_expected.value());
    }

    media_library_return Blender::Impl::add_overlay(const DateTimeOverlay &overlay)
    {
        auto overlay_expected = DateTimeOverlayImpl::create(overlay);
        if (!overlay_expected.has_value())
        {
            LOGGER__ERROR("Failed to create datetime overlay {}", overlay.id);
            return overlay_expected.error();
        }

        return add_overlay(overlay_expected.value());
    }

    media_library_return Blender::Impl::add_overlay(const CustomOverlay &overlay)
    {
        auto overlay_expected = CustomOverlayImpl::create(overlay);
        if (!overlay_expected.has_value())
        {
            LOGGER__ERROR("Failed to create custom overlay {}", overlay.id);
            return overlay_expected.error();
        }

        return add_overlay(overlay_expected.value());
    }

    media_library_return Blender::Impl::add_overlay(const OverlayImplPtr overlay)
    {
        if (m_overlays.contains(overlay->get_id()))
        {
            LOGGER__ERROR("Overlay with id {} already exists", overlay->get_id());
            return MEDIA_LIBRARY_INVALID_ARGUMENT;
        }

        if (m_frame_size_set) // if frame size is not set, the overlays will be initialized when the frame size is set
        {
            auto ret = overlay->create_dsp_overlays(m_frame_width, m_frame_height);
            if (!ret.has_value())
            {
                return ret.error();
            }
        }

        std::unique_lock ulock(m_mutex);
        return add_overlay_internal(overlay);
    }

    // this method is not thread safe, the caller should have a mutex locked
    media_library_return Blender::Impl::add_overlay_internal(const OverlayImplPtr overlay)
    {
        if (m_overlays.contains(overlay->get_id()))
        {
            LOGGER__ERROR("Overlay with id {} already exists", overlay->get_id());
            return MEDIA_LIBRARY_INVALID_ARGUMENT;
        }

        LOGGER__DEBUG("Inserting overlay with id {}", overlay->get_id());

        auto result1 = m_overlays.insert({overlay->get_id(), overlay});
        if (!result1.second)
        {
            LOGGER__ERROR("Failed to insert overlay with id {}", overlay->get_id());
            return MEDIA_LIBRARY_ERROR;
        }

        auto result2 = m_prioritized_overlays.insert(overlay);
        if (!result2.second)
        {
            LOGGER__ERROR("Failed to insert overlay with id {}", overlay->get_id());
            m_overlays.erase(overlay->get_id());
            return MEDIA_LIBRARY_ERROR;
        }
        result1.first->second->set_priority_iterator(result2.first);
        return MEDIA_LIBRARY_SUCCESS;
    }

    media_library_return Blender::Impl::remove_overlay(const std::string &id)
    {
        if (!m_overlays.contains(id))
        {
            LOGGER__ERROR("No overlay with id {}", id);
            return MEDIA_LIBRARY_INVALID_ARGUMENT;
        }

        std::unique_lock lock(m_mutex);
        return remove_overlay_internal(id);
    }

    // this method is not thread safe, the caller should have a mutex locked
    media_library_return Blender::Impl::remove_overlay_internal(const std::string &id)
    {
        if (!m_overlays.contains(id))
        {
            LOGGER__ERROR("No overlay with id {}", id);
            return MEDIA_LIBRARY_INVALID_ARGUMENT;
        }

        LOGGER__DEBUG("Removing overlay with id {}", id);

        m_prioritized_overlays.erase(m_overlays[id]->get_priority_iterator());
        m_overlays.erase(id);
        return MEDIA_LIBRARY_SUCCESS;
    }

    std::shared_future<media_library_return> Blender::Impl::remove_overlay_async(const std::string &id)
    {
        return std::async(std::launch::async, [this, id]()
                          { return remove_overlay(id); })
            .share();
    }

    tl::expected<std::shared_ptr<osd::Overlay>, media_library_return> Blender::Impl::get_overlay(const std::string &id)
    {
        std::shared_lock lock(m_mutex);
        if (!m_overlays.contains(id))
        {
            LOGGER__ERROR("No overlay with id {}", id);
            return tl::make_unexpected(MEDIA_LIBRARY_INVALID_ARGUMENT);
        }
        auto overlay = m_overlays[id];
        return m_overlays[id]->get_metadata();
    }

    std::shared_future<media_library_return> Blender::Impl::set_overlay_async(const ImageOverlay &overlay)
    {
        return std::async(std::launch::async, [this, overlay]()
                          {
                            auto overlay_result = ImageOverlayImpl::create_async(overlay);
                            auto overlay_expected = overlay_result.get();
                            if (!overlay_expected.has_value())
                            {
                                LOGGER__ERROR("Failed to set ImageOverlay {}", overlay.id);
                                return overlay_expected.error();
                            }
                                                        
                            return set_overlay(overlay_expected.value()); });
    }

    std::shared_future<media_library_return> Blender::Impl::set_overlay_async(const TextOverlay &overlay)
    {
        return std::async(std::launch::async, [this, overlay]()
                          {
                            auto overlay_result = TextOverlayImpl::create_async(overlay);
                            auto overlay_expected = overlay_result.get();
                            if (!overlay_expected.has_value())
                            {
                                LOGGER__ERROR("Failed to set TextOverlay {}", overlay.id);
                                return overlay_expected.error();
                            }

                            return set_overlay(overlay_expected.value()); })
            .share();
    }

    std::shared_future<media_library_return> Blender::Impl::set_overlay_async(const DateTimeOverlay &overlay)
    {
        return std::async(std::launch::async, [this, overlay]()
                          {
                            auto overlay_result = DateTimeOverlayImpl::create_async(overlay);
                            auto overlay_expected = overlay_result.get();
                            if (!overlay_expected.has_value())
                            {
                                LOGGER__ERROR("Failed to set DateTimeOverlay {}", overlay.id);
                                return overlay_expected.error();
                            }

                            return set_overlay(overlay_expected.value()); })
            .share();
    }

    std::shared_future<media_library_return> Blender::Impl::set_overlay_async(const CustomOverlay &overlay)
    {
        return std::async(std::launch::async, [this, overlay]()
                          {
                            auto overlay_result = CustomOverlayImpl::create_async(overlay);
                            auto overlay_expected = overlay_result.get();
                            if (!overlay_expected.has_value())
                            {
                                LOGGER__ERROR("Failed to set CustomOverlay {}", overlay.id);
                                return overlay_expected.error();
                            }
                            
                            return set_overlay(overlay_expected.value()); })
            .share();
    }

    media_library_return Blender::Impl::set_overlay(const ImageOverlay &overlay)
    {
        auto overlay_expected = ImageOverlayImpl::create(overlay);
        if (!overlay_expected.has_value())
        {
            LOGGER__ERROR("Failed to set text overlay {}", overlay.id);
            return overlay_expected.error();
        }

        return set_overlay(overlay_expected.value());
    }

    media_library_return Blender::Impl::set_overlay(const TextOverlay &overlay)
    {
        auto overlay_expected = TextOverlayImpl::create(overlay);
        if (!overlay_expected.has_value())
        {
            LOGGER__ERROR("Failed to set text overlay {}", overlay.id);
            return overlay_expected.error();
        }

        return set_overlay(overlay_expected.value());
    }

    media_library_return Blender::Impl::set_overlay(const DateTimeOverlay &overlay)
    {
        auto overlay_expected = DateTimeOverlayImpl::create(overlay);
        if (!overlay_expected.has_value())
        {
            LOGGER__ERROR("Failed to set datettime overlay {}", overlay.id);
            return overlay_expected.error();
        }

        return set_overlay(overlay_expected.value());
    }

    media_library_return Blender::Impl::set_overlay(const CustomOverlay &overlay)
    {
        auto overlay_expected = CustomOverlayImpl::create(overlay);
        if (!overlay_expected.has_value())
        {
            LOGGER__ERROR("Failed to set custom overlay {}", overlay.id);
            return overlay_expected.error();
        }

        return set_overlay(overlay_expected.value());
    }

    media_library_return Blender::Impl::set_overlay(const OverlayImplPtr overlay)
    {
        if (!m_overlays.contains(overlay->get_id()))
        {
            LOGGER__ERROR("No overlay with id {}", overlay->get_id());
            return MEDIA_LIBRARY_INVALID_ARGUMENT;
        }

        if (m_frame_size_set) // if frame size is not set, the overlays will be initialized when the frame size is set
        {
            auto ret = overlay->create_dsp_overlays(m_frame_width, m_frame_height);
            if (!ret.has_value())
            {
                return ret.error();
            }
        }

        std::unique_lock lock(m_mutex);
        if (remove_overlay_internal(overlay->get_id()) != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__ERROR("Failed to remove overlay with id {}", overlay->get_id());
            return MEDIA_LIBRARY_ERROR;
        }

        return add_overlay_internal(overlay);
    }

    media_library_return Blender::Impl::blend(dsp_image_properties_t &input_image_properties)
    {
        std::unique_lock lock(m_mutex);

        // We prepare to blend all overlays at once
        std::vector<dsp_overlay_properties_t> all_overlays_to_blend;
        all_overlays_to_blend.reserve(m_overlays.size());
        for (const auto &overlay : m_prioritized_overlays)
        {
            if (!overlay->get_ready_to_blend())
            {
                continue;
            }

            auto dsp_overlays_expected = overlay->get_dsp_overlays();
            if (!dsp_overlays_expected.has_value())
            {
                LOGGER__ERROR("Failed to get DSP compatible overlays ({})", dsp_overlays_expected.error());
                return dsp_overlays_expected.error();
            }
            auto dsp_overlays = dsp_overlays_expected.value();
            all_overlays_to_blend.insert(all_overlays_to_blend.end(), dsp_overlays.begin(), dsp_overlays.end());
        }

        LOGGER__DEBUG("Blending {} overlays", all_overlays_to_blend.size());

        // Perform blending for all overlays
        for (unsigned int i = 0; i < all_overlays_to_blend.size(); i += dsp_utils::max_blend_overlays)
        {
            auto first = all_overlays_to_blend.begin() + i;
            auto last = all_overlays_to_blend.end();

            if (i + dsp_utils::max_blend_overlays < all_overlays_to_blend.size())
            {
                last = first + dsp_utils::max_blend_overlays;
            }

            std::vector blend_chuck(first, last);

            dsp_status status = dsp_utils::perform_dsp_multiblend(&input_image_properties, blend_chuck.data(), blend_chuck.size());
            if (status != DSP_SUCCESS)
            {
                LOGGER__ERROR("DSP blend failed with {}", status);
                return MEDIA_LIBRARY_DSP_OPERATION_ERROR;
            }
        }

        return MEDIA_LIBRARY_SUCCESS;
    }

    media_library_return Blender::Impl::set_frame_size(int frame_width, int frame_height)
    {
        std::unique_lock lock(m_mutex);

        m_frame_width = frame_width;
        m_frame_height = frame_height;
        m_frame_size_set = true;

        // Initialize static images
        std::vector<dsp_overlay_properties_t> overlays;
        overlays.reserve(m_overlays.size());
        for (const auto &overlay : m_prioritized_overlays)
        {
            auto overlays_expected = overlay->create_dsp_overlays(frame_width, frame_height);
            if (!overlays_expected.has_value())
            {
                LOGGER__ERROR("Failed to prepare overlays ({})", overlays_expected.error());
                return overlays_expected.error();
            }
        }

        return MEDIA_LIBRARY_SUCCESS;
    }
}