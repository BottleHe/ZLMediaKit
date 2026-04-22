/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef ZLMEDIAKIT_JT1078_TYPES_H
#define ZLMEDIAKIT_JT1078_TYPES_H

#if defined(ENABLE_RTPPROXY)
#include <cstdint>
#include <cstring>

namespace mediakit {

/**
 * JT/T 1078 协议常量
 */
struct JT1078Const {
    // 帧头标识
    static const uint8_t kFrameHeader[4];

    // JT/T 1078 包头固定长度
    static constexpr size_t kFrameHeaderSize = 4;     // 帧头标识长度
    static constexpr size_t kRtpHeaderSize = 8;       // RTP 头长度 (变种RTP: V+P+PT+Seq+SIM)
    static constexpr size_t kExtHeaderSize = 16;      // 扩展头长度 (不含数据体长度字段)
    static constexpr size_t kPayloadLenSize = 2;       // 数据体长度字段长度
    static constexpr size_t kFixedHeaderSize = 30;     // 固定头部长度 = 4 + 8 + 16 + 2

    // 扩展头中各字段的偏移量 (相对于帧头)
    static constexpr size_t kExtSimOffset = 8;        // SIM卡号偏移
    static constexpr size_t kExtChannelOffset = 14;    // 通道号偏移
    static constexpr size_t kExtTypeMarkOffset = 15;  // 数据类型+分包标记偏移
    static constexpr size_t kExtTimestampOffset = 16; // 时间戳偏移
    static constexpr size_t kPayloadLengthOffset = 28; // 数据体长度偏移

    // 最大数据体长度
    static constexpr size_t kMaxPayloadSize = 950;
};

/**
 * Subpackage mark
 */
enum class JT1078SubpackageMark : uint8_t {
    Atomic = 0x00,       // Complete data, not splittable
    First = 0x01,        // First subpackage of large frame
    Last = 0x02,         // Last subpackage of large frame
    Middle = 0x03        // Middle subpackage of large frame
};

/**
 * Data type
 */
enum class JT1078DataType : uint8_t {
    VideoIFrame = 0x00,
    VideoPFrame = 0x01,
    VideoBFrame = 0x02,
    Audio = 0x03,
    Passthrough = 0x04
};

/**
 * JT/T 1078 扩展头解析结果
 */
struct JT1078ExtHeader {
    uint8_t sim[6];              // SIM卡号 (BCD编码)
    uint8_t channel;            // 逻辑通道号
    JT1078DataType data_type;    // 数据类型
    JT1078SubpackageMark subpackage_mark; // 分包标记
    uint64_t timestamp;          // 时间戳 (ms)

    /**
     * 解析扩展头
     * @param data 指向扩展头起始位置的指针 (offset 8, SIM卡号起始位置)
     */
    void parse(const char *data) {
        // SIM卡号 (6 bytes, BCD编码)
        memcpy(sim, data, 6);

        // 通道号 (1 byte)
        channel = static_cast<uint8_t>(data[6]);

        // 数据类型 + 分包标记 (1 byte)
        // 有些设备使用 高4位=数据类型, 低4位=分包标记 (标准JT1078)
        // 有些设备使用 低4位=数据类型, 高4位=分包标记
        uint8_t type_and_mark = static_cast<uint8_t>(data[7]);

        // 先按标准解析：高4位=数据类型，低4位=分包标记
        data_type = static_cast<JT1078DataType>(type_and_mark >> 4);
        subpackage_mark = static_cast<JT1078SubpackageMark>(type_and_mark & 0x0F);

        // Check if values are invalid, try alternative format
        if (data_type > JT1078DataType::Passthrough || subpackage_mark > JT1078SubpackageMark::Middle) {
            // Try low 4 bits = data type, high 4 bits = subpackage mark
            data_type = static_cast<JT1078DataType>(type_and_mark & 0x0F);
            subpackage_mark = static_cast<JT1078SubpackageMark>(type_and_mark >> 4);
        }

        // 时间戳 (8 bytes, 大端序, ms)
        timestamp = 0;
        for (int i = 0; i < 8; ++i) {
            timestamp = (timestamp << 8) | static_cast<uint8_t>(data[8 + i]);
        }
    }

    bool isVideo() const {
        return data_type == JT1078DataType::VideoIFrame ||
               data_type == JT1078DataType::VideoPFrame ||
               data_type == JT1078DataType::VideoBFrame;
    }

    bool isAudio() const {
        return data_type == JT1078DataType::Audio;
    }

    bool isKeyFrame() const {
        return data_type == JT1078DataType::VideoIFrame;
    }
};

/**
 * JT/T 1078 包解析结果
 */
struct JT1078PacketInfo {
    // RTP 头信息 (偏移4-15)
    uint8_t version = 0;
    uint8_t pt = 0;        // 负载类型
    uint16_t seq = 0;      // 序列号
    uint32_t stamp = 0;    // 时间戳

    // 扩展头信息 (偏移16-27)
    JT1078ExtHeader ext_header;

    // 数据体信息
    uint16_t payload_len = 0;   // 数据体长度
    const char *payload = nullptr; // 数据体指针

    /**
     * 解析完整的 JT1078 包
     * @param data 指向帧头 0x30316364 的指针
     * @param len 数据长度
     * @return 是否解析成功
     */
    bool parse(const char *data, size_t len) {
        if (len < JT1078Const::kFixedHeaderSize) {
            return false;
        }

        // 检查帧头
        if (data[0] != 0x30 || data[1] != 0x31 || data[2] != 0x63 || data[3] != 0x64) {
            return false;
        }

        // 解析 RTP 头 (offset 4-15)
        const char *rtp_data = data + 4;
        version = (static_cast<uint8_t>(rtp_data[0]) >> 6) & 0x03;
        pt = rtp_data[1] & 0x7F;
        seq = (static_cast<uint8_t>(rtp_data[2]) << 8) | static_cast<uint8_t>(rtp_data[3]);
        stamp = (static_cast<uint32_t>(static_cast<uint8_t>(rtp_data[4])) << 24) |
                (static_cast<uint32_t>(static_cast<uint8_t>(rtp_data[5])) << 16) |
                (static_cast<uint32_t>(static_cast<uint8_t>(rtp_data[6])) << 8) |
                static_cast<uint32_t>(static_cast<uint8_t>(rtp_data[7]));

        // 解析扩展头 (offset 16-27)
        ext_header.parse(data + JT1078Const::kExtSimOffset);

        // 解析数据体长度 (offset 28-29, big-endian)
        payload_len = (static_cast<uint8_t>(data[28]) << 8) | static_cast<uint8_t>(data[29]);

        // 数据体指针 (offset 30)
        payload = data + JT1078Const::kFixedHeaderSize;

        // 检查数据体是否完整
        return len >= JT1078Const::kFixedHeaderSize + payload_len;
    }
};

} // namespace mediakit
#endif // defined(ENABLE_RTPPROXY)
#endif // ZLMEDIAKIT_JT1078_TYPES_H