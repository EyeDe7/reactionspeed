#pragma once

#include "crypto.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#ifndef DEFAULT_SERVER_URL
#define DEFAULT_SERVER_URL "http://127.0.0.1:8787"
#endif

#ifndef DEFAULT_INVITE_CODE
#define DEFAULT_INVITE_CODE "FRIENDS-2026"
#endif

namespace net {

namespace fs = std::filesystem;

inline std::string detectOsUsername() {
    const char* u = std::getenv("USER");
    if (!u || !*u) u = std::getenv("USERNAME");
    if (u && *u) {
        std::string s(u);
        std::string clean;
        for (char c : s) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') {
                clean.push_back(c);
            }
        }
        if (clean.size() >= 2 && clean.size() <= 20) return clean;
    }
    return "";
}

struct ClientConfig {
    std::string server_url = DEFAULT_SERVER_URL;
    std::string nickname = "";
    std::string invite_code = DEFAULT_INVITE_CODE;
    std::string player_id = "";
    std::string api_token = "";
    std::string private_seed_hex = "";
    std::string public_key_hex = "";
    bool online_enabled = true;
    bool nickname_confirmed = false;

    void loadOrInit(const fs::path& path) {
        if (fs::exists(path)) {
            std::ifstream in(path);
            std::string line;
            while (std::getline(in, line)) {
                if (line.empty() || line[0] == '#' || line[0] == ';') continue;
                auto eq = line.find('=');
                if (eq == std::string::npos) continue;
                std::string k = trim(line.substr(0, eq));
                std::string v = trim(line.substr(eq + 1));
                if (k == "server_url" && !v.empty()) server_url = v;
                else if (k == "nickname" && !v.empty()) {
                    nickname = v;
                    nickname_confirmed = true;
                }
                else if (k == "invite_code" && !v.empty()) invite_code = v;
                else if (k == "player_id") player_id = v;
                else if (k == "api_token") api_token = v;
                else if (k == "private_seed_hex") private_seed_hex = v;
                else if (k == "public_key_hex") public_key_hex = v;
                else if (k == "online_enabled") online_enabled = (v == "1" || v == "true" || v == "yes");
            }
        }

        // Always prefer baked-in server_url if config had localhost placeholder
        if (server_url == "http://127.0.0.1:8787" && std::string(DEFAULT_SERVER_URL) != "http://127.0.0.1:8787") {
            server_url = DEFAULT_SERVER_URL;
        }
        if (invite_code == "FRIENDS-2026" && std::string(DEFAULT_INVITE_CODE) != "FRIENDS-2026") {
            invite_code = DEFAULT_INVITE_CODE;
        }

        if (nickname.empty()) {
            nickname = detectOsUsername();
            if (nickname.empty()) nickname = "Player";
        }

        // Ensure valid Ed25519 keypair exists locally
        uint8_t seed[32];
        if (!crypto::SHA256::fromHex(private_seed_hex, seed, 32)) {
            std::random_device rd;
            for (int i = 0; i < 32; ++i) {
                seed[i] = static_cast<uint8_t>(rd() & 0xFFu);
            }
            private_seed_hex = crypto::SHA256::toHex(seed, 32);
        }
        uint8_t pk[32];
        crypto::ed25519::derivePublicKey(seed, pk);
        public_key_hex = crypto::SHA256::toHex(pk, 32);

        while (!server_url.empty() && server_url.back() == '/') {
            server_url.pop_back();
        }
    }

    void save(const fs::path& path) const {
        std::ofstream out(path, std::ios::trunc);
        out << "# Automatically managed identity cache (server_url & invite_code are baked into the binary)\n";
        out << "online_enabled=" << (online_enabled ? "true" : "false") << "\n";
        out << "server_url=" << server_url << "\n";
        out << "nickname=" << nickname << "\n";
        out << "invite_code=" << invite_code << "\n";
        out << "player_id=" << player_id << "\n";
        out << "api_token=" << api_token << "\n";
        out << "public_key_hex=" << public_key_hex << "\n";
        out << "private_seed_hex=" << private_seed_hex << "\n";
    }

