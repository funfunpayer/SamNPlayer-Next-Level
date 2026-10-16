#include "samn/device/tcode.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace samn::device {

namespace {

// Table of the standard OSR2 / SR6 channel assignment (stroke L0 is separate).
struct AxisCode {
    const char* name;
    const char* code;
};
const AxisCode kAxes[] = {{kChSurge, "L1"}, {kChSway, "L2"}, {kChTwist, "R0"},
                          {kChRoll, "R1"},  {kChPitch, "R2"}};

std::string Trim(const std::string& s) {
    const char* ws = " \t\r\n\v\f";
    const std::size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) return "";
    const std::size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Go net.SplitHostPort. Returns false on its errors.
bool SplitHostPort(const std::string& hostport, std::string* host, std::string* port) {
    const std::size_t last = hostport.rfind(':');
    if (last == std::string::npos) return false;  // missing port
    std::size_t j = 0, k = 0;
    if (!hostport.empty() && hostport[0] == '[') {
        const std::size_t end = hostport.find(']');
        if (end == std::string::npos) return false;  // missing ']'
        if (end + 1 == hostport.size()) return false;  // missing port
        if (end + 1 != last) return false;             // too many colons / missing port
        *host = hostport.substr(1, end - 1);
        j = 1;
        k = end + 1;
    } else {
        *host = hostport.substr(0, last);
        if (host->find(':') != std::string::npos) return false;  // too many colons
        j = 0;
        k = last;
    }
    if (hostport.find('[', j) != std::string::npos) return false;
    if (hostport.find(']', k) != std::string::npos) return false;
    *port = hostport.substr(last + 1);
    return true;
}

std::string JoinHostPort(const std::string& host, const std::string& port) {
    if (host.find(':') != std::string::npos) return "[" + host + "]:" + port;
    return host + ":" + port;
}

bool IsSchemeChar(char c, bool first) {
    if (std::isalpha(static_cast<unsigned char>(c))) return true;
    return !first && (std::isdigit(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.');
}

// The host part of "scheme://authority/...", like net/url: userinfo dropped,
// the optional port must be digits. False when Go's url.Parse would fail.
bool ParseUrlHost(const std::string& rest, std::string* host) {
    std::size_t end = rest.find_first_of("/?#");
    std::string authority = rest.substr(0, end == std::string::npos ? rest.size() : end);
    const std::size_t at = authority.rfind('@');
    if (at != std::string::npos) authority = authority.substr(at + 1);
    for (char c : authority) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u <= 0x20 || u == 0x7f || c == '<' || c == '>' || c == '"' || c == '\\' || c == '^' ||
            c == '`' || c == '{' || c == '|' || c == '}')
            return false;
    }
    std::string colon_port;
    if (!authority.empty() && authority[0] == '[') {
        const std::size_t close = authority.find(']');
        if (close == std::string::npos) return false;
        colon_port = authority.substr(close + 1);
    } else {
        const std::size_t colon = authority.rfind(':');
        if (colon != std::string::npos) colon_port = authority.substr(colon);
    }
    if (!colon_port.empty()) {
        if (colon_port[0] != ':') return false;
        for (std::size_t i = 1; i < colon_port.size(); ++i)
            if (!std::isdigit(static_cast<unsigned char>(colon_port[i]))) return false;
    }
    *host = authority;
    return true;
}

}  // namespace

std::string TCodeValue(double pos) {
    if (!(pos >= 0)) pos = 0;  // also NaN
    if (pos > 1) pos = 1;
    // Digits are the decimal fraction ("8000" = 0.8); 1.0 caps at "9999".
    const int scale = 10000;  // 10^kTCodeDigits
    int v = static_cast<int>(std::round(pos * scale));
    if (v > scale - 1) v = scale - 1;
    char buf[16];
    std::snprintf(buf, sizeof buf, "%0*d", kTCodeDigits, v);
    return buf;
}

std::string TCodeMove(const std::string& axis, double pos, std::chrono::milliseconds d) {
    long long ms = d.count();
    if (ms < 1) ms = 1;
    return axis + TCodeValue(pos) + "I" + std::to_string(ms);
}

