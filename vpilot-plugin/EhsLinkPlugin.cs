// EHS Link plugin for vPilot: publishes your autopilot selections (selected altitude, heading,
// IAS/Mach, V/S) so controllers running the EHS Link EuroScope plugin can see them.
// Data goes to a public MQTT broker, never over the VATSIM network. See PROTOCOL.md.
//
// Install: copy EhsLink.vPilot.dll into vPilot's Plugins folder and restart vPilot.
// Optional: EhsLink.ini next to the DLL (see ReadConfig) to change brokers or timings.

using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using RossCarlson.Vatsim.Vpilot.Plugins;

namespace EhsLink
{
    public class EhsLinkPlugin : IPlugin
    {
        public const string Version = "0.2.2";
        public const string TopicRoot = "ehslink/v1/";

        private IBroker _broker;
        private SynchronizationContext _ui;
        private readonly object _gate = new object();
        private readonly ModuleReader _sim = new ModuleReader();

        // config
        private List<KeyValuePair<string, int>> _brokers = new List<KeyValuePair<string, int>>
        {
            new KeyValuePair<string, int>("broker.hivemq.com", 1883),
            new KeyValuePair<string, int>("broker.emqx.io", 1883),
        };
        private int _pollMs = 250, _minIntervalMs = 1000, _heartbeatMs = 10000, _currentSpeedMs = 5000;
        private Selections _lastSentSel;

        // state
        private volatile string _callsign;          // non-null while connected to VATSIM as a pilot
        private MqttClient _mqtt;
        private int _brokerIndex;
        private DateTime _nextConnectAttempt = DateTime.MinValue;
        private string _lastKey;
        private DateTime _lastSent = DateTime.MinValue;
        private ReadStatus? _lastStatus;
        private string _lastSource;

        public string Name => "EHS Link";

        public void Initialize(IBroker broker)
        {
            _broker = broker;
            _ui = SynchronizationContext.Current;
            ReadConfig();

            broker.NetworkConnected += (s, e) =>
            {
                if (e.ObserverMode) return;
                lock (_gate) { _callsign = Sanitize(e.Callsign); _lastKey = null; }
            };
            broker.NetworkDisconnected += (s, e) => StopSession();
            broker.SessionEnded += (s, e) =>
            {
                StopSession();
                _sim.Dispose();   // close SimConnect cleanly before vPilot exits
            };

            new Thread(Loop) { IsBackground = true, Name = "EHS Link" }.Start();
            Post("EHS Link " + Version + " loaded. Your autopilot selections will be shared with EHS Link controllers while you're connected.");
        }

        // ------------------------------------------------------------------ main loop

        private void Loop()
        {
            var sim = _sim;
            sim.Start();
            {
                while (true)
                {
                    Thread.Sleep(_pollMs);
                    try { Tick(sim); }
                    catch (Exception ex) { Post("EHS Link error: " + ex.Message); Thread.Sleep(5000); }
                }
            }
        }

        private void Tick(ModuleReader sim)
        {
            string callsign;
            lock (_gate) callsign = _callsign;
            if (callsign == null) return;

            Selections sel;
            var status = sim.Read(out sel);
            ReportStatus(status, sim, sel);
            if (status != ReadStatus.Ok) return;

            // Same lock as StopSession, so a disconnect can never interleave with a publish
            // (which would leave stale retained data on the broker).
            lock (_gate)
            {
                if (_callsign != callsign) return;
                if (!EnsureMqtt(callsign)) return;
                PublishIfDue(callsign, sel);
            }
        }

        private void PublishIfDue(string callsign, Selections sel)
        {
            var now = DateTime.UtcNow;
            var key = sel.Key();
            var since = (now - _lastSent).TotalMilliseconds;
            bool changed = key != _lastKey;
            // Current IAS/Mach moves all the time: send it like a radar sweep, at most every 5 s.
            bool speedDue = since >= _currentSpeedMs && sel.CurrentSpeedDiffers(_lastSentSel);
            if ((changed && since >= _minIntervalMs) || speedDue || since >= _heartbeatMs)
            {
                var unix = (long)(now - new DateTime(1970, 1, 1)).TotalSeconds;
                try
                {
                    _mqtt.Publish(TopicRoot + callsign, Encoding.UTF8.GetBytes(sel.ToJson(callsign, unix)), true);
                    _lastKey = key;
                    _lastSent = now;
                    _lastSentSel = sel;
                }
                catch { DropMqtt(); }
            }
        }

        private bool EnsureMqtt(string callsign)
        {
            if (_mqtt != null && _mqtt.IsConnected) return true;
            if (DateTime.UtcNow < _nextConnectAttempt) return false;

            var b = _brokers[_brokerIndex % _brokers.Count];
            var client = new MqttClient();
            try
            {
                var clientId = "ehsp-" + callsign + "-" + Guid.NewGuid().ToString("N").Substring(0, 8);
                client.Connect(b.Key, b.Value, clientId, 30, TopicRoot + callsign, new byte[0], true);
                _mqtt = client;
                _lastKey = null;                 // force an immediate publish
                _lastSent = DateTime.MinValue;
                Post("EHS Link connected to " + b.Key + ".");
                return true;
            }
            catch (Exception ex)
            {
                client.Dispose();
                Post("EHS Link couldn't reach " + b.Key + " (" + ex.Message + "). Trying the next broker in 10 s.");
                _brokerIndex++;
                _nextConnectAttempt = DateTime.UtcNow.AddSeconds(10);
                return false;
            }
        }

