/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "JT1078Splitter.h"
#include "Util/logger.h"
#include "Util/util.h"

using namespace std;
using namespace toolkit;

namespace mediakit {

// 帧头标识
const uint8_t JT1078Const::kFrameHeader[4] = {0x30, 0x31, 0x63, 0x64};

bool JT1078Splitter::isJT1078(const char *data, size_t len) {
    if (len < 4) {
        return false;
    }
    return data[0] == 0x30 && data[1] == 0x31 &&
           data[2] == 0x63 && data[3] == 0x64;
}

bool JT1078Splitter::getPayloadLength(const char *data, size_t len, size_t &payload_len) {
    // JT/T 1078 包结构:
    // offset 0-3:   帧头标识 0x30316364 (4 bytes)
    // offset 4:      V(2bit) + P(1bit) + X(1bit) + CC(4bit)
    // offset 5:     M(1bit) + PT(7bit)
    // offset 6-7:   包序号 (2 bytes)
    // offset 8-13:  SIM卡号 (6 bytes)
    // offset 14:    逻辑通道号 (1 byte)
    // offset 15:    数据类型(4bit) + 分包处理标记(4bit)
    // offset 16-23: 时间戳 (8 bytes)
    // offset 24-25: Last I Frame Interval (2 bytes)
    // offset 26-27: Last Frame Interval (2 bytes)
    // offset 28-29: 数据体长度 (2 bytes, big-endian)
    // offset 30+:   数据体

    if (len < JT1078Const::kFixedHeaderSize) {
        return false;
    }

    // 读取数据体长度 (offset 28, 2 bytes, big-endian)
    payload_len = (static_cast<uint8_t>(data[28]) << 8) | static_cast<uint8_t>(data[29]);
    return true;
}

const char *JT1078Splitter::onSearchPacketTail(const char *data, size_t len) {
    // 先尝试在数据中搜索 JT/T 1078 帧头标识
    // 因为数据流中间也可能包含帧头标识（粘包情况）
    TraceL << "onSearchPacketTail, len=" << len;
    const char *ptr = data;
    size_t remain = len;

    while (remain >= JT1078Const::kFixedHeaderSize) {
        // 搜索帧头标识
        const char *frame_header = nullptr;
        for (size_t i = 0; i <= remain - 4; ++i) {
            if (isJT1078(ptr + i, remain - i)) {
                frame_header = ptr + i;
                break;
            }
        }

        if (!frame_header) {
            // 没有找到帧头标识
            TraceL << "frame header not found, remain=" << remain;
            return nullptr;
        }

        TraceL << "found frame header at offset=" << (frame_header - ptr);

        // 计算从帧头开始的数据
        size_t from_header = frame_header - ptr;
        size_t header_remain = remain - from_header;

        if (header_remain < JT1078Const::kFixedHeaderSize) {
            // 数据不够
            return nullptr;
        }

        // 获取数据体长度
        size_t payload_len = 0;
        if (!getPayloadLength(frame_header, header_remain, payload_len)) {
            // 解析失败，尝试在下一个位置搜索
            ptr = frame_header + 1;
            remain = remain - from_header - 1;
            continue;
        }

        // 计算完整包长度
        size_t total_len = JT1078Const::kFixedHeaderSize + payload_len;

        if (header_remain < total_len) {
            // 数据不够，等待更多数据
            TraceL << "waiting for more data, have=" << header_remain << ", need=" << total_len;
            return nullptr;
        }

        // 返回包末尾
        TraceL << "packet complete, total_len=" << total_len;
        return frame_header + total_len;
    }

    return nullptr;
}

ssize_t JT1078Splitter::onRecvHeader(const char *data, size_t len) {
    // 传递完整数据（包括帧头标识），让 onRtpPacket 统一按原始偏移计算
    if (len > 0) {
        onRtpPacket(data, len);
    }
    return 0;
}

} // namespace mediakit
