/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Author/Developer(s): Harihara Kadayam
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/
#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#ifdef __linux__
#include <event2/dns.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/thread.h>
#include <event2/util.h>
#include <evhtp.h>
#include <evhtp/evhtp.h>
#include <evhtp/sslutils.h>
#include <sys/stat.h>
#endif

#include <boost/intrusive_ptr.hpp>
#include <boost/intrusive/slist.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <evhtp/evhtp.h>
#include <nlohmann/json.hpp>

#include "auth_manager/auth_manager.hpp"
#include "logging/logging.h"
#include "options/options.h"
#include "utility/obj_life_counter.hpp"
#include "utility/thread_factory.hpp"

SISL_LOGGING_DECL(httpserver_lmod)

namespace sisl {
class AuthManager;

////////////////////// Config Definitions //////////////////////
struct HttpServerConfig {
    bool is_tls_enabled;
    std::string tls_cert_path;
    std::string tls_key_path;
    std::string bind_address;
    uint32_t server_port;
    uint32_t read_write_timeout_secs;
    bool is_auth_enabled;
};

////////////////////// Internal Event Definitions //////////////////////
enum event_type_t {
    CALLBACK,
};
struct HttpEvent {
    event_type_t m_event_type;
    std::function< void() > m_closure;
};
typedef std::list< HttpEvent > EventList;

////////////////////// API CallData Definitions //////////////////////
struct _http_calldata : public boost::intrusive_ref_counter< _http_calldata >, sisl::ObjLifeCounter< _http_calldata > {
public:
    friend class HttpServer;

    _http_calldata(evhtp_request_t* req, void* arg = nullptr) :
            m_req{req}, m_completed{false}, m_arg{arg}, m_http_code{EVHTP_RES_OK}, m_content_type{"application/json"} {
        m_req->cbarg = this;
    }

    void set_response(evhtp_res code, const std::string& msg) {
        m_http_code = code;
        m_response_msg = msg;
    }

    void complete() { m_completed = true; }
    bool is_completed() const { return m_completed; }
    evhtp_request_t* request() { return m_req; }
    void* cookie() { return m_arg; }

private:
    evhtp_request_t* m_req;
    bool m_completed;
    void* m_arg;
    std::string m_response_msg;
    evhtp_res m_http_code;
    const char* m_content_type;
};

typedef boost::intrusive_ptr< _http_calldata > HttpCallData;

////////////////////// Handler Definitions //////////////////////
typedef std::function< void(HttpCallData) > HttpRequestHandler;
struct _handler_info {
    std::string m_uri;
    evhtp_callback_cb m_callback;
    void* m_arg;

    _handler_info(const std::string& uri, evhtp_callback_cb cb, void* arg = nullptr) :
            m_uri{uri}, m_callback{cb}, m_arg{arg} {}

    bool operator<(const _handler_info& other) const { return m_uri < other.m_uri; }
};

template < void (*Handler)(HttpCallData) >
static void _request_handler(evhtp_request_t* req, void* arg) {
    const HttpCallData cd{new _http_calldata(req, arg)};
    Handler(cd);
}

#define handler_info(uri, cb, arg) sisl::_handler_info(uri, sisl::_request_handler< cb >, arg)

////////////////////// Server Implementation //////////////////////
class HttpServer {
public:
    HttpServer(const HttpServerConfig& cfg, const std::vector< _handler_info >& handlers) :
            m_cfg{cfg}, m_handlers{handlers}, m_ev_base{nullptr}, m_htp{nullptr}, m_internal_event{nullptr} {}

    HttpServer(const HttpServerConfig& cfg, const std::vector< _handler_info >& handlers,
               const std::shared_ptr< AuthManager > auth_mgr) :
            m_cfg{cfg},
            m_handlers{handlers},
            m_ev_base{nullptr},
            m_htp{nullptr},
            m_internal_event{nullptr},
            m_auth_mgr{auth_mgr} {}

    HttpServer(const HttpServerConfig& cfg) : HttpServer{cfg, {}} {}

    virtual ~HttpServer() {
        std::lock_guard lock{m_event_mutex};
        while (!m_event_list.empty()) {
            auto c{std::move(m_event_list.front())};
            m_event_list.pop_front();
        }
    }

