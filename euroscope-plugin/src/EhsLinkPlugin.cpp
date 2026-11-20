// EHS Link plugin for EuroScope: shows pilots' downlinked autopilot selections (from the
// EHS Link vPilot plugin) as TAG items and alerts when they don't match what you assigned.
//
// TAG items:   EHS selected altitude / heading / speed / V/S, EHS summary, EHS status.
//              Each item is coloured live: match colour (default neutral green) when the pilot's
//              selection agrees with what you assigned, mismatch colour (default soft red) when not.
// AC list:     "EHS Link" — every equipped aircraft, assigned value next to the selected one.
//              The EHS items can also be added as columns to any standard ES list.
// Commands:    .ehs                     status
//              .ehs list                 show the EHS Link list
//              .ehs code <prefix>        flight plan marker CODE/<prefix>... (default FE5)
//              .ehs baro pilot|hpa|inhg  altimeter display unit (default: the pilot's own unit)
//              .ehs color match|mismatch R,G,B   (or 'default')
//              .ehs notify mine|all|off  who triggers chat alerts (default: mine = tracked by me)
//              .ehs sound on|off|<file.wav>      (default off)
//              .ehs grace <seconds>      how long a mismatch must last before alerting (default 10)
//              .ehs tol alt|hdg|spd|vs <value>
//              .ehs brokers host[:port],host[:port]
// Bridge:      with the EuroScope Plugin Bridge loaded, the data is also published for other
//              plugins under the provider id "ehslink" (.esb schema ehslink).

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <mmsystem.h>

#include "EuroScopePlugIn.h"
#include "EhsAlerts.h"
#include "EhsBridge.h"
#include "EhsCore.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>

using namespace EuroScopePlugIn;

namespace
{
const char* kName = "EHS Link";
const char* kVersion = "0.2.2";

enum TagItems { kTagAlt = 1, kTagHdg, kTagSpd, kTagVs, kTagSummary, kTagStatus, kTagBaro, kTagCurIas, kTagCurMach };

constexpr uint64_t kStaleMs = 30000;          // no message for 30 s = no data
constexpr int64_t kMaxRetainedAgeSec = 120;   // ignore retained messages older than this (sender clock)
constexpr double kMaxPositionErrorNm = 10.0;  // reject data whose position doesn't match the radar target

const COLORREF kDefaultMatchColor = RGB(110, 190, 120);     // neutral green
const COLORREF kDefaultMismatchColor = RGB(230, 110, 110);  // soft red
const COLORREF kDefaultNearColor = RGB(100, 160, 235);      // soft blue: close but not exact
const COLORREF kDimColor = RGB(140, 140, 140);

// "R,G,B" (also accepts spaces or ';') -> COLORREF; fallback if malformed.
COLORREF ParseColor(const std::string& text, COLORREF fallback)
{
    int r, g, b;
    std::string t = text;
    for (auto& c : t) if (c == ',' || c == ';') c = ' ';
    if (sscanf_s(t.c_str(), "%d %d %d", &r, &g, &b) != 3 || r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255)
        return fallback;
    return RGB(r, g, b);
}

std::string FormatColor(COLORREF c)
{
    return std::to_string(GetRValue(c)) + "," + std::to_string(GetGValue(c)) + "," + std::to_string(GetBValue(c));
}

const std::vector<ehs::Broker> kDefaultBrokers = {{"broker.hivemq.com", 1883}, {"broker.emqx.io", 1883}};
}   // namespace

class CEhsLinkPlugin : public CPlugIn
{
public:
    CEhsLinkPlugin()
        : CPlugIn(COMPATIBILITY_CODE, kName, kVersion, "EHS Link contributors", "MIT License")
    {
        RegisterTagItemType("EHS selected altitude", kTagAlt);
        RegisterTagItemType("EHS selected heading", kTagHdg);
        RegisterTagItemType("EHS selected speed", kTagSpd);
        RegisterTagItemType("EHS selected V/S", kTagVs);
        RegisterTagItemType("EHS summary (ALT HDG SPD)", kTagSummary);
        RegisterTagItemType("EHS status", kTagStatus);
        RegisterTagItemType("EHS altimeter setting", kTagBaro);
        RegisterTagItemType("EHS current IAS", kTagCurIas);
        RegisterTagItemType("EHS current Mach", kTagCurMach);
        CreateList();

        LoadSettings();
        StartSubscriber();
        Show("EHS Link " + std::string(kVersion) + " loaded. Type .ehs for status.");
    }

