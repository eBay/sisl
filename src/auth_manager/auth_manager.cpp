#include <chrono>
#include <stdexcept>

#include <fmt/format.h>

#include "sisl/auth_manager/auth_manager.hpp"

namespace sisl {

static std::string md5_sum(std::string const& b) {
    std::array< unsigned char, MD5_DIGEST_LENGTH > result;
    uint32_t md_len;
    auto mdctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex2(mdctx, EVP_md5(), nullptr);
    EVP_DigestUpdate(mdctx, b.c_str(), b.size());
    EVP_DigestFinal_ex(mdctx, result.data(), &md_len);
    EVP_MD_CTX_free(mdctx);
    if (md_len != MD5_DIGEST_LENGTH) {
        LOGERROR("Bad digest length, expected [{}] got [{}]!", MD5_DIGEST_LENGTH, md_len);
        return std::string();
    }

    // convert to hex
    std::ostringstream ss;
    ss << std::hex;
    for (auto const c : result) {
        ss << static_cast< unsigned >(c);
    }
    return ss.str();
}

struct incomplete_verification_error : std::exception {
    explicit incomplete_verification_error(const std::string& error) : error_(error) {}
    const char* what() const noexcept { return error_.c_str(); }

private:
    const std::string error_;
};

AuthVerifyStatus AuthManager::verify(const std::string& token, std::string& msg) const {
    // if we have it in cache, just use it to make the decision
    auto const token_hash = md5_sum(token);
    if (auto const ct = m_cached_tokens.get(token_hash); ct) {
        auto const& cached_token = ct->get();
        if (cached_token.valid) {
            auto now = std::chrono::system_clock::now();
            if (now > cached_token.expires_at + std::chrono::seconds(SECURITY_DYNAMIC_CONFIG(auth_manager->leeway))) {
                m_cached_tokens.put(
                    token_hash, CachedToken{AuthVerifyStatus::UNAUTH, "token expired", false, cached_token.expires_at});
            }
        }
        msg = cached_token.msg;
        return cached_token.response_status;
    }

    // not found in cache
    CachedToken cached_token;
    std::string app_name;
    try {
        // this may throw if token is ill formed
        const auto decoded{jwt::decode< traits >(token)};

        // for any reason that causes the verification failure, an
        // exception is thrown.
        verify_decoded(decoded);
        app_name = get_app(decoded);
        cached_token.expires_at = decoded.get_expires_at();
        cached_token.set_valid();
    } catch (const incomplete_verification_error& e) {
        // verification incomplete, the token validity is not determined, shouldn't
        // cache
        msg = e.what();
        return AuthVerifyStatus::UNAUTH;
    } catch (const std::exception& e) {
        cached_token.set_invalid(AuthVerifyStatus::UNAUTH, e.what());
        m_cached_tokens.put(token_hash, cached_token);
        msg = cached_token.msg;
        return cached_token.response_status;
    }

    // check client application

    if (AUTH_CONFIG(trf_verifier->auth_allowed_apps) != "all") {
        if (AUTH_CONFIG(trf_verifier->auth_allowed_apps).find(app_name) == std::string::npos) {
            cached_token.set_invalid(AuthVerifyStatus::FORBIDDEN,
                                     fmt::format("application '{}' is not allowed to perform the request", app_name));
        }
    }

    m_cached_tokens.put(token_hash, cached_token);
    msg = cached_token.msg;
    return cached_token.response_status;
}

void AuthManager::verify_decoded(const jwt::decoded_jwt& decoded) const {
    const auto alg{decoded.get_algorithm()};
    if (alg != "RS256") throw std::runtime_error(fmt::format("unsupported algorithm: {}", alg));

    std::string signing_key;
    std::string key_id;
    auto should_cache_key = true;

    if (decoded.has_key_id()) {
        key_id = decoded.get_key_id();
        auto cached_key = m_cached_keys.get(key_id);
        if (cached_key) {
            signing_key = cached_key->get();
            should_cache_key = false;
        }
    } else {
        should_cache_key = false;
    }

    if (signing_key.empty()) {
        if (!decoded.has_header_claim("x5u")) throw std::runtime_error("no indication of verification key");

        auto key_url = decoded.get_header_claim("x5u").as_string();

        if (key_url.rfind(AUTH_CONFIG(trf_verifier->tf_token_url), 0) != 0) {
            throw std::runtime_error(fmt::format("key url {} is not trusted", key_url));
        }
        signing_key = download_key(key_url);
    }

    if (should_cache_key) { m_cached_keys.put(key_id, signing_key); }

    const auto verifier{jwt::verify< traits >()
                            .with_issuer(SECURITY_DYNAMIC_CONFIG(auth_manager->issuer))
                            .allow_algorithm(jwt::algorithm::rs256(signing_key))
                            .expires_at_leeway(SECURITY_DYNAMIC_CONFIG(auth_manager->leeway))};

    // if verification fails, an instance of std::system_error subclass is thrown.
    verifier.verify(decoded);
}

std::string AuthManager::download_key(const std::string& key_url) const {
    cpr::Session session;
    session.SetUrl(cpr::Url{key_url});
    if (SECURITY_DYNAMIC_CONFIG(auth_manager->verify)) {
        auto ca_file{SECURITY_DYNAMIC_CONFIG(ssl_ca_file)};
        auto cert_file{SECURITY_DYNAMIC_CONFIG(ssl_cert_file)};
        auto key_file{SECURITY_DYNAMIC_CONFIG(ssl_key_file)};

        // constructor for CaInfo does std::move(filename)
        auto sslOpts{cpr::Ssl(cpr::ssl::CaInfo{std::move(ca_file)})};
        sslOpts.SetOption(cpr::ssl::CertFile{std::move(cert_file)});
        sslOpts.SetOption(cpr::ssl::KeyFile{std::move(key_file)});
        session.SetOption(sslOpts);
    }

    session.SetTimeout(std::chrono::milliseconds{5000});
    const auto resp{session.Get()};

    if (resp.error) { throw std::runtime_error(fmt::format("download key failed: {}", resp.error.message)); }
    if (resp.status_code != 200) { throw std::runtime_error(fmt::format("download key failed: {}", resp.text)); }

    return resp.text;
}

std::string AuthManager::get_app(const jwt::decoded_jwt& decoded) const {
    // get app name from client_id, which is the "sub" field in the decoded token
    // body
    // https://pages.github.corp.ebay.com/security-platform/documents/tf-documentation/tessintegration/#environment-variables
    if (!decoded.has_payload_claim("sub")) return "";

    const auto client_id{decoded.get_payload_claim("sub").as_string()};
    const auto start{client_id.find(",o=") + 3};
    const auto end{client_id.find_first_of(",", start)};
    return client_id.substr(start, end - start);
}
} // namespace sisl