    int start() {
        try {
            if (::evthread_use_pthreads() != 0) { throw std::runtime_error{"evthread_use_pthreads error!"}; }
            m_http_thread = sisl::make_unique_thread("httpserver", &HttpServer::_run, this);
        } catch (const std::system_error& e) {
            LOGERROR("Thread creation failed: {} ", e.what());
            return -1;
        }

        {
            std::unique_lock< std::mutex > lk{m_running_mutex};
            m_ready_cv.wait(lk, [this] { return m_is_running; });
        }
        return 0;
    }

    int stop() {
        run_in_http_thread([this]() {
            LOGINFO("Stopping http server event loop.");
            if (::event_base_loopbreak(m_ev_base) != 0) { LOGERROR("Error breaking out of admin server loop: "); }
        });

        /* wait for not running indication */
        LOGINFO("Waiting for http server event loop to be stopped.");
        {
            std::unique_lock< std::mutex > lk{m_running_mutex};
            m_ready_cv.wait(lk, [this] { return !m_is_running; });
        }
        LOGINFO("HTTP server event loop stopped.");

        LOGINFO("Waiting for http server thread to join..");
        if (m_http_thread && m_http_thread->joinable()) {
            try {
                m_http_thread->join();
            } catch (std::exception& e) { LOGERROR("Http thread join error: {}", e.what()); }
        }
        LOGINFO("HTTP Server thread joined.");

        return 0;
    }

    void register_handler_info(const _handler_info& hinfo) {
        ::evhtp_set_cb(m_htp, hinfo.m_uri.c_str(), hinfo.m_callback, hinfo.m_arg);
    }

    // Commands for admin/diagnostic purposes
    // Holding handles to these commands here
    evbase_t* get_base() const { return m_ev_base; }

    void run_in_http_thread(std::function< void() > closure) {
        HttpEvent event;
        event.m_event_type = event_type_t::CALLBACK;
        event.m_closure = std::move(closure);

        {
            std::lock_guard< std::mutex > lock{m_event_mutex};
            m_event_list.emplace_back(std::move(event));
        }

        ::event_active(m_internal_event, EV_READ | EV_WRITE, 1);
    }

    void respond_OK(HttpCallData cd, evhtp_res http_code, const std::string& msg,
                    const char* content_type = "application/json") {
        cd->m_http_code = http_code;
        cd->m_response_msg = msg;
        cd->m_content_type = content_type;
        respond_OK(cd);
    }

    void respond_NOTOK(HttpCallData cd, evhtp_res http_code, const std::string& msg) {
        cd->m_http_code = http_code;
        cd->m_response_msg = msg;
        respond_OK(cd);
    }

    void respond_OK(HttpCallData cd) {
        if (std::this_thread::get_id() == m_http_thread->get_id()) {
            http_OK(cd);
        } else {
            run_in_http_thread([this, cd]() { http_OK(cd); });
        }
    }

    void respond_NOTOK(HttpCallData cd) {
        if (std::this_thread::get_id() == m_http_thread->get_id()) {
            http_NOTOK(cd);
        } else {
            run_in_http_thread([this, cd]() { http_NOTOK(cd); });
        }
    }

    static evhtp_res to_evhtp_res(const AuthVerifyStatus status) {
        evhtp_res ret;
        switch (status) {
        case AuthVerifyStatus::OK:
            ret = EVHTP_RES_OK;
            break;
        case AuthVerifyStatus::UNAUTH:
            ret = EVHTP_RES_UNAUTH;
            break;
        case AuthVerifyStatus::FORBIDDEN:
            ret = EVHTP_RES_FORBIDDEN;
            break;
        default:
            ret = EVHTP_RES_BADREQ;
            break;
        }
        return ret;
    }