    ~CEhsLinkPlugin() override { m_mqtt.Stop(); }

    // ------------------------------------------------------------------ TAG items

    // EuroScope callbacks: an exception escaping into EuroScope would crash it, so each one is
    // wrapped and a failure only skips that update.
    void OnGetTagItem(CFlightPlan FlightPlan, CRadarTarget RadarTarget, int ItemCode, int TagData,
                      char sItemString[16], int* pColorCode, COLORREF* pRGB, double* pFontSize) override
    {
        try { GetTagItem(FlightPlan, RadarTarget, ItemCode, sItemString, pColorCode, pRGB); }
        catch (...) {}
    }

    void OnTimer(int /*Counter*/) override
    {
        try { Tick(); }
        catch (...) {}
    }

    void GetTagItem(CFlightPlan& FlightPlan, CRadarTarget& RadarTarget, int ItemCode,
                    char sItemString[16], int* pColorCode, COLORREF* pRGB)
    {
        const char* cs = RadarTarget.IsValid() ? RadarTarget.GetCallsign()
                       : FlightPlan.IsValid() ? FlightPlan.GetCallsign() : nullptr;
        if (!cs || !*cs) return;
        auto data = Data(cs);
        if (!data)
        {
            // Filed as EHS Link equipped (CODE/ in the flight plan) but no data arriving:
            // the pilot's plugin isn't running or connected.
            if (ItemCode == kTagStatus && FlightPlan.IsValid() && IsFiled(FlightPlan))
            {
                strcpy_s(sItemString, 16, "ehs");
                *pColorCode = TAG_COLOR_RGB_DEFINED;
                *pRGB = kDimColor;
            }
            return;
        }

        auto st = m_alerts.find(cs);
        const ehs::AircraftAlerts* al = st != m_alerts.end() ? &st->second : nullptr;
        bool posBad = m_positionBad.count(cs) > 0;

        std::string text;
        int field = -1;
        switch (ItemCode)
        {
            case kTagAlt: text = ehs::TagAlt(*data); field = ehs::kAlt; break;
            case kTagHdg: text = ehs::TagHdg(*data); field = ehs::kHdg; break;
            case kTagSpd: text = ehs::TagSpd(*data); field = ehs::kSpd; break;
            case kTagVs: text = ehs::TagVs(*data); field = ehs::kVs; break;
            case kTagSummary: text = ehs::TagSummary(*data); break;
            case kTagBaro: text = ehs::TagBaro(*data, m_baroDisplay); field = ehs::kBaro; break;
            case kTagCurIas: text = ehs::TagCurIas(*data); field = -2; break;     // display only, no colour
            case kTagCurMach: text = ehs::TagCurMach(*data); field = -2; break;
            case kTagStatus:
                text = posBad ? "EHS?" : (al && al->AnyAlerting() ? "EHS!" : "EHS");
                if (!posBad && !data->verified) text += "~";   // data from an unverified (draft) profile
                break;
            default: return;
        }
        if (posBad) text = (ItemCode == kTagStatus) ? text : "?";
        strncpy_s(sItemString, 16, text.c_str(), _TRUNCATE);

        if (posBad) { *pColorCode = TAG_COLOR_RGB_DEFINED; *pRGB = kDimColor; return; }
        if (!al) return;
        // Colour shows the live comparison with what you assigned: green exact, blue close
        // (within tolerance), red off. Nothing assigned = default colour. Summary/status items
        // take the worst of all fields.
        if (field == -2) return;   // current speed: plain information, never coloured
        ehs::Match m = ehs::Match::None;
        if (field >= 0) m = al->f[field].match;
        else for (auto& f : al->f) m = Worse(m, f.match);
        if (m == ehs::Match::None) return;
        *pColorCode = TAG_COLOR_RGB_DEFINED;
        COLORREF c = m == ehs::Match::Exact ? m_matchColor : m == ehs::Match::Near ? m_nearColor : m_mismatchColor;
        *pRGB = data->verified ? c : Dimmed(c);
    }

    static ehs::Match Worse(ehs::Match a, ehs::Match b)
    {
        return static_cast<int>(a) > static_cast<int>(b) ? a : b;   // None < Exact < Near < Off
    }

