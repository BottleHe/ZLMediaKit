/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#if defined(ENABLE_RTPPROXY)
#include "Util/uv_errno.h"
#include "JT1078Server.h"
#include "Common/config.h"

using namespace std;
using namespace toolkit;

namespace mediakit{

JT1078Server::~JT1078Server() {
    if (_on_cleanup) {
        _on_cleanup();
    }
}

void JT1078Server::start(uint16_t local_port, const char *local_ip, const MediaTuple &tuple, TcpMode tcp_mode, bool re_use_port, uint32_t ssrc, int only_track) {
    _tuple = tuple;
    _only_track = only_track;
    _tcp_mode = tcp_mode;

    auto poller = EventPollerPool::Instance().getPoller();

    // 创建 UDP socket
    _rtp_socket = Socket::createSocket(poller, true);
    // 端口为0时，操作系统会自动分配随机端口
    if (!_rtp_socket->bindUdpSock(local_port, local_ip, re_use_port)) {
        throw std::runtime_error(StrPrinter << "创建jt1078端口 " << local_ip << ":" << local_port << " 失败:" << get_uv_errmsg(true));
    }
    // 获取实际分配的端口
    local_port = _rtp_socket->get_local_port();

    // 设置udp socket读缓存
    GET_CONFIG(int, udpRecvSocketBuffer, RtpProxy::kUdpRecvSocketBuffer);
    SockUtil::setRecvBuf(_rtp_socket->rawFD(), udpRecvSocketBuffer);

    // 创建 UDP 服务器
    _udp_server = std::make_shared<UdpServer>();
    (*_udp_server)[JT1078Session::kOnlyTrack] = only_track;
    (*_udp_server)[JT1078Session::kUdpRecvBuffer] = udpRecvSocketBuffer;
    (*_udp_server)[JT1078Session::kVhost] = tuple.vhost;
    (*_udp_server)[JT1078Session::kApp] = tuple.app;
    (*_udp_server)[JT1078Session::kStreamID] = tuple.stream;
    (*_udp_server)[JT1078Session::kSSRC] = ssrc;
    (*_udp_server)[JT1078Session::kCheckSSRC] = (ssrc == 0) ? 0 : 1; // ssrc为0时，不校验SSRC
    _udp_server->start<JT1078Session>(local_port, local_ip);

    // TCP 服务器
    if (tcp_mode == PASSIVE) {
        _tcp_server = std::make_shared<TcpServer>(poller);
        (*_tcp_server)[JT1078Session::kVhost] = tuple.vhost;
        (*_tcp_server)[JT1078Session::kApp] = tuple.app;
        (*_tcp_server)[JT1078Session::kStreamID] = tuple.stream;
        (*_tcp_server)[JT1078Session::kSSRC] = ssrc;
        (*_tcp_server)[JT1078Session::kOnlyTrack] = only_track;
        (*_tcp_server)[JT1078Session::kCheckSSRC] = (ssrc == 0) ? 0 : 1; // ssrc为0时，不校验SSRC
        _tcp_server->start<JT1078Session>(local_port, local_ip, 1024, nullptr);
    }

    _on_cleanup = [this]() {
        if (_rtp_socket) {
            _rtp_socket->setOnRead(nullptr);
        }
    };

    auto ssrc_ptr = std::make_shared<uint32_t>(ssrc);
    _ssrc = ssrc_ptr;
}

uint16_t JT1078Server::getPort() {
    return _udp_server ? _udp_server->getPort() : _rtp_socket->get_local_port();
}

std::string JT1078Server::getLocalIp() {
    return _rtp_socket->get_local_ip();
}

void JT1078Server::setOnDetach(JT1078Process::onDetachCB cb) {
    // Not implemented yet - would need to store the callback and apply to sessions
}

void JT1078Server::updateSSRC(uint32_t ssrc) {
    if (_ssrc) {
        *_ssrc = ssrc;
    }
    if (_tcp_server) {
        (*_tcp_server)[JT1078Session::kSSRC] = ssrc;
    }
}

uint32_t JT1078Server::getSSRC() const {
    if (_ssrc) {
        return *_ssrc;
    }
    if (_tcp_server) {
        return (*_tcp_server)[JT1078Session::kSSRC];
    }
    return 0;
}

const MediaTuple& JT1078Server::getMediaTuple() const {
    return _tuple;
}

}//namespace mediakit
#endif//defined(ENABLE_RTPPROXY)