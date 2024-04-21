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
 * @file encoder.hpp
 * @brief MediaLibrary Encoder + OSD CPP API module
 **/
#pragma once
#include "media_library/buffer_pool.hpp"
#include "media_library/media_library_types.hpp"
#include "osd.hpp"
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <tl/expected.hpp>

/*!
 * @brief type for user callback function to receive encoded frames
 *
 * the provided user function receives the encoded frames and their sizes
 * as uint32_t argument.
 * @ref EncoderOSD::subscribe
 */
typedef std::function<void(HailoMediaLibraryBufferPtr, uint32_t)>
    AppWrapperCallback;

/** @defgroup encoder_type_definitions MediaLibrary Encoder + OSD CPP API
 * definitions
 *  @{
 */
class MediaLibraryEncoder;
using MediaLibraryEncoderPtr = std::shared_ptr<MediaLibraryEncoder>;

/**
 * @brief Encode stream object with OSD features.
 *
 * Each instance represents an encoded output stream.
 * Per each stream user can configure several static and dynamic OSD overlays.
 *
 * Encoding and static OSD configurations are applied during instance
 * construction.
 *
 * Encoder static configuration includes the resolution, framerate, encoding
 * format.
 * Encoder dynamic configuration includes rate, Qp, ROI and other arguments.
 * @ref TBD json configuration.
 *
 * OSD static overlays support images, static text and timestamp blocks.
 * @ref TBD json configuration.
 *
 * @note OSD is mostly executed by the DSP co-prossesor, and encoding is
 * executed by the A53 or the HEVC/H264 encoder co-processor.
 */
class MediaLibraryEncoder
{
private:
  class Impl;
  std::shared_ptr<Impl> m_impl;

public:
  /**
   * @brief Constructs an MediaLibraryEncoder object
   *
   * @param[in] json_config - json configuration string
   * @return tl::expected<MediaLibraryEncoderPtr, media_library_return> -
   * An expected object that holds either a shared pointer
   *  to an MediaLibraryEncoder object, or a error code.
   */
  static tl::expected<MediaLibraryEncoderPtr, media_library_return>
  create(std::string json_config);
  /**
   * @brief Start the MediaLibraryEncoder module, the MediaLibraryEncoder
   * module will be ready to encode buffers.
   * @return media_library_return - status of the start operation
   */
  media_library_return start();
  /**
   * @brief Stop the MediaLibraryEncoder module, the MediaLibraryEncoder
   * module will stop encoding buffers.
   * @return media_library_return - status of the stop operation
   */
  media_library_return stop();
  /**
   * @brief Subscribe to the MediaLibraryEncoder module to receive the output
   * buffers
   * @param[in] callback - callback function to be called when a buffer is
   * ready. The callback function AppWrapperCallback object. The callback
   * function should be thread safe. The callback function should not block.
   * The callback function should not throw exceptions.
   * The callback function should not call the MediaLibraryEncoder module.
   * @return media_library_return - status of the subscription operation
   * @note if user wishes to add additional arguments for the callback
   * execution it may use lambda wrapper and pass as single argument to the
   * subscribe function.
   * @ref AppWrapperCallback
   */
  media_library_return subscribe(AppWrapperCallback callback);
  /**
   * @brief Add a buffer to the MediaLibraryEncoder module, to be encoded.
   * The add_buffer function receives raw video frame, and applies the
   * stream's overlays on the raw frames, before encoding according to the
   * streams configuration.
   * The OSD and encoding are executed in a separate context, allowing
   * pipeline smooth flow.
   * @param[in] ptr - a shared pointer to hailo_media_library_buffer to be
   * encoded
   * @return media_library_return - status of the add buffer operation.
   * @note The MediaLibraryEncoder module will take ownership of the buffer.
   */
  media_library_return add_buffer(HailoMediaLibraryBufferPtr ptr);

  /**
   * @brief Get an overlay manager object
   * @return :shared_ptr containing the object
   * @ref osd::Blender
   */
  std::shared_ptr<osd::Blender> get_blender();

  /**
   * @brief Constructor for the encoder module
   *
   * @param[in] impl - shared pointer to the implementation object
   * @note This constructor is used internally by the create function.
   */
  MediaLibraryEncoder(std::shared_ptr<Impl> impl);

  /**
   * @brief Destructor for the encoder module
   */
  ~MediaLibraryEncoder();
};