        private void DropMqtt()
        {
            try { _mqtt?.Dispose(); } catch { }
            _mqtt = null;
            _nextConnectAttempt = DateTime.UtcNow.AddSeconds(2);
        }

        private void StopSession()
        {
            lock (_gate)
            {
                var cs = _callsign;
                _callsign = null;
                var m = _mqtt;
                if (cs != null && m != null && m.IsConnected)
                {
                    try { m.Publish(TopicRoot + cs, new byte[0], true); } catch { }   // clear our retained data
                }
                try { m?.Disconnect(); } catch { }
                _mqtt = null;
                _brokerIndex = 0;
                _lastStatus = null;
            }
        }

        private void ReportStatus(ReadStatus status, ModuleReader sim, Selections sel)
        {
            var source = sel?.Source;
            if (status == _lastStatus && source == _lastSource) return;
            _lastStatus = status; _lastSource = source;
            switch (status)
            {
                case ReadStatus.NoSim:
                    Post("EHS Link is waiting for the simulator.");
                    break;
                case ReadStatus.SimConnectUnavailable:
                    Post("EHS Link can't use SimConnect (" + sim.FatalError + "). Please report this with your vPilot version.");
                    break;
                case ReadStatus.NoModule:
                    Post("EHS Link: the EHS Link module isn't running in MSFS. Copy the 'ehslink-module' folder into your " +
                         "MSFS Community folder and restart the simulator.");
                    break;
                case ReadStatus.NoProfiles:
                    Post("EHS Link: the module can't read its profiles.ini. Reinstall the 'ehslink-module' folder.");
                    break;
                case ReadStatus.NoProfile:
                    Post("EHS Link: no profile for this aircraft (" + sim.LastTitle + "), so nothing is being shared.");
                    break;
                case ReadStatus.PmdgBroadcastOff:
                    Post("EHS Link: PMDG data isn't available. Add these lines to the end of the aircraft's Options ini " +
                         "(e.g. ...\\pmdg-aircraft-77w\\work\\777_Options.ini), make the file read-only, then reload the aircraft:  [SDK]  EnableDataBroadcast=1");
                    break;
                case ReadStatus.NoApData:
                    Post("EHS Link: this aircraft (" + sim.LastTitle + ") doesn't publish its autopilot selections in a way EHS Link can read yet, " +
                         "so nothing is being shared. An aircraft profile can be added to profiles.ini.");
                    break;
                case ReadStatus.Ok:
                    Post("EHS Link reading " + sim.LastTitle + " with the '" + source + "' profile" +
                         (sel.Verified ? "." : " (not yet verified for this aircraft: please report if values look wrong)."));
                    break;
            }
        }

        // ------------------------------------------------------------------ helpers

        private void ReadConfig()
        {
            try
            {
                var dir = Path.GetDirectoryName(typeof(EhsLinkPlugin).Assembly.Location);
                var path = Path.Combine(dir ?? ".", "EhsLink.ini");
                if (!File.Exists(path)) return;
                foreach (var raw in File.ReadAllLines(path))
                {
                    var line = raw.Trim();
                    if (line.Length == 0 || line[0] == ';' || line[0] == '#' || line[0] == '[') continue;
                    var eq = line.IndexOf('=');
                    if (eq < 0) continue;
                    var k = line.Substring(0, eq).Trim().ToLowerInvariant();
                    var v = line.Substring(eq + 1).Trim();
                    int n;
                    switch (k)
                    {
                        case "brokers":   // brokers = host[:port], host[:port]
                            var list = new List<KeyValuePair<string, int>>();
                            foreach (var item in v.Split(','))
                            {
                                var parts = item.Trim().Split(':');
                                if (parts[0].Length == 0) continue;
                                list.Add(new KeyValuePair<string, int>(parts[0], parts.Length > 1 && int.TryParse(parts[1], out n) ? n : 1883));
                            }
                            if (list.Count > 0) _brokers = list;
                            break;
                        case "min_interval_ms": if (int.TryParse(v, out n) && n >= 200) _minIntervalMs = n; break;
                        case "heartbeat_ms": if (int.TryParse(v, out n) && n >= 2000) _heartbeatMs = n; break;
                    }
                }
            }
            catch { /* defaults */ }
        }

        private static string Sanitize(string callsign)
        {
            var s = Regex.Replace((callsign ?? "").ToUpperInvariant(), "[^A-Z0-9_]", "");
            return s.Length == 0 ? null : s;
        }

        private void Post(string text)
        {
            Action a = () => { try { _broker.PostDebugMessage(text); } catch { } };
            if (_ui != null) _ui.Post(_ => a(), null); else a();
        }
    }
}
