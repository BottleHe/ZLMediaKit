/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef ZLMEDIAKIT_JT1078_PROCESS_H
#define ZLMEDIAKIT_JT1078_PROCESS_H

#if defined(ENABLE_RTPPROXY)
#include "../Rtp/ProcessInterface.h"
#include "../Rtcp/RtcpContext.h"
#include "Common/MultiMediaSourceMuxer.h"
#include "Network/Socket.h"
#include "JT1078RtpDecoder.h"

namespace mediakit {

static constexpr char kJT1078AppName[] = "jt1078";

class JT1078Process final : public RtcpContextForRecv, public ProcessInterface, public MediaSinkInterface, public MediaSourceEvent, public toolkit::SockInfo, public std::enable_shared_from_this<JT1078Process> {
public:
    using Ptr = std::shared_ptr<JT1078Process>;
    using onDetachCB = std::function<void(const toolkit::SockException &ex)>;

    JT1078Process(const MediaTuple &tuple);
    ~JT1078Process() override;

    static Ptr createProcess(const MediaTuple &tuple);

    /// ProcessInterface override
    bool inputRtp(bool is_udp, const char *data, size_t data_len) override;
    void flush() override;

    /**
     * 输入 RTP 数据
     * @param is_udp 是否为 UDP 模式
     * @param sock 本地监听的 socket
     * @param data RTP 数据指针
     * @param len RTP 数据长度
     * @param addr 数据源地址
     * @param dts_out 解析出的最新 dts
     * @return 是否解析成功
     */
    bool inputRtp(bool is_udp, const toolkit::Socket::Ptr &sock, const char *data, size_t len, const struct sockaddr *addr, uint64_t *dts_out = nullptr);

    /**
     * 超时时移除触发
     */
    void onDetach(const toolkit::SockException &ex);

    /**
     * 设置断开回调
     */
    void setOnDetach(onDetachCB cb);

    /**
     * 暂停/恢复超时检测
     */
    void pauseTimeout(bool pause, uint32_t pause_seconds = 0);

    /**
     * 设置单 track
     */
    void setOnlyTrack(int only_track);

    /// SockInfo override
    std::string get_local_ip() override;
    uint16_t get_local_port() override;
    std::string get_peer_ip() override;
    uint16_t get_peer_port() override;
    std::string getIdentifier() const override;

    const toolkit::Socket::Ptr& getSock() const;

protected:
    /// MediaSinkInterface override
    bool inputFrame(const Frame::Ptr &frame) override;
    bool addTrack(const Track::Ptr &track) override;
    void addTrackCompleted() override;
    void resetTracks() override {};

    /// MediaSourceEvent override
    MediaOriginType getOriginType(MediaSource &sender) const override;
    std::string getOriginUrl(MediaSource &sender) const override;
    std::shared_ptr<toolkit::SockInfo> getOriginSock(MediaSource &sender) const override;
    toolkit::EventPoller::Ptr getOwnerPoller(MediaSource &sender) override;
    bool close(MediaSource &sender) override;

private:
    void emitOnPublish();
    void doCachedFunc();
    bool alive();
    void onManager();
    void createTimer();
    void onDecode(const Frame::Ptr &frame);

private:
    bool _pause_timeout = false;
    uint32_t _pause_seconds = 5 * 60;
    uint64_t _dts = 0;
    uint64_t _total_bytes = 0;
    int _only_track = 0;
    bool _track_completed = false;
    std::string _auth_err;
    std::unique_ptr<sockaddr_storage> _addr;
    toolkit::Socket::Ptr _sock;
    MediaInfo _media_info;
    toolkit::Ticker _last_frame_time;
    onDetachCB _on_detach;
    std::shared_ptr<FILE> _save_file;
    JT1078RtpDecoder::Ptr _decoder;
    MultiMediaSourceMuxer::Ptr _muxer;
    toolkit::Timer::Ptr _timer;
    toolkit::Ticker _last_check_alive;
    std::recursive_mutex _func_mtx;
    toolkit::Ticker _cache_ticker;
    std::deque<std::function<void()> > _cached_func;
};

} // namespace mediakit
#endif // defined(ENABLE_RTPPROXY)
#endif // ZLMEDIAKIT_JT1078_PROCESS_H