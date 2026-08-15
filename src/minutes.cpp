#include "minutes.h"

#include <cctype>
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

}  // namespace

bool writeMinutes(const MinutesOptions& o, std::string& error) {
    std::ofstream f(o.outputPath);
    if (!f) {
        error = "Impossibile aprire " + o.outputPath;
        return false;
    }

    const std::string date = o.date.empty() ? today() : o.date;
    const std::vector<Segment> segments = parseSegments(o.transcriptText);

    // Analisi euristica sul testo (solo i contenuti se ci sono segmenti strutturati).
    std::string analysisText = o.transcriptText;
    if (!segments.empty()) {
        std::string joined;
        for (const auto& s : segments) {
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

    f << "# " << o.title << "\n\n";
    f << "- **Data:** " << date << "\n";
    if (!o.attendees.empty()) f << "- **Partecipanti:** " << o.attendees << "\n";
    if (!segments.empty()) {
        std::set<std::string> speakers;
        for (const auto& s : segments)
            if (!s.speaker.empty()) speakers.insert(s.speaker);
        if (!speakers.empty()) {
            std::string joined;
            for (auto it = speakers.begin(); it != speakers.end(); ++it) {
                if (it != speakers.begin()) joined += ", ";
                joined += *it;
            }
            f << "- **Relatori rilevati:** " << joined << "\n";
        }
    }
    f << "\n";

    const auto section = [&f](const char* title, const std::vector<std::string>& items) {
        f << "## " << title << "\n\n";
        if (items.empty()) {
            f << "- _(nessun elemento rilevato)_\n\n";
        } else {
            for (const auto& i : items) f << "- " << i << "\n";
            f << "\n";
        }
    };

    section("Punti discussi", topics);
    section("Decisioni", decisions);
    section("Azioni / To-do", actions);
    section("Rischi / Blocker", risks);

    if (!segments.empty()) {
        f << "## Trascrizione per relatori\n\n";
        for (const auto& s : segments) {
            f << "- [" << s.start << " - " << s.end << "]";
            if (!s.speaker.empty()) f << " **Relatore " << s.speaker << "**:";
            f << " " << s.content << "\n";
        }
        f << "\n";
    }

    f << "## Trascrizione integrale\n\n";
    f << o.transcriptText << "\n";

    f.close();
    return true;
}
