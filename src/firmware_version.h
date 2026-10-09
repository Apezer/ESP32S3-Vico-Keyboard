/**
 * @file firmware_version.h
 * @brief Vico Keyboard 固件版本的唯一来源。
 *
 * 发布固件时只需修改本文件，OLED 数字孪生状态包和配套软件中的版本目录
 * 都应与这里保持一致。
 */

#pragma once

#include <cstdint>

namespace VicoFirmwareVersion {

constexpr uint8_t MAJOR = 1;
constexpr uint8_t MINOR = 0;
constexpr uint8_t PATCH = 1;
constexpr const char *TEXT = "1.0.1";

}  // namespace VicoFirmwareVersion
