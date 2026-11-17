// HTTP in both directions.
// Client: curl over a subprocess, so HTTPS works everywhere without linking TLS.
// Server: a small socket server (Winsock on Windows) with multipart uploads.
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace pgr {

struct HttpPart {
    std::string name;
    std::string filename;
    std::string content_type;
    std::string data;
};

struct HttpRequest {
    std::string method = "GET";
    std::string url;    // client side: absolute URL
    std::string path;   // server side: path only
    std::string query;
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> params;
    std::string body;
    std::vector<HttpPart> parts;
    std::string remote;
    int timeout_ms = 30000;
};

struct HttpResponse {
    int status = 0;
    std::map<std::string, std::string> headers;
    std::string body;
    std::string error;
    double elapsed_ms = 0.0;
};

HttpResponse http_request(const HttpRequest& request);

std::string url_encode(const std::string& s);
std::string url_decode(const std::string& s);
std::map<std::string, std::string> parse_query(const std::string& query);
std::string mime_for(const std::string& path);
std::string base64_encode(const std::string& bytes);
std::string base64_decode(const std::string& text);

class HttpServer {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    HttpServer(int port, Handler handler);
    ~HttpServer();

    bool start(std::string* error);
    void stop();
    bool running() const { return running_; }
    int port() const { return port_; }

private:
    void accept_loop();
    void handle_connection(long long socket_fd);
    bool read_request(long long socket_fd, HttpRequest* request, std::string* error);
    void write_response(long long socket_fd, const HttpResponse& response);

    int port_;
    Handler handler_;
    std::atomic<bool> running_{false};
    long long listen_fd_ = -1;
    std::thread accept_thread_;
};

// Extracts a boundary-delimited body. Returns false when the header is absent.
bool parse_multipart(const std::string& content_type, const std::string& body, std::vector<HttpPart>* parts);

}  // namespace pgr
