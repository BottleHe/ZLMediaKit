/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "JT1078RtpDecoder.h"
#include "Util/logger.h"
#include "Extension/Frame.h"
#include "Extension/Factory.h"
#include "ext-codec/H264.h"

using namespace std;
using namespace toolkit;

namespace mediakit {

JT1078RtpDecoder::JT1078RtpDecoder() = default;

JT1078RtpDecoder::~JT1078RtpDecoder() = default;

void JT1078RtpDecoder::setOnTrack(onTrackCB cb) {
    _on_track = std::move(cb);
}

void JT1078RtpDecoder::setOnTrackCompleted(onTrackCompletedCB cb) {
    _on_track_completed = std::move(cb);
}

bool JT1078RtpDecoder::inputData(const char *data, size_t len) {
    if (len < JT1078Const::kFixedHeaderSize) {
        WarnL << "JT1078 data too short: " << len;
        return false;
    }

    // 解析包
    JT1078PacketInfo info;
    if (!info.parse(data, len)) {
        WarnL << "Failed to parse JT1078 packet";
        return false;
    }

    return inputPacket(info, data, len);
}

bool JT1078RtpDecoder::inputPacket(const JT1078PacketInfo &info, const char *data, size_t len) {
    // 计算 payload 位置和长度
    size_t payload_offset = JT1078Const::kFixedHeaderSize;
    size_t payload_len = info.payload_len;

    if (payload_len > len - payload_offset) {
        WarnL << "Invalid payload length: " << payload_len << " > " << (len - payload_offset);
        return false;
    }

    const char *payload = data + payload_offset;

    // 解析 RTP 头获取序列号
    uint16_t seq = info.seq;
    uint32_t stamp = info.stamp;

    // 处理分包
    return handleSubpackage(payload, payload_len, info.ext_header, seq, stamp);
}

bool JT1078RtpDecoder::handleSubpackage(const char *payload, size_t len,
                                        const JT1078ExtHeader &ext_header,
                                        uint16_t seq, uint32_t stamp) {
    // 转换时间戳
    uint64_t dts = convertTimestamp(stamp);
    uint64_t pts = dts;
    if (ext_header.timestamp > 0) {
        // 如果扩展头有时间戳，使用它
        pts = ext_header.timestamp;
    }

    auto channel = ext_header.channel;

    switch (ext_header.subpackage_mark) {
        case JT1078SubpackageMark::Atomic:
            // 独立完整帧，直接输出
            return outputFrame(payload, len, ext_header, dts, pts, ext_header.isKeyFrame());

        case JT1078SubpackageMark::First: {
            auto &cache = _frame_cache[channel];
            cache.data.clear();
            cache.data.assign(payload, payload + len);
            cache.first_seq = seq;
            cache.last_seq = seq;
            cache.timestamp = dts;
            cache.data_type = ext_header.data_type;
            cache.started = true;
            TraceL << "JT1078 first packet, channel=" << (int)channel
                   << ", seq=" << seq << ", data_type=" << (int)ext_header.data_type;
            return true;
        }

        case JT1078SubpackageMark::Middle: {
            auto it = _frame_cache.find(channel);
            if (it == _frame_cache.end() || !it->second.started) {
                WarnL << "JT1078 middle packet without start, channel=" << (int)channel;
                return false;
            }
            auto &cache = it->second;
            cache.data.insert(cache.data.end(), payload, payload + len);
            cache.last_seq = seq;
            TraceL << "JT1078 middle packet, channel=" << (int)channel
                   << ", seq=" << seq << ", accumulated=" << cache.data.size();
            return true;
        }

        case JT1078SubpackageMark::Last: {
            auto it = _frame_cache.find(channel);
            if (it == _frame_cache.end() || !it->second.started) {
                WarnL << "JT1078 last packet without start, channel=" << (int)channel;
                return false;
            }
            auto &cache = it->second;
            cache.data.insert(cache.data.end(), payload, payload + len);
            cache.last_seq = seq;

            TraceL << "JT1078 last packet, channel=" << (int)channel
                   << ", seq=" << seq << ", total_size=" << cache.data.size();

            // 组装完整帧并输出
            bool ret = outputFrame(cache.data.data(), cache.data.size(),
                                   ext_header, cache.timestamp, dts, ext_header.isKeyFrame());

            // 清除缓存
            _frame_cache.erase(it);
            return ret;
        }

        default:
            WarnL << "Unknown subpackage mark: " << (int)ext_header.subpackage_mark;
            return false;
    }
}

bool JT1078RtpDecoder::outputFrame(const char *payload, size_t len,
                                   const JT1078ExtHeader &ext_header,
                                   uint64_t dts, uint64_t pts, bool key_frame) {
    Frame::Ptr frame;

    if (ext_header.isVideo()) {
        if (key_frame) {
            createTrackIfNeed(payload, len, dts, ext_header.channel, true);
        }
        frame = makeH264Frame(payload, len, dts, pts, key_frame);
    } else if (ext_header.isAudio()) {
        if (!_audio_track_created) {
            _audio_track_created = true;
            auto track = Factory::getTrackByCodecId(CodecAAC, 8000, 1, 16);
            if (track && _on_track) {
                _on_track(track);
            }
        }
        frame = makeAACFrame(payload, len, dts);
    } else {
        return true;
    }

    if (!frame) {
        return false;
    }

    _last_dts = dts;

    if (_on_frame) {
        return _on_frame(frame);
    }

    return true;
}

Frame::Ptr JT1078RtpDecoder::makeH264Frame(const char *data, size_t len,
                                           uint64_t dts, uint64_t pts, bool key_frame) {
    auto frame_imp = FrameImp::create<H264Frame>();
    frame_imp->_codec_id = CodecH264;
    frame_imp->_dts = dts;
    frame_imp->_pts = pts;

    auto prefix = prefixSize(data, len);
    if (prefix > 0) {
        frame_imp->_prefix_size = prefix;
        frame_imp->_buffer.assign(data, len);
        return frame_imp;
    }

    frame_imp->_prefix_size = 4;
    frame_imp->_buffer.assign("\x00\x00\x00\x01", 4);
    frame_imp->_buffer.append(data, len);

    return frame_imp;
}

Frame::Ptr JT1078RtpDecoder::makeAACFrame(const char *data, size_t len, uint64_t dts) {
    // AAC 需要 ADTS 头
    // 使用简化的 ADTS 头，实际采样率/通道从设备获取或使用默认值
    auto frame = FrameImp::create();
    frame->_codec_id = CodecAAC;
    frame->_dts = dts;
    frame->_pts = dts;
    frame->_prefix_size = 7; // ADTS 头占 7 字节

    // ADTS 头 (7 bytes)
    // profile: AAC-LC (01)
    // sampling_frequency_index: 0x0B = 8000Hz (根据 JT1078 音频通常 8kHz)
    // channel_configuration: 1 (单通道)
    uint8_t adts[7] = {
        0xFF,                       // sync word
        0xF1,                       // ID=0, layer=00, protection absent=1
        0x00,                       // profile=0(AAC Main), private=0
        0x0B,                       // sampling_frequency_index=11(8000Hz)
        0x00,                       // private=0, configuration=00
        0x1F,                       // frame length high 5 bits + 私有位
        0x00                        // frame length low 8 bits
    };

    // 计算帧长度 = ADTS头(7) + AAC数据
    size_t frame_length = 7 + len;
    adts[5] = (frame_length >> 11) & 0x03;
    adts[6] = (frame_length >> 3) & 0xFF;

    frame->_buffer.append((char *)adts, 7);
    frame->_buffer.append(data, len);

    return frame;
}

uint64_t JT1078RtpDecoder::convertTimestamp(uint32_t stamp) {
    // RTP timestamp 基于 90kHz，转换为毫秒
    return (uint64_t)stamp * 1000 / 90000;
}

bool JT1078RtpDecoder::isFrameComplete(uint8_t channel) {
    auto it = _frame_cache.find(channel);
    if (it == _frame_cache.end()) {
        return false;
    }
    return (it->second.last_seq - it->second.first_seq + 1) >= it->second.expected_count;
}

void JT1078RtpDecoder::flush() {
    for (auto &[channel, cache] : _frame_cache) {
        if (cache.started && !cache.data.empty()) {
            WarnL << "Flushing incomplete frame, channel=" << (int)channel
                  << ", data_size=" << cache.data.size();
            JT1078ExtHeader ext_header;
            ext_header.channel = channel;
            ext_header.data_type = cache.data_type;
            ext_header.timestamp = cache.timestamp;
            bool key_frame = (cache.data_type == JT1078DataType::VideoIFrame);
            outputFrame(cache.data.data(), cache.data.size(),
                       ext_header, key_frame ? cache.timestamp : 0,
                       cache.timestamp, key_frame);
        }
    }
    _frame_cache.clear();
}

void JT1078RtpDecoder::reset() {
    _frame_cache.clear();
    _last_dts = 0;
}

void JT1078RtpDecoder::setOnFrame(onFrameCB cb) {
    _on_frame = std::move(cb);
}

bool JT1078RtpDecoder::createTrackIfNeed(const char *data, size_t len, uint64_t dts, uint8_t channel, bool key_frame) {
    if (_video_track_created) {
        return true;
    }

    string sps;
    string pps;

    const char *ptr = data;
    size_t remain = len;

    while (remain >= 4) {
        size_t start_code_len = 0;
        if (ptr[0] == 0x00 && ptr[1] == 0x00) {
            if (ptr[2] == 0x01) {
                start_code_len = 3;
            } else if (ptr[2] == 0x00 && ptr[3] == 0x01) {
                start_code_len = 4;
            }
        }

        // 排除假阳性: 连续零填充中产生的伪 start code
        if (start_code_len > 0 && ptr > data && remain > start_code_len) {
            uint8_t nal_type = ptr[start_code_len];
            if (ptr[-1] == 0x00 && nal_type == 0x00) {
                ptr++;
                remain--;
                continue;
            }
        }

        if (start_code_len > 0) {
            uint8_t nal_type = ptr[start_code_len] & 0x1F;
            const char *nal_start = ptr + start_code_len;
            size_t nal_len = remain - start_code_len;

            const char *next_start = nullptr;
            for (size_t i = 4; i + 1 < nal_len; ++i) {
                if (nal_start[i] == 0x00 && nal_start[i+1] == 0x00) {
                    if (nal_start[i+2] == 0x01 || (i + 3 < nal_len && nal_start[i+2] == 0x00 && nal_start[i+3] == 0x01)) {
                        next_start = nal_start + i;
                        break;
                    }
                }
            }

            if (next_start) {
                nal_len = next_start - nal_start;
            }

            if (nal_type == 7) {
                sps.assign(nal_start, nal_len);
                TraceL << "Found SPS (start code), length=" << nal_len;
            } else if (nal_type == 8) {
                pps.assign(nal_start, nal_len);
                TraceL << "Found PPS (start code), length=" << nal_len;
            }

            if (next_start) {
                ptr = next_start;
                remain = (ptr < data + len) ? (data + len - ptr) : 0;
            } else {
                ptr = nal_start + nal_len;
                remain = (ptr < data + len) ? (data + len - ptr) : 0;
                if (remain < 4) {
                    break;
                }
            }
        } else {
            break;
        }
    }

    if (sps.empty() && pps.empty() && len >= 5) {
        ptr = data;
        remain = len;
        while (remain >= 5) {
            size_t nal_len = (static_cast<uint8_t>(ptr[0]) << 24) |
                             (static_cast<uint8_t>(ptr[1]) << 16) |
                             (static_cast<uint8_t>(ptr[2]) << 8) |
                             static_cast<uint8_t>(ptr[3]);

            uint8_t nal_type = ptr[4] & 0x1F;

            if (nal_len > remain - 4 || nal_len == 0) {
                break;
            }

            if (nal_type == 7) {
                sps.assign(ptr + 4, nal_len);
                TraceL << "Found SPS (length prefix), length=" << nal_len;
            } else if (nal_type == 8) {
                pps.assign(ptr + 4, nal_len);
                TraceL << "Found PPS (length prefix), length=" << nal_len;
            }

            ptr += (4 + nal_len);
            remain -= (4 + nal_len);
        }
    }

    if (!sps.empty() && !pps.empty()) {
        _video_track_created = true;
        // sps/pps 数据已不含 start code，prefix_len 传 0
        auto track = std::make_shared<H264Track>(sps, pps, 0, 0);
        InfoL << "Created H264Track, sps_size=" << sps.size() << ", pps_size=" << pps.size()
              << ", width=" << track->getVideoWidth() << ", height=" << track->getVideoHeight();
        if (_on_track) {
            _on_track(track);
        }
        if (_on_track_completed) {
            _on_track_completed();
        }
        return true;
    }

    return false;
}

} // namespace mediakit