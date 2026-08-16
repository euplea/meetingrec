#include "minutes.h"

#include <cctype>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string today() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tmv);
    return buf;
}

std::vector<std::string> splitSentences(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        cur += c;
        bool isEnd = false;
        if (c == '!' || c == '?') {
            isEnd = true;
        } else if (c == '.') {
            // Evita di spezzare numeri decimali / versioni come "2.0".
            const bool prevDigit = (i > 0 && std::isdigit(static_cast<unsigned char>(text[i - 1])));
            const bool nextDigit =
                (i + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[i + 1])));
            if (!(prevDigit && nextDigit)) isEnd = true;
        }
        if (isEnd) {
            const std::string s = trim(cur);
            if (!s.empty()) out.push_back(s);
            cur.clear();
        }
    }
    const std::string s = trim(cur);
    if (!s.empty()) out.push_back(s);
    return out;
}

bool containsAny(const std::string& s, const std::vector<std::string>& keywords) {
    const std::string l = toLower(s);
    for (const auto& k : keywords) {
        if (l.find(k) != std::string::npos) return true;
    }
    return false;
}

// Segmento strutturato in stile VibeVoice-ASR (Who/When/What):
//   [Start - End] Speaker N: contenuto
struct Segment {
    std::string start;
    std::string end;
    std::string speaker;
    std::string content;
};

