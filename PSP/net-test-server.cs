// Stand-in for Radio Browser, LRCLIB and a few radio stations, for the emulator's network test
// (run-test.ps1 -Kind Net). Plain HTTP on 127.0.0.1:8088, so the test needs no internet.
// usage: dotnet run net-test-server.cs -- <folder with stream.mp3 and stream.aac>
using System.Net;
using System.Net.Sockets;
using System.Text;

var media = args.Length > 0 ? args[0] : ".";
var mp3 = File.ReadAllBytes(Path.Combine(media, "stream.mp3"));
var aac = File.ReadAllBytes(Path.Combine(media, "stream.aac"));
// podcasts: a 30 s MP3 episode (with an ID3 tag and cover art), a 12 s M4A one, and a cover
var epMp3 = File.ReadAllBytes(Path.Combine(media, "ep.mp3"));
var epM4a = File.ReadAllBytes(Path.Combine(media, "ep.m4a"));
var art = File.ReadAllBytes(Path.Combine(media, "art.jpg"));
bool addedEpisode = false;
var listener = new TcpListener(IPAddress.Loopback, 8088);
listener.Start();
Console.WriteLine("ZOID net test server on 127.0.0.1:8088");
const string B = "http://127.0.0.1:8088";

string Station(string name, string url, string codec, int kbps, int hls = 0) =>
    $"{{\"changeuuid\":\"x\",\"stationuuid\":\"{Guid.NewGuid()}\",\"name\":\"{name}\",\"url\":\"{url}\",\"url_resolved\":\"{url}\",\"homepage\":\"\",\"favicon\":\"\",\"tags\":\"test,jazz\",\"country\":\"Testland\",\"countrycode\":\"TL\",\"state\":\"\",\"language\":\"english\",\"votes\":5,\"codec\":\"{codec}\",\"bitrate\":{kbps},\"hls\":{hls},\"lastcheckok\":1,\"geo_lat\":null,\"has_extended_info\":false}}";
string Stations() => "[" + string.Join(",",
    Station("Zoid Test FM \\u00e9t\\u00e9", B + "/stream.mp3", "MP3", 128),
    Station("Zoid AAC Radio", B + "/stream.aac", "AAC+", 64),
    Station("Zoid Playlist Station", B + "/station.pls", "MP3", 128),
    Station("Zoid Moved Station", B + "/redir", "MP3", 128),
    Station("Ogg Station (hidden)", B + "/x.ogg", "OGG", 128),
    Station("HLS Station (hidden)", B + "/x.m3u8", "AAC", 128, 1),
    Station("Zoid Missing Station", B + "/missing", "MP3", 128)) + "]";
const string Tags = "[{\"name\":\"jazz\",\"stationcount\":5000},{\"name\":\"rock\",\"stationcount\":4000},{\"name\":\"tiny\",\"stationcount\":3}]";
string Pod(int id, string name, string author, string feed) =>
    $"{{\"wrapperType\":\"track\",\"kind\":\"podcast\",\"collectionId\":{id},\"artistName\":\"{author}\",\"collectionName\":\"{name}\",\"feedUrl\":\"{feed}\",\"artworkUrl600\":\"{B}/600x600bb/art.jpg\",\"trackCount\":3,\"primaryGenreName\":\"Music\"}}";
string PodA() => Pod(111, "Zoid Test Podcast", "Zoid Test Author", B + "/feed.xml");
string PodB() => Pod(222, "Zoid Video Cast", "Zoid Two", B + "/feed2.xml");
// the top chart: entries with the podcast's link and id (and a category, whose im:id must be skipped)
string ChartEntry(int id, string name) =>
    $"{{\"im:name\":{{\"label\":\"{name}\"}},\"id\":{{\"label\":\"https://podcasts.apple.com/us/podcast/x/id{id}?uo=2\",\"attributes\":{{\"im:id\":\"{id}\"}}}},\"category\":{{\"attributes\":{{\"im:id\":\"1310\",\"term\":\"Music\"}}}}}}";
