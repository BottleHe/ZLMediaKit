/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef ZLMEDIAKIT_JT1078MANAGER_H
#define ZLMEDIAKIT_JT1078MANAGER_H

#if defined(ENABLE_RTPPROXY)
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include "JT1078Server.h"

namespace mediakit {

/**
 * JT1078 服务管理器
 * 用于管理多个 JT1078 拉流会话
 */
class JT1078Manager {
public:
    using Ptr = std::shared_ptr<JT1078Manager>;

    /**
     * 启动一个 JT1078 监听
     * @param stream_id 流 ID
     * @param vhost vhost
     * @param app app
     * @param ssrc 指定 SSRC，0 表示自动生成
     * @param port 指定端口，0 表示自动分配
     * @param local_ip 本地监听 IP，默认读取配置
     * @return {ip, port} 监听地址
     */
    std::tuple<std::string, uint16_t> startRecvJt1078(const std::string &stream_id, const std::string &vhost = DEFAULT_VHOST, const std::string &app = kJT1078AppName, uint32_t ssrc = 0, int port = 0, const std::string &local_ip = "");

    /**
     * 停止 JT1078 监听
     * @param stream_id 流 ID
     */
    void stopRecvJt1078(const std::string &stream_id);

    /**
     * 获取 JT1078 Server
     * @param stream_id 流 ID
     */
    JT1078Server::Ptr getJt1078Server(const std::string &stream_id);

    /**
     * 获取所有正在监听的流
     */
    std::vector<std::string> getActiveStreams();

private:
    std::unordered_map<std::string, JT1078Server::Ptr> _servers;
    std::mutex _mutex;
};

}//namespace mediakit
#endif//defined(ENABLE_RTPPROXY)
#endif //ZLMEDIAKIT_JT1078MANAGER_H