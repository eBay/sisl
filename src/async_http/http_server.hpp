//
// Created by Kadayam, Hari on 12/14/18.
//
#pragma once

#include <condition_variable>
#include <thread>
#include <string>
#include <evhtp.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/dns.h>
#include <event2/util.h>
#include <event2/thread.h>
#include <evhtp/evhtp.h>
#include <evhtp/sslutils.h>
#include <boost/intrusive_ptr.hpp>
#include <nlohmann/json.hpp>

#include <sys/stat.h>
#include <random>
#include <signal.h>
#include <optional>
#include <set>
#include <boost/filesystem.hpp>
#include <sds_logging/logging.h>
#include <boost/intrusive/slist.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include "utility/thread_factory.hpp"
#include "utility/obj_life_counter.hpp"
#include <sds_options/options.h>

SDS_LOGGING_DECL(httpserver_lmod)

namespace sisl {

////////////////////// Config Definitions //////////////////////
struct HttpServerConfig {
    bool            is_tls_enabled;
    std::string     tls_cert_path;
    std::string     tls_key_path;
    std::string     bind_address;
    uint32_t        server_port;
    uint32_t        read_write_timeout_secs;
};

////////////////////// Internal Event Definitions //////////////////////
enum event_type_t {
    CALLBACK,
};
struct HttpEvent : public boost::intrusive::slist_base_hook<> {
    event_type_t          m_event_type;
    std::function<void()> m_closure;
};
typedef boost::intrusive::slist< HttpEvent, boost::intrusive::cache_last<true> > EventList;

////////////////////// API CallData Definitions //////////////////////
struct _http_calldata : public boost::intrusive_ref_counter< _http_calldata >, sisl::ObjLifeCounter< _http_calldata > {
public:
    friend class HttpServer;

    _http_calldata(evhtp_request_t* req, void* arg = nullptr) :
            m_req(req),
            m_completed(false),
            m_arg(arg),
            m_http_code(EVHTP_RES_OK),
            m_content_type("application/json") {
        m_req->cbarg = (void *)this;
    }

    void set_response(evhtp_res code, const std::string& msg) {
        m_http_code = code;
        m_response_msg = msg;
    }

    void complete() { m_completed = true; }
    bool is_completed() const { return m_completed; }
    evhtp_request_t *request() { return m_req; }
    void* cookie() { return m_arg; }

private:
    evhtp_request_t *m_req;
    bool m_completed;
    void *m_arg;
    std::string m_response_msg;
    evhtp_res m_http_code;
    const char *m_content_type;
};

typedef boost::intrusive_ptr< _http_calldata > HttpCallData;

////////////////////// Handler Definitions //////////////////////
typedef std::function<void(HttpCallData)> HttpRequestHandler;
struct _handler_info {
    std::string        m_uri;
    evhtp_callback_cb  m_callback;
    void*              m_arg;

    _handler_info(const std::string& uri, evhtp_callback_cb cb, void *arg = nullptr) :
            m_uri(uri), m_callback(cb), m_arg(arg) {}

    bool operator<(const _handler_info& other) const {
        return m_uri < other.m_uri;
    }
};

template <void (*Handler)(HttpCallData)>
static void _request_handler(evhtp_request_t* req, void* arg) {
    HttpCallData cd(new _http_calldata(req, arg));
    Handler(cd);
}

#define handler_info(uri, cb, arg) sisl::_handler_info(uri, sisl::_request_handler< cb >, arg)

////////////////////// Server Implementation //////////////////////
class HttpServer {
public:
    HttpServer(const HttpServerConfig& cfg, const std::vector< _handler_info > &handlers) :
            m_cfg(cfg),
            m_handlers(handlers),
            m_ev_base(nullptr),
            m_htp(nullptr),
            m_internal_event(nullptr) {}

    HttpServer(const HttpServerConfig& cfg) : HttpServer(cfg, {}) {}

    virtual ~HttpServer() {
        while (!m_event_list.empty()) {
            auto &c = m_event_list.front();
            m_event_list.pop_front();
            delete(&c);
        }
    }

    int start() {
        try {
            if (evthread_use_pthreads() != 0) {
                throw std::runtime_error("evthread_use_pthreads error!");
            }
            m_http_thread = sisl::make_unique_thread("httpserver", &HttpServer::_run, this);
        } catch (std::system_error& e) {
            LOGERROR("Thread creation failed: {} ", e.what());
            return -1;
        }

        {
            std::unique_lock <std::mutex> lk(m_mutex);
            m_ready_cv.wait(lk, [this] { return (m_is_running); });
        }
        return 0;
    }