    void OnNewMetarReceived(const char* sStation, const char* sFullMetar) override
    {
        if (!sStation || !sFullMetar) return;
        try
        {
            auto q = ehs::ParseMetarQnh(sFullMetar);
            if (q) m_metarQnh[sStation] = *q;
        }
        catch (...) {}
    }

    // ------------------------------------------------------------------ once a second

    void Tick()
    {
        FlushStatusMessages();
        m_store.PurgeOlderThan(kStaleMs * 4);

        uint64_t now = GetTickCount64();
        std::map<std::string, ehs::AircraftAlerts> next;
        m_positionBad.clear();

        // Only listen for aircraft EuroScope can see: broker traffic then grows with the number
        // of pilots, not pilots x controllers.
        std::set<std::string> topics;
        for (CRadarTarget rt = RadarTargetSelectFirst(); rt.IsValid(); rt = RadarTargetSelectNext(rt))
            if (rt.GetCallsign() && *rt.GetCallsign()) topics.insert(std::string(ehs::kTopicRoot) + rt.GetCallsign());
        if (topics != m_topics) { m_topics = topics; m_mqtt.SetTopics(topics); }

        for (CRadarTarget rt = RadarTargetSelectFirst(); rt.IsValid(); rt = RadarTargetSelectNext(rt))
        {
            const char* rcs = rt.GetCallsign();
            if (!rcs || !*rcs) continue;
            std::string cs = rcs;
            auto data = Data(cs);
            if (!data) continue;

            if (data->hasPos)
            {
                CPosition p;
                p.m_Latitude = data->lat;
                p.m_Longitude = data->lon;
                if (rt.GetPosition().GetPosition().DistanceTo(p) > kMaxPositionErrorNm)
                {
                    m_positionBad.insert(cs);
                    continue;
                }
            }

            CFlightPlan fp = rt.GetCorrelatedFlightPlan();
            if (!fp.IsValid()) continue;
            CFlightPlanControllerAssignedData cad = fp.GetControllerAssignedData();
            ehs::Assigned a;
            a.cfl = cad.GetClearedAltitude();
            a.hdg = cad.GetAssignedHeading();
            a.spd = cad.GetAssignedSpeed();
            a.mach = cad.GetAssignedMach();
            a.rate = cad.GetAssignedRate();
            a.baroRef = ehs::ExpectedBaro(rt.GetPosition().GetPressureAltitude(), GetTransitionAltitude(), a.cfl,
                                          ReferenceQnh(fp, rt.GetPosition().GetPosition()));

            ehs::AircraftAlerts& st = next[cs];
            auto prev = m_alerts.find(cs);
            if (prev != m_alerts.end()) st = prev->second;

            unsigned started = ehs::Evaluate(*data, a, m_tol, st, now);
            if (started && ShouldNotify(fp))
            {
                std::string msg;
                for (int i = 0; i < ehs::kFieldCount; ++i)
                    if (started & (1u << i)) msg += (msg.empty() ? "" : "; ") + ehs::Describe(static_cast<ehs::Field>(i), *data, a);
                if (!data->verified) msg += " (unverified profile)";
                DisplayUserMessage(kName, cs.c_str(), msg.c_str(), true, true, true, true, false);
                PlayAlertSound();
            }
        }
        m_alerts.swap(next);
        UpdateList();
        PublishToBridge();
    }

    // Shares what the EHS tags show with other plugins through the EuroScope Plugin Bridge:
    // fresh, position-checked data only, cleared as soon as it no longer is. The bridge is
    // optional: without it this returns at once.
    void PublishToBridge()
    {
        std::string message;
        bool ready = m_bridge.Tick(message);
        if (!message.empty()) Show(message);
        if (!ready) return;

        std::set<std::string> live;
        for (CRadarTarget rt = RadarTargetSelectFirst(); rt.IsValid(); rt = RadarTargetSelectNext(rt))
        {
            const char* rcs = rt.GetCallsign();
            if (!rcs || !*rcs) continue;
            std::string cs = rcs;
            if (m_positionBad.count(cs)) continue;
            auto data = Data(cs);
            if (!data) continue;
            m_bridge.Publish(*data);
            live.insert(cs);
        }
        std::vector<std::string> gone;
        for (auto& cs : m_bridge.Published())
            if (!live.count(cs)) gone.push_back(cs);
        for (auto& cs : gone) m_bridge.Clear(cs);
    }

    // ------------------------------------------------------------------ commands