    static std::string trim(std::string s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
        return s;
    }
};

struct RemoteScoreEntry {
    std::string nickname;
    double ms = 0.0;
    double best_ms = 0.0;
    double avg_ms = 0.0;
    std::string timestamp;
    bool signature_valid = true;
};

struct ChallengeTicket {
    bool valid = false;
    std::string challenge_id;
    std::string challenge_nonce;
    std::vector<int> wait_delays_ms;
    std::string error_msg;
};

struct LeaderboardData {
    bool connected = false;
    bool ledger_intact = true;
    int total_verified_entries = 0;
    std::string status_text = "Offline";
    std::vector<RemoteScoreEntry> top_singles;
    std::vector<RemoteScoreEntry> top_averages;
    std::vector<RemoteScoreEntry> friend_bests;
};

namespace json_util {

inline std::string escapeJson(const std::string& s) {
    std::ostringstream oss;
    for (char c : s) {
        if (c == '"') oss << "\\\"";
        else if (c == '\\') oss << "\\\\";
        else if (c == '\n') oss << "\\n";
        else if (c == '\r') oss << "\\r";
        else oss << c;
    }
    return oss.str();
}

inline std::string getString(const std::string& json, const std::string& key, size_t start_pos = 0) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle, start_pos);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return "";
    size_t end = pos + 1;
    std::string out;
    while (end < json.size()) {
        if (json[end] == '\\' && end + 1 < json.size()) {
            out.push_back(json[end + 1]);
            end += 2;
        } else if (json[end] == '"') {
            break;
        } else {
            out.push_back(json[end]);
            end++;
        }
    }
    return out;
}

inline double getNumber(const std::string& json, const std::string& key, double def = 0.0, size_t start_pos = 0) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle, start_pos);
    if (pos == std::string::npos) return def;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return def;
    pos++;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) pos++;
    size_t end = pos;
    while (end < json.size() && (std::isdigit(static_cast<unsigned char>(json[end])) || json[end] == '.' || json[end] == '-')) {
        end++;
    }
    if (end == pos) return def;
    try {
        return std::stod(json.substr(pos, end - pos));
    } catch (...) {
        return def;
    }
}

inline bool getBool(const std::string& json, const std::string& key, bool def = false, size_t start_pos = 0) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle, start_pos);
    if (pos == std::string::npos) return def;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return def;
    pos++;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) pos++;
    if (json.compare(pos, 4, "true") == 0) return true;
    if (json.compare(pos, 5, "false") == 0) return false;
    return def;
}

inline std::vector<int> getIntArray(const std::string& json, const std::string& key) {
    std::vector<int> res;
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return res;
    size_t open_b = json.find('[', pos);
    size_t close_b = json.find(']', open_b);
    if (open_b == std::string::npos || close_b == std::string::npos) return res;
    std::string inner = json.substr(open_b + 1, close_b - open_b - 1);
    std::stringstream ss(inner);
    std::string item;
    while (std::getline(ss, item, ',')) {
        try {
            res.push_back(std::stoi(item));
        } catch (...) {}
    }
    return res;
}