std::vector<Segment> parseSegments(const std::string& text) {
    std::vector<Segment> out;
    std::istringstream iss(text);
    std::string line;
    const std::regex re(
        R"(^\s*\[\s*([0-9]+(?:\.[0-9]+)?)\s*-\s*([0-9]+(?:\.[0-9]+)?)\s*\]\s*(?:Speaker\s*([0-9]+)\s*:\s*)?(.*)$)");
    while (std::getline(iss, line)) {
        std::smatch m;
        if (std::regex_match(line, m, re)) {
            Segment s;
            s.start = m[1].str();
            s.end = m[2].str();
            s.speaker = m[3].matched ? m[3].str() : "";
            s.content = trim(m[4].str());
            if (!s.content.empty()) out.push_back(s);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Modello del documento (condiviso dai renderer Markdown e ODT).
// ---------------------------------------------------------------------------
struct MinuteDoc {
    std::string title;
    std::string date;
    std::string attendees;
    std::string speakers;
    std::vector<std::pair<std::string, std::vector<std::string>>> sections;
    std::vector<Segment> segments;
    std::string transcript;
    bool hasSegments = false;
};

bool buildDoc(const MinutesOptions& o, MinuteDoc& doc) {
    doc.title = o.title;
    doc.date = o.date.empty() ? today() : o.date;
    doc.attendees = o.attendees;
    doc.transcript = o.transcriptText;
    doc.segments = parseSegments(o.transcriptText);
    doc.hasSegments = !doc.segments.empty();

    // Analisi euristica sul testo (solo i contenuti se ci sono segmenti strutturati).
    std::string analysisText = o.transcriptText;
    if (!doc.segments.empty()) {
        std::string joined;
        for (const auto& s : doc.segments) {
            if (!joined.empty()) joined += " ";
            joined += s.content;
        }
        analysisText = joined;
    }
    const std::vector<std::string> sentences = splitSentences(analysisText);

    static const std::vector<std::string> decisionKw = {
        "decision", "deciso", "approv", "conferm", "concord", "stabilit",
        "concluso", "accordo", "allineat"};
    static const std::vector<std::string> actionKw = {
        "action", "todo", "to-do", "to do", "dobbiamo", "deve fare", "da fare",
        "entro", "assegn", "next step", "owner", "responsabil", "farà", "fara",
        "will "};
    static const std::vector<std::string> riskKw = {
        "rischio", "problema", "blocc", "block", "impedi", "ritardo", "delay",
        "issue", "criticit"};

    std::vector<std::string> decisions, actions, risks, topics;
    for (const auto& s : sentences) {
        if (containsAny(s, decisionKw)) decisions.push_back(s);
        else if (containsAny(s, riskKw)) risks.push_back(s);
        else if (containsAny(s, actionKw)) actions.push_back(s);
        else topics.push_back(s);
    }
    doc.sections.emplace_back("Punti discussi", topics);
    doc.sections.emplace_back("Decisioni", decisions);
    doc.sections.emplace_back("Azioni / To-do", actions);
    doc.sections.emplace_back("Rischi / Blocker", risks);

    if (!doc.segments.empty()) {
        std::set<std::string> speakers;
        for (const auto& s : doc.segments)
            if (!s.speaker.empty()) speakers.insert(s.speaker);
        if (!speakers.empty()) {
            std::string joined;
            for (auto it = speakers.begin(); it != speakers.end(); ++it) {
                if (it != speakers.begin()) joined += ", ";
                joined += *it;
            }
            doc.speakers = joined;
        }
    }
    return true;
}

std::string renderMarkdown(const MinuteDoc& d) {
    std::string out;
    out += "# " + d.title + "\n\n";
    out += "- **Data:** " + d.date + "\n";
    if (!d.attendees.empty()) out += "- **Partecipanti:** " + d.attendees + "\n";
    if (!d.speakers.empty()) out += "- **Relatori rilevati:** " + d.speakers + "\n";
    out += "\n";

    const auto section = [&out](const char* title, const std::vector<std::string>& items) {
        out += "## " + std::string(title) + "\n\n";
        if (items.empty()) {
            out += "- _(nessun elemento rilevato)_\n\n";
        } else {
            for (const auto& i : items) out += "- " + i + "\n";
            out += "\n";
        }
    };

    for (const auto& sec : d.sections) section(sec.first.c_str(), sec.second);

    if (d.hasSegments) {
        out += "## Trascrizione per relatori\n\n";
        for (const auto& s : d.segments) {
            out += "- [" + s.start + " - " + s.end + "]";
            if (!s.speaker.empty()) out += " **Relatore " + s.speaker + "**:";
            out += " " + s.content + "\n";
        }
        out += "\n";
    }

    out += "## Trascrizione integrale\n\n";
    out += d.transcript + "\n";
    return out;
}

// ---------------------------------------------------------------------------
// Export ODT (OpenDocument Text) — pacchetto ZIP con XML minimale.
// ---------------------------------------------------------------------------

std::string xmlEscape(const std::string& s) {
    std::string r;
    for (char c : s) {
        switch (c) {
            case '&': r += "&amp;"; break;
            case '<': r += "&lt;"; break;
            case '>': r += "&gt;"; break;
            case '"': r += "&quot;"; break;
            case '\'': r += "&apos;"; break;
            default: r += c;
        }
    }
    return r;
}

uint32_t crc32(const void* data, size_t len) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

void put16(std::string& s, uint16_t v) {
    s.push_back(static_cast<char>(v & 0xFF));
    s.push_back(static_cast<char>((v >> 8) & 0xFF));
}
void put32(std::string& s, uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        s.push_back(static_cast<char>(v & 0xFF));
        v >>= 8;
    }
}

// ZIP in modalità STORE (senza compressione) — sufficiente e compatibile.
std::string makeZip(const std::vector<std::pair<std::string, std::string>>& entries) {
    std::string out;
    std::vector<uint32_t> offsets;

    for (const auto& e : entries) {
        offsets.push_back(static_cast<uint32_t>(out.size()));
        out += "PK\x03\x04";
        put16(out, 20);  // version needed
        put16(out, 0);   // flags
        put16(out, 0);   // method: stored
        put16(out, 0);   // mod time
        put16(out, 0x21);  // mod date
        put32(out, crc32(e.second.data(), e.second.size()));
        put32(out, static_cast<uint32_t>(e.second.size()));
        put32(out, static_cast<uint32_t>(e.second.size()));
        put16(out, static_cast<uint16_t>(e.first.size()));
        put16(out, 0);  // extra
        out += e.first;
        out += e.second;
    }

    const uint32_t cdStart = static_cast<uint32_t>(out.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        out += "PK\x01\x02";
        put16(out, 0x031E);  // version made by
        put16(out, 20);      // version needed
        put16(out, 0);
        put16(out, 0);       // stored
        put16(out, 0);
        put16(out, 0x21);
        put32(out, crc32(entries[i].second.data(), entries[i].second.size()));
        put32(out, static_cast<uint32_t>(entries[i].second.size()));
        put32(out, static_cast<uint32_t>(entries[i].second.size()));
        put16(out, static_cast<uint16_t>(entries[i].first.size()));  // filename len
        put16(out, 0);       // extra len
        put16(out, 0);       // comment len
        put16(out, 0);       // disk number start
        put16(out, 0);       // internal attrs
        put32(out, 0);       // external attrs
        put32(out, offsets[i]);
        out += entries[i].first;
    }
    const uint32_t cdSize = static_cast<uint32_t>(out.size()) - cdStart;

    out += "PK\x05\x06";
    put16(out, 0);
    put16(out, 0);
    put16(out, static_cast<uint16_t>(entries.size()));
    put16(out, static_cast<uint16_t>(entries.size()));
    put32(out, cdSize);
    put32(out, cdStart);
    put16(out, 0);
    return out;
}

std::string renderOdt(const MinuteDoc& d) {
    auto p = [](const std::string& text) {
        return std::string("<text:p>") + xmlEscape(text) + "</text:p>";
    };
    auto h = [](int lvl, const std::string& text) {
        return std::string("<text:h text:outline-level=\"") + std::to_string(lvl) + "\">" +
               xmlEscape(text) + "</text:h>";
    };

    std::string body;
    body += h(1, d.title);
    body += p("Data: " + d.date);
    if (!d.attendees.empty()) body += p("Partecipanti: " + d.attendees);
    if (!d.speakers.empty()) body += p("Relatori rilevati: " + d.speakers);

    for (const auto& sec : d.sections) {
        body += h(2, sec.first);
        if (sec.second.empty()) {
            body += p("(nessun elemento rilevato)");
        } else {
            for (const auto& it : sec.second) body += p("•  " + it);
        }
    }

    if (d.hasSegments) {
        body += h(2, "Trascrizione per relatori");
        for (const auto& s : d.segments) {
            std::string line = "[" + s.start + " - " + s.end + "]";
            if (!s.speaker.empty()) line += "  Relatore " + s.speaker + ":";
            line += "  " + s.content;
            body += p(line);
        }
    }

    body += h(2, "Trascrizione integrale");
    body += p(d.transcript);

    const std::string contentXml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<office:document-content "
        "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
        "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
        "office:version=\"1.2\">\n"
        "<office:body><office:text>" + body +
        "</office:text></office:body>\n"
        "</office:document-content>\n";

    const std::string stylesXml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<office:document-styles "
        "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
        "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" "
        "xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" "
        "office:version=\"1.2\">\n"
        "<office:styles>\n"
        "  <style:default-style style:family=\"paragraph\">\n"
        "    <style:text-properties fo:font-size=\"11pt\"/>\n"
        "  </style:default-style>\n"
        "</office:styles>\n"
        "</office:document-styles>\n";

    const std::string metaXml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<office:document-meta "
        "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
        "xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\" "
        "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
        "office:version=\"1.2\">\n"
        "<office:meta>\n"
        "  <meta:generator>meetingrec 2026.08.6</meta:generator>\n"
        "  <dc:title>" + xmlEscape(d.title) + "</dc:title>\n"
        "</office:meta>\n"
        "</office:document-meta>\n";

    const std::string manifestXml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<manifest:manifest "
        "xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" "
        "manifest:version=\"1.2\">\n"
        "  <manifest:file-entry manifest:full-path=\"/\" "
        "manifest:media-type=\"application/vnd.oasis.opendocument.text\"/>\n"
        "  <manifest:file-entry manifest:full-path=\"content.xml\" "
        "manifest:media-type=\"text/xml\"/>\n"
        "  <manifest:file-entry manifest:full-path=\"styles.xml\" "
        "manifest:media-type=\"text/xml\"/>\n"
        "  <manifest:file-entry manifest:full-path=\"meta.xml\" "
        "manifest:media-type=\"text/xml\"/>\n"
        "</manifest:manifest>\n";

    std::vector<std::pair<std::string, std::string>> entries;
    entries.emplace_back("mimetype", "application/vnd.oasis.opendocument.text");
    entries.emplace_back("META-INF/manifest.xml", manifestXml);
    entries.emplace_back("content.xml", contentXml);
    entries.emplace_back("styles.xml", stylesXml);
    entries.emplace_back("meta.xml", metaXml);
    return makeZip(entries);
}

}  // namespace

bool writeMinutes(const MinutesOptions& o, std::string& error) {
    MinuteDoc doc;
    if (!buildDoc(o, doc)) {
        error = "Impossibile generare la minuta";
        return false;
    }
    std::ofstream f(o.outputPath);
    if (!f) {
        error = "Impossibile aprire " + o.outputPath;
        return false;
    }
    f << renderMarkdown(doc);
    f.close();
    return true;
}

bool writeMinutesOdt(const MinutesOptions& o, std::string& error) {
    MinuteDoc doc;
    if (!buildDoc(o, doc)) {
        error = "Impossibile generare la minuta";
        return false;
    }
    const std::string odt = renderOdt(doc);
    std::ofstream f(o.outputPath, std::ios::binary);
    if (!f) {
        error = "Impossibile aprire " + o.outputPath;
        return false;
    }
    f << odt;
    f.close();
    return true;
}
