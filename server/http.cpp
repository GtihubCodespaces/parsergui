#include "http.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#include "../agents/exec.h"

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
#define PGR_INVALID INVALID_SOCKET
#define pgr_close closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
#define PGR_INVALID (-1)
#define pgr_close close
#endif

namespace pgr {
namespace {

double now_ms() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double, std::milli>(clock::now().time_since_epoch()).count();
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string temp_path(const std::string& suffix) {
    std::error_code ec;
    auto dir = std::filesystem::temp_directory_path(ec);
    if (ec) dir = std::filesystem::current_path(ec);
    std::random_device rd;
    char name[64];
    std::snprintf(name, sizeof(name), "pgr_%08x_%08x%s", rd(), rd(), suffix.c_str());
    return (dir / name).string();
}

struct ParsedUrl {
    std::string scheme, host, port, path;
    bool valid = false;
};

ParsedUrl parse_url(const std::string& url) {
    ParsedUrl out;
    const size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos) return out;
    out.scheme = lower(url.substr(0, scheme_end));
    const size_t host_start = scheme_end + 3;
    const size_t path_start = url.find('/', host_start);
    std::string authority = url.substr(host_start, path_start == std::string::npos ? std::string::npos : path_start - host_start);
    out.path = path_start == std::string::npos ? "/" : url.substr(path_start);
    const size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
        out.host = authority.substr(0, colon);
        out.port = authority.substr(colon + 1);
    } else {
        out.host = authority;
        out.port = out.scheme == "https" ? "443" : "80";
    }
    out.valid = !out.host.empty();
    return out;
}

}  // namespace

std::string url_encode(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out.push_back(static_cast<char>(c));
        else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

std::string url_decode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            out.push_back(static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16)));
            i += 2;
        } else if (s[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

std::map<std::string, std::string> parse_query(const std::string& query) {
    std::map<std::string, std::string> out;
    size_t start = 0;
    while (start <= query.size()) {
        const size_t amp = query.find('&', start);
        const std::string pair = query.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
        if (!pair.empty()) {
            const size_t eq = pair.find('=');
            if (eq == std::string::npos)
                out[url_decode(pair)] = "";
            else
                out[url_decode(pair.substr(0, eq))] = url_decode(pair.substr(eq + 1));
        }
        if (amp == std::string::npos) break;
        start = amp + 1;
    }
    return out;
}

std::string mime_for(const std::string& path) {
    const size_t dot = path.rfind('.');
    const std::string ext = dot == std::string::npos ? "" : lower(path.substr(dot + 1));
    if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
    if (ext == "css") return "text/css; charset=utf-8";
    if (ext == "js" || ext == "mjs") return "application/javascript; charset=utf-8";
    if (ext == "json") return "application/json; charset=utf-8";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "txt" || ext == "md" || ext == "cpp" || ext == "h") return "text/plain; charset=utf-8";
    if (ext == "ico") return "image/x-icon";
    return "application/octet-stream";
}

std::string base64_encode(const std::string& bytes) {
    static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const uint32_t v = (uint8_t(bytes[i]) << 16) | (uint8_t(bytes[i + 1]) << 8) | uint8_t(bytes[i + 2]);
        out.push_back(k[(v >> 18) & 63]);
        out.push_back(k[(v >> 12) & 63]);
        out.push_back(k[(v >> 6) & 63]);
        out.push_back(k[v & 63]);
    }
    if (i < bytes.size()) {
        uint32_t v = uint8_t(bytes[i]) << 16;
        if (i + 1 < bytes.size()) v |= uint8_t(bytes[i + 1]) << 8;
        out.push_back(k[(v >> 18) & 63]);
        out.push_back(k[(v >> 12) & 63]);
        out.push_back(i + 1 < bytes.size() ? k[(v >> 6) & 63] : '=');
        out.push_back('=');
    }
    return out;
}

std::string base64_decode(const std::string& text) {
    static const std::string k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int bits = 0;
    uint32_t acc = 0;
    for (char c : text) {
        const size_t idx = k.find(c);
        if (idx == std::string::npos) continue;
        acc = (acc << 6) | static_cast<uint32_t>(idx);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((acc >> bits) & 0xFF));
        }
    }
    return out;
}