inline std::vector<RemoteScoreEntry> getScoreArray(const std::string& json, const std::string& key) {
    std::vector<RemoteScoreEntry> out;
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return out;
    size_t open_b = json.find('[', pos);
    if (open_b == std::string::npos) return out;

    int depth = 0;
    size_t close_b = std::string::npos;
    for (size_t i = open_b; i < json.size(); ++i) {
        if (json[i] == '[') depth++;
        else if (json[i] == ']') {
            depth--;
            if (depth == 0) {
                close_b = i;
                break;
            }
        }
    }
    if (close_b == std::string::npos) return out;

    size_t cur = open_b + 1;
    while (cur < close_b) {
        size_t obj_start = json.find('{', cur);
        if (obj_start == std::string::npos || obj_start >= close_b) break;
        size_t obj_end = json.find('}', obj_start);
        if (obj_end == std::string::npos || obj_end >= close_b) break;

        std::string obj = json.substr(obj_start, obj_end - obj_start + 1);
        RemoteScoreEntry e;
        e.nickname = getString(obj, "nickname");
        e.ms = getNumber(obj, "ms", 0.0);
        e.best_ms = getNumber(obj, "best_ms", e.ms);
        e.avg_ms = getNumber(obj, "avg_ms", e.ms);
        e.timestamp = getString(obj, "timestamp");
        e.signature_valid = getBool(obj, "signature_valid", true);
        if (!e.nickname.empty() && e.ms > 0.0) {
            out.push_back(e);
        }
        cur = obj_end + 1;
    }
    return out;
}

} // namespace json_util

struct ParsedUrl {
    bool https = false;
    std::string host;
    int port = 80;
    std::string path = "/";
};

inline ParsedUrl parseUrl(const std::string& full_url) {
    ParsedUrl u;
    std::string work = full_url;
    if (work.rfind("https://", 0) == 0) {
        u.https = true;
        u.port = 443;
        work = work.substr(8);
    } else if (work.rfind("http://", 0) == 0) {
        u.https = false;
        u.port = 80;
        work = work.substr(7);
    }
    size_t slash = work.find('/');
    std::string host_port = (slash == std::string::npos) ? work : work.substr(0, slash);
    u.path = (slash == std::string::npos) ? "/" : work.substr(slash);

    size_t colon = host_port.find(':');
    if (colon != std::string::npos) {
        u.host = host_port.substr(0, colon);
        try {
            u.port = std::stoi(host_port.substr(colon + 1));
        } catch (...) {}
    } else {
        u.host = host_port;
    }
    return u;
}

inline bool httpRequest(const std::string& method,
                        const std::string& full_url,
                        const std::string& body,
                        const std::map<std::string, std::string>& headers,
                        int& status_code,
                        std::string& response_body) {
    status_code = 0;
    response_body.clear();
    ParsedUrl u = parseUrl(full_url);
    if (u.host.empty()) return false;

#ifdef _WIN32
    HINTERNET hSession = WinHttpOpen(L"ReactionBenchmark/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;
    WinHttpSetTimeouts(hSession, 2500, 2500, 2500, 2500);

    std::wstring whost(u.host.begin(), u.host.end());
    HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(), static_cast<INTERNET_PORT>(u.port), 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::wstring wmethod(method.begin(), method.end());
    std::wstring wpath(u.path.begin(), u.path.end());
    DWORD flags = u.https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, wmethod.c_str(), wpath.c_str(),
                                            nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::wstring wheaders = L"Content-Type: application/json\r\n";
    for (const auto& kv : headers) {
        std::string hline = kv.first + ": " + kv.second + "\r\n";
        wheaders += std::wstring(hline.begin(), hline.end());
    }

    BOOL bResults = WinHttpSendRequest(
        hRequest,
        wheaders.c_str(), static_cast<DWORD>(wheaders.size()),
        body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
        static_cast<DWORD>(body.size()),
        static_cast<DWORD>(body.size()), 0
    );

    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, nullptr);
    }
    if (bResults) {
        DWORD dwStatusCode = 0;
        DWORD dwSize = sizeof(dwStatusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);
        status_code = static_cast<int>(dwStatusCode);

        DWORD dwDownloaded = 0;
        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize) || dwSize == 0) break;
            std::vector<char> buf(dwSize);
            if (WinHttpReadData(hRequest, buf.data(), dwSize, &dwDownloaded) && dwDownloaded > 0) {
                response_body.append(buf.data(), dwDownloaded);
            }
        } while (dwSize > 0);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return bResults == TRUE;
