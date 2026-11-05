// Timing bench of the Player's device path (docs/TIMING.md). Run by hand,
// never by ctest: it takes minutes and its numbers depend on the machine.
//
// It plays synthetic scripts through the REAL ScriptClock (or a QMediaPlayer
// on the test-pattern clip), PlaybackOutput, DeviceController and its worker
// thread and the real TCode / Intiface transports, into local stand-ins that
// stamp every received write with the steady clock:
//  - "neo": a Sam Neo 2 stand-in transport (vibration + suction); the stamp
//    is taken when the worker hands the packet to the transport. An optional
//    acknowledgement delay models a GATT write with response.
//  - "udp" / "tcp": a TCode line sink on its own thread (stroke L0 + twist R0).
//  - "ws": a Buttplug v3 server on its own thread (vibration + linear).
// The script clock and the sinks use std::chrono::steady_clock (the
// monotonic clock QElapsedTimer uses as well).
//
// Usage: timing_bench [--seconds N] [--variant-seconds N] [--load N]
//                     [--only name,name] [--media-runs N]
// Defaults: 60 s baseline scripts, 20 s variants, 3 busy threads for the
// load runs, every scenario. Output: one Markdown table per scenario.
// The video scenarios: "media" and "media +load3" as the Player sends (the
// reports and the estimate between them), "media raw" and "media raw
// +load3" with the reports only (the path before the estimate).

#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QHostAddress>
#include <QMediaPlayer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUdpSocket>
#include <QUrl>
#include <QVideoSink>
#include <QWebSocket>
#include <QWebSocketServer>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

#include "device_controller.h"
#include "device_io/qt_transport.h"
#include "media_playhead.h"
#include "playback_output.h"
#include "samn/device/protocol.h"
#include "samn/playback.h"
#include "samn/stroke_filter.h"
#include "script_clock.h"

using samn::ui::DeviceController;
using samn::ui::MediaPlayhead;
using samn::ui::PlaybackOutput;
using samn::ui::ScriptClock;
using json = nlohmann::json;

namespace {

int64_t NowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string LoadAvg() {
    QFile f("/proc/loadavg");
    if (!f.open(QIODevice::ReadOnly)) return "n/a";
    const QList<QByteArray> parts = f.readAll().split(' ');
    if (parts.size() < 3) return "n/a";
    return (parts[0] + " " + parts[1] + " " + parts[2]).toStdString();
}

// Runs the event loop of this thread for `ms`.
void Spin(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, Qt::PreciseTimer, &loop, &QEventLoop::quit);
    loop.exec();
}

bool SpinUntil(const std::function<bool()>& done, int timeoutMs) {
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > timeoutMs) return false;
        Spin(5);
    }
    return true;
}

// --- what the far ends received ---------------------------------------------

struct WireEvent {
    int64_t ns;
    std::string data;
};

class Recorder {
public:
    void Add(int64_t ns, std::string data) {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back({ns, std::move(data)});
    }
    std::vector<WireEvent> Take() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<WireEvent> out = std::move(events_);
        events_.clear();
        return out;
    }
    void Clear() { Take(); }

private:
    std::mutex mutex_;
    std::vector<WireEvent> events_;
};

// The Sam Neo 2 link without Bluetooth: every write is stamped when the
// worker hands it over; `ackMs` > 0 then blocks the worker that long, as a
// GATT write with response waits for its acknowledgement (a model, not a
// measured radio).
class NeoStandIn : public samn::ui::QtTransport {
public:
    NeoStandIn(std::shared_ptr<Recorder> rec, int ackMs) : rec_(std::move(rec)), ackMs_(ackMs) {}
    samn::device::Status Connect() override {
        open_ = true;
        return samn::device::Status::Ok();
    }
    void Disconnect() override { open_ = false; }
    bool IsConnected() const override { return open_; }
    samn::device::Status Send(const samn::device::Bytes& data, samn::device::WriteMode) override {
        if (!open_) return Fail("Bluetooth: not connected");
        rec_->Add(NowNs(), std::string(data.begin(), data.end()));
        if (ackMs_ > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ackMs_));
        return samn::device::Status::Ok();
    }
    samn::device::ReceiveResult Receive(std::chrono::milliseconds) override {
        return samn::device::ReceiveResult::Timeout();
    }
    samn::device::PeerInfo Peer() const override { return {"Sam Neo 2 (timing stand-in)", "00:11:22:33:44:55", -40}; }

private:
    std::shared_ptr<Recorder> rec_;
    int ackMs_ = 0;
    bool open_ = false;
};

// A far end on its own thread and event loop, so its stamps never wait for
// the GUI thread: a UDP or TCP TCode line sink, or a Buttplug v3 server.
class SinkThread : public QThread {
public:
    enum class Kind { kUdp, kTcp, kWs };
    SinkThread(Kind kind, std::shared_ptr<Recorder> rec) : kind_(kind), rec_(std::move(rec)) {}
    ~SinkThread() override { Stop(); }