HttpResponse http_request(const HttpRequest& request) {
    HttpResponse response;
    const double started = now_ms();
    const ParsedUrl url = parse_url(request.url);
    if (!url.valid) {
        response.error = "invalid URL: " + request.url;
        return response;
    }
    if (request.url.rfind("file://", 0) == 0) {
        std::ifstream in(request.url.substr(7), std::ios::binary);
        if (!in) {
            response.error = "cannot open file";
            return response;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        response.status = 200;
        response.body = ss.str();
        return response;
    }

    const std::string body_file = request.body.empty() ? "" : temp_path(".body");
    const std::string out_file = temp_path(".out");
    if (!body_file.empty()) {
        std::ofstream out(body_file, std::ios::binary);
        out.write(request.body.data(), static_cast<std::streamsize>(request.body.size()));
    }

    std::vector<std::string> argv{"curl", "-sS", "-L", "--max-time",
                                  std::to_string(std::max(1, request.timeout_ms / 1000)), "-o", out_file, "-w",
                                  "%{http_code}"};
    if (request.method != "GET") argv.push_back("-X"), argv.push_back(request.method);
    for (const auto& kv : request.headers) {
        argv.push_back("-H");
        argv.push_back(kv.first + ": " + kv.second);
    }
    if (!body_file.empty()) {
        argv.push_back("--data-binary");
        argv.push_back("@" + body_file);
    }
    argv.push_back(request.url);

    ProcessResult proc = run_process(argv, "", request.timeout_ms + 5000);
    if (!proc.spawned) {
        response.error = "curl is required for HTTP requests and was not found on PATH";
    } else if (!proc.err.empty() && proc.out.empty()) {
        response.error = trim(proc.err);
    } else {
        response.status = std::atoi(trim(proc.out).c_str());
        if (response.status == 0) response.status = 502;
        std::ifstream in(out_file, std::ios::binary);
        if (in) {
            std::ostringstream ss;
            ss << in.rdbuf();
            response.body = ss.str();
        }
        if (!proc.err.empty()) response.error = trim(proc.err);
        if (proc.timed_out) response.error = "request timed out";
    }
    std::error_code ec;
    std::filesystem::remove(out_file, ec);
    if (!body_file.empty()) std::filesystem::remove(body_file, ec);
    response.elapsed_ms = now_ms() - started;
    return response;
}

bool parse_multipart(const std::string& content_type, const std::string& body, std::vector<HttpPart>* parts) {
    if (!parts) return false;
    parts->clear();
    const std::string lower_ct = lower(content_type);
    const size_t bpos = lower_ct.find("boundary=");
    if (bpos == std::string::npos) return false;
    std::string boundary = content_type.substr(bpos + 9);
    if (!boundary.empty() && boundary.front() == '"') {
        const size_t end = boundary.find('"', 1);
        boundary = boundary.substr(1, end == std::string::npos ? std::string::npos : end - 1);
    } else {
        const size_t semi = boundary.find(';');
        if (semi != std::string::npos) boundary = boundary.substr(0, semi);
    }
    if (boundary.empty()) return false;
    const std::string delimiter = "--" + boundary;
    size_t pos = body.find(delimiter);
    if (pos == std::string::npos) return false;
    pos += delimiter.size();
    while (pos < body.size()) {
        if (body.compare(pos, 2, "--") == 0) break;
        if (body.compare(pos, 2, "\r\n") == 0) pos += 2;
        const size_t header_end = body.find("\r\n\r\n", pos);
        if (header_end == std::string::npos) break;
        const std::string raw_headers = body.substr(pos, header_end - pos);
        const size_t data_start = header_end + 4;
        const size_t next = body.find(delimiter, data_start);
        if (next == std::string::npos) break;
        size_t data_end = next;
        if (data_end >= 2 && body.compare(data_end - 2, 2, "\r\n") == 0) data_end -= 2;

        HttpPart part;
        std::istringstream hs(raw_headers);
        std::string line;
        while (std::getline(hs, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const std::string l = lower(line);
            if (l.rfind("content-disposition:", 0) == 0) {
                const size_t n = l.find("name=");
                if (n != std::string::npos) {
                    size_t s = n + 5;
                    if (s < line.size() && line[s] == '"') {
                        const size_t e = line.find('"', s + 1);
                        part.name = line.substr(s + 1, e == std::string::npos ? std::string::npos : e - s - 1);
                    } else {
                        const size_t e = line.find(';', s);
                        part.name = line.substr(s, e == std::string::npos ? std::string::npos : e - s);
                    }
                }
                const size_t f = l.find("filename=");
                if (f != std::string::npos) {
                    size_t s = f + 9;
                    if (s < line.size() && line[s] == '"') {
                        const size_t e = line.find('"', s + 1);
                        part.filename = line.substr(s + 1, e == std::string::npos ? std::string::npos : e - s - 1);
                    } else {
                        const size_t e = line.find(';', s);
                        part.filename = line.substr(s, e == std::string::npos ? std::string::npos : e - s);
                    }
                }
            } else if (l.rfind("content-type:", 0) == 0) {
                part.content_type = trim(line.substr(13));
            }
        }
        part.data = body.substr(data_start, data_end - data_start);
        parts->push_back(std::move(part));
        pos = next + delimiter.size();
    }
    return !parts->empty();
}

HttpServer::HttpServer(int port, Handler handler) : port_(port), handler_(std::move(handler)) {}

HttpServer::~HttpServer() { stop(); }

bool HttpServer::start(std::string* error) {
    if (running_) return true;
#ifdef _WIN32
    static bool wsa = false;
    if (!wsa) {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            if (error) *error = "WSAStartup failed";
            return false;
        }
        wsa = true;
    }
#endif
    socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == PGR_INVALID) {
        if (error) *error = "socket() failed";
        return false;
    }
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<uint16_t>(port_));
    if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        pgr_close(fd);
        if (error) *error = "port " + std::to_string(port_) + " is busy";
        return false;
    }
    if (listen(fd, 64) != 0) {
        pgr_close(fd);
        if (error) *error = "listen() failed";
        return false;
    }
    listen_fd_ = static_cast<long long>(fd);
    running_ = true;
    accept_thread_ = std::thread([this] { accept_loop(); });
    return true;
}

