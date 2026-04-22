/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef ZLMEDIAKIT_JT1078_SESSION_H
#define ZLMEDIAKIT_JT1078_SESSION_H

#if defined(ENABLE_RTPPROXY)

#include "Network/Session.h"
#include "JT1078Splitter.h"
#include "JT1078Process.h"
#include "Util/TimeTicker.h"

namespace mediakit {

class JT1078Session : public toolkit::Session, public JT1078Splitter {
public:
    static const std::string kVhost;
    static const std::string kApp;
    static const std::string kStreamID;
    static const std::string kSSRC;
    static const std::string kOnlyTrack;
    static const std::string kUdpRecvBuffer;
    static const std::string kCheckSSRC;

    JT1078Session(const toolkit::Socket::Ptr &sock);
    ~JT1078Session() override;
    void onRecv(const toolkit::Buffer::Ptr &) override;
    void onError(const toolkit::SockException &err) override;
    void onManager() override;
    void setParams(toolkit::mINI &ini);
    void attachServer(const toolkit::Server &server) override;
    void setRtpProcess(JT1078Process::Ptr process);

protected:
    // 收到 JT1078 RTP 包回调
    void onRtpPacket(const char *data, size_t len) override;
    // JT1078Splitter override
    const char *onSearchPacketTail(const char *data, size_t len) override;

private:
    bool _is_udp = false;
    bool _emit_detach = false;
    int _only_track = 0;
    uint32_t _ssrc = 0;
    toolkit::Ticker _ticker;
    MediaTuple _tuple;
    struct sockaddr_storage _addr;
    JT1078Process::Ptr _process;
};

} // namespace mediakit
#endif // defined(ENABLE_RTPPROXY)
#endif // ZLMEDIAKIT_JT1078_SESSION_H