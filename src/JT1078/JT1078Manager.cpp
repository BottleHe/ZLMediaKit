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
#include "JT1078Manager.h"
#include "Util/logger.h"
#include "Common/config.h"
#include "Util/NoticeCenter.h"

using namespace std;
using namespace toolkit;

namespace mediakit {

std::tuple<std::string, uint16_t> JT1078Manager::startRecvJt1078(const string &stream_id, const string &vhost, const string &app, uint32_t ssrc, int port, const string &local_ip) {
    auto tuple = MediaTuple{vhost, app, stream_id, ""};

    auto server = std::make_shared<JT1078Server>();
    // port 为 0 表示随机分配, local_ip 为空则使用配置文件的 listen_ip
    string bind_ip = local_ip.empty() ? General::kListenIP : local_ip;
    server->start(port, bind_ip.c_str(), tuple, JT1078Server::TcpMode::PASSIVE, true, ssrc, 0);

    auto local_ip_ret = server->getLocalIp();
    auto local_port = server->getPort();

    InfoL << "启动 JT1078 监听, stream_id=" << stream_id << ", ip=" << local_ip_ret << ", port=" << local_port;

    lock_guard<mutex> lck(_mutex);
    _servers[stream_id] = server;

    return make_tuple(local_ip_ret, local_port);
}

void JT1078Manager::stopRecvJt1078(const string &stream_id) {
    lock_guard<mutex> lck(_mutex);
    auto it = _servers.find(stream_id);
    if (it != _servers.end()) {
        InfoL << "停止 JT1078 监听, stream_id=" << stream_id;
        _servers.erase(it);
    }
}

JT1078Server::Ptr JT1078Manager::getJt1078Server(const string &stream_id) {
    lock_guard<mutex> lck(_mutex);
    auto it = _servers.find(stream_id);
    if (it != _servers.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::string> JT1078Manager::getActiveStreams() {
    lock_guard<mutex> lck(_mutex);
    std::vector<std::string> ret;
    for (auto &kv : _servers) {
        ret.push_back(kv.first);
    }
    return ret;
}

}//namespace mediakit
#endif//defined(ENABLE_RTPPROXY)