string Chart() => $"{{\"feed\":{{\"entry\":[{ChartEntry(111, "Zoid Test Podcast")},{ChartEntry(222, "Zoid Video Cast")}],\"id\":{{\"label\":\"https://itunes.apple.com/us/rss/toppodcasts/limit=50/json\"}}}}}}";
// the feed: out of date order, a CDATA title, an entity, three ways to write a length, a wrong file size
string Item(string title, string date, string dur, string url, string type, int len, string guid) =>
    $"<item>\n <title>{title}</title>\n <description><![CDATA[<p>Show notes with <b>tags</b> & things</p>]]></description>\n <pubDate>{date}</pubDate>\n" +
    $" <guid isPermaLink=\"false\">{guid}</guid>\n <itunes:duration>{dur}</itunes:duration>\n <enclosure url=\"{url}\" length=\"{len}\" type=\"{type}\"/>\n</item>\n";
string Feed() => $"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<rss version=\"2.0\" xmlns:itunes=\"http://www.itunes.com/dtds/podcast-1.0.dtd\">\n<channel>\n<title>Zoid Test Podcast</title>\n" +
    $"<itunes:author>Zoid Test Author</itunes:author>\n<itunes:image href=\"{B}/art.jpg\"/>\n<image><url>{B}/other.jpg</url><title>Not this title</title></image>\n" +
    (addedEpisode ? Item("Episode 4 (M4A)", "Wed, 04 Nov 2026 08:00:00 GMT", "12", B + "/ep4.m4a", "audio/x-m4a", epM4a.Length, "ep-4") : "") +
    Item("Episode 2", "Mon, 02 Nov 2026 08:00:00 GMT", "30", B + "/ep2.mp3?src=feed&amp;x=1", "audio/mpeg", epMp3.Length, "ep-2") +
    Item("<![CDATA[Episode 3 & the CDATA]]>", "Tue, 03 Nov 2026 08:00:00 +0000", "00:00:30", B + "/ep3.mp3", "audio/mpeg", 1000, "ep-3") +
    Item("Episode&#32;1", "Sun, 01 Nov 2026 08:00:00 GMT", "0:30", B + "/ep1.mp3", "audio/mpeg", 0, "ep-1") +
    "</channel>\n</rss>\n";
// LRCLIB: "Song One" (the MP3 station's first title) answers /api/get; "Net Song" only /api/search (chunked)
string Lyrics(string track, string artist, int dur) =>
    $"{{\"id\":1,\"trackName\":\"{track}\",\"artistName\":\"{artist}\",\"albumName\":\"Net Album\",\"duration\":{dur},\"instrumental\":false,\"plainLyrics\":\"First line\\nSecond line\",\"syncedLyrics\":\"[00:00.50] First line of {track}\\n[00:02.00] Second line\\n[00:04.00] Third line \\u266a\\n[00:30.00] Last line\"}}";

while (true)
{
    var client = await listener.AcceptTcpClientAsync();
    _ = Task.Run(() => Serve(client));
}