    /*
     * The user of the http_server must add a line to call http_auth_verify at the beginning of all the apis defined.
     * The ideal way would be for the server to intercept all incoming api calls and do verification before sending it
     * down to the url callback function. No proper way could be found to do this.
     * One potential way is to use the hooks (per connection hooks/ per request hooks or per cb hooks) which can be set
     * at different points in the life cycle of a req. (like on_headers etc) These hooks from evhtp library do not seem
     * to work properly when the hook cb functions return anything other than EVHTP_RES_OK For a perfect implementation
     * that avoids users to add http_auth_verify before all the apis they define, we need to either explore evhtp lib
     * more or switch to a different server like Pistache.
     */

    evhtp_res http_auth_verify(evhtp_request_t* req, std::string& msg) {
        if (!m_cfg.is_auth_enabled) { return EVHTP_RES_OK; }

        const std::string bearer{"Bearer "};
        auto* token{::evhtp_header_find(req->headers_in, "Authorization")};
        if (!token) {
            msg = "missing auth token in request header";
            LOGDEBUGMOD(httpserver_lmod, "Processing req={}; {}", static_cast< void* >(req), msg);
            return EVHTP_RES_UNAUTH;
        }
        const std::string token_str{token};
        if (token_str.rfind(bearer, 0) != 0) {
            msg = "require bearer token in request header";
            LOGDEBUGMOD(httpserver_lmod, "Processing req={}; {}", static_cast< void* >(req), msg);
            return EVHTP_RES_UNAUTH;
        }
        const auto raw_token{token_str.substr(bearer.length())};
        // verify method is expected to not throw
        return to_evhtp_res(m_auth_mgr->verify(raw_token, msg));
    }

#define request_callback(cb)                                                                                           \
    (evhtp_callback_cb) std::bind(&HttpServer::cb, this, std::placeholders::_1, std::placeholders::_2)
#define error_callback(cb)                                                                                             \
    (evhtp_hook) std::bind(&HttpServer::cb, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)

protected:
    /* ****************** All default connection handlers ****************** */
    static evhtp_res register_connection_handlers(evhtp_connection_t* conn, void* arg) {
        evhtp_connection_set_hook(conn, evhtp_hook_on_path, (evhtp_hook)HttpServer::request_on_path_handler, arg);
        evhtp_connection_set_hook(conn, evhtp_hook_on_request_fini, (evhtp_hook)HttpServer::request_fini_handler, arg);
        evhtp_connection_set_hook(conn, evhtp_hook_on_conn_error, (evhtp_hook)HttpServer::connection_error_callback,
                                  arg);
        evhtp_connection_set_hook(conn, evhtp_hook_on_error, (evhtp_hook)HttpServer::request_error_handler, arg);
        return EVHTP_RES_OK;
    }

    static void default_request_handler(evhtp_request_t* req, void* arg) {
        HttpServer* const server{static_cast< HttpServer* >(arg)};
        const HttpCallData cd{new _http_calldata(req, arg)};
        server->respond_NOTOK(cd, EVHTP_RES_BADREQ, "Request can't be matched with any handlers\n");
    }

    static evhtp_res request_on_path_handler(evhtp_request_t* req, void* arg) {
        [[maybe_unused]] HttpServer* const server{static_cast< HttpServer* >(arg)};

        const char* path{""};
        if (req->uri && req->uri->path && req->uri->path->full) { path = req->uri->path->full; }

        LOGDEBUGMOD(httpserver_lmod, "Processing req={} path={}", static_cast< void* >(req), path);
        return EVHTP_RES_OK;
    }

    static evhtp_res request_fini_handler(evhtp_request_t* req, void* arg) {
        [[maybe_unused]] HttpServer* const server{static_cast< HttpServer* >(arg)};

        const char* path{""};
        if (req->uri && req->uri->path && req->uri->path->full) { path = req->uri->path->full; }
        LOGDEBUGMOD(httpserver_lmod, "Finishing req={}, path={}", static_cast< void* >(req), path);

        if (req->cbarg != nullptr) {
            _http_calldata* const cd{static_cast< _http_calldata* >(req->cbarg)};
            cd->complete();
            intrusive_ptr_release(cd);
        }
        return EVHTP_RES_OK;
    }

    static void connection_error_callback([[maybe_unused]] evhtp_connection_t* conn, evhtp_error_flags type,
                                          void* arg) {
        [[maybe_unused]] HttpServer* const server{static_cast< HttpServer* >(arg)};
        LOGERROR("unhandled connection error of type: {}", type);
    }