    quint16 Begin() {
        start();
        std::unique_lock<std::mutex> lock(mutex_);
        ready_cv_.wait(lock, [this] { return ready_; });
        return port_;
    }
    void Stop() {
        if (!isRunning()) return;
        quit();
        wait();
    }

protected:
    void run() override {
        auto owner = std::make_unique<QObject>();
        quint16 port = 0;
        if (kind_ == Kind::kUdp) {
            auto* s = new QUdpSocket(owner.get());
            if (s->bind(QHostAddress::LocalHost, 0)) port = s->localPort();
            QObject::connect(s, &QUdpSocket::readyRead, s, [this, s] {
                while (s->hasPendingDatagrams()) {
                    const int64_t ns = NowNs();
                    QByteArray d(int(s->pendingDatagramSize()), 0);
                    s->readDatagram(d.data(), d.size());
                    rec_->Add(ns, d.toStdString());
                }
            });
        } else if (kind_ == Kind::kTcp) {
            auto* server = new QTcpServer(owner.get());
            if (server->listen(QHostAddress::LocalHost, 0)) port = server->serverPort();
            QObject::connect(server, &QTcpServer::newConnection, server, [this, server] {
                QTcpSocket* c = server->nextPendingConnection();
                auto pending = std::make_shared<std::string>();
                QObject::connect(c, &QTcpSocket::readyRead, c, [this, c, pending] {
                    const int64_t ns = NowNs();
                    *pending += c->readAll().toStdString();
                    for (std::size_t nl; (nl = pending->find('\n')) != std::string::npos;) {
                        rec_->Add(ns, pending->substr(0, nl + 1));  // lines of one read share its stamp
                        pending->erase(0, nl + 1);
                    }
                });
            });
        } else {
            auto* server = new QWebSocketServer("timing-sink", QWebSocketServer::NonSecureMode, owner.get());
            if (server->listen(QHostAddress::LocalHost, 0)) port = server->serverPort();
            QObject::connect(server, &QWebSocketServer::newConnection, server, [this, server] {
                QWebSocket* c = server->nextPendingConnection();
                c->setParent(server);
                QObject::connect(c, &QWebSocket::textMessageReceived, c,
                                 [this, c](const QString& text) { Buttplug(c, text, NowNs()); });
            });
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            port_ = port;
            ready_ = true;
        }
        ready_cv_.notify_all();
        exec();
        owner.reset();
    }

private:
    // The Buttplug v3 answers the client needs (as tests/app/fake_servers.h);
    // one device with a vibrator and a linear axis.
    void Buttplug(QWebSocket* c, const QString& text, int64_t ns) {
        const json batch = json::parse(text.toStdString(), nullptr, false);
        if (!batch.is_array()) return;
        for (const auto& message : batch) {
            if (!message.is_object()) continue;
            rec_->Add(ns, message.dump());
            for (const auto& item : message.items()) {
                const std::string& name = item.key();
                const json id = item.value().value("Id", json());
                json reply;
                if (name == "RequestServerInfo") {
                    reply = {{"ServerInfo",
                              {{"Id", id}, {"ServerName", "TimingSink"}, {"MessageVersion", 3}, {"MaxPingTime", 0}}}};
                } else if (name == "RequestDeviceList") {
                    const json messages = {
                        {"ScalarCmd",
                         json::array({json{{"StepCount", 20}, {"ActuatorType", "Vibrate"}, {"FeatureDescriptor", "Vib"}}})},
                        {"LinearCmd", json::array({json{{"StepCount", 100}, {"ActuatorType", "Position"},
                                                        {"FeatureDescriptor", "Stroke"}}})},
                        {"StopDeviceCmd", json::object()}};
                    reply = {{"DeviceList",
                              {{"Id", id},
                               {"Devices", json::array({json{{"DeviceIndex", 0},
                                                             {"DeviceName", "Timing stand-in"},
                                                             {"DeviceMessages", messages}}})}}}};
                } else {
                    reply = {{"Ok", {{"Id", id}}}};
                }
                c->sendTextMessage(QString::fromStdString(json::array({reply}).dump()));
            }
        }
    }

    Kind kind_;
    std::shared_ptr<Recorder> rec_;
    std::mutex mutex_;
    std::condition_variable ready_cv_;
    bool ready_ = false;
    quint16 port_ = 0;
};

// Busy threads that compete with the player for the cores.
class BusyLoad {
public:
    explicit BusyLoad(int threads) {
        for (int i = 0; i < threads; ++i)
            threads_.emplace_back([this] {
                volatile double x = 1;
                while (!stop_.load(std::memory_order_relaxed)) x = x * 1.0000001 + 1e-9;
            });
    }
    ~BusyLoad() {
        stop_ = true;
        for (auto& t : threads_) t.join();
    }

private:
    std::atomic<bool> stop_{false};
    std::vector<std::thread> threads_;
};

// --- the received writes, decoded --------------------------------------------

// One command on the wire: channel "vib", "suc", "L0", "R0", "stop".
struct Rec {
    int64_t ns = 0;
    std::string ch;
    double value = 0;
    int dur = 0;
};

std::vector<Rec> Decode(const std::string& kind, const std::vector<WireEvent>& events) {
    std::vector<Rec> out;
    for (const WireEvent& e : events) {
        if (kind == "neo") {
            const std::string& d = e.data;
            if (d.size() == 7 && uint8_t(d[0]) == 0x55) {
                const uint8_t cmd = uint8_t(d[1]), v = uint8_t(d[5]);
                if (cmd == 0x03) out.push_back({e.ns, "vib", v / 10.0, 0});
                if (cmd == 0x09) out.push_back({e.ns, "suc", v / 5.0, 0});
            }
        } else if (kind == "ws") {
            const json m = json::parse(e.data, nullptr, false);
            if (!m.is_object()) continue;
            if (m.contains("ScalarCmd"))
                for (const auto& s : m["ScalarCmd"].value("Scalars", json::array()))
                    out.push_back({e.ns, "vib", s.value("Scalar", 0.0), 0});
            if (m.contains("LinearCmd"))
                for (const auto& v : m["LinearCmd"].value("Vectors", json::array()))
                    out.push_back({e.ns, "L0", v.value("Position", 0.0), v.value("Duration", 0)});
            if (m.contains("StopDeviceCmd")) out.push_back({e.ns, "stop", 0, 0});
        } else {  // TCode lines "L05000I250", "DSTOP"
            std::string line = e.data;
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
            if (line == "DSTOP") {
                out.push_back({e.ns, "stop", 0, 0});
                continue;
            }
            const std::size_t i = line.find('I');
            if (line.size() < 7 || i == std::string::npos) continue;
            Rec r;
            r.ns = e.ns;
            r.ch = line.substr(0, 2);
            r.value = std::stoi(line.substr(2, i - 2)) / 10000.0;
            r.dur = std::stoi(line.substr(i + 1));
            out.push_back(r);
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Rec& a, const Rec& b) { return a.ns < b.ns; });
    return out;
}

