#include "config.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace j {

// Mini parser/writer JSON (solo l'essenziale, nessuna dipendenza).
struct Value {
    enum Type { Null, Bool, Number, String, Object, Array };
    Type type = Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<std::pair<std::string, Value>> obj;
    std::vector<Value> arr;

    const Value* find(const std::string& k) const {
        for (const auto& p : obj)
            if (p.first == k) return &p.second;
        return nullptr;
    }
    std::string getString(const std::string& k, const std::string& d = "") const {
        const Value* v = find(k);
        return (v && v->type == String) ? v->str : d;
    }
    double getNumber(const std::string& k, double d = 0.0) const {
        const Value* v = find(k);
        return (v && v->type == Number) ? v->num : d;
    }
};

struct Parser {
    const std::string& s;
    size_t i = 0;
    explicit Parser(const std::string& text) : s(text) {}

    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }
    bool parse(Value& out) { ws(); return parseValue(out); }

    bool parseValue(Value& out) {
        ws();
        if (i >= s.size()) return false;
        const char c = s[i];
        if (c == '{') return parseObject(out);
        if (c == '[') return parseArray(out);
        if (c == '"') return parseString(out);
        if (c == 't' || c == 'f') return parseBool(out);
        if (c == 'n') {
            if (s.compare(i, 4, "null") == 0) { i += 4; out.type = Value::Null; return true; }
            return false;
        }
        return parseNumber(out);
    }

    bool parseObject(Value& out) {
        out.type = Value::Object;
        ++i;  // {
        ws();
        if (i < s.size() && s[i] == '}') { ++i; return true; }
        for (;;) {
            ws();
            Value k;
            if (!parseString(k)) return false;
            ws();
            if (i >= s.size() || s[i] != ':') return false;
            ++i;
            Value v;
            if (!parseValue(v)) return false;
            out.obj.emplace_back(k.str, std::move(v));
            ws();
            if (i < s.size() && s[i] == ',') { ++i; continue; }
            if (i < s.size() && s[i] == '}') { ++i; return true; }
            return false;
        }
    }

    bool parseArray(Value& out) {
        out.type = Value::Array;
        ++i;  // [
        ws();
        if (i < s.size() && s[i] == ']') { ++i; return true; }
        for (;;) {
            Value v;
            if (!parseValue(v)) return false;
            out.arr.push_back(std::move(v));
            ws();
            if (i < s.size() && s[i] == ',') { ++i; continue; }
            if (i < s.size() && s[i] == ']') { ++i; return true; }
            return false;
        }
    }

    bool parseString(Value& out) {
        if (i >= s.size() || s[i] != '"') return false;
        ++i;
        std::string r;
        while (i < s.size()) {
            const char c = s[i];
            if (c == '"') { ++i; out.type = Value::String; out.str = r; return true; }
            if (c == '\\' && i + 1 < s.size()) {
                const char n = s[i + 1];
                if (n == 'n') r += '\n';
                else if (n == 't') r += '\t';
                else if (n == 'r') r += '\r';
                else if (n == '"') r += '"';
                else if (n == '\\') r += '\\';
                else if (n == '/') r += '/';
                else if (n == 'b') r += '\b';
                else if (n == 'f') r += '\f';
                else if (n == 'u') {
                    if (i + 6 > s.size()) return false;
                    unsigned code = 0;
                    for (int k = 0; k < 4; ++k) {
                        const char h = s[i + 2 + k];
                        code <<= 4;
                        if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
                        else return false;
                    }
                    if (code < 0x80) r += static_cast<char>(code);
                    else if (code < 0x800) {
                        r += static_cast<char>(0xC0 | (code >> 6));
                        r += static_cast<char>(0x80 | (code & 0x3F));
                    } else {
                        r += static_cast<char>(0xE0 | (code >> 12));
                        r += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                        r += static_cast<char>(0x80 | (code & 0x3F));
                    }
                    i += 6;
                    continue;
                } else {
                    return false;
                }
                i += 2;
                continue;
            }
            if (c == '\n' || c == '\r') return false;
            r += c;
            ++i;
        }
        return false;
    }

    bool parseNumber(Value& out) {
        const size_t start = i;
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
        while (i < s.size() &&
               (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.' ||
                s[i] == 'e' || s[i] == 'E' || s[i] == '-' || s[i] == '+'))
            ++i;
        if (i == start) return false;
        out.type = Value::Number;
        out.num = std::atof(s.substr(start, i - start).c_str());
        return true;
    }

    bool parseBool(Value& out) {
        if (s.compare(i, 4, "true") == 0) { i += 4; out.type = Value::Bool; out.b = true; return true; }
        if (s.compare(i, 5, "false") == 0) { i += 5; out.type = Value::Bool; out.b = false; return true; }
        return false;
    }
};

