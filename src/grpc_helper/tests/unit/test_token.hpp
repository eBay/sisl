#pragma once

namespace grpc_helper::testing {
// public and private keys for unit test

static const std::string rsa_pub_key = "-----BEGIN PUBLIC KEY-----\n"
                                       "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuGbXWiK3dQTyCbX5xdE4\n"
                                       "yCuYp0AF2d15Qq1JSXT/lx8CEcXb9RbDddl8jGDv+spi5qPa8qEHiK7FwV2KpRE9\n"
                                       "83wGPnYsAm9BxLFb4YrLYcDFOIGULuk2FtrPS512Qea1bXASuvYXEpQNpGbnTGVs\n"
                                       "WXI9C+yjHztqyL2h8P6mlThPY9E9ue2fCqdgixfTFIF9Dm4SLHbphUS2iw7w1JgT\n"
                                       "69s7of9+I9l5lsJ9cozf1rxrXX4V1u/SotUuNB3Fp8oB4C1fLBEhSlMcUJirz1E8\n"
                                       "AziMCxS+VrRPDM+zfvpIJg3JljAh3PJHDiLu902v9w+Iplu1WyoB2aPfitxEhRN0\n"
                                       "YwIDAQAB\n"
                                       "-----END PUBLIC KEY-----";

static const std::string rsa_priv_key = "-----BEGIN PRIVATE KEY-----\n"
                                        "MIIEvwIBADANBgkqhkiG9w0BAQEFAASCBKkwggSlAgEAAoIBAQC4ZtdaIrd1BPIJ\n"
                                        "tfnF0TjIK5inQAXZ3XlCrUlJdP+XHwIRxdv1FsN12XyMYO/6ymLmo9ryoQeIrsXB\n"
                                        "XYqlET3zfAY+diwCb0HEsVvhisthwMU4gZQu6TYW2s9LnXZB5rVtcBK69hcSlA2k\n"
                                        "ZudMZWxZcj0L7KMfO2rIvaHw/qaVOE9j0T257Z8Kp2CLF9MUgX0ObhIsdumFRLaL\n"
                                        "DvDUmBPr2zuh/34j2XmWwn1yjN/WvGtdfhXW79Ki1S40HcWnygHgLV8sESFKUxxQ\n"
                                        "mKvPUTwDOIwLFL5WtE8Mz7N++kgmDcmWMCHc8kcOIu73Ta/3D4imW7VbKgHZo9+K\n"
                                        "3ESFE3RjAgMBAAECggEBAJTEIyjMqUT24G2FKiS1TiHvShBkTlQdoR5xvpZMlYbN\n"
                                        "tVWxUmrAGqCQ/TIjYnfpnzCDMLhdwT48Ab6mQJw69MfiXwc1PvwX1e9hRscGul36\n"
                                        "ryGPKIVQEBsQG/zc4/L2tZe8ut+qeaK7XuYrPp8bk/X1e9qK5m7j+JpKosNSLgJj\n"
                                        "NIbYsBkG2Mlq671irKYj2hVZeaBQmWmZxK4fw0Istz2WfN5nUKUeJhTwpR+JLUg4\n"
                                        "ELYYoB7EO0Cej9UBG30hbgu4RyXA+VbptJ+H042K5QJROUbtnLWuuWosZ5ATldwO\n"
                                        "u03dIXL0SH0ao5NcWBzxU4F2sBXZRGP2x/jiSLHcqoECgYEA4qD7mXQpu1b8XO8U\n"
                                        "6abpKloJCatSAHzjgdR2eRDRx5PMvloipfwqA77pnbjTUFajqWQgOXsDTCjcdQui\n"
                                        "wf5XAaWu+TeAVTytLQbSiTsBhrnoqVrr3RoyDQmdnwHT8aCMouOgcC5thP9vQ8Us\n"
                                        "rVdjvRRbnJpg3BeSNimH+u9AHgsCgYEA0EzcbOltCWPHRAY7B3Ge/AKBjBQr86Kv\n"
                                        "TdpTlxePBDVIlH+BM6oct2gaSZZoHbqPjbq5v7yf0fKVcXE4bSVgqfDJ/sZQu9Lp\n"
                                        "PTeV7wkk0OsAMKk7QukEpPno5q6tOTNnFecpUhVLLlqbfqkB2baYYwLJR3IRzboJ\n"
                                        "FQbLY93E8gkCgYB+zlC5VlQbbNqcLXJoImqItgQkkuW5PCgYdwcrSov2ve5r/Acz\n"
                                        "FNt1aRdSlx4176R3nXyibQA1Vw+ztiUFowiP9WLoM3PtPZwwe4bGHmwGNHPIfwVG\n"
                                        "m+exf9XgKKespYbLhc45tuC08DATnXoYK7O1EnUINSFJRS8cezSI5eHcbQKBgQDC\n"
                                        "PgqHXZ2aVftqCc1eAaxaIRQhRmY+CgUjumaczRFGwVFveP9I6Gdi+Kca3DE3F9Pq\n"
                                        "PKgejo0SwP5vDT+rOGHN14bmGJUMsX9i4MTmZUZ5s8s3lXh3ysfT+GAhTd6nKrIE\n"
                                        "kM3Nh6HWFhROptfc6BNusRh1kX/cspDplK5x8EpJ0QKBgQDWFg6S2je0KtbV5PYe\n"
                                        "RultUEe2C0jYMDQx+JYxbPmtcopvZQrFEur3WKVuLy5UAy7EBvwMnZwIG7OOohJb\n"
                                        "vkSpADK6VPn9lbqq7O8cTedEHttm6otmLt8ZyEl3hZMaL3hbuRj6ysjmoFKx6CrX\n"
                                        "rK0/Ikt5ybqUzKCMJZg2VKGTxg==\n"
                                        "-----END PRIVATE KEY-----";

struct TestToken {
    using token_t = jwt::builder;

    TestToken() :
            token{jwt::create()
                      .set_type("JWT")
                      .set_algorithm("RS256")
                      .set_key_id("abc123")
                      .set_issuer("trustfabric")
                      .set_header_claim("x5u", jwt::claim(std::string{"http://127.0.0.1:12346/download_key"}))
                      .set_audience(std::set< std::string >{"test-sisl", "protegoreg"})
                      .set_issued_at(std::chrono::system_clock::now() - std::chrono::seconds(180))
                      .set_not_before(std::chrono::system_clock::now() - std::chrono::seconds(180))
                      .set_expires_at(std::chrono::system_clock::now() + std::chrono::seconds(180))
                      .set_subject("uid=sdsapp,networkaddress=dummy_ip,ou=orchmanager+l="
                                   "production,o=testapp,dc=tess,dc=ebay,dc=com")
                      .set_payload_claim("ver", jwt::claim(std::string{"2"}))
                      .set_payload_claim("vpc", jwt::claim(std::string{"production"}))
                      .set_payload_claim("instances", jwt::claim(std::string{"dummy_ip"}))} {}

    std::string sign_rs256() { return token.sign(jwt::algorithm::rs256(rsa_pub_key, rsa_priv_key, "", "")); }
    std::string sign_rs512() { return token.sign(jwt::algorithm::rs512(rsa_pub_key, rsa_priv_key, "", "")); }
    token_t& get_token() { return token; }

private:
    token_t token;
};
} // namespace grpc_helper::testing