void HttpServer::stop() {
    if (!running_.exchange(false)) return;
    if (listen_fd_ >= 0) {
#ifdef _WIN32
        shutdown(static_cast<socket_t>(listen_fd_), SD_BOTH);
#else
        shutdown(static_cast<socket_t>(listen_fd_), SHUT_RDWR);
#endif
        pgr_close(static_cast<socket_t>(listen_fd_));
        listen_fd_ = -1;
    }
    if (accept_thread_.joinable()) accept_thread_.join();
}

void HttpServer::accept_loop() {
    while (running_) {
        sockaddr_in client{};
#ifdef _WIN32
        int len = sizeof(client);
#else
        socklen_t len = sizeof(client);
#endif
        socket_t fd = accept(static_cast<socket_t>(listen_fd_), reinterpret_cast<sockaddr*>(&client), &len);
        if (fd == PGR_INVALID) {
            if (!running_) break;
            continue;
        }
        char ip[64] = {0};
        inet_ntop(AF_INET, &client.sin_addr, ip, sizeof(ip));
        const std::string remote = ip;
        std::thread([this, fd, remote] {
            handle_connection(static_cast<long long>(fd));
            (void)remote;
        }).detach();
    }
}

bool HttpServer::read_request(long long socket_fd, HttpRequest* request, std::string* error) {
    socket_t fd = static_cast<socket_t>(socket_fd);
    std::string buffer;
    char chunk[8192];
    size_t header_end = std::string::npos;
    while (header_end == std::string::npos) {
        const int n = static_cast<int>(recv(fd, chunk, sizeof(chunk), 0));
        if (n <= 0) {
            *error = "connection closed";
            return false;
        }
        buffer.append(chunk, static_cast<size_t>(n));
        header_end = buffer.find("\r\n\r\n");
        if (buffer.size() > 128 * 1024) {
            *error = "header too large";
            return false;
        }
    }
    const std::string head = buffer.substr(0, header_end);
    std::istringstream hs(head);
    std::string line;
    std::getline(hs, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    {
        std::istringstream ls(line);
        std::string target, version;
        ls >> request->method >> target >> version;
        const size_t q = target.find('?');
        request->path = q == std::string::npos ? target : target.substr(0, q);
        request->query = q == std::string::npos ? "" : target.substr(q + 1);
        request->params = parse_query(request->query);
    }
    size_t content_length = 0;
    while (std::getline(hs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string key = lower(trim(line.substr(0, colon)));
        const std::string value = trim(line.substr(colon + 1));
        request->headers[key] = value;
        if (key == "content-length") content_length = static_cast<size_t>(std::atoll(value.c_str()));
    }
    if (content_length > 256ull * 1024 * 1024) {
        *error = "body too large";
        return false;
    }
    std::string body = buffer.substr(header_end + 4);
    while (body.size() < content_length) {
        const int n = static_cast<int>(recv(fd, chunk, sizeof(chunk), 0));
        if (n <= 0) break;
        body.append(chunk, static_cast<size_t>(n));
    }
    request->body = body.substr(0, content_length);
    const auto ct = request->headers.find("content-type");
    if (ct != request->headers.end() && lower(ct->second).find("multipart/form-data") != std::string::npos) {
        parse_multipart(ct->second, request->body, &request->parts);
    } else if (ct != request->headers.end() &&
               lower(ct->second).find("application/x-www-form-urlencoded") != std::string::npos) {
        request->params = parse_query(request->body);
    }
    return true;
}

void HttpServer::write_response(long long socket_fd, const HttpResponse& response) {
    socket_t fd = static_cast<socket_t>(socket_fd);
    std::ostringstream head;
    head << "HTTP/1.1 " << response.status << " "
         << (response.status == 200   ? "OK"
             : response.status == 201 ? "Created"
             : response.status == 204 ? "No Content"
             : response.status == 400 ? "Bad Request"
             : response.status == 404 ? "Not Found"
             : response.status == 405 ? "Method Not Allowed"
             : response.status == 413 ? "Payload Too Large"
             : response.status == 500 ? "Internal Server Error"
                                      : "Error")
         << "\r\n";
    head << "Content-Length: " << response.body.size() << "\r\n";
    head << "Connection: close\r\n";
    head << "Cache-Control: no-store\r\n";
    head << "X-Content-Type-Options: nosniff\r\n";
    auto ct = response.headers.find("Content-Type");
    head << "Content-Type: " << (ct != response.headers.end() ? ct->second : "application/json; charset=utf-8")
         << "\r\n";
    for (const auto& kv : response.headers)
        if (lower(kv.first) != "content-type" && lower(kv.first) != "content-length") head << kv.first << ": " << kv.second << "\r\n";
    head << "\r\n";
    const std::string full = head.str() + response.body;
    size_t sent = 0;
    while (sent < full.size()) {
        const int n = static_cast<int>(send(fd, full.data() + sent, static_cast<int>(full.size() - sent), 0));
        if (n <= 0) break;
        sent += static_cast<size_t>(n);
    }
}

void HttpServer::handle_connection(long long socket_fd) {
    HttpRequest request;
    std::string error;
    if (read_request(socket_fd, &request, &error)) {
        HttpResponse response;
        try {
            response = handler_(request);
        } catch (const std::exception& e) {
            response.status = 500;
            response.body = std::string("{\"error\":\"") + e.what() + "\"}";
        } catch (...) {
            response.status = 500;
            response.body = "{\"error\":\"unhandled exception\"}";
        }
        write_response(socket_fd, response);
    } else {
        HttpResponse response;
        response.status = 400;
        response.body = "{\"error\":\"" + error + "\"}";
        write_response(socket_fd, response);
    }
    pgr_close(static_cast<socket_t>(socket_fd));
}

}  // namespace pgr