    int stop() {
        run_in_http_thread([this]() {
            LOGINFO("Stopping http server event loop.");
            if (event_base_loopbreak(m_ev_base) != 0) {
                LOGERROR("Error breaking out of admin server loop: ");
            }
        });

        LOGINFO("Waiting for http server thread to join..");
        if (m_http_thread != nullptr && m_http_thread->joinable()) {
            try {
                m_http_thread->join();
            } catch (std::exception& e) {
                LOGERROR("Http thread join error: {}", e.what());
            }
        }
        return 0;
    }

    void register_handler_info(const _handler_info& hinfo) {
        evhtp_set_cb(m_htp, hinfo.m_uri.c_str(), hinfo.m_callback, hinfo.m_arg);
    }

    // Commands for admin/diagnostic purposes
    // Holding handles to these commands here
    evbase_t* get_base() const { return m_ev_base; }

    void run_in_http_thread(std::function<void()> closure) {
        HttpEvent *event = new HttpEvent();
        event->m_event_type = event_type_t::CALLBACK;
        event->m_closure = closure;

        {
            std::lock_guard <std::mutex> lock(m_mutex);
            m_event_list.push_back(*event);
        }

        event_active(m_internal_event, EV_READ|EV_WRITE, 1);
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
            run_in_http_thread([this, cd]() {
                http_OK(cd);
            });
        }
    }

    void respond_NOTOK(HttpCallData cd) {
        if (std::this_thread::get_id() == m_http_thread->get_id()) {
            http_NOTOK(cd);
        } else {
            run_in_http_thread([this, cd]() {
                http_NOTOK(cd);
            });
        }
    }

#define request_callback(cb) (evhtp_callback_cb)std::bind(&HttpServer::cb, this, std::placeholders::_1, std::placeholders::_2)
#define error_callback(cb) (evhtp_hook)std::bind(&HttpServer::cb, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)

protected:
    /* ****************** All default connection handlers ****************** */
    static evhtp_res register_connection_handlers(evhtp_connection_t* conn, void* arg) {
        evhtp_connection_set_hook(conn, evhtp_hook_on_path, (evhtp_hook)HttpServer::request_on_path_handler, arg);
        evhtp_connection_set_hook(conn, evhtp_hook_on_request_fini, (evhtp_hook)HttpServer::request_fini_handler, arg);
        evhtp_connection_set_hook(conn, evhtp_hook_on_conn_error, (evhtp_hook)HttpServer::connection_error_callback, arg);
        evhtp_connection_set_hook(conn, evhtp_hook_on_error, (evhtp_hook)HttpServer::request_error_handler, arg);
        return EVHTP_RES_OK;
    }

    static void default_request_handler(evhtp_request_t* req, void* arg) {
        HttpServer *server = (HttpServer *)arg;
        HttpCallData cd = new _http_calldata(req, arg);
        server->respond_NOTOK(cd, EVHTP_RES_BADREQ, "Request can't be matched with any handlers\n");
    }

#if 0
    bool http_auth_verify(evhtp_request_t* req) {
        bool flag = false;
        int error;
        jwt_t* context;
        const char bearer[] = "Bearer ";
        const char cookiePrefix[] = "MONSTOR_ACCESS_TOKEN=";

        auto* token = evhtp_header_find(req->headers_in, "Authorization");
        if (token) {
            if (strncmp(token, bearer, sizeof(bearer) - 1) == 0) {
                token = token + sizeof(bearer) - 1;
            } else {
                LOG(ERROR) << "Invalid token prefix: " << token;
                return false;
            }
        } else if ((token = evhtp_header_find(req->headers_in, "Cookie"))) {
            if (strncmp(token, cookiePrefix, sizeof(cookiePrefix) - 1) == 0) {
                token = token + sizeof(cookiePrefix) - 1;
            } else {
                LOG(ERROR) << "Invalid cookie prefix: " << token;
                return false;
            }
        } else {
            LOG(ERROR) << "Missing token in request http header";
            return false;
        }

        // read the secrets file
        std::ifstream ifs(monstordb_settings_safe()->config->httpServer->httpAuth->httpAuthorizationCert);
        std::stringstream cert;
        if (!ifs) {
            LOG(ERROR) << "cannot open http auth file: "
                       << monstordb_settings_safe()->config->httpServer->httpAuth->httpAuthorizationCert;
            return false;
        }
        cert << ifs.rdbuf();

        error = jwt_decode(&context, token, (const unsigned char*)cert.str().c_str(), (int)cert.str().length());
        if (error != 0) {
            LOGERROR("jwt_decode failed, error = {}", error);
        } else {
            // verifying that role contains AGENT or WATCHER
            auto* roles = jwt_get_grants_json(context, "$int_roles");
            if (roles) {
                try {
                    nlohmann::json array = nlohmann::json::parse(roles);
                    for (auto&& jobject : array) {
                        std::string str = jobject.dump();
                        if (strcasecmp(str.c_str(), "\"AGENT\"") == 0 || strcasecmp(str.c_str(), "\"WATCHER\"") == 0) {
                            flag = true;
                            break;
                        }
                    }
                } catch (nlohmann::json::exception& ex) {
                    LOGERROR("roles parse error: {}", roles);
                }

                if (!flag) {
                    LOGERROR("Invalid roles = {}", roles);
                }
            } else {
                LOGERROR("no roles in jwt token");
            }
        }

        jwt_free(context);
        return flag;
    }
#endif

