/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "JT1078Process.h"
#include "../Rtcp/RtcpContext.h"
#include "Util/File.h"
#include "Common/config.h"
#include "../Rtsp/Rtsp.h"

using namespace std;
using namespace toolkit;

namespace mediakit {

// 推流前缓存 frame，防止丢包，提高体验；最多缓存 10 秒数据
static constexpr size_t kMaxCachedFrameMS = 10 * 1000;

JT1078Process::Ptr JT1078Process::createProcess(const MediaTuple &tuple) {
    JT1078Process::Ptr ret(new JT1078Process(tuple));
    ret->createTimer();
    return ret;
}

JT1078Process::JT1078Process(const MediaTuple &tuple) {
    _media_info.schema = kJT1078AppName;
    static_cast<MediaTuple &>(_media_info) = tuple;

    GET_CONFIG(string, dump_dir, RtpProxy::kDumpDir);
    if (!dump_dir.empty()) {
        FILE *fp = File::create_file(File::absolutePath(_media_info.stream + ".jt1078", dump_dir), "wb");
        if (fp) {
            _save_file.reset(fp, [](FILE *fp) {
                fclose(fp);
            });
        }
    }
}

void JT1078Process::flush() {
    if (_decoder) {
        _decoder->flush();
    }
}

JT1078Process::~JT1078Process() {
    uint64_t duration = (_last_frame_time.createdTime() - _last_frame_time.elapsedTime()) / 1000;
    WarnP(this) << "JT1078 推流器("
                 << _media_info.shortUrl()
                 << ")断开,耗时(s):" << duration;

    // 流量统计事件广播
    GET_CONFIG(uint32_t, iFlowThreshold, General::kFlowThreshold);
    if (_total_bytes >= iFlowThreshold * 1024) {
        try {
            NOTICE_EMIT(BroadcastFlowReportArgs, Broadcast::kBroadcastFlowReport, _media_info, _total_bytes, duration, false, *this);
        } catch (std::exception &ex) {
            WarnL << "Exception occurred: " << ex.what();
        }
    }
}

void JT1078Process::onManager() {
    if (!alive()) {
        onDetach(SockException(Err_timeout, "JT1078Process timeout"));
    }
}

void JT1078Process::createTimer() {
    weak_ptr<JT1078Process> weakSelf = shared_from_this();
    _timer = std::make_shared<Timer>(3.0f, [weakSelf]() {
        auto strongSelf = weakSelf.lock();
        if (!strongSelf) {
            return false;
        }
        strongSelf->onManager();
        return true;
    }, EventPollerPool::Instance().getPoller());
}

bool JT1078Process::inputRtp(bool is_udp, const Socket::Ptr &sock, const char *data, size_t len, const struct sockaddr *addr, uint64_t *dts_out) {
    TraceP(this) << "JT1078Process::inputRtp, is_udp=" << is_udp << ", len=" << len;

    if (!_auth_err.empty()) {
        throw SockException(Err_other, _auth_err);
    }

    if (_sock != sock) {
        // 第一次运行
        bool first = !_sock;
        _sock = sock;
        _addr.reset(new sockaddr_storage(*((sockaddr_storage *)addr)));
        if (first) {
            TraceP(this) << "first JT1078 packet";
            emitOnPublish();
            _cache_ticker.resetTime();
        }
    }

    _total_bytes += len;

    if (_save_file) {
        uint16_t size = (uint16_t)len;
        size = htons(size);
        fwrite((uint8_t *)&size, 2, 1, _save_file.get());
        fwrite((uint8_t *)data, len, 1, _save_file.get());
    }

    // 创建 JT1078 解码器
    if (!_decoder) {
        _media_info.protocol = is_udp ? "udp" : "tcp";
        _decoder = std::make_shared<JT1078RtpDecoder>();
        weak_ptr<JT1078Process> weak_self = shared_from_this();
        _decoder->setOnFrame([weak_self](const Frame::Ptr &frame) {
            auto strong_self = weak_self.lock();
            if (strong_self) {
                return strong_self->inputFrame(frame);
            }
            return false;
        });
        _decoder->setOnTrack([weak_self](const Track::Ptr &track) {
            auto strong_self = weak_self.lock();
            if (strong_self) {
                return strong_self->addTrack(track);
            }
            return false;
        });
        InfoP(this) << "JT1078 decoder created";
    }

    // 检查最小长度
    if (len < 32) {
        WarnP(this) << "Data too short: " << len;
        return false;
    }

    // 解析包序号 (offset 6-7)
    uint16_t seq = (static_cast<uint8_t>(data[6]) << 8) | static_cast<uint8_t>(data[7]);

    // 解析时间戳 (offset 16-23, big-endian)
    uint32_t stamp = (static_cast<uint32_t>(static_cast<uint8_t>(data[16])) << 24) |
                     (static_cast<uint32_t>(static_cast<uint8_t>(data[17])) << 16) |
                     (static_cast<uint32_t>(static_cast<uint8_t>(data[18])) << 8) |
                     static_cast<uint32_t>(static_cast<uint8_t>(data[19]));

    // 解析扩展头
    // data[8-13]: SIM卡号BCD[6]
    // data[14]: 逻辑通道号(1 byte)
    // data[15]: 数据类型(4bit高) + 分包处理标记(4bit低)
    // data[16-23]: 时间戳BYTE[8]
    // data[24-25]: Last I Frame Interval(2 byte)
    // data[26-27]: Last Frame Interval(2 byte)
    // data[28-29]: 数据体长度(2 byte)
    // data[30]+: 数据体
    uint8_t channel = data[14];
    uint8_t type_and_mark = data[15];
    uint8_t data_type_val = type_and_mark >> 4;
    uint8_t subpackage_mark_val = type_and_mark & 0x0F;

    // 构建 ext_header
    JT1078ExtHeader ext_header;
    ext_header.channel = channel;
    ext_header.data_type = static_cast<JT1078DataType>(data_type_val);
    ext_header.subpackage_mark = static_cast<JT1078SubpackageMark>(subpackage_mark_val);
    ext_header.timestamp = 0;

    // 解析数据体长度 (offset 28-29, big-endian)
    uint16_t payload_len = (static_cast<uint8_t>(data[28]) << 8) | static_cast<uint8_t>(data[29]);

    if (payload_len > len - 30) {
        WarnP(this) << "Invalid payload length: " << payload_len << " > " << (len - 30);
        return false;
    }

    const char *payload = data + 30;

    // 更新 RTCP 统计
    onRtp(seq, stamp, 0, 90000, len);

    // 调用解码器处理分包
    bool ret = _decoder->handleSubpackage(payload, payload_len, ext_header, seq, stamp);

    if (dts_out) {
        *dts_out = _dts;
    }

    GET_CONFIG(string, dump_dir, RtpProxy::kDumpDir);
    if (_muxer && !_muxer->isEnabled() && !dts_out && dump_dir.empty()) {
        // 无人访问、且不取时间戳、不导出调试文件时，直接丢弃数据
        _last_frame_time.resetTime();
        return false;
    }

    return ret;
}

// ProcessInterface override - base class signature
bool JT1078Process::inputRtp(bool is_udp, const char *data, size_t data_len) {
    // This is a dummy implementation to satisfy the base class
    // The actual processing is done in the extended inputRtp method
    // This should never be called directly
    return false;
}

bool JT1078Process::inputFrame(const Frame::Ptr &frame) {
    _dts = frame->dts();
    if (_muxer) {
        _last_frame_time.resetTime();
        return _muxer->inputFrame(frame);
    }
    if (_cache_ticker.elapsedTime() > kMaxCachedFrameMS) {
        WarnL << "Cached frame of stream(" << _media_info.stream << ") is too much, your on_publish hook responded too late!";
        return false;
    }
    auto frame_cached = Frame::getCacheAbleFrame(frame);
    lock_guard<recursive_mutex> lck(_func_mtx);
    _cached_func.emplace_back([this, frame_cached]() {
        _last_frame_time.resetTime();
        _muxer->inputFrame(frame_cached);
    });
    return true;
}

bool JT1078Process::addTrack(const Track::Ptr &track) {
    if (_muxer) {
        bool ret = _muxer->addTrack(track);
        if (ret && !_track_completed) {
            _track_completed = true;
            _muxer->addTrackCompleted();
            InfoP(this) << "JT1078 tracks ready";
        }
        return ret;
    }
    lock_guard<recursive_mutex> lck(_func_mtx);
    _cached_func.emplace_back([this, track]() {
        _muxer->addTrack(track);
        if (!_track_completed) {
            _track_completed = true;
            _muxer->addTrackCompleted();
            InfoP(this) << "JT1078 tracks ready (cached)";
        }
    });
    return true;
}

void JT1078Process::addTrackCompleted() {
    if (_muxer) {
        if (!_track_completed) {
            _track_completed = true;
            _muxer->addTrackCompleted();
        }
    } else {
        lock_guard<recursive_mutex> lck(_func_mtx);
        _cached_func.emplace_back([this]() {
            if (!_track_completed) {
                _track_completed = true;
                _muxer->addTrackCompleted();
            }
        });
    }
}

void JT1078Process::doCachedFunc() {
    lock_guard<recursive_mutex> lck(_func_mtx);
    for (auto &func : _cached_func) {
        func();
    }
    _cached_func.clear();
}

bool JT1078Process::alive() {
    if (_pause_timeout) {
        if (_last_check_alive.elapsedTime() < _pause_seconds * 1000) {
            return true;
        }
        _pause_timeout = false;
    }

    _last_check_alive.resetTime();
    GET_CONFIG(uint64_t, timeoutSec, RtpProxy::kTimeoutSec);
    return _last_frame_time.elapsedTime() < timeoutSec * 1000;
}

void JT1078Process::pauseTimeout(bool pause, uint32_t pause_seconds) {
    _pause_timeout = pause;
    _pause_seconds = pause_seconds ? pause_seconds : 300;
    if (!pause) {
        _last_frame_time.resetTime();
    }
}

void JT1078Process::setOnlyTrack(int only_track) {
    _only_track = only_track;
}

void JT1078Process::onDetach(const SockException &ex) {
    if (_on_detach) {
        WarnL << ex << ", stream_id: " << getIdentifier();
        _on_detach(ex);
    }
    if (ex.getErrCode() == Err_timeout) {
        // 超时时触发广播事件
        uint16_t local_port = _sock ? _sock->get_local_port() : 0;
        MediaTuple tuple = _media_info;
        NOTICE_EMIT(BroadcastJt1078ServerTimeoutArgs, Broadcast::kBroadcastJt1078ServerTimeout, local_port, tuple);
    }
}

void JT1078Process::setOnDetach(onDetachCB cb) {
    _on_detach = std::move(cb);
}

string JT1078Process::get_peer_ip() {
    try {
        return _addr ? SockUtil::inet_ntoa((sockaddr *)_addr.get()) : "::";
    } catch (std::exception &ex) {
        return "::";
    }
}

uint16_t JT1078Process::get_peer_port() {
    try {
        return _addr ? SockUtil::inet_port((sockaddr *)_addr.get()) : 0;
    } catch (std::exception &ex) {
        return 0;
    }
}

string JT1078Process::get_local_ip() {
    return _sock ? _sock->get_local_ip() : "::";
}

uint16_t JT1078Process::get_local_port() {
    return _sock ? _sock->get_local_port() : 0;
}

string JT1078Process::getIdentifier() const {
    return _media_info.stream;
}

void JT1078Process::emitOnPublish() {
    TraceP(this) << "emitOnPublish called, stream=" << _media_info.stream;
    weak_ptr<JT1078Process> weak_self = shared_from_this();
    Broadcast::PublishAuthInvoker invoker = [weak_self](const string &err, const ProtocolOption &option) {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }
        auto poller = strong_self->getOwnerPoller(MediaSource::NullMediaSource());
        poller->async([weak_self, err, option]() {
            auto strong_self = weak_self.lock();
            if (!strong_self) {
                return;
            }
            if (err.empty()) {
                strong_self->_muxer = std::make_shared<MultiMediaSourceMuxer>(strong_self->_media_info, 0.0f, option);
                switch (strong_self->_only_track) {
                    case 1: strong_self->_muxer->setOnlyAudio(); break;
                    case 2: strong_self->_muxer->enableAudio(false); break;
                    default: break;
                }
                strong_self->_muxer->setMediaListener(strong_self);
                strong_self->doCachedFunc();
                InfoP(strong_self) << "允许 JT1078 推流";
            } else {
                strong_self->_auth_err = err;
                WarnP(strong_self) << "禁止 JT1078 推流:" << err;
            }
        });
    };