#else
    if (u.https) {
        std::ostringstream cmd;
        cmd << "curl -sS -m 3 -X " << method << " -H 'Content-Type: application/json'";
        for (const auto& kv : headers) {
            cmd << " -H '" << kv.first << ": " << kv.second << "'";
        }
        if (!body.empty()) {
            std::string esc;
            for (char c : body) {
                if (c == '\'') esc += "'\\''";
                else esc += c;
            }
            cmd << " --data-raw '" << esc << "'";
        }
        cmd << " -w '\\n%{http_code}' '" << full_url << "' 2>/dev/null";

        FILE* pipe = popen(cmd.str().c_str(), "r");
        if (!pipe) return false;
        char buffer[1024];
        std::string full_out;
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            full_out += buffer;
        }
        int rc = pclose(pipe);
        if (rc != 0 && full_out.empty()) return false;

        size_t last_nl = full_out.find_last_of('\n');
        if (last_nl != std::string::npos) {
            try {
                status_code = std::stoi(full_out.substr(last_nl + 1));
                response_body = full_out.substr(0, last_nl);
                return status_code > 0;
            } catch (...) {}
        }
        return false;
    }

    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    std::string port_str = std::to_string(u.port);
    if (getaddrinfo(u.host.c_str(), port_str.c_str(), &hints, &res) != 0 || !res) {
        return false;
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(res);
        return false;
    }

    struct timeval tv{};
    tv.tv_sec = 2;
    tv.tv_usec = 500000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        close(sock);
        freeaddrinfo(res);
        return false;
    }
    freeaddrinfo(res);

    std::ostringstream req;
    req << method << " " << u.path << " HTTP/1.1\r\n"
        << "Host: " << u.host << ":" << u.port << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Connection: close\r\n";
    for (const auto& kv : headers) {
        req << kv.first << ": " << kv.second << "\r\n";
    }
    req << "Content-Length: " << body.size() << "\r\n\r\n" << body;

    std::string req_str = req.str();
    size_t total_sent = 0;
    while (total_sent < req_str.size()) {
        ssize_t n = send(sock, req_str.data() + total_sent, req_str.size() - total_sent, 0);
        if (n <= 0) {
            close(sock);
            return false;
        }
        total_sent += static_cast<size_t>(n);
    }

    std::string raw_resp;
    char buf[4096];
    ssize_t n = 0;
    while ((n = recv(sock, buf, sizeof(buf), 0)) > 0) {
        raw_resp.append(buf, static_cast<size_t>(n));
    }
    close(sock);

    size_t hdr_end = raw_resp.find("\r\n\r\n");
    if (hdr_end == std::string::npos) return false;
    std::string hdr = raw_resp.substr(0, hdr_end);
    response_body = raw_resp.substr(hdr_end + 4);

    std::istringstream iss(hdr);
    std::string http_ver;
    iss >> http_ver >> status_code;
    return status_code > 0;
#endif
}

class FriendsScoreClient {
public:
    FriendsScoreClient(ClientConfig& cfg, const fs::path& cfg_path, std::string binary_sha256)
        : cfg_(cfg), cfg_path_(cfg_path), binary_sha256_(std::move(binary_sha256)) {}

    void resetRegistrationForNewName(const std::string& new_nickname) {
        cfg_.nickname = new_nickname;
        cfg_.nickname_confirmed = true;
        cfg_.player_id.clear();
        cfg_.api_token.clear();
        cfg_.save(cfg_path_);
    }