// --- statistics ---------------------------------------------------------------

// Nearest rank.
double Pct(const std::vector<double>& sorted, double p) {
    if (sorted.empty()) return NAN;
    const double rank = std::ceil(p / 100.0 * double(sorted.size()));
    const std::size_t i = rank < 1 ? 0 : std::min(sorted.size() - 1, std::size_t(rank) - 1);
    return sorted[i];
}

void Row(const std::string& scenario, const std::string& metric, std::vector<double> v, const std::string& note = {}) {
    std::sort(v.begin(), v.end());
    if (v.empty()) {
        std::printf("| %s | %s | 0 | - | - | - | - | - | - | %s |\n", scenario.c_str(), metric.c_str(), note.c_str());
        return;
    }
    double sum = 0;
    for (double x : v) sum += x;
    std::printf("| %s | %s | %zu | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %s |\n", scenario.c_str(), metric.c_str(),
                v.size(), sum / v.size(), v.front(), Pct(v, 50), Pct(v, 95), Pct(v, 99), v.back(), note.c_str());
}

void TableHead(const std::string& title) {
    std::printf("\n### %s\n\n", title.c_str());
    std::printf("| scenario | metric (ms unless noted) | n | mean | min | p50 | p95 | p99 | max | note |\n");
    std::printf("|---|---|---|---|---|---|---|---|---|---|\n");
}

// --- the scripts ----------------------------------------------------------------

struct Lanes {
    std::vector<samn::Action> stroke, twist, vibration, suction;
};

// Event i of a grid of `period` ms, moved by (37 * i) mod `span`: 400 ms is
// a multiple of the 16 ms clock tick and of the 100 ms media report, so an
// exact grid would always meet the reports at the same phase. 37 is prime to
// 16, 50 and 100, so the due times fall on every phase of both.
int64_t SpreadAt(int64_t i, int64_t period, int64_t span) { return i * period + (i * 37) % span; }

// Strokes about every 400 ms (10 <-> 90, twist 30 <-> 70 at the same times);
// vibration steps about every 100 ms (20, 80, 50, 100), suction about every
// 400 ms (40, 100). Each step holds its value until 1 ms before the next one.
std::vector<samn::Action> Strokes(int64_t lengthMs, int lo, int hi) {
    std::vector<samn::Action> out;
    for (int64_t i = 0; SpreadAt(i, 400, 100) <= lengthMs; ++i) out.push_back({SpreadAt(i, 400, 100), i % 2 ? hi : lo});
    return out;
}

std::vector<samn::Action> Steps(int64_t lengthMs, int64_t period, int64_t span, const std::vector<int>& values) {
    std::vector<samn::Action> out;
    for (int64_t k = 0; SpreadAt(k + 1, period, span) <= lengthMs; ++k) {
        const int v = values[std::size_t(k) % values.size()];
        out.push_back({SpreadAt(k, period, span), v});
        out.push_back({SpreadAt(k + 1, period, span) - 1, v});
    }
    return out;
}

Lanes MakeLanes(int64_t lengthMs) {
    Lanes l;
    l.stroke = Strokes(lengthMs, 10, 90);
    l.twist = Strokes(lengthMs, 30, 70);
    l.vibration = Steps(lengthMs, 100, 50, {20, 80, 50, 100});
    l.suction = Steps(lengthMs, 400, 100, {40, 100});
    return l;
}

// A level that becomes due at `at` (script ms) with the value the wire carries.
struct Step {
    int64_t at;
    double value;
};

std::vector<Step> StepsOf(const std::vector<samn::Action>& lane, int quantSteps) {
    std::vector<Step> out;
    for (std::size_t i = 0; i < lane.size(); i += 2) {
        double v = lane[i].pos / 100.0;
        if (quantSteps > 0) v = std::round(v * quantSteps) / quantSteps;
        out.push_back({lane[i].at, v});
    }
    return out;
}

// --- one run ------------------------------------------------------------------

struct Tick {
    int64_t ns;       // steady clock when the playhead report arrived
    qint64 clockPos;  // video / clock position
    qint64 scriptMs;  // after the offset (what PlaybackOutput::update got)
    int64_t updateNs; // time spent in PlaybackOutput::update
};

// Script ms -> the steady-clock time it is due at, from the reports of one
// playing segment. The script clock is linear by construction (position()
// is computed from the monotonic clock at the report), so the report that
// is least behind its position sets the anchor. A media clock may stall
// (decoding late); its due times follow the clock's own reports: linear
// between the two reports around the position (`points`, when given).
struct Mapping {
    double anchorNs = 0;
    double rate = 1;
    int offset = 0;
    std::vector<std::pair<double, double>> points;  // (clock ms, steady ns), increasing
    double IdealNs(double scriptMs, bool withOffset = true) const {
        const double clockMs = scriptMs + (withOffset ? offset : 0);
        if (points.size() >= 2) {
            auto hi = std::upper_bound(points.begin(), points.end(), clockMs,
                                       [](double v, const std::pair<double, double>& p) { return v < p.first; });
            if (hi == points.begin()) ++hi;
            if (hi == points.end()) --hi;
            const auto lo = hi - 1;
            const double slope = (hi->second - lo->second) / std::max(1e-9, hi->first - lo->first);
            return lo->second + (clockMs - lo->first) * slope;
        }
        return anchorNs + clockMs * 1e6 / rate;
    }
};