async Task Serve(TcpClient client)
{
    using var _c = client;
    try
    {
        var s = client.GetStream();
        var head = new StringBuilder();
        var one = new byte[1];
        while (!head.ToString().EndsWith("\r\n\r\n") && head.Length < 8192)
        {
            if (await s.ReadAsync(one) == 0) return;
            head.Append((char)one[0]);
        }
        var lines = head.ToString().Split("\r\n");
        var target = lines[0].Split(' ')[1];
        var rangeLine = lines.FirstOrDefault(l => l.StartsWith("Range: bytes=", StringComparison.OrdinalIgnoreCase));
        int rangeFrom = rangeLine != null ? int.Parse(rangeLine.Substring(13).Split('-')[0]) : -1;
        bool icy = lines.Any(l => l.StartsWith("Icy-MetaData: 1", StringComparison.OrdinalIgnoreCase));
        var path = target.Split('?')[0];
        var query = target.Contains('?') ? Uri.UnescapeDataString(target[(target.IndexOf('?') + 1)..].Replace("+", " ")) : "";
        Console.WriteLine($"{DateTime.Now:HH:mm:ss} {target}");
        async Task Send(int code, string type, string body, bool chunked = false)
        {
            var bytes = Encoding.UTF8.GetBytes(body);
            var reason = code == 200 ? "OK" : code == 302 ? "Found" : "Not Found";
            var h = $"HTTP/1.1 {code} {reason}\r\nContent-Type: {type}\r\nDate: {DateTime.UtcNow:R}\r\nConnection: close\r\n" +
                    (chunked ? "Transfer-Encoding: chunked\r\n" : $"Content-Length: {bytes.Length}\r\n") + "\r\n";
            await s.WriteAsync(Encoding.ASCII.GetBytes(h));
            if (!chunked) { await s.WriteAsync(bytes); return; }
            for (int i = 0; i < bytes.Length; i += 700)
            {
                int n = Math.Min(700, bytes.Length - i);
                await s.WriteAsync(Encoding.ASCII.GetBytes($"{n:x}\r\n"));
                await s.WriteAsync(bytes.AsMemory(i, n));
                await s.WriteAsync(Encoding.ASCII.GetBytes("\r\n"));
            }
            await s.WriteAsync(Encoding.ASCII.GetBytes("0\r\n\r\n"));
        }
        // a file, with Range: 206 and Content-Range from where asked
        async Task SendFile(string type, byte[] data)
        {
            int from = rangeFrom > 0 && rangeFrom < data.Length ? rangeFrom : 0;
            var h = (from > 0 ? $"HTTP/1.1 206 Partial Content\r\nContent-Range: bytes {from}-{data.Length - 1}/{data.Length}\r\n" : "HTTP/1.1 200 OK\r\n") +
                    $"Content-Type: {type}\r\nContent-Length: {data.Length - from}\r\nAccept-Ranges: bytes\r\nConnection: close\r\n\r\n";
            await s.WriteAsync(Encoding.ASCII.GetBytes(h));
            for (int i = from; i < data.Length; i += 8192) await s.WriteAsync(data.AsMemory(i, Math.Min(8192, data.Length - i)));
        }
        // Podcasts (the iTunes Search API, a top chart, feeds and episodes)
        if (path == "/search") { await Send(200, "text/javascript", query.Contains("term=zoid") ? "{\"resultCount\":2,\"results\":[" + PodA() + "," + PodB() + "]}" : "{\"resultCount\":0,\"results\":[]}"); return; }
        if (path == "/lookup") { await Send(200, "text/javascript", "{\"resultCount\":2,\"results\":[" + PodB() + "," + PodA() + "]}"); return; } // not in chart order
        if (path.StartsWith("/us/rss/toppodcasts/limit=50")) { await Send(200, "application/json", Chart()); return; }
        if (path == "/feed.xml") { await Send(200, "application/rss+xml; charset=utf-8", Feed(), chunked: true); return; }
        if (path == "/feed2.xml") { await Send(200, "application/rss+xml", "<rss><channel><title>Zoid Video Cast</title><item><title>Only one</title><enclosure url=\"" + B + "/ep1.mp3\" type=\"audio/mpeg\"/></item></channel></rss>"); return; }
        if (path == "/test/add-episode") { addedEpisode = true; await Send(200, "text/plain", "ok"); return; }
        if (path.StartsWith("/slow/")) { await Task.Delay(1500); await SendFile("audio/mpeg", epMp3); return; } // a host that is slow to answer
        if (path == "/ep1.mp3" || path == "/ep2.mp3" || path == "/ep3.mp3") { await SendFile("audio/mpeg", epMp3); return; }
        if (path == "/ep4.m4a") { await SendFile("audio/x-m4a", epM4a); return; }
        if (path.EndsWith("art.jpg")) { await SendFile("image/jpeg", art); return; }
        // iHeartRadio: its cities (one with no stations, left out; one in Mexico), a city's lineup (one HLS-only station, left out), search
        if (path == "/api/v2/content/markets")
        {
            string Mk(int id, string city, string state, string cc, int n) => $"{{\"name\":\"X\",\"marketId\":{id},\"loc\":{{\"lat\":1,\"lon\":2}},\"stationCount\":{n},\"stateAbbreviation\":\"TS\",\"stateName\":\"{state}\",\"city\":\"{city}\",\"countryAbbreviation\":\"{cc}\"}}";
            await Send(200, "application/json", "{\"duration\":1,\"total\":5,\"hits\":[" + string.Join(",", Mk(28, "Small Town", "Test State", "US", 1), Mk(27, "Zoid City", "Test State", "US", 3), Mk(29, "Baja Town", "Baja Test", "MX", 2), Mk(30, "Ghost Town", "Test State", "US", 0), Mk(31, "Alpha City", "Alpha State", "US", 4)) + "]}", chunked: true);
            return;
        }
        string Ih(int id, string name, string band, string call, string freq, string stream, string city = "Zoid City") =>
            $"{{\"id\":{id},\"name\":\"{name}\",\"band\":\"{band}\",\"callLetters\":\"{call}\",\"freq\":\"{freq}\",\"streams\":{{\"hls_stream\":\"{B}/x.m3u8\"" + (stream != "" ? $",\"shoutcast_stream\":\"{stream}\",\"secure_shoutcast_stream\":\"https://127.0.0.1:1/x\"" : "") +
            $"}},\"markets\":[{{\"name\":\"X\",\"city\":\"{city}\",\"stateAbbreviation\":\"TS\",\"primary\":true}},{{\"city\":\"Other\"}}],\"genres\":[{{\"id\":1,\"name\":\"Pop\"}}]}}";
        if (path == "/api/v2/content/liveStations" && query.Contains("marketId=27"))
        {
            await Send(200, "application/json", "{\"duration\":1,\"total\":3,\"hits\":[" + string.Join(",", Ih(1, "Zoid 93.3", "FM", "KZOI-FM", "93.3", B + "/stream.mp3"), Ih(2, "HLS Only (hidden)", "FM", "KHLS-FM", "99.9", ""), Ih(3, "Zoid News 600", "AM", "KZNW-AM", "600", B + "/stream.aac")) + "]}");
            return;
        }
        if (path == "/api/v3/search/all")
        {
            await Send(200, "application/json", query.Contains("keywords=zoid") ? "{\"results\":{\"albums\":[],\"stations\":[{\"id\":3,\"name\":\"Zoid News 600\"},{\"id\":1,\"name\":\"Zoid 93.3\"}],\"artists\":[]}}" : "{\"results\":{\"stations\":[]}}");
            return;
        }
        if (path == "/api/v2/content/liveStations" && query.Contains("id=3,1")) // answered in the directory's order, not the search's
        {
            await Send(200, "application/json", "{\"hits\":[" + Ih(1, "Zoid 93.3", "FM", "KZOI-FM", "93.3", B + "/stream.mp3") + "," + Ih(3, "Zoid News 600", "AM", "KZNW-AM", "600", B + "/stream.aac", "Far City") + "]}");
            return;
        }
        // Local stations: countries (Emptyland has none, so it is not listed), Testland's places, and a place's own stations
        if (path.StartsWith("/json/countries")) { await Send(200, "application/json", "[{\"name\":\"Testland\",\"iso_3166_1\":\"TL\",\"stationcount\":7},{\"name\":\"Emptyland\",\"iso_3166_1\":\"EL\",\"stationcount\":0}]"); return; }
        if (path == "/json/states/Testland/") { await Send(200, "application/json", "[{\"name\":\"Test State\",\"country\":\"Testland\",\"stationcount\":5},{\"name\":\"Test City TS\",\"country\":\"Testland\",\"stationcount\":2}]"); return; }
        if (path.StartsWith("/json/stations") && query.Contains("countrycode=TL") && query.Contains("state=Test State"))
        { await Send(200, "application/json", "[" + Station("Test State Public Radio", B + "/stream.mp3", "MP3", 128) + "," + Station("Test State Rock", B + "/stream.aac", "AAC", 64) + "]"); return; }
        if (path.StartsWith("/json/stations")) { await Send(200, "application/json", Stations()); return; }
        if (path.StartsWith("/json/tags")) { await Send(200, "application/json", Tags); return; }
        if (path == "/station.pls") { await Send(200, "audio/x-scpls", $"[playlist]\nNumberOfEntries=1\nFile1={B}/stream.mp3\nTitle1=Zoid\n"); return; }
        if (path == "/redir") { await s.WriteAsync(Encoding.ASCII.GetBytes($"HTTP/1.1 302 Found\r\nLocation: /stream.mp3\r\nContent-Length: 0\r\n\r\n")); return; }
        if (path == "/api/get")
        {
            if (query.Contains("track_name=Song One")) await Send(200, "application/json", Lyrics("Song One", "Zoid Test Artist", 0));
            else await Send(404, "application/json", "{\"code\":404,\"name\":\"TrackNotFound\",\"message\":\"Failed to find specified track\"}");
            return;
        }
        if (path == "/api/search")
        {
            if (query.Contains("track_name=Net Song")) await Send(200, "application/json", "[" + Lyrics("Net Song", "Net Artist", 20) + "]", chunked: true);
            else if (query.Contains("track_name=Song One") || query.Contains("track_name=Song Two")) await Send(200, "application/json", "[" + Lyrics(query.Contains("Two") ? "Song Two" : "Song One", "Zoid Test Artist", 0) + "]", chunked: true);
            else await Send(200, "application/json", "[]", chunked: true);
            return;
        }
        if (path == "/stream.mp3" || path == "/stream.aac")
        {
            bool isMp3 = path == "/stream.mp3";
            var data = isMp3 ? mp3 : aac;
            int metaint = isMp3 && icy ? 8192 : 0;
            var h = isMp3
                ? $"ICY 200 OK\r\nContent-Type: audio/mpeg\r\nicy-name: Zoid Test FM\r\nicy-genre: Test\r\nicy-br: 128\r\n" + (metaint > 0 ? $"icy-metaint: {metaint}\r\n" : "") + "\r\n"
                : "HTTP/1.0 200 OK\r\nContent-Type: audio/aacp\r\nicy-br: 64\r\n\r\n";
            await s.WriteAsync(Encoding.ASCII.GetBytes(h));
            int rate = isMp3 ? 16000 : 8000, pos = 0, sinceMeta = 0;
            var start = DateTime.UtcNow; long sent = 0;
            while (true)
            {
                int n = Math.Min(2048, data.Length - pos);
                if (metaint > 0) n = Math.Min(n, metaint - sinceMeta);
                await s.WriteAsync(data.AsMemory(pos, n));
                pos = (pos + n) % data.Length; sent += n; sinceMeta += n;
                if (metaint > 0 && sinceMeta == metaint)
                {
                    // the title changes every 20 s of audio: Song One, then Song Two
                    var title = (sent / rate / 20) % 2 == 0 ? "Zoid Test Artist - Song One" : "Zoid Test Artist - Song Two";
                    var m = Encoding.UTF8.GetBytes($"StreamTitle='{title}';");
                    int blocks = (m.Length + 15) / 16; var mb = new byte[1 + blocks * 16]; mb[0] = (byte)blocks; m.CopyTo(mb, 1);
                    await s.WriteAsync(mb); sinceMeta = 0;
                }
                // a quick start (about 4 s), then 1.5x real time
                double ahead = sent - (64 * 1024 + (DateTime.UtcNow - start).TotalSeconds * rate * 1.5);
                if (ahead > 0) await Task.Delay((int)(ahead / (rate * 1.5) * 1000));
            }
        }
        await Send(404, "text/html", "<h1>Not found</h1>");
    }
    catch (IOException) { }
    catch (SocketException) { }
}