    bool ensureRegistered() {
        if (!cfg_.online_enabled) return false;
        if (!cfg_.player_id.empty() && !cfg_.api_token.empty()) return true;

        std::ostringstream body;
        body << "{"
             << "\"nickname\":\"" << json_util::escapeJson(cfg_.nickname) << "\","
             << "\"invite_code\":\"" << json_util::escapeJson(cfg_.invite_code) << "\","
             << "\"public_key\":\"" << cfg_.public_key_hex << "\","
             << "\"binary_sha256\":\"" << binary_sha256_ << "\""
             << "}";

        int status = 0;
        std::string resp;
        if (!httpRequest("POST", cfg_.server_url + "/api/v1/auth/register", body.str(), {}, status, resp)) {
            last_error_ = "Server unreachable";
            return false;
        }
        if (status != 200) {
            last_error_ = json_util::getString(resp, "error");
            if (last_error_.empty()) last_error_ = "Auth failed (" + std::to_string(status) + ")";
            return false;
        }
        cfg_.player_id = json_util::getString(resp, "player_id");
        cfg_.api_token = json_util::getString(resp, "api_token");
        cfg_.save(cfg_path_);
        return !cfg_.player_id.empty();
    }

    ChallengeTicket requestChallenge() {
        ChallengeTicket ticket;
        if (!ensureRegistered()) {
            ticket.error_msg = last_error_;
            return ticket;
        }

        uint8_t seed[32];
        crypto::SHA256::fromHex(cfg_.private_seed_hex, seed, 32);

        std::string ts = std::to_string(std::time(nullptr));
        std::string msg_to_sign = "CHALLENGE|" + cfg_.player_id + "|" + ts + "|" + binary_sha256_;
        std::string ed_sig = crypto::ed25519::signMessageHex(seed, msg_to_sign);

        std::ostringstream body;
        body << "{"
             << "\"player_id\":\"" << cfg_.player_id << "\","
             << "\"timestamp\":" << ts << ","
             << "\"binary_sha256\":\"" << binary_sha256_ << "\","
             << "\"ed25519_signature\":\"" << ed_sig << "\""
             << "}";

        std::map<std::string, std::string> hdrs = {
            {"X-Player-Id", cfg_.player_id},
            {"X-Api-Token", cfg_.api_token},
            {"X-Binary-Sha256", binary_sha256_}
        };

        int status = 0;
        std::string resp;
        if (!httpRequest("POST", cfg_.server_url + "/api/v1/challenge", body.str(), hdrs, status, resp) || status != 200) {
            ticket.error_msg = json_util::getString(resp, "error");
            if (ticket.error_msg.empty()) ticket.error_msg = "Challenge failed";
            return ticket;
        }

        ticket.challenge_id = json_util::getString(resp, "challenge_id");
        ticket.challenge_nonce = json_util::getString(resp, "challenge_nonce");
        ticket.wait_delays_ms = json_util::getIntArray(resp, "wait_delays_ms");
        ticket.valid = !ticket.challenge_id.empty() && ticket.wait_delays_ms.size() == 5;
        return ticket;
    }