Mapping MapOf(const std::vector<Tick>& ticks, double rate, int offset) {
    Mapping m;
    m.rate = rate;
    m.offset = offset;
    m.anchorNs = INFINITY;
    for (const Tick& t : ticks) m.anchorNs = std::min(m.anchorNs, double(t.ns) - double(t.clockPos) * 1e6 / rate);
    return m;
}

// The media clock's reports as the mapping (positions strictly increasing).
Mapping MediaMapOf(const std::vector<Tick>& ticks) {
    Mapping m = MapOf(ticks, 1, 0);
    for (const Tick& t : ticks)
        if (m.points.empty() || double(t.clockPos) > m.points.back().first)
            m.points.push_back({double(t.clockPos), double(t.ns)});
    return m;
}

struct Results {
    std::vector<double> tickInterval, tickJitter, updateUs, staleness;
    std::map<std::string, std::vector<double>> pipeline, lateness, shift, startLag, endErr, durErr, squeeze, sendInterval,
        effSpeed;
    std::map<std::string, int> skipped, missing, found, limited;
};

const Tick* FirstTickAtOrAfter(const std::vector<Tick>& ticks, int64_t scriptMs) {
    for (const Tick& t : ticks)
        if (t.scriptMs >= scriptMs) return &t;
    return nullptr;
}

// The first write of `want` on `ch` in [fromNs, toNs).
const Rec* FirstRec(const std::vector<Rec>& recs, const std::string& ch, int64_t fromNs, int64_t toNs, double want) {
    for (const Rec& r : recs) {
        if (r.ns < fromNs || r.ch != ch) continue;
        if (r.ns >= toNs) return nullptr;
        if (std::abs(r.value - want) < 1e-4) return &r;
    }
    return nullptr;
}

// A write is matched to its step only before the same value is due again:
// a step whose value was coalesced away (newest wins) must not borrow the
// write of a later step with that value.
int64_t UntilNs(const Tick* again) { return again ? again->ns : INT64_MAX; }

void AnalyzeLevels(const std::string& ch, const std::vector<Step>& steps, const std::vector<Tick>& ticks,
                   const std::vector<Rec>& recs, const Mapping& map, Results& r) {
    for (std::size_t k = 1; k < steps.size(); ++k) {
        const Tick* t = FirstTickAtOrAfter(ticks, steps[k].at);
        if (!t) break;
        if (k + 1 < steps.size() && t->scriptMs >= steps[k + 1].at) {
            ++r.skipped[ch];  // no report fell inside this step
            continue;
        }
        const Tick* again = nullptr;
        for (std::size_t j = k + 1; j < steps.size() && !again; ++j)
            if (std::abs(steps[j].value - steps[k].value) < 1e-6) again = FirstTickAtOrAfter(ticks, steps[j].at);
        const Rec* hit = FirstRec(recs, ch, t->ns, UntilNs(again), steps[k].value);
        if (!hit) {
            ++r.missing[ch];
            continue;
        }
        ++r.found[ch];
        r.pipeline[ch].push_back((hit->ns - t->ns) / 1e6);
        r.lateness[ch].push_back((hit->ns - map.IdealNs(steps[k].at)) / 1e6);
        r.shift[ch].push_back((hit->ns - map.IdealNs(steps[k].at, false)) / 1e6);
    }
}

// A move to action i goes out at the first report inside [a[i-1], a[i]) as
// "there in (a[i] - t) / speed ms" (PlaybackOutput::sendStroke). Measured:
// report -> wire, wire vs when a[i-1] was due (the move should start then),
// wire + duration vs when a[i] is due (the move should end then), and the
// duration against the formula, max(20, (a[i] - t) / speed) and, with a
// speed limit, at least ceil(way * 100 * 1000 / limit).
void AnalyzeStroke(const std::string& ch, const std::vector<samn::Action>& acts, const std::vector<Tick>& ticks,
                   const std::vector<Rec>& recs, const Mapping& map, double speed, int maxSpeed, Results& r) {
    for (std::size_t i = 2; i < acts.size(); ++i) {  // i = 1 is the soft-seek move after the start
        const Tick* t = FirstTickAtOrAfter(ticks, acts[i - 1].at);
        if (!t) break;
        if (t->scriptMs >= acts[i].at) {
            ++r.skipped[ch];
            continue;
        }
        const Tick* again = nullptr;  // the move to the next action with this position
        for (std::size_t j = i + 1; j < acts.size() && !again; ++j)
            if (acts[j].pos == acts[i].pos) again = FirstTickAtOrAfter(ticks, acts[j - 1].at);
        const Rec* hit = FirstRec(recs, ch, t->ns, UntilNs(again), acts[i].pos / 100.0);
        if (!hit) {
            ++r.missing[ch];
            continue;
        }
        ++r.found[ch];
        r.pipeline[ch].push_back((hit->ns - t->ns) / 1e6);
        r.startLag[ch].push_back((hit->ns - map.IdealNs(acts[i - 1].at)) / 1e6);
        r.endErr[ch].push_back((hit->ns + hit->dur * 1e6 - map.IdealNs(acts[i].at)) / 1e6);
        // How much shorter than scripted the move is (a late start squeezes it).
        const double scripted = double(acts[i].at - acts[i - 1].at) / speed;
        if (scripted > 0) r.squeeze[ch].push_back(100.0 * (1.0 - hit->dur / scripted));
        r.shift[ch].push_back((hit->ns + hit->dur * 1e6 - map.IdealNs(acts[i].at, false)) / 1e6);
        int expect = std::max<int>(PlaybackOutput::kMinMoveMs, int(double(acts[i].at - t->scriptMs) / speed));
        const Rec* prev = nullptr;
        for (const Rec& x : recs)
            if (x.ch == ch && x.ns < hit->ns) prev = &x;
        if (maxSpeed > 0 && prev) {
            const double way = std::abs(hit->value - prev->value);
            const int limit = int(std::ceil(way * 100.0 * 1000.0 / maxSpeed));
            if (limit > expect) ++r.limited[ch];
            expect = std::max(expect, limit);
            if (hit->dur > 0) r.effSpeed[ch].push_back(way * 100.0 / (hit->dur / 1000.0));
        }
        r.durErr[ch].push_back(hit->dur - expect);
    }
}