    static void request_error_handler([[maybe_unused]] evhtp_request_t* req, evhtp_error_flags errtype, void* arg) {
        [[maybe_unused]] HttpServer* const server{static_cast< HttpServer* >(arg)};
        LOGERROR("Unhandled request error of type: {}", errtype);
    }

private:
    int _run() {
        int error{0};

        m_ev_base = ::event_base_new();
        if (m_ev_base == nullptr) {
            LOGERROR("event_base_new() failed!");
            return -1;
        }

        m_htp = ::evhtp_new(m_ev_base, nullptr);
        if (m_htp == nullptr) {
            LOGERROR("evhtp_new() failed!");
            ::event_base_free(m_ev_base);
            return -1;
        }

        if (m_cfg.is_tls_enabled) {
            const auto ssl_config{get_ssl_opts_()};
            if (!ssl_config) {
                LOGERROR("get_ssl_opts_ failed!");
                ::evhtp_free(m_htp);
                ::event_base_free(m_ev_base);
                return -1;
            }

            if (::evhtp_ssl_init(m_htp, ssl_config.get()) != 0) {
                LOGERROR("evhtp_ssl_init failed!");
                ::evhtp_free(m_htp);
                ::event_base_free(m_ev_base);
                return -1;
            }
        }

        struct timeval timeout {
            m_cfg.read_write_timeout_secs, 0
        };

        // For internal events
        m_internal_event = ::event_new(m_ev_base, -1, EV_TIMEOUT | EV_READ, &HttpServer::internal_event_handler, this);
        if (m_internal_event == nullptr) {
            LOGERROR("Adding internal event failed!");
            ::evhtp_free(m_htp);
            ::event_base_free(m_ev_base);
            return error;
        }
        ::event_add(m_internal_event, &timeout);

        /* set a callback to set per-connection hooks (via a post_accept cb) */
        ::evhtp_set_post_accept_cb(m_htp, &HttpServer::register_connection_handlers, (void*)this);

        // set read and write timeouts
        ::evhtp_set_timeouts(m_htp, &timeout, &timeout);

        // Register all handlers and a default handler
        for (auto& handler : m_handlers) {
            ::evhtp_set_cb(m_htp, handler.m_uri.c_str(), handler.m_callback, handler.m_arg);
        }
        ::evhtp_set_gencb(m_htp, (evhtp_callback_cb)default_request_handler, (void*)this);

        // bind a socket
        error = ::evhtp_bind_socket(m_htp, m_cfg.bind_address.c_str(), uint16_t(m_cfg.server_port), 128);
        if (error != 0) {
            // handling socket binding failure
            LOGERROR("HTTP listener failed to start at address:port = {}:{} ", m_cfg.bind_address, m_cfg.server_port);
            // Free the http resources
            ::evhtp_free(m_htp);
            ::event_base_free(m_ev_base);
            return error;
        }

        LOGINFO("HTTP Server started at port: {}", m_cfg.server_port);

        // Notify the caller that we are ready.
        {
            std::lock_guard< std::mutex > lk{m_running_mutex};
            m_is_running = true;
        }
        m_ready_cv.notify_one();

        // start event loop, this will block the thread.
        error = ::event_base_loop(m_ev_base, 0);
        if (error != 0) { LOGERROR("Error starting Http listener loop"); }

        {
            std::lock_guard< std::mutex > lk{m_running_mutex};
            m_is_running = false;
        }
        m_ready_cv.notify_one();

        // free the resources
        ::evhtp_unbind_socket(m_htp);

        // free pipe event
        ::event_free(m_internal_event);

        // free evhtp
        ::evhtp_free(m_htp);

        // finally free event base
        ::event_base_free(m_ev_base);

        LOGINFO("Exiting http server event loop.");
        return error;
    }

    void _internal_event_handler(evutil_socket_t, short events) {
        std::vector< HttpEvent > events_queue;
        {
            std::lock_guard lock{m_event_mutex};
            while (!m_event_list.empty()) {
                events_queue.emplace_back(std::move(m_event_list.front()));
                m_event_list.pop_front();
            }
        }

        for (auto& event : events_queue) {
            switch (event.m_event_type) {
            case event_type_t::CALLBACK:
                event.m_closure();
                break;

            default:
                LOGERROR("Unknown internal event type {} ", event.m_event_type);
                break;
            }
        }
    }