    bool OnCompileCommand(const char* sCommandLine) override
    {
        if (!sCommandLine) return false;
        try { return CompileCommand(sCommandLine); }
        catch (...) { return false; }
    }

    bool CompileCommand(const char* sCommandLine)
    {
        std::istringstream in(sCommandLine);
        std::string cmd, sub, arg;
        in >> cmd;
        if (_stricmp(cmd.c_str(), ".ehs") != 0) return false;
        in >> sub;
        std::getline(in, arg);
        while (!arg.empty() && arg.front() == ' ') arg.erase(arg.begin());
        for (auto& c : sub) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

        if (sub.empty())
        {
            char buf[600];
            _snprintf_s(buf, _TRUNCATE, "%s to %s%s, listening for %u aircraft in range; %u sending data, %u filed CODE/%s...; colours match %s, near %s, mismatch %s; notify %s; sound %s; grace %d s; "
                           "near if within: alt %d ft, hdg %d, spd %d kt, M%.2f, V/S %d fpm, QNH %d hPa / %.2f inHg.",
                      m_mqtt.IsConnected() ? "Connected" : "Not connected",
                      m_mqtt.IsConnected() ? m_mqtt.CurrentBroker().c_str() : ehs::FormatBrokerList(m_brokers).c_str(),
                      m_bridge.IsAttached() ? " (shared on the plugin bridge)" : "",
                      static_cast<unsigned>(m_mqtt.SubscribedCount()),
                      static_cast<unsigned>(m_store.Count(kStaleMs)),
                      static_cast<unsigned>(CountFiled()), m_codePrefix.c_str(),
                      FormatColor(m_matchColor).c_str(), FormatColor(m_nearColor).c_str(), FormatColor(m_mismatchColor).c_str(), m_notify.c_str(),
                      m_sound.c_str(), m_tol.graceSec, m_tol.alt, m_tol.hdg, m_tol.spd, m_tol.mach, m_tol.vs, m_tol.qnhHpa, m_tol.qnhInHg);
            Show(buf);
            return true;
        }
        if (sub == "notify" && (arg == "mine" || arg == "all" || arg == "off"))
        {
            m_notify = arg;
            Save("notify", "EHS alerts for", m_notify);
        }
        else if (sub == "sound" && !arg.empty())
        {
            m_sound = arg;
            Save("sound", "EHS alert sound", m_sound);
            PlayAlertSound();
        }
        else if (sub == "code")
        {
            if (!ehs::IsValidCodePrefix(arg)) { Show("Usage: .ehs code <prefix, 1-6 hex digits>   (default FE5)"); return true; }
            m_codePrefix = arg;
            for (auto& c : m_codePrefix) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
            Save("code_prefix", "EHS flight plan code prefix", m_codePrefix);
        }
        else if (sub == "unverified" && (arg == "on" || arg == "off"))
        {
            m_showUnverified = arg == "on";
            Save("unverified", "EHS show unverified profiles", arg);
        }
        else if (sub == "baro" && (arg == "pilot" || arg == "hpa" || arg == "inhg"))
        {
            m_baroDisplay = arg == "hpa" ? ehs::BaroDisplay::Hpa : arg == "inhg" ? ehs::BaroDisplay::InHg : ehs::BaroDisplay::Pilot;
            Save("baro_unit", "EHS altimeter unit", arg);
        }
        else if (sub == "list")
        {
            if (m_list.IsValid()) m_list.ShowFpList(true);
            return true;
        }
        else if (sub == "color" || sub == "colour")
        {
            std::istringstream a(arg);
            std::string which, rest;
            a >> which;
            std::getline(a, rest);
            COLORREF def = which == "match" ? kDefaultMatchColor : which == "near" ? kDefaultNearColor : kDefaultMismatchColor;
            COLORREF c = (rest.find("default") != std::string::npos) ? def : ParseColor(rest, CLR_INVALID);
            if ((which != "match" && which != "near" && which != "mismatch") || c == CLR_INVALID)
            {
                Show("Usage: .ehs color match|near|mismatch R,G,B   (or 'default')");
                return true;
            }
            (which == "match" ? m_matchColor : which == "near" ? m_nearColor : m_mismatchColor) = c;
            Save("color_" + which, "EHS " + which + " colour", FormatColor(c));
        }
        else if (sub == "grace" && atoi(arg.c_str()) >= 0)
        {
            m_tol.graceSec = atoi(arg.c_str());
            Save("grace", "EHS grace seconds", std::to_string(m_tol.graceSec));
        }
        else if (sub == "tol")
        {
            std::istringstream a(arg);
            std::string what, val;
            a >> what >> val;
            double v = val.empty() ? -1 : atof(val.c_str());
            const char* usage = "Usage: .ehs tol alt|hdg|spd|mach|vs|qnh|inhg <value>   (e.g. .ehs tol mach 0.05)";
            if (v < 0) { Show(usage); return true; }
            if (what == "alt") m_tol.alt = static_cast<int>(v);
            else if (what == "hdg") m_tol.hdg = static_cast<int>(v);
            else if (what == "spd") m_tol.spd = static_cast<int>(v);
            else if (what == "mach") m_tol.mach = v;
            else if (what == "vs") m_tol.vs = static_cast<int>(v);
            else if (what == "qnh") m_tol.qnhHpa = static_cast<int>(v);
            else if (what == "inhg") m_tol.qnhInHg = v;
            else { Show(usage); return true; }
            Save("tol_" + what, "", val);
        }
        else if (sub == "brokers")
        {
            auto list = ehs::ParseBrokerList(arg);
            if (list.empty()) { Show("Usage: .ehs brokers host[:port],host[:port]"); return true; }
            m_brokers = list;
            Save("brokers", "EHS brokers", ehs::FormatBrokerList(m_brokers));
            StartSubscriber();
        }
        else
        {
            Show("Commands: .ehs | list | code <prefix> | baro pilot|hpa|inhg | unverified on|off | color match|near|mismatch R,G,B | notify mine|all|off | sound on|off|<file.wav> | grace <s> | tol alt|hdg|spd|vs <n> | brokers <list>");
            return true;
        }
        Show("OK.");
        return true;
    }

private:
    // "EHS Link" AC list: every equipped aircraft in range, assigned value next to the pilot's
    // selection. Column layout is only a default: users can change it and ES saves it.
    void CreateList()
    {
        m_list = RegisterFpList("EHS Link");
        if (m_list.GetColumnNumber() > 0) return;   // layout already loaded from settings
        const char* es = "";
        m_list.AddColumnDefinition("C/S", 8, false, es, TAG_ITEM_TYPE_CALLSIGN, nullptr, TAG_ITEM_FUNCTION_OPEN_FP_DIALOG, nullptr, TAG_ITEM_FUNCTION_NO);
        m_list.AddColumnDefinition("TYPE", 5, false, es, TAG_ITEM_TYPE_PLANE_TYPE, nullptr, TAG_ITEM_FUNCTION_NO, nullptr, TAG_ITEM_FUNCTION_NO);
        m_list.AddColumnDefinition("CFL", 4, true, es, TAG_ITEM_TYPE_TEMP_ALTITUDE, nullptr, TAG_ITEM_FUNCTION_TEMP_ALTITUDE_POPUP, nullptr, TAG_ITEM_FUNCTION_NO);
        m_list.AddColumnDefinition("SEL", 4, true, kName, kTagAlt, nullptr, TAG_ITEM_FUNCTION_NO, nullptr, TAG_ITEM_FUNCTION_NO);
        m_list.AddColumnDefinition("AHDG", 4, true, es, TAG_ITEM_TYPE_ASSIGNED_HEADING, nullptr, TAG_ITEM_FUNCTION_ASSIGNED_HEADING_POPUP, nullptr, TAG_ITEM_FUNCTION_NO);
        m_list.AddColumnDefinition("SHDG", 4, true, kName, kTagHdg, nullptr, TAG_ITEM_FUNCTION_NO, nullptr, TAG_ITEM_FUNCTION_NO);
        m_list.AddColumnDefinition("ASPD", 4, true, es, TAG_ITEM_TYPE_ASSIGNED_SPEED, nullptr, TAG_ITEM_FUNCTION_ASSIGNED_SPEED_POPUP, nullptr, TAG_ITEM_FUNCTION_ASSIGNED_MACH_POPUP);
        m_list.AddColumnDefinition("SSPD", 4, true, kName, kTagSpd, nullptr, TAG_ITEM_FUNCTION_NO, nullptr, TAG_ITEM_FUNCTION_NO);
        m_list.AddColumnDefinition("IAS", 4, true, kName, kTagCurIas, nullptr, TAG_ITEM_FUNCTION_NO, nullptr, TAG_ITEM_FUNCTION_NO);
        m_list.AddColumnDefinition("MACH", 4, true, kName, kTagCurMach, nullptr, TAG_ITEM_FUNCTION_NO, nullptr, TAG_ITEM_FUNCTION_NO);
        m_list.AddColumnDefinition("ARTE", 4, true, es, TAG_ITEM_TYPE_ASSIGNED_RATE, nullptr, TAG_ITEM_FUNCTION_ASSIGNED_RATE_POPUP, nullptr, TAG_ITEM_FUNCTION_NO);
        m_list.AddColumnDefinition("SV/S", 4, true, kName, kTagVs, nullptr, TAG_ITEM_FUNCTION_NO, nullptr, TAG_ITEM_FUNCTION_NO);
        m_list.AddColumnDefinition("QNH", 5, true, kName, kTagBaro, nullptr, TAG_ITEM_FUNCTION_NO, nullptr, TAG_ITEM_FUNCTION_NO);
        m_list.AddColumnDefinition("EHS", 4, true, kName, kTagStatus, nullptr, TAG_ITEM_FUNCTION_NO, nullptr, TAG_ITEM_FUNCTION_NO);
    }