// Between the first and the last report of the playback: the stop at the
// end (in the same call as the last report) is not playback.
void SendIntervals(const std::vector<Rec>& recs, const std::vector<Tick>& ticks, Results& r) {
    if (ticks.empty()) return;
    std::map<std::string, int64_t> last;
    for (const Rec& x : recs) {
        if (x.ch == "stop" || x.ns < ticks.front().ns || x.ns >= ticks.back().ns) continue;
        auto it = last.find(x.ch);
        if (it != last.end()) r.sendInterval[x.ch].push_back((x.ns - it->second) / 1e6);
        last[x.ch] = x.ns;
    }
}

void TickStats(const std::vector<Tick>& ticks, int nominalMs, Results& r) {
    for (std::size_t i = 1; i < ticks.size(); ++i) {
        const double d = (ticks[i].ns - ticks[i - 1].ns) / 1e6;
        r.tickInterval.push_back(d);
        r.tickJitter.push_back(std::abs(d - nominalMs));
    }
    for (const Tick& t : ticks) r.updateUs.push_back(t.updateNs / 1e3);
}

void PrintResults(const std::string& name, const Results& r, bool clockTicks) {
    if (clockTicks) {
        Row(name, "report interval", r.tickInterval);
        Row(name, "report jitter, abs(interval - 16)", r.tickJitter);
    } else {
        Row(name, "report interval", r.tickInterval);
    }
    if (!r.staleness.empty()) Row(name, "media clock behind a linear clock", r.staleness);
    Row(name, "PlaybackOutput::update (us)", r.updateUs);
    for (const auto& [ch, v] : r.pipeline) {
        const auto count = [&](const std::map<std::string, int>& m) {
            const auto it = m.find(ch);
            return it == m.end() ? 0 : it->second;
        };
        const std::string note = "found " + std::to_string(count(r.found)) + ", no report inside " +
                                 std::to_string(count(r.skipped)) + ", not on the wire (coalesced) " +
                                 std::to_string(count(r.missing));
        Row(name, ch + " report -> wire", v, note);
        if (r.lateness.count(ch)) Row(name, ch + " wire - due", r.lateness.at(ch));
        if (r.startLag.count(ch)) Row(name, ch + " move start - due", r.startLag.at(ch));
        if (r.endErr.count(ch)) Row(name, ch + " move end - due", r.endErr.at(ch));
        if (r.shift.count(ch))
            Row(name, ch + (r.endErr.count(ch) ? " move end" : " wire") + " - due, offset ignored", r.shift.at(ch));
        if (r.durErr.count(ch)) Row(name, ch + " duration - formula", r.durErr.at(ch));
        if (r.squeeze.count(ch)) Row(name, ch + " move shorter than scripted (%)", r.squeeze.at(ch));
        if (r.effSpeed.count(ch))
            Row(name, ch + " speed on the wire (u/s)", r.effSpeed.at(ch),
                "limit raised " + std::to_string(count(r.limited)) + " moves");
    }
    for (const auto& [ch, v] : r.sendInterval) Row(name, ch + " interval between sends", v);
}

// --- scenarios ----------------------------------------------------------------

struct RunConfig {
    std::string name;
    std::string kind;  // neo | udp | tcp | ws
    int seconds = 60;
    int offsetMs = 0;
    double speed = 1;
    int load = 0;
    int ackMs = 0;
    int maxSpeed = 0;
};

struct Rig {
    std::shared_ptr<Recorder> rec = std::make_shared<Recorder>();
    std::unique_ptr<SinkThread> sink;
};

bool ConnectRig(DeviceController& dev, const std::string& kind, int ackMs, Rig& rig) {
    quint16 port = 0;
    if (kind != "neo") {
        const auto k = kind == "udp" ? SinkThread::Kind::kUdp
                       : kind == "tcp" ? SinkThread::Kind::kTcp
                                       : SinkThread::Kind::kWs;
        rig.sink = std::make_unique<SinkThread>(k, rig.rec);
        port = rig.sink->Begin();
        if (port == 0) return false;
    }
    if (kind == "neo") {
        auto rec = rig.rec;
        dev.setBleTransportFactory([rec, ackMs] { return std::make_unique<NeoStandIn>(rec, ackMs); });
        dev.connectSamNeo2();
    } else if (kind == "udp") {
        dev.connectTCode(QString("udp://127.0.0.1:%1").arg(port));
    } else if (kind == "tcp") {
        dev.connectTCode(QString("tcp://127.0.0.1:%1").arg(port));
    } else {
        dev.connectIntiface(QString("ws://127.0.0.1:%1").arg(port));
    }
    return SpinUntil([&] { return dev.connected(); }, 15000);
}

void Teardown(DeviceController& dev, Rig& rig) {
    dev.disconnectDevice();
    SpinUntil([&] { return !dev.connected(); }, 5000);
    Spin(100);
    if (rig.sink) rig.sink->Stop();
}