    static evhtp_res request_on_path_handler(evhtp_request_t* req, void* arg) {
        __attribute__((unused)) HttpServer *server = (HttpServer *)arg;

        const char* path = "";
        if (req->uri && req->uri->path && req->uri->path->full) {
            path = req->uri->path->full;
        }

        LOGINFOMOD(httpserver_lmod, "Processing req={} path={}", (void *)req, path);
        return EVHTP_RES_OK;
    }

    static evhtp_res request_fini_handler(evhtp_request_t* req, void* arg) {
        __attribute__((unused)) HttpServer *server = (HttpServer *)arg;

        const char* path = "";
        if (req->uri && req->uri->path && req->uri->path->full) {
            path = req->uri->path->full;
        }
        LOGINFOMOD(httpserver_lmod, "Finishing req={}, path={}", (void *)req, path);

        if (req->cbarg != nullptr) {
            _http_calldata *cd = (_http_calldata *)req->cbarg;
            cd->complete();
            intrusive_ptr_release(cd);
        }
        return EVHTP_RES_OK;
    }

    static void connection_error_callback(__attribute__((unused)) evhtp_connection_t* conn, evhtp_error_flags type, void* arg) {
        __attribute__((unused)) HttpServer *server = (HttpServer *)arg;
        LOGERROR("unhandled connection error of type: {}", type);
    }

    static void request_error_handler(__attribute__((unused)) evhtp_request_t* req, evhtp_error_flags errtype, void* arg) {
        __attribute__((unused)) HttpServer *server = (HttpServer *)arg;
        LOGERROR("Unhandled request error of type: {}", errtype);
    }

private:
    int _run() {
        int error = 0;

        m_ev_base = event_base_new();
        if (m_ev_base == nullptr) {
            LOGERROR("event_base_new() failed!");
            return -1;
        }

        m_htp = evhtp_new(m_ev_base, nullptr);
        if (m_htp == nullptr) {
            LOGERROR("evhtp_new() failed!");
            event_base_free(m_ev_base);
            return -1;
        }

        if (m_cfg.is_tls_enabled) {
            auto ssl_config = get_ssl_opts_();
            if (ssl_config == nullptr) {
                LOGERROR("get_ssl_opts_ failed!");
                evhtp_free(m_htp);
                event_base_free(m_ev_base);
                return -1;
            }

            if (evhtp_ssl_init(m_htp, ssl_config.get()) != 0) {
                LOGERROR("evhtp_ssl_init failed!");
                evhtp_free(m_htp);
                event_base_free(m_ev_base);
                return -1;
            }
        }

        struct timeval timeout { m_cfg.read_write_timeout_secs, 0 };

        // For internal events
        m_internal_event = event_new(m_ev_base, -1, EV_TIMEOUT|EV_READ, &HttpServer::internal_event_handler, this);
        if (m_internal_event == nullptr) {
            LOGERROR("Adding internal event failed!");
            evhtp_free(m_htp);
            event_base_free(m_ev_base);
            return error;
        }
        event_add(m_internal_event, &timeout);

        /* set a callback to set per-connection hooks (via a post_accept cb) */
        evhtp_set_post_accept_cb(m_htp, &HttpServer::register_connection_handlers, (void *)this);

        // set read and write timeouts
        evhtp_set_timeouts(m_htp, &timeout, &timeout);

        // Register all handlers and a default handler
        for (auto& handler : m_handlers) {
            evhtp_set_cb(m_htp, handler.m_uri.c_str(), handler.m_callback, handler.m_arg);
        }
        evhtp_set_gencb(m_htp, (evhtp_callback_cb)default_request_handler, (void *)this);

        // bind a socket
        error = evhtp_bind_socket(m_htp, m_cfg.bind_address.c_str(), uint16_t(m_cfg.server_port), 128);
        if (error != 0) {
            // handling socket binding failure
            LOGERROR("HTTP listener failed to start at address:port = {}:{} ", m_cfg.bind_address, m_cfg.server_port);
            // Free the http resources
            evhtp_free(m_htp);
            event_base_free(m_ev_base);
            return error;
        }

        LOGINFO("HTTP Server started at port: {}", m_cfg.server_port);

        // Notify the caller that we are ready.
        {
            std::lock_guard <std::mutex> lk(m_mutex);
            m_is_running = true;
        }
        m_ready_cv.notify_one();

        // start event loop, this will block the thread.
        error = event_base_loop(m_ev_base, 0);
        if (error != 0) {
            LOGERROR("Error starting Http listener loop");
        }

        m_is_running = false;

        // free the resources
        evhtp_unbind_socket(m_htp);

        // free pipe event
        event_free(m_internal_event);

        // free evhtp
        evhtp_free(m_htp);

        // finally free event base
        event_base_free(m_ev_base);

        return error;
    }