    void UpdateList()   // main thread, once a second
    {
        if (!m_list.IsValid()) return;
        // Current IAS/Mach change almost every update; redrawing rows for them alone made the
        // list churn (remove + add) every second per aircraft. They now refresh at most every
        // kCurSpeedRefreshMs; selections and colours still redraw immediately.
        constexpr uint64_t kCurSpeedRefreshMs = 10000;
        uint64_t tick = GetTickCount64();
        bool curSpeedDue = tick - m_lastCurSpeedRefresh >= kCurSpeedRefreshMs;
        if (curSpeedDue) m_lastCurSpeedRefresh = tick;

        std::map<std::string, std::string> now;   // callsign -> what the row currently shows
        for (CRadarTarget rt = RadarTargetSelectFirst(); rt.IsValid(); rt = RadarTargetSelectNext(rt))
        {
            const char* rcs = rt.GetCallsign();
            if (!rcs || !*rcs) continue;
            std::string cs = rcs;
            CFlightPlan fp = rt.GetCorrelatedFlightPlan();
            if (!fp.IsValid()) continue;
            auto data = Data(cs);
            if (!data && !IsFiled(fp)) continue;

            std::string key = RowKey(cs, data ? &*data : nullptr);
            std::string curKey = data ? ehs::TagCurIas(*data) + "|" + ehs::TagCurMach(*data) : "";
            auto old = m_inList.find(cs);
            if (old == m_inList.end())
            {
                m_list.AddFpToTheList(fp);
                m_curShown[cs] = curKey;
            }
            else if (old->second != key || (curSpeedDue && m_curShown[cs] != curKey))
            {
                // ES only redraws a list row when the flight plan changes, so EHS updates
                // (which aren't FP changes) need the row re-added to show.
                m_list.RemoveFpFromTheList(fp);
                m_list.AddFpToTheList(fp);
                m_curShown[cs] = curKey;
            }
            now[cs] = key;
        }
        for (auto it = m_curShown.begin(); it != m_curShown.end();)
            it = now.count(it->first) ? std::next(it) : m_curShown.erase(it);
        for (auto& kv : m_inList)
        {
            if (now.count(kv.first)) continue;
            CFlightPlan fp = FlightPlanSelect(kv.first.c_str());
            if (fp.IsValid()) m_list.RemoveFpFromTheList(fp);
        }
        m_inList.swap(now);
    }