bool RunScript(const RunConfig& cfg) {
    const int64_t lengthMs = int64_t(cfg.seconds) * 1000;
    const Lanes lanes = MakeLanes(lengthMs);
    Rig rig;
    DeviceController dev;
    if (!ConnectRig(dev, cfg.kind, cfg.ackMs, rig)) {
        std::printf("| %s | connect failed | | | | | | | | |\n", cfg.name.c_str());
        return false;
    }
    PlaybackOutput out(nullptr);
    out.setDevice(&dev);
    out.setSoftStartMs(0);  // the soft start has its own scenario
    out.setMaxSpeed(cfg.maxSpeed);
    out.setSpeed(cfg.speed);
    const bool levels = cfg.kind == "neo" || cfg.kind == "ws";
    const bool tcode = cfg.kind == "udp" || cfg.kind == "tcp";
    std::map<std::string, std::vector<samn::Action>> axes;
    if (tcode) axes["twist"] = lanes.twist;
    out.setScript(cfg.kind == "neo" ? std::vector<samn::Action>{} : lanes.stroke, levels ? lanes.vibration : std::vector<samn::Action>{},
                  cfg.kind == "neo" ? lanes.suction : std::vector<samn::Action>{}, axes);

    ScriptClock clock;
    clock.setDuration(lengthMs + std::max(0, cfg.offsetMs) + 200);
    clock.setPlaybackRate(cfg.speed);
    std::vector<Tick> ticks;
    ticks.reserve(size_t(lengthMs / 16 / cfg.speed) + 1000);
    QObject::connect(&clock, &ScriptClock::playbackStateChanged, &out,
                     [&] { out.setPlaying(clock.playbackState() == ScriptClock::PlayingState); });
    // The Player page's path: positionChanged -> setVideoPositionMs ->
    // script_time(video, offset) -> PlaybackOutput::update.
    QObject::connect(&clock, &ScriptClock::positionChanged, &out, [&] {
        if (clock.playbackState() != ScriptClock::PlayingState) return;
        Tick t;
        t.ns = NowNs();
        t.clockPos = clock.position();
        t.scriptMs = samn::script_time(t.clockPos, cfg.offsetMs);
        out.update(t.scriptMs);
        t.updateNs = NowNs() - t.ns;
        ticks.push_back(t);
    });

    const std::string loadStart = LoadAvg();
    {
        BusyLoad load(cfg.load);
        Spin(300);
        rig.rec->Clear();  // the handshake is not playback
        QEventLoop loop;
        QObject::connect(&clock, &ScriptClock::finished, &loop, &QEventLoop::quit);
        QTimer guard;
        guard.setSingleShot(true);
        QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
        guard.start(int(double(lengthMs) / cfg.speed) + 15000);
        clock.play();
        loop.exec();
        Spin(400);
    }
    const std::string loadEnd = LoadAvg();
    Teardown(dev, rig);
    const std::vector<Rec> recs = Decode(cfg.kind, rig.rec->Take());

    Results r;
    TickStats(ticks, 16, r);
    const Mapping map = MapOf(ticks, cfg.speed, cfg.offsetMs);
    if (cfg.kind == "neo") {
        AnalyzeLevels("vib", StepsOf(lanes.vibration, 10), ticks, recs, map, r);
        AnalyzeLevels("suc", StepsOf(lanes.suction, 5), ticks, recs, map, r);
    } else if (cfg.kind == "ws") {
        AnalyzeLevels("vib", StepsOf(lanes.vibration, 0), ticks, recs, map, r);
    }
    if (cfg.kind != "neo") {
        samn::StrokeFilter f;
        f.maxSpeed = cfg.maxSpeed;
        AnalyzeStroke("L0", samn::ApplyStrokeFilter(lanes.stroke, f), ticks, recs, map, cfg.speed, cfg.maxSpeed, r);
    }
    if (tcode) AnalyzeStroke("R0", lanes.twist, ticks, recs, map, cfg.speed, 0, r);
    SendIntervals(recs, ticks, r);

    char title[256];
    std::snprintf(title, sizeof title, "%s: %s, %d s, offset %+d ms, speed %.1fx, %d busy threads, loadavg %s -> %s",
                  cfg.name.c_str(), cfg.kind.c_str(), cfg.seconds, cfg.offsetMs, cfg.speed, cfg.load,
                  loadStart.c_str(), loadEnd.c_str());
    TableHead(title);
    PrintResults(cfg.name, r, true);
    return true;
}