    void http_OK(HttpCallData cd) {
        evhtp_request_t* const req{cd->request()};

        const auto* const conn{::evhtp_request_get_connection(req)};
        if (m_cfg.is_tls_enabled) { ::htp_sslutil_add_xheaders(req->headers_out, conn->ssl, HTP_SSLUTILS_XHDR_ALL); }
        if (cd->m_content_type) {
            ::evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", cd->m_content_type, 0, 0));
        }

        std::ostringstream ss;
        ss << cd->m_response_msg.size();

        /* valloc should be 1 because ss.str().c_str() is freed once control goes out of this function */
        ::evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Length", ss.str().c_str(), 0, 1));
        ::evbuffer_add(req->buffer_out, cd->m_response_msg.c_str(), cd->m_response_msg.size());

        // Need to increment the calldata reference since evhtp_send_reply will call finish asyncronously and calldata
        // needs to stay relavant till that call.
        intrusive_ptr_add_ref(cd.get());
        ::evhtp_send_reply(req, cd->m_http_code);
    }

    void http_NOTOK(HttpCallData cd) {
        evhtp_request_t* const req{cd->request()};

        const nlohmann::json json = {{"errorCode", cd->m_http_code}, {"errorDetail", cd->m_response_msg}};
        const std::string json_str{json.dump()};
        ::evhtp_headers_add_header(req->headers_out, ::evhtp_header_new("Content-Type", "application/json", 0, 0));

        std::ostringstream ss;
        ss << json_str.size();
        /* valloc should be 1 because ss.str().c_str() is freed once control goes out of this function */
        ::evhtp_headers_add_header(req->headers_out, ::evhtp_header_new("Content-Length", ss.str().c_str(), 0, 1));
        ::evbuffer_add(req->buffer_out, json_str.c_str(), json_str.size());

        // Need to increment the calldata reference since evhtp_send_reply will call finish asyncronously and calldata
        // needs to stay relavant till that call.
        intrusive_ptr_add_ref(cd.get());
        ::evhtp_send_reply(req, cd->m_http_code);
    }

    static void internal_event_handler(evutil_socket_t socket, short events, void* user_data) {
        HttpServer* server{static_cast< HttpServer* >(user_data)};
        server->_internal_event_handler(socket, events);
    }

    std::unique_ptr< evhtp_ssl_cfg_t > get_ssl_opts_() {
        struct stat f_stat;
        auto ssl_config{std::make_unique< evhtp_ssl_cfg_t >()};

        ssl_config->ssl_opts = 0; // SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1;
        ssl_config->pemfile = (char*)m_cfg.tls_cert_path.c_str();
        ssl_config->privfile = (char*)m_cfg.tls_key_path.c_str();

        if (ssl_config->pemfile) {
            if (::stat(ssl_config->pemfile, &f_stat) != 0) {
                LOGERROR("Cannot load SSL cert: {}", ssl_config->pemfile);
                return nullptr;
            }
        }

        if (ssl_config->privfile) {
            if (::stat(ssl_config->privfile, &f_stat) != 0) {
                LOGERROR("Cannot load SSL key: {}", ssl_config->privfile);
                return nullptr;
            }
        }

        return ssl_config;
    }

private:
    HttpServerConfig m_cfg;
    std::unique_ptr< std::thread > m_http_thread;
    std::vector< _handler_info > m_handlers;

    // Maintaining a list of pipe events because multiple threads could add events at the same time.
    // Additions and deletions from this list are protected by m_mutex defined .
    std::mutex m_event_mutex;
    EventList m_event_list;

    mutable evbase_t* m_ev_base;
    evhtp_t* m_htp;
    struct event* m_internal_event;

    std::mutex m_running_mutex;
    bool m_is_running{false};
    std::condition_variable m_ready_cv;

    std::shared_ptr< AuthManager > m_auth_mgr;
};

} // namespace sisl