    void _internal_event_handler(evutil_socket_t, short events) {
        m_mutex.lock();
        while (!m_event_list.empty()) {
            auto &c = m_event_list.front();
            m_event_list.pop_front();
            m_mutex.unlock();

            switch (c.m_event_type) {
            case event_type_t::CALLBACK:
                c.m_closure();
                break;

            default:
                LOGERROR("Unknown internal event type {} ", c.m_event_type);
                break;
            }
            delete(&c);

            m_mutex.lock();
        }
        m_mutex.unlock();

    }

    void http_OK(HttpCallData cd) {
        evhtp_request_t* req = cd->request();

        auto conn = evhtp_request_get_connection(req);
        if (m_cfg.is_tls_enabled) {
            htp_sslutil_add_xheaders(req->headers_out, conn->ssl, HTP_SSLUTILS_XHDR_ALL);
        }
        if (cd->m_content_type) {
            evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", cd->m_content_type, 0, 0));
        }

        std::stringstream ss;
        ss << cd->m_response_msg.size();

        /* valloc should be 1 because ss.str().c_str() is freed once control goes out of this function */
        evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Length", ss.str().c_str(), 0, 1));
        evbuffer_add(req->buffer_out, cd->m_response_msg.c_str(), cd->m_response_msg.size());

        // Need to increment the calldata reference since evhtp_send_reply will call finish asyncronously and calldata
        // needs to stay relavant till that call.
        intrusive_ptr_add_ref(cd.get());
        evhtp_send_reply(req, cd->m_http_code);
    }

    void http_NOTOK(HttpCallData cd) {
        evhtp_request_t* req = cd->request();

        nlohmann::json json = {{"errorCode", cd->m_http_code}, {"errorDetail", cd->m_response_msg}};
        std::string json_str = json.dump();
        evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", "application/json", 0, 0));

        std::stringstream ss;
        ss << json_str.size();
        /* valloc should be 1 because ss.str().c_str() is freed once control goes out of this function */
        evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Length", ss.str().c_str(), 0, 1));
        evbuffer_add(req->buffer_out, json_str.c_str(), json_str.size());

        // Need to increment the calldata reference since evhtp_send_reply will call finish asyncronously and calldata
        // needs to stay relavant till that call.
        intrusive_ptr_add_ref(cd.get());
        evhtp_send_reply(req, cd->m_http_code);
    }

    static void internal_event_handler(evutil_socket_t socket, short events, void *user_data) {
        HttpServer *server = (HttpServer *)user_data;
        server->_internal_event_handler(socket, events);
    }

    std::unique_ptr<evhtp_ssl_cfg_t> get_ssl_opts_() {
        struct stat f_stat;
        auto ssl_config = std::make_unique<evhtp_ssl_cfg_t>();

        ssl_config->ssl_opts = 0; // SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1;
        ssl_config->pemfile = (char*)m_cfg.tls_cert_path.c_str();
        ssl_config->privfile = (char*)m_cfg.tls_key_path.c_str();

        if (ssl_config->pemfile) {
            if (stat(ssl_config->pemfile, &f_stat) != 0) {
                LOGERROR("Cannot load SSL cert: {}", ssl_config->pemfile);
                return nullptr;
            }
        }

        if (ssl_config->privfile) {
            if (stat(ssl_config->privfile, &f_stat) != 0) {
                LOGERROR("Cannot load SSL key: {}", ssl_config->privfile);
                return nullptr;
            }
        }

        return ssl_config;
    }

private:
    HttpServerConfig m_cfg;
    std::unique_ptr<std::thread> m_http_thread;
    std::vector< _handler_info > m_handlers;

    // Maintaining a list of pipe events because multiple threads could add events at the same time.
    // Additions and deletions from this list are protected by m_mutex defined .
    std::mutex m_mutex;
    EventList m_event_list;

    mutable evbase_t* m_ev_base;
    evhtp_t* m_htp;
    struct event *m_internal_event;

    bool m_is_running = false;
    std::condition_variable m_ready_cv;
};

}; // namespace sisl