// Soft start: a constant full vibration lane, played and paused five times.
// Each start ramps in 10 steps over 500 ms; step k (1-10) is due (k-1) * 50
// ms after the first step (samn::SoftStartFactor).
bool RunSoftStart(const std::string& name, int load) {
    constexpr int kSoftMs = 500, kCycles = 5;
    Rig rig;
    DeviceController dev;
    if (!ConnectRig(dev, "neo", 0, rig)) {
        std::printf("| %s | connect failed | | | | | | | | |\n", name.c_str());
        return false;
    }
    PlaybackOutput out(nullptr);
    out.setDevice(&dev);
    out.setSoftStartMs(kSoftMs);
    out.setScript({}, {{0, 100}, {600000, 100}}, {});
    ScriptClock clock;
    clock.setDuration(600000);
    QObject::connect(&clock, &ScriptClock::playbackStateChanged, &out,
                     [&] { out.setPlaying(clock.playbackState() == ScriptClock::PlayingState); });
    QObject::connect(&clock, &ScriptClock::positionChanged, &out, [&] {
        if (clock.playbackState() == ScriptClock::PlayingState) out.update(clock.position());
    });
    std::vector<int64_t> playNs;
    const std::string loadStart = LoadAvg();
    {
        BusyLoad busy(load);
        Spin(300);
        rig.rec->Clear();
        for (int c = 0; c < kCycles; ++c) {
            playNs.push_back(NowNs());
            clock.play();
            Spin(1200);
            clock.pause();
            Spin(400);
        }
    }
    const std::string loadEnd = LoadAvg();
    Teardown(dev, rig);
    const std::vector<Rec> recs = Decode("neo", rig.rec->Take());

    std::vector<double> stepLate, firstStep;
    int found = 0, expected = 0;
    double last = 0;
    std::size_t cycle = 0;
    for (std::size_t i = 0; i < recs.size(); ++i) {
        const Rec& x = recs[i];
        if (x.ch != "vib") continue;
        if (std::abs(x.value - 0.1) < 1e-6 && last < 1e-6) {  // a ramp begins
            if (cycle < playNs.size()) firstStep.push_back((x.ns - playNs[cycle]) / 1e6);
            ++cycle;
            for (int k = 2; k <= 10; ++k) {
                ++expected;
                const Rec* hit = FirstRec(recs, "vib", x.ns, x.ns + 1000000000LL, k / 10.0);
                if (!hit) continue;
                ++found;
                stepLate.push_back((hit->ns - x.ns - (k - 1) * (kSoftMs / 10) * 1e6) / 1e6);
            }
        }
        last = x.value;
    }
    char title[256];
    std::snprintf(title, sizeof title, "%s: soft start %d ms, %d starts, %d busy threads, loadavg %s -> %s",
                  name.c_str(), kSoftMs, kCycles, load, loadStart.c_str(), loadEnd.c_str());
    TableHead(title);
    Row(name, "first step after play()", firstStep, std::to_string(cycle) + " ramps");
    Row(name, "step k on the wire - (k-1)*50 ms after step 1", stepLate,
        std::to_string(found) + " of " + std::to_string(expected) + " steps");
    return true;
}

// The video path: QMediaPlayer (the Player page's MediaPlayer) on the
// 4 s test-pattern clip drives PlaybackOutput into the UDP TCode sink, as
// PlayerController does: every report, and between the reports the
// estimate of MediaPlayhead. `raw`: the reports only (the path before the
// estimate, for comparison under the same load).
bool RunMedia(const std::string& name, int runs, int load, bool raw) {
    const QString clip = QStringLiteral(SAMN_TEST_DATA "/testpattern_4s.mp4");
    Rig rig;
    DeviceController dev;
    if (!ConnectRig(dev, "udp", 0, rig)) {
        std::printf("| %s | connect failed | | | | | | | | |\n", name.c_str());
        return false;
    }
    const std::vector<samn::Action> stroke = Strokes(4000, 10, 90);
    PlaybackOutput out(nullptr);
    out.setDevice(&dev);
    out.setSoftStartMs(0);
    out.setScript(stroke, {}, {});

    QMediaPlayer player;
    QVideoSink sink;
    player.setVideoSink(&sink);
    player.setSource(QUrl::fromLocalFile(clip));
    if (!SpinUntil([&] { return player.mediaStatus() == QMediaPlayer::LoadedMedia; }, 10000)) {
        std::printf("| %s | media did not load (%s) | | | | | | | | |\n", name.c_str(),
                    player.errorString().toStdString().c_str());
        Teardown(dev, rig);
        return false;
    }
    // segments: the media reports (the media clock); updates: every
    // position PlaybackOutput got (reports and estimates).
    std::vector<std::vector<Tick>> segments, updates;
    MediaPlayhead head;
    QObject::connect(&player, &QMediaPlayer::playbackStateChanged, &out, [&] {
        const bool playing = player.playbackState() == QMediaPlayer::PlayingState;
        if (!playing && !raw) head.setPlaying(false);  // PlayerController::setMediaPlaying
        out.setPlaying(playing);
        if (playing && !raw) head.setPlaying(true);
    });
    QObject::connect(&player, &QMediaPlayer::positionChanged, &out, [&](qint64 pos) {
        const qint64 send = raw ? pos : head.report(pos);
        if (player.playbackState() != QMediaPlayer::PlayingState || segments.empty()) return;
        Tick t;
        t.ns = NowNs();
        t.clockPos = pos;
        t.scriptMs = samn::script_time(send, 0);
        out.update(t.scriptMs);
        t.updateNs = NowNs() - t.ns;
        Tick u = t;
        u.clockPos = send;
        segments.back().push_back(t);
        updates.back().push_back(u);
    });
    QObject::connect(&head, &MediaPlayhead::advanced, &out, [&](qint64 videoMs) {
        if (updates.empty()) return;
        Tick u;
        u.ns = NowNs();
        u.clockPos = videoMs;
        u.scriptMs = samn::script_time(videoMs, 0);
        out.update(u.scriptMs, false);
        u.updateNs = NowNs() - u.ns;
        updates.back().push_back(u);
    });
    const std::string loadStart = LoadAvg();
    std::unique_ptr<BusyLoad> busy = std::make_unique<BusyLoad>(load);
    Spin(300);
    rig.rec->Clear();
    for (int i = 0; i < runs; ++i) {
        player.setPosition(0);
        Spin(200);
        segments.emplace_back();
        updates.emplace_back();
        QEventLoop loop;
        const auto c = QObject::connect(&player, &QMediaPlayer::playbackStateChanged, &loop, [&] {
            if (player.playbackState() == QMediaPlayer::StoppedState) loop.quit();
        });
        QTimer::singleShot(15000, &loop, &QEventLoop::quit);
        player.play();
        loop.exec();
        QObject::disconnect(c);
        Spin(300);
    }
    busy.reset();
    const std::string loadEnd = LoadAvg();
    player.stop();
    Teardown(dev, rig);
    const std::vector<Rec> recs = Decode("udp", rig.rec->Take());

    Results r;
    for (std::size_t s = 0; s < segments.size(); ++s) {
        const std::vector<Tick>& seg = segments[s];
        if (seg.size() < 3) continue;
        // The report at the end of the clip (positionChanged(duration)) is not a timer tick.
        std::vector<Tick> ticks(seg.begin(), seg.end() - 1);
        TickStats(ticks, 0, r);
        // How far the media clock fell behind a steady clock (decoding stalls) ...
        const Mapping linear = MapOf(ticks, 1, 0);
        for (const Tick& t : ticks) r.staleness.push_back((t.ns - linear.IdealNs(double(t.clockPos))) / 1e6);
        // ... and the moves (sent at the reports and the estimates) against
        // the media clock itself (the picture follows it).
        AnalyzeStroke("L0", stroke, updates[s], recs, MediaMapOf(ticks), 1, 0, r);
    }
    char title[256];
    std::snprintf(title, sizeof title,
                  "%s: QMediaPlayer (%s backend) on the 4 s clip x %d, %s, UDP TCode, %d busy threads, loadavg %s -> %s",
                  name.c_str(), qgetenv("QT_MEDIA_BACKEND").constData(), runs,
                  raw ? "reports only" : "reports + estimate", load, loadStart.c_str(), loadEnd.c_str());
    TableHead(title);
    PrintResults(name, r, false);
    return true;
}