    bool submitVerifiedSet(const ChallengeTicket& ticket,
                           const std::vector<double>& rounds_ms,
                           const std::vector<int>& used_delays_ms,
                           std::string& out_status_msg) {
        if (!ticket.valid || rounds_ms.size() != 5 || used_delays_ms.size() != 5) {
            out_status_msg = "Offline / No active server challenge";
            return false;
        }

        double sum = 0.0;
        double best = rounds_ms[0];
        for (double r : rounds_ms) {
            sum += r;
            best = std::min(best, r);
        }
        double avg = sum / 5.0;

        std::ostringstream r_ss;
        r_ss << std::fixed << std::setprecision(3);
        for (size_t i = 0; i < 5; ++i) {
            if (i > 0) r_ss << ",";
            r_ss << rounds_ms[i];
        }

        std::ostringstream w_ss;
        for (size_t i = 0; i < 5; ++i) {
            if (i > 0) w_ss << ",";
            w_ss << used_delays_ms[i];
        }

        std::ostringstream canon;
        canon << "v1|" << cfg_.player_id << "|" << cfg_.nickname << "|"
              << ticket.challenge_id << "|" << ticket.challenge_nonce << "|"
              << std::fixed << std::setprecision(3) << avg << "|" << best << "|"
              << r_ss.str() << "|" << w_ss.str() << "|" << binary_sha256_;
        std::string canonical_payload = canon.str();

        uint8_t seed[32];
        crypto::SHA256::fromHex(cfg_.private_seed_hex, seed, 32);
        std::string ed25519_sig = crypto::ed25519::signMessageHex(seed, canonical_payload);

        std::string attest_key = crypto::hmacSha256Hex(
            crypto::getProtocolAttestationSecret(),
            binary_sha256_ + ":" + ticket.challenge_nonce + ":" + cfg_.api_token
        );
        std::string attestation_hmac = crypto::hmacSha256Hex(attest_key, canonical_payload);

        std::ostringstream body;
        body << "{"
             << "\"player_id\":\"" << cfg_.player_id << "\","
             << "\"challenge_id\":\"" << ticket.challenge_id << "\","
             << "\"avg_ms\":" << std::fixed << std::setprecision(3) << avg << ","
             << "\"best_ms\":" << std::fixed << std::setprecision(3) << best << ","
             << "\"rounds_ms\":[" << r_ss.str() << "],"
             << "\"wait_delays_ms\":[" << w_ss.str() << "],"
             << "\"binary_sha256\":\"" << binary_sha256_ << "\","
             << "\"canonical_payload\":\"" << json_util::escapeJson(canonical_payload) << "\","
             << "\"ed25519_signature\":\"" << ed25519_sig << "\","
             << "\"attestation_hmac\":\"" << attestation_hmac << "\""
             << "}";

        std::map<std::string, std::string> hdrs = {
            {"X-Player-Id", cfg_.player_id},
            {"X-Api-Token", cfg_.api_token},
            {"X-Binary-Sha256", binary_sha256_}
        };

        int status = 0;
        std::string resp;
        if (!httpRequest("POST", cfg_.server_url + "/api/v1/submit", body.str(), hdrs, status, resp)) {
            out_status_msg = "Server unreachable";
            return false;
        }
        if (status != 200) {
            std::string err = json_util::getString(resp, "error");
            out_status_msg = "Rejected: " + (err.empty() ? std::to_string(status) : err);
            return false;
        }

        std::string entry_hash = json_util::getString(resp, "entry_hash");
        out_status_msg = "Verified & Signed (#" + entry_hash.substr(0, 8) + ")";
        return true;
    }

    LeaderboardData fetchLeaderboard() {
        LeaderboardData lb;
        if (!cfg_.online_enabled) {
            lb.status_text = "Offline Mode";
            return lb;
        }

        int status = 0;
        std::string resp;
        if (!httpRequest("GET", cfg_.server_url + "/api/v1/leaderboard", "", {}, status, resp) || status != 200) {
            lb.status_text = "Offline (" + cfg_.server_url + ")";
            return lb;
        }

        lb.connected = true;
        lb.ledger_intact = json_util::getBool(resp, "ledger_intact", true);
        lb.total_verified_entries = static_cast<int>(json_util::getNumber(resp, "total_entries", 0));
        lb.top_singles = json_util::getScoreArray(resp, "top_singles");
        lb.top_averages = json_util::getScoreArray(resp, "top_averages");
        lb.friend_bests = json_util::getScoreArray(resp, "friend_bests");

        if (lb.ledger_intact) {
            lb.status_text = "Connected (" + std::to_string(lb.total_verified_entries) + " signed scores, Chain Verified)";
        } else {
            lb.status_text = "WARNING: SERVER DATABASE TAMPER DETECTED!";
        }
        return lb;
    }

    const std::string& binarySha256() const { return binary_sha256_; }

private:
    ClientConfig& cfg_;
    fs::path cfg_path_;
    std::string binary_sha256_;
    std::string last_error_;
};

} // namespace net
