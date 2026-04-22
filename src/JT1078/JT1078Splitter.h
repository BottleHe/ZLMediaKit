/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef ZLMEDIAKIT_JT1078_SPLITTER_H
#define ZLMEDIAKIT_JT1078_SPLITTER_H

#if defined(ENABLE_RTPPROXY)
#include "Http/HttpRequestSplitter.h"
#include "JT1078Types.h"

namespace mediakit {

class JT1078Splitter : public HttpRequestSplitter {
protected:
    /**
     * 收到 JT/T 1078 数据包回调
     * @param data RTP包数据指针（已跳过帧头标识，指向 RTP 头）
     * @param len RTP包数据长度
     */
    virtual void onRtpPacket(const char *data, size_t len) = 0;

protected:
    ssize_t onRecvHeader(const char *data, size_t len) override;
    const char *onSearchPacketTail(const char *data, size_t len) override;

private:
    const char *onSearchPacketTail_l(const char *data, size_t len);

    // 检测是否是 JT/T 1078 数据
    static bool isJT1078(const char *data, size_t len);

    // 解析 JT/T 1078 包，获取数据体长度
    static bool getPayloadLength(const char *data, size_t len, size_t &payload_len);
};

} // namespace mediakit
#endif // defined(ENABLE_RTPPROXY)
#endif // ZLMEDIAKIT_JT1078_SPLITTER_H
