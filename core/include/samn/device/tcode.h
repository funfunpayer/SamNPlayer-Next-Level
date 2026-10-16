#pragma once

// TCode (Go device/tcode.go, tcode_serial.go): the open text protocol of
// OSR2 / SR6 and other DIY strokers. A command is "<axis><value>[I<ms>]";
// the value is a decimal fraction written as digits ("L05" = 0.5,
// "L09999" = 0.9999). One line per move, "\n" executes it; "DSTOP" stops.
//
// TCodeDevice writes one line per Transport::Send (a UDP datagram, a TCP
// write or a serial write). TCode needs no replies for motion. The transport
// is built by the app from ParseTCodeAddress() (network) or the serial port
// (115200 baud 8N1, kTCodeSerialBaud).
// Left to the I/O layer: opening the socket / port, SerialPorts().

#include <chrono>
#include <string>
#include <vector>

#include "samn/device/capabilities.h"
#include "samn/device/device.h"
#include "samn/device/transport.h"

namespace samn::device {

inline constexpr int kTCodeDigits = 4;  // value precision: 0-9999
inline constexpr const char* kDefaultTCodePort = "8000";  // OSR / SR6 Wi-Fi firmwares
inline constexpr int kTCodeSerialBaud = 115200;           // OSR / SR6 USB firmwares

// pos (0-1) as TCode digits: 0.5 -> "5000", 1.0 caps at "9999"; NaN -> "0000".
std::string TCodeValue(double pos);

// One linear move: axis to pos within d ("L05000I250"); d below 1 ms -> 1 ms.
std::string TCodeMove(const std::string& axis, double pos, std::chrono::milliseconds d);

struct TCodeAddress {
    std::string network;   // "udp" or "tcp"
    std::string hostport;  // "192.168.1.50:8000"
};

// Splits a user address ("udp://192.168.1.50:8000", "tcp://osr.local:8000";
// without scheme UDP, without port 8000). On failure Status carries the
// message shown to the user.
Status ParseTCodeAddress(const std::string& address, TCodeAddress* out);

// SamNPlayer axis name -> TCode channel ("surge" -> "L1"), "" if unknown.
// Standard OSR2 / SR6 assignment; a device ignores channels it does not have.
std::string TCodeAxisCode(const std::string& axis);

class TCodeDevice : public Device,
                    public LinearDevice,
                    public MultiAxisDevice,
                    public CapabilityReporter {
public:
    // Network device: `address` as typed by the user, shown in Info().
    static TCodeDevice OverNetwork(Transport& transport, const std::string& address);
    // USB serial device ("COM3", "/dev/ttyUSB0" ...).
    static TCodeDevice OverSerial(Transport& transport, const std::string& port);

    Status Connect() override;
    Status Disconnect() override;
    // An OSR2 / SR6 has neither: no-ops, so the feel channels do not error.
    Status SetVibration(double) override { return Status::Ok(); }
    Status SetSuction(double) override { return Status::Ok(); }
    Status Stop() override;  // DSTOP; nothing when not connected

    bool HasLinear() const override { return true; }
    Status SetPosition(double pos, std::chrono::milliseconds d) override;
    bool HasAxis(const std::string& axis) const override;
    Status SetAxis(const std::string& axis, double pos, std::chrono::milliseconds d) override;

    // Stroke (L0) plus the standard extra axes; which of them the hardware
    // has cannot be read back over UDP.
    Capabilities DeviceCapabilities() const override;
    ConnectionInfo Info() const;

private:
    TCodeDevice(Transport& transport, std::string address, std::string serial_port, bool serial)
        : transport_(&transport),
          address_(std::move(address)),
          serial_port_(std::move(serial_port)),
          serial_(serial) {}
    Status WriteLine(const std::string& line);

    Transport* transport_;
    std::string address_;
    std::string serial_port_;
    bool serial_ = false;
    bool open_ = false;       // the transport is connected (Go: conn != nil)
    bool connected_ = false;  // cleared by a failed write
};

}  // namespace samn::device
