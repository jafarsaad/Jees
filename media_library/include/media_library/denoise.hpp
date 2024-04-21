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
 * @file denoise.hpp
 * @brief MediaLibrary Denoise CPP API module
 **/

#pragma once
#include <stdint.h>
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <tl/expected.hpp>

#include "media_library_types.hpp"

/** @defgroup denoise_type_definitions MediaLibrary Denoise CPP API definitions
 *  @{
 */
class MediaLibraryDenoise;
using MediaLibraryDenoisePtr = std::shared_ptr<MediaLibraryDenoise>;

class MediaLibraryDenoise
{
protected:
  class Impl;
  std::shared_ptr<Impl> m_impl;

public:
  /**
   * @brief Create the denoise module
   *
   * @param[in] config_string - json configuration string
   * @return tl::expected<MediaLibraryDenoisePtr, media_library_return> -
   * An expected object that holds either a shared pointer
   * to an MediaLibraryDenoise object, or a error code.
   */
  static tl::expected<std::shared_ptr<MediaLibraryDenoise>, media_library_return> create(std::string config_string);

  /**
   * @brief Constructor for the denoise module
   *
   * @param[in] impl - shared pointer to the implementation object
   * @note This constructor is used internally by the create function.
   */
  MediaLibraryDenoise(std::shared_ptr<MediaLibraryDenoise::Impl> impl);

  /**
   * @brief Destructor for the denoise module
   */
  ~MediaLibraryDenoise();

  /**
   * @brief Configure the denoise module with new json string
   *
   * Read the json string and decode it to create the denoise_config_t object
   * @param[in] config_string - configuration json as string
   * @return media_library_return - status of the configuration operation
   */
  media_library_return configure(std::string config_string);

  /**
   * @brief Configure the denoise module with denoise_config_t object
   *
   * Update the denoise_config_t object
   * @param[in] denoise_config_t - denoise_config_t object
   * @param[in] hailort_t - hailort_t object
   * @return media_library_return - status of the configuration operation
   */
  media_library_return configure(denoise_config_t &denoise_configs, hailort_t &hailort_configs);

  /**
   * @brief get the denoise configurations object
   *
   * @return denoise_config_t - denoise configurations
   */
  denoise_config_t &get_denoise_configs();

  /**
   * @brief get the hailort configurations object
   *
   * @return hailort_t - hailort configurations
   */
  hailort_t &get_hailort_configs();
};

/** @} */ // end of denoise_type_definitions
