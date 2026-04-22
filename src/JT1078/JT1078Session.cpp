/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "JT1078Session.h"
#include "Network/TcpServer.h"
#include "Common/config.h"
#include "Util/logger.h"
#include "Rtsp/Rtsp.h"

using namespace std;
using namespace toolkit;

namespace mediakit {

const string JT1078Session::kVhost = "vhost";
const string JT1078Session::kApp = "app";
const string JT1078Session::kStreamID = "stream_id";
const string JT1078Session::kSSRC = "ssrc";
const string JT1078Session::kOnlyTrack = "only_track";
const string JT1078Session::kUdpRecvBuffer = "udp_recv_socket_buffer";
const string JT1078Session::kCheckSSRC = "check_ssrc";

void JT1078Session::attachServer(const Server &server) {
    setParams(const_cast<Server &>(server));
}

void JT1078Session::setParams(mINI &ini) {
    _tuple.vhost = ini[kVhost];
    _tuple.app = ini[kApp];
    _tuple.stream = ini[kStreamID];
    _ssrc = ini[kSSRC];
    _only_track = ini[kOnlyTrack];
    int udp_socket_buffer = ini[kUdpRecvBuffer];
    if (_is_udp) {
        SockUtil::setRecvBuf(getSock()->rawFD(),
            (udp_socket_buffer > 0) ? udp_socket_buffer : (4 * 1024 * 1024));
    }
}

JT1078Session::JT1078Session(const Socket::Ptr &sock)
    : Session(sock) {
    socklen_t addr_len = sizeof(_addr);
    getpeername(sock->rawFD(), (struct sockaddr *)&_addr, &addr_len);
    _is_udp = sock->sockType() == SockNum::Sock_UDP;
}

JT1078Session::~JT1078Session() = default;

void JT1078Session::onRecv(const Buffer::Ptr &data) {
    TraceP(this) << "JT1078Session::onRecv, size=" << data->size() << ", is_udp=" << _is_udp;
    if (_is_udp) {
        onRtpPacket(data->data(), data->size());
        return;
    }
    input(data->data(), data->size());
}

void JT1078Session::onError(const SockException &err) {
    if (_emit_detach && _process) {
        _process->onDetach(err);
    }
    WarnP(this) << _tuple.shortUrl() << " " << err;
}

void JT1078Session::onManager() {
    if (!_process && _ticker.createdTime() > 10 * 1000) {
        shutdown(SockException(Err_timeout, "illegal connection"));
    }
}

void JT1078Session::setRtpProcess(JT1078Process::Ptr process) {
    _emit_detach = true;
    _process = std::move(process);
}

void JT1078Session::onRtpPacket(const char *data, size_t len) {
    // data 包含完整的 JT1078 数据包（包括4字节帧头标识）
    // 数据结构：
    // offset 0-3:   帧头标识 0x30316364 (4 bytes)
    // offset 4-5:     V+P+PT
    // offset 6-7:   包序号 (2 bytes)
    // offset 8-13:  SIM卡号 (6 bytes)
    // offset 14:    逻辑通道号 (1 byte)
    // offset 15:    数据类型+分包标记
    // offset 16-23: 时间戳 (8 bytes)
    // offset 24-25: Last I Frame Interval
    // offset 26-27: Last Frame Interval
    // offset 28-29: 数据体长度 (2 bytes)
    // offset 30+:   数据体

    // 检查最小长度
    if (len < 32) {
        WarnP(this) << "JT1078 packet too short: " << len;
        return;
    }

    // 解析包序号 (offset 6-7)
    uint16_t seq = (static_cast<uint8_t>(data[6]) << 8) | static_cast<uint8_t>(data[7]);

    // 解析时间戳 (offset 16-23, big-endian)
    uint32_t stamp = (static_cast<uint32_t>(static_cast<uint8_t>(data[16])) << 24) |
                     (static_cast<uint32_t>(static_cast<uint8_t>(data[17])) << 16) |
                     (static_cast<uint32_t>(static_cast<uint8_t>(data[18])) << 8) |
                     static_cast<uint32_t>(static_cast<uint8_t>(data[19]));

    // 解析数据体长度 (offset 28-29, big-endian)
    uint16_t payload_len = (static_cast<uint8_t>(data[28]) << 8) | static_cast<uint8_t>(data[29]);

    if (payload_len > len - 30) {
        WarnP(this) << "Invalid payload length: " << payload_len << " > " << (len - 30);
        return;
    }

    const char *payload = data + 30;

    // 创建 Process 如果还没有
    if (!_process) {
        InfoP(this) << "creating JT1078Process for stream: " << _tuple.stream;
        _process = JT1078Process::createProcess(_tuple);
        _process->setOnlyTrack(_only_track);
        weak_ptr<JT1078Session> weak_self = static_pointer_cast<JT1078Session>(shared_from_this());
        _process->setOnDetach([weak_self](const SockException &ex) {
            if (auto strong_self = weak_self.lock()) {
                strong_self->safeShutdown(ex);
            }
        });
    }

    try {
        // 调用解码器处理 JT1078 数据包
        // 注意: Splitter 现在保留完整数据（包括4字节帧头标识）
        _process->inputRtp(false, getSock(), data, len, (struct sockaddr *)&_addr);
    } catch (std::exception &ex) {
        WarnL << "Exception processing JT1078 packet: " << ex.what();
    }
    _ticker.resetTime();
}

const char *JT1078Session::onSearchPacketTail(const char *data, size_t len) {
    // 调用父类的粘包处理
    return JT1078Splitter::onSearchPacketTail(data, len);
}

} // namespace mediakit