    // Everything an EHS list row shows: values plus colour state.
    std::string RowKey(const std::string& cs, const ehs::Selections* d)
    {
        if (!d) return "filed";
        std::string k = ehs::TagAlt(*d) + "|" + ehs::TagHdg(*d) + "|" + ehs::TagSpd(*d) + "|" + ehs::TagVs(*d) +
                        "|" + ehs::TagBaro(*d, m_baroDisplay);   // current IAS/Mach: see UpdateList
        if (!d->verified) k += "|~";
        if (m_positionBad.count(cs)) return k + "|pos";
        auto it = m_alerts.find(cs);
        if (it != m_alerts.end())
            for (auto& f : it->second.f) k += static_cast<char>('0' + static_cast<int>(f.match));
        return k;
    }

    // QNH of the nearer of origin/destination that has a METAR in EuroScope.
    std::optional<double> ReferenceQnh(CFlightPlan& fp, CPosition acPos)
    {
        LoadAirports();
        CFlightPlanData d = fp.GetFlightPlanData();
        std::optional<double> best;
        double bestDist = 1e9;
        for (const char* icao : {d.GetOrigin(), d.GetDestination()})
        {
            if (!icao || !*icao) continue;
            auto q = m_metarQnh.find(icao);
            if (q == m_metarQnh.end()) continue;
            auto p = m_airports.find(icao);
            double dist = p != m_airports.end() ? acPos.DistanceTo(p->second) : 5e8;   // unknown position: last resort
            if (dist < bestDist) { bestDist = dist; best = q->second; }
        }
        return best;
    }