Status ParseTCodeAddress(const std::string& address, TCodeAddress* out) {
    std::string a = Trim(address);
    if (a.empty())
        return Status::Error("tcode: no address — enter the device's IP, e.g. 192.168.1.50");
    std::string network = "udp";
    const std::size_t sep = a.find("://");
    if (sep != std::string::npos) {
        const std::string invalid = "tcode: invalid address \"" + address + "\"";
        bool scheme_ok = sep > 0;
        for (std::size_t i = 0; i < sep && scheme_ok; ++i) scheme_ok = IsSchemeChar(a[i], i == 0);
        std::string host;
        if (!scheme_ok || !ParseUrlHost(a.substr(sep + 3), &host) || host.empty())
            return Status::Error(invalid);
        const std::string scheme = Lower(a.substr(0, sep));
        if (scheme != "udp" && scheme != "tcp")
            return Status::Error("tcode: use udp:// or tcp:// (got \"" + scheme + "\")");
        network = scheme;
        a = host;
    }
    std::string h, p;
    if (!SplitHostPort(a, &h, &p)) {
        std::string bare = a;
        while (!bare.empty() && (bare.front() == '[' || bare.front() == ']')) bare.erase(0, 1);
        while (!bare.empty() && (bare.back() == '[' || bare.back() == ']')) bare.pop_back();
        a = JoinHostPort(bare, kDefaultTCodePort);
    }
    out->network = network;
    out->hostport = a;
    return Status::Ok();
}

std::string TCodeAxisCode(const std::string& axis) {
    for (const auto& a : kAxes)
        if (axis == a.name) return a.code;
    return "";
}

TCodeDevice TCodeDevice::OverNetwork(Transport& transport, const std::string& address) {
    return TCodeDevice(transport, Trim(address), "", false);
}

TCodeDevice TCodeDevice::OverSerial(Transport& transport, const std::string& port) {
    return TCodeDevice(transport, Trim(port), Trim(port), true);
}

// UDP has no handshake, so a wrong address only shows when the device does not
// move; TCP and serial fail here.
Status TCodeDevice::Connect() {
    if (serial_) {
        if (serial_port_.empty())
            return Status::Error("tcode: no serial port chosen — pick the device's port (COM…, /dev/tty…)");
        Status s = transport_->Connect();
        if (!s)
            return Status::Error("tcode: cannot open " + serial_port_ +
                                 " — check the cable and that no other program (e.g. MultiFunPlayer) uses the port: " +
                                 s.message);
    } else {
        TCodeAddress target;
        Status parsed = ParseTCodeAddress(address_, &target);
        if (!parsed) return parsed;
        Status s = transport_->Connect();
        if (!s)
            return Status::Error("tcode: no connection to " + target.hostport + " (" + target.network +
                                 "): " + s.message);
    }
    open_ = true;
    connected_ = true;
    return Status::Ok();
}

Status TCodeDevice::Disconnect() {
    (void)Stop();
    if (!open_) return Status::Ok();
    transport_->Disconnect();
    open_ = false;
    connected_ = false;
    return Status::Ok();
}

Status TCodeDevice::WriteLine(const std::string& line) {
    if (!open_) return Status::Error("tcode: not connected");
    Status s = transport_->Send(ToBytes(line + "\n"), WriteMode::kWithoutResponse);
    if (!s) {
        connected_ = false;
        return Status::Error("tcode: send failed: " + s.message);
    }
    return Status::Ok();
}

Status TCodeDevice::Stop() {
    if (!open_) return Status::Ok();
    return WriteLine("DSTOP");
}

Status TCodeDevice::SetPosition(double pos, std::chrono::milliseconds d) {
    return WriteLine(TCodeMove("L0", pos, d));
}

bool TCodeDevice::HasAxis(const std::string& axis) const { return !TCodeAxisCode(axis).empty(); }

Status TCodeDevice::SetAxis(const std::string& axis, double pos, std::chrono::milliseconds d) {
    const std::string code = TCodeAxisCode(axis);
    if (code.empty()) return Status::Error("tcode: unknown axis \"" + axis + "\"");
    return WriteLine(TCodeMove(code, pos, d));
}

Capabilities TCodeDevice::DeviceCapabilities() const {
    Capabilities c;
    c.device = "TCode device";
    c.features.push_back({kChStroke, kKindLinear, 0, "L0", true});
    int i = 0;
    for (const auto& a : kAxes) c.features.push_back({a.name, kKindLinear, ++i, a.code, true});
    return c;
}

ConnectionInfo TCodeDevice::Info() const {
    std::string addr = address_;
    if (serial_) {
        addr = "serial://" + serial_port_;
    } else {
        TCodeAddress t;
        if (ParseTCodeAddress(address_, &t)) addr = t.network + "://" + t.hostport;
    }
    ConnectionInfo info;
    info.connected = connected_;
    info.name = "TCode device (L0 + extra axes)";
    info.address = addr;
    return info;
}

}  // namespace samn::device