    // 触发推流鉴权事件
    auto flag = NOTICE_EMIT(BroadcastMediaPublishArgs, Broadcast::kBroadcastMediaPublish, MediaOriginType::rtp_push, _media_info, invoker, *this);
    if (!flag) {
        // 该事件无人监听，默认不鉴权
        invoker("", ProtocolOption());
    }
}

MediaOriginType JT1078Process::getOriginType(MediaSource &sender) const {
    return MediaOriginType::rtp_push;
}

string JT1078Process::getOriginUrl(MediaSource &sender) const {
    return _media_info.getUrl();
}

std::shared_ptr<toolkit::SockInfo> JT1078Process::getOriginSock(MediaSource &sender) const {
    return const_cast<JT1078Process *>(this)->shared_from_this();
}

bool JT1078Process::close(MediaSource &sender) {
    onDetach(SockException(Err_shutdown, "close media"));
    return true;
}

toolkit::EventPoller::Ptr JT1078Process::getOwnerPoller(MediaSource &sender) {
    if (_sock) {
        return _sock->getPoller();
    }
    throw std::runtime_error("JT1078Process::getOwnerPoller failed:" + _media_info.stream);
}

const toolkit::Socket::Ptr& JT1078Process::getSock() const {
    return _sock;
}

} // namespace mediakit