    void LoadAirports()   // sector file airports, once (retried while empty)
    {
        if (!m_airports.empty()) return;
        uint64_t now = GetTickCount64();
        if (now - m_lastAirportLoad < 30000 && m_lastAirportLoad != 0) return;
        m_lastAirportLoad = now;
        for (CSectorElement e = SectorFileElementSelectFirst(SECTOR_ELEMENT_AIRPORT); e.IsValid();
             e = SectorFileElementSelectNext(e, SECTOR_ELEMENT_AIRPORT))
        {
            CPosition p;
            if (e.GetName() && e.GetPosition(&p, 0)) m_airports[e.GetName()] = p;
        }
    }

    bool IsFiled(CFlightPlan& fp)
    {
        const char* rmk = fp.GetFlightPlanData().GetRemarks();
        return rmk && (ehs::HasEhsCode(rmk, m_codePrefix) || ehs::HasEhsRemark(rmk));
    }

    size_t CountFiled()
    {
        size_t n = 0;
        for (CRadarTarget rt = RadarTargetSelectFirst(); rt.IsValid(); rt = RadarTargetSelectNext(rt))
        {
            CFlightPlan fp = rt.GetCorrelatedFlightPlan();
            if (fp.IsValid() && IsFiled(fp)) ++n;
        }
        return n;
    }

    void StartSubscriber()
    {
        m_mqtt.Start(m_brokers,
            [this](const std::string& topic, const std::string& payload) { OnMessage(topic, payload); },
            [this](const std::string& text) { QueueStatus(text); });
    }

    // Network thread.
    void OnMessage(const std::string& topic, const std::string& payload)
    {
        const size_t rootLen = strlen(ehs::kTopicRoot);
        if (topic.size() <= rootLen || topic.compare(0, rootLen, ehs::kTopicRoot) != 0) return;
        std::string cs = topic.substr(rootLen);
        if (payload.empty()) { m_store.Remove(cs); return; }   // pilot disconnected
        ehs::Selections s;
        if (!ehs::ParsePayload(payload, s) || s.callsign != cs) return;
        if (s.sentUnix > 0 && static_cast<int64_t>(time(nullptr)) - s.sentUnix > kMaxRetainedAgeSec) return;
        s.receivedTick = GetTickCount64();
        m_store.Put(s);
    }

    bool ShouldNotify(CFlightPlan& fp)
    {
        if (m_notify == "off") return false;
        if (m_notify == "all") return true;
        return fp.GetTrackingControllerIsMe();
    }

    void PlayAlertSound()
    {
        if (m_sound == "off") return;
        if (m_sound == "on" || !PlaySoundA(m_sound.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT))
            MessageBeep(MB_ICONEXCLAMATION);
    }

    void QueueStatus(const std::string& text)   // any thread
    {
        std::lock_guard<std::mutex> g(m_statusLock);
        if (m_statusQueue.size() < 20) m_statusQueue.push_back(text);
    }

    void FlushStatusMessages()   // main thread
    {
        std::deque<std::string> q;
        {
            std::lock_guard<std::mutex> g(m_statusLock);
            q.swap(m_statusQueue);
        }
        for (auto& t : q)
        {
            if (t == m_lastStatus) continue;   // don't spam repeated reconnect messages
            m_lastStatus = t;
            Show(t);
        }
    }

    void Show(const std::string& text)
    {
        DisplayUserMessage(kName, "", text.c_str(), true, false, false, false, false);
    }

    void Save(const std::string& key, const std::string& description, const std::string& value)
    {
        SaveDataToSettings(key.c_str(), description.c_str(), value.c_str());
    }

