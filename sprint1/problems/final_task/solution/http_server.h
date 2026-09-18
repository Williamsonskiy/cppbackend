#pragma once
#include "sdk.h"
#define BOOST_BEAST_USE_STD_STRING_VIEW

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <iostream>
#include <memory>

namespace http_server {

namespace net = boost::asio;
using tcp = net::ip::tcp;
namespace beast = boost::beast;
namespace http = beast::http;
using namespace std::literals;

void ReportError(beast::error_code ec, std::string_view what);

class SessionBase {
public:
    SessionBase(const SessionBase&) = delete;
    SessionBase& operator=(const SessionBase&) = delete;

    void Run();

protected:
    explicit SessionBase(tcp::socket&& socket)
        : stream_(std::move(socket)) {
    }

    virtual ~SessionBase() = default;

    template <typename Body, typename Fields, typename Handler>
    void Write(http::response<Body, Fields>&& response, Handler&& handler) {
        auto safe_response = std::make_shared<http::response<Body, Fields>>(std::move(response));
        auto self = GetSharedThis();
        http::async_write(
            stream_, *safe_response,
            [safe_response, self, handler = std::forward<Handler>(handler)](beast::error_code ec, std::size_t bytes_written) mutable {
                handler(ec, bytes_written, safe_response->need_eof());
            });
    }

    void Read() {
        req_ = {};
        stream_.expires_after(std::chrono::seconds(30));
        auto self = GetSharedThis();
        http::async_read(stream_, buffer_, req_,
            [self](beast::error_code ec, std::size_t bytes_transferred) {
                self->OnRead(ec, bytes_transferred);
            });
    }

    void Close() {
        beast::error_code ec;
        stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
    }

    virtual std::shared_ptr<SessionBase> GetSharedThis() = 0;
    virtual void HandleRequest(http::request<http::string_body>&& req) = 0;

private:
    void OnRead(beast::error_code ec, std::size_t bytes_transferred) {
        if (ec == http::error::end_of_stream) {
            return Close();
        }
        if (ec) {
            return ReportError(ec, "read"sv);
        }
        HandleRequest(std::move(req_));
    }

    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;
};

template <typename RequestHandler>
class Session : public SessionBase, public std::enable_shared_from_this<Session<RequestHandler>> {
public:
    template <typename Handler>
    Session(tcp::socket&& socket, Handler&& request_handler)
        : SessionBase(std::move(socket))
        , request_handler_(std::forward<Handler>(request_handler)) {
    }

private:
    std::shared_ptr<SessionBase> GetSharedThis() override {
        return this->shared_from_this();
    }

    void HandleRequest(http::request<http::string_body>&& req) override {
        auto self = this->shared_from_this();
        request_handler_(std::move(req), [self](auto&& response) {
            self->Write(std::forward<decltype(response)>(response), [self](beast::error_code ec, std::size_t, bool close) {
                if (ec) {
                    return ReportError(ec, "write"sv);
                }
                if (close) {
                    return self->Close();
                }
                self->Read();
            });
        });
    }

    RequestHandler request_handler_;
};

template <typename RequestHandler>
class Listener : public std::enable_shared_from_this<Listener<RequestHandler>> {
public:
    template <typename Handler>
    Listener(net::io_context& ioc, const tcp::endpoint& endpoint, Handler&& request_handler)
        : ioc_(ioc)
        , acceptor_(net::make_strand(ioc), endpoint)
        , request_handler_(std::forward<Handler>(request_handler)) {
    }

    void Run() {
        DoAccept();
    }

private:
    void DoAccept() {
        acceptor_.async_accept(
            net::make_strand(ioc_),
            beast::bind_front_handler(&Listener::OnAccept, this->shared_from_this()));
    }

    void OnAccept(beast::error_code ec, tcp::socket socket) {
        if (ec) {
            return ReportError(ec, "accept"sv);
        }
        AsyncRunSession(std::move(socket));
        DoAccept();
    }

    void AsyncRunSession(tcp::socket&& socket) {
        std::make_shared<Session<RequestHandler>>(std::move(socket), request_handler_)->Run();
    }

    net::io_context& ioc_;
    tcp::acceptor acceptor_;
    RequestHandler request_handler_;
};

template <typename RequestHandler>
void ServeHttp(net::io_context& ioc, const tcp::endpoint& endpoint, RequestHandler&& handler) {
    using MyListener = Listener<std::decay_t<RequestHandler>>;
    std::make_shared<MyListener>(ioc, endpoint, std::forward<RequestHandler>(handler))->Run();
}

}  // namespace http_server
