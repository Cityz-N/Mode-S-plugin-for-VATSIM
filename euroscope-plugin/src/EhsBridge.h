// EuroScope Plugin Bridge publisher: makes the pilots' downlinked selections available to
// other EuroScope plugins under the provider id "ehslink" (".esb schema ehslink").
//
// The bridge is optional. Without it this class does nothing and the plugin works as before.
// Every method must be called on the EuroScope main thread.
#pragma once

#include <set>
#include <string>

#include "EhsCore.h"

// The bridge's C ABI stays in EhsBridge.cpp: its client shim defines file-static state, so
// esbridge.h must be included by exactly one translation unit.
struct ESB_Api_v1;
struct ESB_Provider;

namespace ehs
{
class Bridge
{
public:
    Bridge() = default;
    ~Bridge();
    Bridge(const Bridge&) = delete;
    Bridge& operator=(const Bridge&) = delete;

    // Attach and register. Cheap once settled. Returns true when publishing is possible.
    // Messages for the user (provider id conflict) are returned through 'message'.
    bool Tick(std::string& message);

    // Writes this aircraft's current data; fields the pilot has not got (null in the
    // payload) are cleared so a consumer never reads a value that no longer applies.
    void Publish(const Selections& s);

    // Clears every value for this aircraft (data stale, position mismatch, hidden).
    void Clear(const std::string& callsign);

    // Callsigns currently carrying values, so the caller can clear the ones that drop out.
    const std::set<std::string>& Published() const { return m_published; }

    bool IsAttached() const { return Api() != nullptr; }

private:
    const ESB_Api_v1* Api() const;
    const ESB_Api_v1* Attach();
    void Register(const ESB_Api_v1* api, std::string& message);

    // Everything the bridge module issued: invalid the moment it is unloaded, so dropped as one.
    struct Attachment
    {
        const ESB_Api_v1* api = nullptr;
        void* module = nullptr;
        ESB_Provider* provider = nullptr;
        uint32_t selAlt = 0, selHdg = 0, selTrk = 0, selIas = 0, selMach = 0, selVs = 0;
        uint32_t baro = 0, curIas = 0, curMach = 0, verified = 0;
    };
    mutable Attachment m_bridge;
    mutable std::set<std::string> m_published;
    bool m_providerConflict = false;   // another module owns "ehslink": stop retrying
};
}   // namespace ehs