bool Selected(const std::string& name, const std::vector<std::string>& only) {
    if (only.empty()) return true;
    for (const auto& o : only)
        if (name == o) return true;
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    if (qEnvironmentVariableIsEmpty("QT_MEDIA_BACKEND")) qputenv("QT_MEDIA_BACKEND", "ffmpeg");
    QGuiApplication app(argc, argv);
    QTemporaryDir config;
    qputenv("SAMN_CONFIG_DIR", config.path().toUtf8());  // never the user's settings

    int seconds = 60, variantSeconds = 20, load = 3, mediaRuns = 5;
    std::vector<std::string> only;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString a = args[i];
        const bool hasValue = i + 1 < args.size();
        if (a == "--seconds" && hasValue) seconds = args[++i].toInt();
        else if (a == "--variant-seconds" && hasValue) variantSeconds = args[++i].toInt();
        else if (a == "--load" && hasValue) load = args[++i].toInt();
        else if (a == "--media-runs" && hasValue) mediaRuns = args[++i].toInt();
        else if (a == "--only" && hasValue)
            for (const QString& s : args[++i].split(',', Qt::SkipEmptyParts)) only.push_back(s.toStdString());
        else {
            std::fprintf(stderr, "usage: timing_bench [--seconds N] [--variant-seconds N] [--load N] [--media-runs N] [--only a,b]\n");
            return 2;
        }
    }
    seconds = std::max(5, seconds);
    variantSeconds = std::max(5, variantSeconds);

    std::printf("# Timing bench\n\n");
    std::printf("- date (UTC): %s\n", QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString().c_str());
    std::printf("- Qt %s, platform %s, media backend %s, %u hardware threads\n", qVersion(),
                QGuiApplication::platformName().toStdString().c_str(), qgetenv("QT_MEDIA_BACKEND").constData(),
                std::thread::hardware_concurrency());
#ifdef NDEBUG
    std::printf("- build: NDEBUG\n");
#else
    std::printf("- build: assertions on (no NDEBUG)\n");
#endif
    std::printf("- loadavg at start: %s\n", LoadAvg().c_str());

    std::vector<RunConfig> runs;
    for (int l : {0, load}) {
        const std::string suffix = l ? " +load" + std::to_string(l) : "";
        runs.push_back({"neo" + suffix, "neo", seconds, 0, 1, l, 0, 0});
        runs.push_back({"neo-ack15" + suffix, "neo", seconds, 0, 1, l, 15, 0});
        runs.push_back({"udp" + suffix, "udp", seconds, 0, 1, l, 0, 0});
        runs.push_back({"tcp" + suffix, "tcp", seconds, 0, 1, l, 0, 0});
        runs.push_back({"ws" + suffix, "ws", seconds, 0, 1, l, 0, 0});
        if (load == 0) break;
    }
    for (const char* kind : {"udp", "neo"}) {
        const std::string k = kind;
        runs.push_back({k + " offset+200", k, variantSeconds, 200, 1, 0, 0, 0});
        runs.push_back({k + " offset-200", k, variantSeconds, -200, 1, 0, 0, 0});
        runs.push_back({k + " speed0.5", k, variantSeconds, 0, 0.5, 0, 0, 0});
        runs.push_back({k + " speed2", k, variantSeconds, 0, 2, 0, 0, 0});
    }
    runs.push_back({"udp speedlimit150", "udp", variantSeconds, 0, 1, 0, 0, 150});

    bool ok = true;
    for (const RunConfig& cfg : runs)
        if (Selected(cfg.name, only)) ok = RunScript(cfg) && ok;
    if (Selected("softstart", only)) ok = RunSoftStart("softstart", 0) && ok;
    const std::string softLoad = "softstart +load" + std::to_string(load);
    if (load > 0 && Selected(softLoad, only)) ok = RunSoftStart(softLoad, load) && ok;
    for (const bool raw : {true, false}) {
        const std::string media = raw ? "media raw" : "media";
        if (Selected(media, only)) ok = RunMedia(media, mediaRuns, 0, raw) && ok;
        const std::string mediaLoad = media + " +load" + std::to_string(load);
        if (load > 0 && Selected(mediaLoad, only)) ok = RunMedia(mediaLoad, mediaRuns, load, raw) && ok;
    }
    std::printf("\n- loadavg at the end: %s\n", LoadAvg().c_str());
    return ok ? 0 : 1;
}
