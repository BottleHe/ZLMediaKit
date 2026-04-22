/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef ZLMEDIAKIT_JT1078SERVER_H
#define ZLMEDIAKIT_JT1078SERVER_H

#if defined(ENABLE_RTPPROXY)
#include <memory>
#include "Network/Socket.h"
#include "Network/TcpServer.h"
#include "Network/UdpServer.h"
#include "JT1078Session.h"

namespace mediakit {

/**
 * JT1078服务器，支持UDP/TCP
 * JT1078 server, supports UDP/TCP
 */
class JT1078Server : public std::enable_shared_from_this<JT1078Server> {
public:
    using Ptr = std::shared_ptr<JT1078Server>;
    enum TcpMode { NONE = 0, PASSIVE, ACTIVE };

    ~JT1078Server();

    /**
     * 开启服务器，可能抛异常
     * @param local_port 本地端口，0时为随机端口
     * @param local_ip 绑定的本地网卡ip
     * @param tuple 媒体元组
     * @param tcp_mode tcp服务模式
     * @param re_use_port 是否设置socket为re_use属性
     * @param ssrc 指定的ssrc
     * Start the server, may throw an exception
     */
    void start(uint16_t local_port, const char *local_ip = "::", const MediaTuple &tuple = MediaTuple{DEFAULT_VHOST, kJT1078AppName, "", ""}, TcpMode tcp_mode = PASSIVE,
               bool re_use_port = true, uint32_t ssrc = 0, int only_track = 0);

    /**
     * 获取绑定的本地端口
     * Get the bound local port
     */
    uint16_t getPort();

    /**
     * 设置JT1078Process onDetach事件回调
     * Set JT1078Process onDetach event callback
     */
    void setOnDetach(JT1078Process::onDetachCB cb);

    /**
     * 更新ssrc
     * Update ssrc
     */
    void updateSSRC(uint32_t ssrc);

    uint32_t getSSRC() const;
    int getOnlyTrack() const { return _only_track; }
    TcpMode getTcpMode() const { return _tcp_mode; }
    std::string getLocalIp();
    const MediaTuple& getMediaTuple() const;

private:
    toolkit::Socket::Ptr _rtp_socket;
    toolkit::UdpServer::Ptr _udp_server;
    toolkit::TcpServer::Ptr _tcp_server;
    std::shared_ptr<uint32_t> _ssrc;
    std::function<void()> _on_cleanup;

    int _only_track = 0;
    TcpMode _tcp_mode = NONE;
    MediaTuple _tuple;
};

}//namespace mediakit
#endif//defined(ENABLE_RTPPROXY)
#endif //ZLMEDIAKIT_JT1078SERVER_H