bool parse(const std::string& text, Value& out) {
    Parser p(text);
    return p.parse(out);
}

std::string escape(const std::string& s) {
    std::string r;
    for (char c : s) {
        switch (c) {
            case '"': r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n"; break;
            case '\t': r += "\\t"; break;
            case '\r': r += "\\r"; break;
            default: r += c;
        }
    }
    return r;
}

void serialize(const Value& v, std::string& out, int depth) {
    const std::string ind(static_cast<size_t>(depth) * 2, ' ');
    switch (v.type) {
        case Value::Null: out += "null"; break;
        case Value::Bool: out += v.b ? "true" : "false"; break;
        case Value::Number: {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%g", v.num);
            out += buf;
            break;
        }
        case Value::String: out += "\"" + escape(v.str) + "\""; break;
        case Value::Array: {
            out += "[";
            for (size_t i = 0; i < v.arr.size(); ++i) {
                if (i) out += ", ";
                serialize(v.arr[i], out, depth + 1);
            }
            out += "]";
            break;
        }
        case Value::Object: {
            if (v.obj.empty()) { out += "{}"; break; }
            out += "{\n";
            for (size_t i = 0; i < v.obj.size(); ++i) {
                out += ind + "  \"" + escape(v.obj[i].first) + "\": ";
                serialize(v.obj[i].second, out, depth + 1);
                if (i + 1 < v.obj.size()) out += ",";
                out += "\n";
            }
            out += ind + "}";
            break;
        }
    }
}

std::string serialize(const Value& v) {
    std::string out;
    serialize(v, out, 0);
    return out;
}

}  // namespace j

bool Config::load() {
    std::ifstream f(path);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();

    j::Value root;
    if (!j::parse(ss.str(), root) || root.type != j::Value::Object) return false;

    mode = root.getString("mode", mode);
    apiUrl = root.getString("api_url", apiUrl);
    apiKey = root.getString("api_key", apiKey);
    apiModel = root.getString("api_model", apiModel);
    language = root.getString("language", language);
    responseKey = root.getString("response_key", responseKey);
    vibeasrBin = root.getString("vibeasr_bin", vibeasrBin);
    vibeasrVae = root.getString("vibeasr_vae", vibeasrVae);
    vibeasrLm = root.getString("vibeasr_lm", vibeasrLm);
    vibeasrThreads = static_cast<int>(root.getNumber("vibeasr_threads", vibeasrThreads));
    vibeasrContext = root.getString("vibeasr_context", vibeasrContext);
    vibeasrFormat = root.getString("vibeasr_format", vibeasrFormat);
    vibeasrCtx = static_cast<int>(root.getNumber("vibeasr_ctx", vibeasrCtx));
    vibeasrChunkSec = static_cast<int>(root.getNumber("vibeasr_chunk_sec", vibeasrChunkSec));
    outputDir = root.getString("output_dir", outputDir);
    title = root.getString("title", title);
    loaded = true;
    return true;
}

bool Config::save() const {
    j::Value root;
    root.type = j::Value::Object;
    const auto put = [&root](const char* k, const std::string& v) {
        j::Value x;
        x.type = j::Value::String;
        x.str = v;
        root.obj.emplace_back(k, std::move(x));
    };
    put("mode", mode);
    put("api_url", apiUrl);
    put("api_key", apiKey);
    put("api_model", apiModel);
    put("language", language);
    put("response_key", responseKey);
    put("vibeasr_bin", vibeasrBin);
    put("vibeasr_vae", vibeasrVae);
    put("vibeasr_lm", vibeasrLm);
    {
        j::Value x;
        x.type = j::Value::Number;
        x.num = vibeasrThreads;
        root.obj.emplace_back("vibeasr_threads", std::move(x));
    }
    put("vibeasr_context", vibeasrContext);
    put("vibeasr_format", vibeasrFormat);
    {
        j::Value x;
        x.type = j::Value::Number;
        x.num = vibeasrCtx;
        root.obj.emplace_back("vibeasr_ctx", std::move(x));
    }
    {
        j::Value x;
        x.type = j::Value::Number;
        x.num = vibeasrChunkSec;
        root.obj.emplace_back("vibeasr_chunk_sec", std::move(x));
    }
    put("output_dir", outputDir);
    put("title", title);

    std::ofstream f(path);
    if (!f) return false;
    f << j::serialize(root) << "\n";
    return true;
}
