/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef ZLMEDIAKIT_JT1078RTPDECODER_H
#define ZLMEDIAKIT_JT1078RTPDECODER_H

#if defined(ENABLE_RTPPROXY)
#include <memory>
#include <vector>
#include <map>
#include <functional>
#include "JT1078Types.h"
#include "Extension/Frame.h"
#include "Extension/Track.h"
#include "Network/Buffer.h"

namespace mediakit {

/**
 * JT1078 RTP 解码器
 * 负责解析 JT1078 数据包，提取 payload，转换为标准 Frame
 */
class JT1078RtpDecoder {
public:
    using Ptr = std::shared_ptr<JT1078RtpDecoder>;
    using onFrameCB = std::function<bool(const Frame::Ptr &frame)>;

    JT1078RtpDecoder();
    ~JT1078RtpDecoder();

    /**
     * 输入 JT1078 包数据进行解码
     * @param data 完整的 JT1078 包 (包含帧头)
     * @param len 包长度
     * @return 是否处理成功
     */
    bool inputData(const char *data, size_t len);

    /**
     * 输入已解析的 JT1078 包信息
     * @param info 包信息
     * @param data 包数据
     * @param len 包长度
     * @return 是否处理成功
     */
    bool inputPacket(const JT1078PacketInfo &info, const char *data, size_t len);

    /**
     * 设置帧回调
     */
    void setOnFrame(onFrameCB cb);

    /**
     * 设置 Track 创建回调
     * 当解码器需要创建 Track 时调用此回调
     */
    using onTrackCB = std::function<bool(const Track::Ptr &track)>;
    void setOnTrack(onTrackCB cb);

    /**
     * 处理分包
     * @param payload 数据负载指针
     * @param len 数据长度
     * @param ext_header 扩展头信息
     * @param seq 序列号
     * @param stamp RTP时间戳
     * @return 是否处理成功
     */
    bool handleSubpackage(const char *payload, size_t len,
                         const JT1078ExtHeader &ext_header,
                         uint16_t seq, uint32_t stamp);

    /**
     * 刷新缓存，输出未完成的帧
     */
    void flush();

    /**
     * 重置解码器状态
     */
    void reset();

private:
    // 输出帧
    bool outputFrame(const char *payload, size_t len,
                    const JT1078ExtHeader &ext_header,
                    uint64_t dts, uint64_t pts, bool key_frame);

    // 创建 H264 Frame
    Frame::Ptr makeH264Frame(const char *data, size_t len, uint64_t dts, uint64_t pts, bool key_frame);

    // 创建 AAC Frame
    Frame::Ptr makeAACFrame(const char *data, size_t len, uint64_t dts);

    // 时间戳转换 (90k -> ms)
    uint64_t convertTimestamp(uint32_t stamp_ms);

    // 检查帧是否完整
    bool isFrameComplete(uint8_t channel);

    // 从 H.264 数据中提取 SPS/PPS 并创建 Track
    bool createTrackIfNeed(const char *data, size_t len, uint64_t dts, uint8_t channel, bool key_frame);

private:
    onFrameCB _on_frame;
    onTrackCB _on_track;

    // Track 创建状态
    bool _video_track_created = false;
    bool _audio_track_created = false;

    // 分包重组缓存
    struct FrameCache {
        std::vector<char> data;       // 缓存的数据
        uint64_t timestamp = 0;       // 第一个包的时间戳
        uint16_t first_seq = 0;       // 第一个序列号
        uint16_t last_seq = 0;        // 最后一个序列号
        uint16_t expected_count = 0;  // 预期包数量
        JT1078DataType data_type;      // 数据类型
        bool started = false;         // 是否已开始
    };
    std::map<uint8_t, FrameCache> _frame_cache; // key: channel

    // 上次输出的时间戳
    uint64_t _last_dts = 0;
};

} // namespace mediakit
#endif // defined(ENABLE_RTPPROXY)
#endif // ZLMEDIAKIT_JT1078RTPDECODER_H