    std::string Load(const char* key, const std::string& fallback)
    {
        const char* v = GetDataFromSettings(key);
        return (v && *v) ? std::string(v) : fallback;
    }

    void LoadSettings()
    {
        m_notify = Load("notify", "mine");
        m_sound = Load("sound", "off");   // visual alerts first; sound is opt-in
        m_showUnverified = Load("unverified", "on") != "off";
        std::string bu = Load("baro_unit", "pilot");
        m_baroDisplay = bu == "hpa" ? ehs::BaroDisplay::Hpa : bu == "inhg" ? ehs::BaroDisplay::InHg : ehs::BaroDisplay::Pilot;
        m_codePrefix = Load("code_prefix", ehs::kDefaultCodePrefix);
        if (!ehs::IsValidCodePrefix(m_codePrefix)) m_codePrefix = ehs::kDefaultCodePrefix;
        m_matchColor = ParseColor(Load("color_match", ""), kDefaultMatchColor);
        m_mismatchColor = ParseColor(Load("color_mismatch", ""), kDefaultMismatchColor);
        m_nearColor = ParseColor(Load("color_near", ""), kDefaultNearColor);
        m_tol.graceSec = atoi(Load("grace", "10").c_str());
        m_tol.alt = atoi(Load("tol_alt", "300").c_str());
        m_tol.hdg = atoi(Load("tol_hdg", "5").c_str());
        m_tol.spd = atoi(Load("tol_spd", "5").c_str());
        m_tol.mach = atof(Load("tol_mach", "0.05").c_str());
        m_tol.vs = atoi(Load("tol_vs", "200").c_str());
        m_tol.qnhHpa = atoi(Load("tol_qnh", "1").c_str());
        m_tol.qnhInHg = atof(Load("tol_inhg", "0.01").c_str());
        auto list = ehs::ParseBrokerList(Load("brokers", ""));
        m_brokers = list.empty() ? kDefaultBrokers : list;
    }

    ehs::MqttSubscriber m_mqtt;
    ehs::Bridge m_bridge;   // unregisters in its destructor (only if the bridge is still loaded)
    ehs::Store m_store;
    std::vector<ehs::Broker> m_brokers;
    ehs::Tolerances m_tol;
    std::string m_notify, m_sound, m_codePrefix;
    bool m_showUnverified = true;

    // Latest data for a callsign, honouring ".ehs unverified off".
    std::optional<ehs::Selections> Data(const std::string& cs) const
    {
        auto d = m_store.Get(cs, kStaleMs);
        if (d && !d->verified && !m_showUnverified) return std::nullopt;
        return d;
    }

    // Unverified data: same hue, halfway to grey, so it reads as "indicative".
    static COLORREF Dimmed(COLORREF c)
    {
        return RGB((GetRValue(c) + 140) / 2, (GetGValue(c) + 140) / 2, (GetBValue(c) + 140) / 2);
    }
    ehs::BaroDisplay m_baroDisplay = ehs::BaroDisplay::Pilot;
    COLORREF m_matchColor = kDefaultMatchColor, m_mismatchColor = kDefaultMismatchColor, m_nearColor = kDefaultNearColor;
    std::map<std::string, double> m_metarQnh;      // station -> QNH hPa (main thread)
    std::map<std::string, CPosition> m_airports;   // sector file airports (main thread)
    uint64_t m_lastAirportLoad = 0;

    std::map<std::string, ehs::AircraftAlerts> m_alerts;   // main thread only
    std::set<std::string> m_positionBad;                   // main thread only
    CFlightPlanList m_list;
    std::map<std::string, std::string> m_inList;           // main thread only: callsign -> row key
    std::map<std::string, std::string> m_curShown;         // main thread only: current IAS/Mach last drawn
    uint64_t m_lastCurSpeedRefresh = 0;
    std::set<std::string> m_topics;                        // main thread only: topics requested

    std::mutex m_statusLock;
    std::deque<std::string> m_statusQueue;
    std::string m_lastStatus;
};

static CEhsLinkPlugin* g_plugin = nullptr;

void __declspec(dllexport) EuroScopePlugInInit(EuroScopePlugIn::CPlugIn** ppPlugInInstance)
{
    *ppPlugInInstance = g_plugin = new CEhsLinkPlugin();
}

void __declspec(dllexport) EuroScopePlugInExit(void)
{
    delete g_plugin;
    g_plugin = nullptr;
}
