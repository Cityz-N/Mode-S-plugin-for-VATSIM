// Desktop unit tests for the module's profile parser (same header the module uses).
//   cl /std:c++17 /EHsc profiles_test.cpp && profiles_test.exe ..\package\profiles.ini
#include "../src/Profiles.h"

#include <cstdio>
#include <fstream>
#include <sstream>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

int main(int argc, char** argv)
{
    using namespace ehsprof;
    ProfileSet s;

    // The shipped file must parse, be read-only, and have the expected structure.
    if (argc > 1)
    {
        std::ifstream f(argv[1], std::ios::binary);
        std::stringstream ss; ss << f.rdbuf();
        CHECK(ParseProfiles(ss.str(), s));
        CHECK(s.version >= 1);
        CHECK(s.url.rfind("https://", 0) == 0);
        CHECK(FindProfile(s, "pmdg 777-300er air france|simobjects\\airplanes\\pmdg 777-300er\\aircraft.cfg")->name == "PMDG 777");
        CHECK(FindProfile(s, "pmdg 777-300ersf emirates skycargo|x")->alt == "(L:ngx_ALTwindow)");   // no SDK broadcast needed
        CHECK(FindProfile(s, "pmdg 737-800 ryanair|x")->name == "PMDG 737");
        CHECK(FindProfile(s, "cessna 152 asobo|x")->name == "Standard");   // fallback
        CHECK(FindProfile(s, "atr 72-600 highline|simobjects\\airplanes\\x\\aircraft.cfg")->name == "ATR 42/72-600");
        CHECK(FindProfile(s, "atr 72-600 highline|x")->alt == "(L:MSATR_FGCP_SELALT)");
        CHECK(FindProfile(s, "airbus a320 neo asobo|x")->name == "A320neo (Asobo)");
        CHECK(FindProfile(s, "airbus a320neo flybywire|simobjects\\airplanes\\flybywire_a320_neo\\aircraft.cfg")->name == "A32NX (FlyByWire)");
        CHECK(FindProfile(s, "airbus a320neo flybywire|x")->speed.find("A32NX_AUTOPILOT_SPEED_SELECTED") != std::string::npos);
        CHECK(FindProfile(s, "airbus a330-900neo headwind simulations|c:\\...\\community\\headwindsim-aircraft-a330-900\\simobjects\\airplanes\\headwind_a330neo\\aircraft.cfg")->name == "A339 (Headwind)");
        CHECK(FindProfile(s, "aerosoft a346-house livery|c:\\...\\community\\aerosoft-aircraft-a346-pro\\simobjects\\airplanes\\airbus-a346-pro\\aircraft.cfg")->name == "A346 (Aerosoft)");
        CHECK(FindProfile(s, "airbus a350-900 afr la28|c:\\...\\community\\inibuilds-aircraft-a350\\simobjects\\airplanes\\inibuilds-aircraft-a350-900\\aircraft.cfg")->name == "A350 (iniBuilds)");
        // HubHop drafts: matched, unverified, and not stealing any verified aircraft
        CHECK(FindProfile(s, "fenix a320 air france|c:\\...\\community\\fnx-aircraft-a320\\x\\aircraft.cfg")->name == "Fenix A319/A320/A321");
        CHECK(FindProfile(s, "boeing 787-9 (ge) horizonsim house|c:\\...\\community\\horizonsim-aircraft-787-9\\x")->name == "Boeing 787 (HorizonSim / Asobo / Kuro)");
        CHECK(FindProfile(s, "boeing 787-10 asobo|c:\\...\\community\\mge-asobo-aircraft-b787-10\\x")->name == "Boeing 787 (HorizonSim / Asobo / Kuro)");
        CHECK(FindProfile(s, "boeing 787-8 base-ffcrr|c:\\...\\community\\kuro_b787-8\\simobjects\\airplanes\\kuro_b787_8\\aircraft.cfg")->name == "Boeing 787 (HorizonSim / Asobo / Kuro)");
        CHECK(FindProfile(s, "boeing 787-9 (ge) horizonsim house|x")->hdg == "(A:AUTOPILOT HEADING LOCK DIR:1, degrees)");
        CHECK(FindProfile(s, "some other 787-9 mod|x")->name == "Boeing 787 (draft)");
        CHECK(FindProfile(s, "lvfr airbus a319 delta|c:\\...\\community\\lvfr-airbus-a319-ceo\\x")->name == "LVFR A318/A319 (draft)");
        CHECK(FindProfile(s, "a330-300 inibuilds|c:\\...\\community\\inibuilds-aircraft-a330-300\\x")->name == "iniBuilds Airbus family (draft)");
        CHECK(FindProfile(s, "a330-belugaxl|c:\\...\\streamedpackages\\fs24-microsoft-aircraft-belugaxl\\content\\simobjects\\airplanes\\microsoft-belugaxl\\x")->name == "A320neo V2 / A321neo / A330 / Beluga XL (iniBuilds, MSFS 2024 defaults)");
        CHECK(FindProfile(s, "microsoft vision jet executive seating|c:\\...\\fs24-microsoft-aircraft-sf50\\content\\simobjects\\airplanes\\microsoft_sf50\\x")->name == "Vision Jet (Microsoft)");
        CHECK(FindProfile(s, "a310-300|c:\\...\\community\\inibuilds-aircraft-a310\\x")->name == "iniBuilds A310");     // not the family
        CHECK(FindProfile(s, "airbus a350-900 afr la28|c:\\...\\community\\inibuilds-aircraft-a350\\x")->name == "A350 (iniBuilds)");   // verified wins
        CHECK(FindProfile(s, "fslabs a321neo|c:\\...\\community\\fsl-aircraft-a321\\x")->name == "FSLabs A32x");
        CHECK(FindProfile(s, "cessna citation longitude asobo|x")->name == "Citation Longitude");
        CHECK(FindProfile(s, "fokker f28 fellowship|c:\\...\\community\\justflight-aircraft-f28\\x")->name == "Just Flight F28 (draft)");
        CHECK(FindProfile(s, "learjet 35a flysimware|x")->name == "Learjet 35A (draft)");
        CHECK(FindProfile(s, "tbm 930 asobo|x")->alt == "(A:AUTOPILOT ALTITUDE LOCK VAR:1, feet)");   // fallback reads slot :1
        CHECK(FindProfile(s, "c750 - livery 1|c:\\...\\onestore\\msfsffx-c750\\simobjects\\airplanes\\ce750\\aircraft.cfg")->name == "Citation X (FlightFX)");
        CHECK(FindProfile(s, "e-jets e175|c:\\...\\community\\fss-aircraft-e175\\x")->name == "FSS E-Jets");
        CHECK(FindProfile(s, "cessna 152 asobo|x")->name == "Standard");
        CHECK(FindProfile(s, "fenix a320 air france|x")->verified);
        CHECK(FindProfile(s, "fenixa320 cfm wf|c:\\...\\community\\fnx-aircraft-320\\x")->name == "Fenix A319/A320/A321");
        CHECK(!FindProfile(s, "leonardo maddog md-82|x")->verified);   // drafts stay unverified
        CHECK(FindProfile(s, "tfdi design md-11 pw|c:\\...\\community\\tfdidesign-aircraft-md11\\x")->name == "TFDi MD-11");
        CHECK(FindProfile(s, "bluebird simulations 757-200|c:\\...\\community\\bluebirdsim-aircraft-757-200\\x")->name == "Bluebird 757");
        CHECK(FindProfile(s, "airbus a320 neo asobo|x")->name == "A320neo (Asobo)");
        CHECK(FindProfile(s, "airbus a320neo flybywire|x")->name == "A32NX (FlyByWire)");
        CHECK(FindProfile(s, "airbus a350-900 afr la28|x")->name == "A350 (iniBuilds)");
        CHECK(FindProfile(s, "pmdg 777-300er air france|x")->name == "PMDG 777");
        CHECK(FindProfile(s, "pride a380x|c:\\...\\community\\flybywire-aircraft-a380-842\\simobjects\\airplanes\\flybywire_a380_842\\aircraft.cfg")->name == "A380X (FlyByWire)");
    }

    const char* good =
        "[Update]\nversion = 7\nurl = https://example.invalid/p.ini\n"
        "[A320]\nmatch = flybywire|A32NX\nalt = (L:X_ALT)\nhdg = (L:X_HDG) 0 max\nverified = yes\n"
        "[Standard]\nalt = (A:AUTOPILOT ALTITUDE LOCK VAR, feet)\n";
    CHECK(ParseProfiles(good, s));
    CHECK(s.version == 7 && s.list.size() == 2 && s.list[0].verified && s.list[0].match.size() == 2);
    CHECK(s.list[0].match[1] == "a32nx");                                           // lower-cased
    CHECK(FindProfile(s, "flybywire a320neo|x")->name == "A320");
    CHECK(FindProfile(s, "something else|x")->name == "Standard");

    // Rejections: writes, no fallback, junk, oversize.
    CHECK(!ParseProfiles("[Standard]\nalt = 1 (>L:EVIL)\n", s) && s.list.empty());
    CHECK(!ParseProfiles("[Standard]\nalt = (A:X, feet)\nvs = 0 (>K:AP_MASTER)\n", s));
    CHECK(!ParseProfiles("[Only]\nmatch = foo\nalt = (L:A)\n", s));                  // no fallback
    CHECK(!ParseProfiles("<html>404 Not Found</html>", s));
    CHECK(!ParseProfiles("", s));
    CHECK(!ParseProfiles(std::string(300 * 1024, 'x'), s));
    CHECK(!ParseProfiles("[Broken\nalt = 1\n", s));
    CHECK(ParseProfiles("[Standard]\nalt = (A:X, feet) 100 > if{ 1 } els{ 0 }\n", s));  // comparison '>' is fine

    std::printf("%s (%d failure%s)\n", g_fail ? "PROFILE TESTS FAILED" : "Profile tests passed", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
