// EuroScope Plugin Bridge publisher, see EhsBridge.h.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>

#define ESB_CLIENT_SHIM   // exactly one .cpp in this plugin
#include "esbridge.h"

#include "EhsBridge.h"

#include <cmath>
#include <iterator>

namespace ehs
{
namespace
{
// Claimed namespace on the bridge, stable forever. Matches the MQTT topic root.
constexpr const char* kProviderId = "ehslink";

// Not SYNC: every controller running EHS Link receives the same data from the broker, so
// relaying it between controllers would only add a second, staler copy.
const ESB_FieldDecl kFields[] = {
    {"sel_alt", ESB_T_I64, ESB_SCOPE_AIRCRAFT, 0, 0,
     "Selected altitude (MCP/FCU), feet. Unset when the pilot's aircraft does not report it"},
    {"sel_hdg", ESB_T_I64, ESB_SCOPE_AIRCRAFT, 0, 0,
     "Selected heading, or track when sel_trk is true, degrees 1-360"},
    {"sel_trk", ESB_T_BOOL, ESB_SCOPE_AIRCRAFT, 0, 0,
     "True when sel_hdg is a track (heading window in TRK mode)"},
    {"sel_ias", ESB_T_I64, ESB_SCOPE_AIRCRAFT, 0, 0,
     "Selected IAS, knots. Unset when Mach is selected or the speed window is blank"},
    {"sel_mach", ESB_T_F64, ESB_SCOPE_AIRCRAFT, 0, 0,
     "Selected Mach (e.g. 0.78). Unset when IAS is selected or the speed window is blank"},
    {"sel_vs", ESB_T_I64, ESB_SCOPE_AIRCRAFT, 0, 0,
     "Selected vertical speed, ft/min, signed. Unset when not in V/S mode"},
    {"baro", ESB_T_F64, ESB_SCOPE_AIRCRAFT, 0, 0,
     "Altimeter setting, hPa (1013.2 when STD is selected)"},
    {"ias", ESB_T_I64, ESB_SCOPE_AIRCRAFT, 0, 0,
     "Current (actual, not selected) indicated airspeed, knots"},
    {"mach", ESB_T_F64, ESB_SCOPE_AIRCRAFT, 0, 0,
     "Current (actual, not selected) Mach"},
    {"verified", ESB_T_BOOL, ESB_SCOPE_AIRCRAFT, 0, 0,
     "True when the pilot's aircraft profile is verified against the real cockpit; false = indicative"},
};
}   // namespace

Bridge::~Bridge()
{
    // Api() rather than the cached pointer: EuroScope may unload the bridge before us, and
    // reading its table then would fault. The bridge drops our registration with its module.
    const ESB_Api_v1* api = Api();
    if (api && m_bridge.provider) api->unregister_provider(m_bridge.provider);
}

const ESB_Api_v1* Bridge::Api() const
{
    if (!m_bridge.api) return nullptr;
    // Still loaded, and still the same module (an unload + reload is a different bridge whose
    // handles are not ours).
    if (GetModuleHandleA(ESB_MODULE_NAME) == m_bridge.module) return m_bridge.api;
    m_bridge = Attachment{};
    m_published.clear();
    return nullptr;
}

const ESB_Api_v1* Bridge::Attach()
{
    // Not ESB_Attach(): the shim caches its pointer forever, which would dangle after the
    // bridge is unloaded. GetModuleHandle, never LoadLibrary (the bridge must be a plugin
    // EuroScope ticks).
    HMODULE module = GetModuleHandleA(ESB_MODULE_NAME);
    if (!module) return nullptr;
    auto get = reinterpret_cast<ESB_GetApiFn>(GetProcAddress(module, ESB_ENTRY_SYMBOL));
    if (!get) return nullptr;
    const ESB_Api_v1* api = get(ESB_ABI_VERSION);
    if (!api) return nullptr;
    m_bridge.api = api;
    m_bridge.module = module;
    return api;
}

bool Bridge::Tick(std::string& message)
{
    const ESB_Api_v1* api = Api();
    if (!api) api = Attach();
    // No bridge: EHS Link does not need it, so no "bridge missing" notice either.
    if (!api) return false;
    if (!m_bridge.provider && !m_providerConflict) Register(api, message);
    return m_bridge.provider != nullptr;
}

void Bridge::Register(const ESB_Api_v1* api, std::string& message)
{
    ESB_ProviderDecl d = {};
    d.struct_size = sizeof d;
    d.provider_id = kProviderId;
    d.schema_major = 1;
    d.schema_minor = 0;
    d.display_name = "EHS Link";
    d.contact = "https://github.com/cityz-n/Mode-S-plugin-for-VATSIM";
    d.fields = kFields;
    d.field_count = static_cast<uint32_t>(std::size(kFields));
    d.module = ESB_SelfModule();   // never GetModuleHandleA("EhsLink.dll"): users rename DLLs

    ESB_Status st = api->register_provider(&d, &m_bridge.provider);
    if (st != ESB_OK)
    {
        m_bridge.provider = nullptr;
        if (st == ESB_E_PROVIDER_TAKEN)
        {
            // A conflict to settle with the other author, not to retry around.
            m_providerConflict = true;
            message = "Another loaded plugin already owns the \"ehslink\" bridge provider id; "
                      "EHS data is not shared with other plugins.";
        }
        return;
    }

    struct { const char* name; uint32_t* id; } own[] = {
        {"sel_alt", &m_bridge.selAlt}, {"sel_hdg", &m_bridge.selHdg}, {"sel_trk", &m_bridge.selTrk},
        {"sel_ias", &m_bridge.selIas}, {"sel_mach", &m_bridge.selMach}, {"sel_vs", &m_bridge.selVs},
        {"baro", &m_bridge.baro}, {"ias", &m_bridge.curIas}, {"mach", &m_bridge.curMach},
        {"verified", &m_bridge.verified},
    };
    for (auto& o : own)
        if (api->own_field(m_bridge.provider, o.name, o.id) != ESB_OK) *o.id = ESB_FIELD_NONE;
}

void Bridge::Publish(const Selections& s)
{
    const ESB_Api_v1* api = Api();
    if (!api || !m_bridge.provider || s.callsign.empty()) return;

    // Resolved from the callsign every time: a cached handle goes stale on reconnect.
    ESB_Aircraft ac = ESB_AIRCRAFT_NONE;
    if (api->aircraft(s.callsign.c_str(), &ac) != ESB_OK) return;   // bridge has not seen it yet

    ESB_Provider* p = m_bridge.provider;
    // No dirty-tracking: the bridge ignores writes of an unchanged value.
    auto setInt = [&](uint32_t f, const std::optional<int>& v) {
        if (f == ESB_FIELD_NONE) return;
        if (v) { ESB_Value x = ESB_I64(*v); api->set_ac(p, ac, f, &x); }
        else api->clear_ac(p, ac, f);
    };
    auto setReal = [&](uint32_t f, const std::optional<double>& v) {
        if (f == ESB_FIELD_NONE) return;
        if (v && std::isfinite(*v)) { ESB_Value x = ESB_F64(*v); api->set_ac(p, ac, f, &x); }
        else api->clear_ac(p, ac, f);
    };
    auto setBool = [&](uint32_t f, bool v) {
        if (f == ESB_FIELD_NONE) return;
        ESB_Value x = ESB_Bool(v ? 1 : 0);
        api->set_ac(p, ac, f, &x);
    };

    setInt(m_bridge.selAlt, s.alt);
    setInt(m_bridge.selHdg, s.hdg);
    if (s.hdg) setBool(m_bridge.selTrk, s.trk);
    else if (m_bridge.selTrk != ESB_FIELD_NONE) api->clear_ac(p, ac, m_bridge.selTrk);
    setInt(m_bridge.selIas, s.ias);
    setReal(m_bridge.selMach, s.mach);
    setInt(m_bridge.selVs, s.vs);
    setReal(m_bridge.baro, s.baro);
    setInt(m_bridge.curIas, s.curIas);
    setReal(m_bridge.curMach, s.curMach);
    setBool(m_bridge.verified, s.verified);
    m_published.insert(s.callsign);
}

void Bridge::Clear(const std::string& callsign)
{
    m_published.erase(callsign);
    const ESB_Api_v1* api = Api();
    if (!api || !m_bridge.provider) return;
    ESB_Aircraft ac = ESB_AIRCRAFT_NONE;
    if (api->aircraft(callsign.c_str(), &ac) != ESB_OK) return;   // gone: the bridge dropped it
    for (uint32_t f : {m_bridge.selAlt, m_bridge.selHdg, m_bridge.selTrk, m_bridge.selIas, m_bridge.selMach,
                       m_bridge.selVs, m_bridge.baro, m_bridge.curIas, m_bridge.curMach, m_bridge.verified})
        if (f != ESB_FIELD_NONE) api->clear_ac(m_bridge.provider, ac, f);
}
}   // namespace ehs
