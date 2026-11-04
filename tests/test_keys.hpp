// RSA-ключи для тестов JWT: генерируются при первом обращении (OpenSSL),
// PEM — во временный файл для crypto::RsaSigningKey. Подписанные ими JWT
// проверяются тем же кодом, что и в сервисе (jwt::verify_rs256 по n/e).
#pragma once

#include <cstdio>
#include <ctime>
#include <memory>
#include <stdexcept>
#include <string>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include "access.hpp"
#include "nlohmann/json.hpp"
#include "util/crypto.hpp"
#include "util/jwt.hpp"

namespace testkeys {

class Key {
public:
    Key() {
        EVP_PKEY* pkey = EVP_RSA_gen(2048);
        if (pkey == nullptr) throw std::runtime_error("EVP_RSA_gen failed");
        char path[] = "/tmp/wallet-mcp-test-key-XXXXXX";
        int fd = mkstemp(path);
        if (fd < 0) throw std::runtime_error("mkstemp failed");
        FILE* f = fdopen(fd, "w");
        PEM_write_PrivateKey(f, pkey, nullptr, nullptr, 0, nullptr, nullptr);
        fclose(f);
        EVP_PKEY_free(pkey);
        key_ = std::make_unique<crypto::RsaSigningKey>(path);
        std::remove(path);
    }

    std::string sign(const nlohmann::json& payload) const { return jwt::sign_rs256(payload, *key_); }
    const std::string& kid() const { return key_->kid(); }
    jwks::PublicKey public_key() const { return {key_->modulus(), key_->exponent()}; }

private:
    std::unique_ptr<crypto::RsaSigningKey> key_;
};

// Ключ «google-auth» и чужой ключ — один раз на процесс (генерация небыстрая).
inline const Key& google_auth() {
    static Key k;
    return k;
}
inline const Key& foreign() {
    static Key k;
    return k;
}

// JWKS с одним ключом google-auth; *lookups считает обращения.
inline mcpaccess::KeyLookup jwks_lookup(int* lookups = nullptr) {
    return [lookups](const std::string& kid) -> std::optional<jwks::PublicKey> {
        if (lookups != nullptr) ++*lookups;
        if (kid != google_auth().kid()) return std::nullopt;
        return google_auth().public_key();
    };
}

inline constexpr const char* kSub = "22222222-2222-4222-8222-222222222222";

// Claims JWT, который wallet-ai получает обменом для wallet-mcp.
inline nlohmann::json mcp_claims(long ttl = 300) {
    std::time_t now = std::time(nullptr);
    return {{"iss", "google-auth"},
            {"sub", kSub},
            {"aud", nlohmann::json::array({"wallet-mcp"})},
            {"scope", "mcp:read"},
            {"iat", now},
            {"exp", now + ttl},
            {"device_id", "d-bot"},
            {"act", {{"sub", "wallet-ai"}}}};
}

// JWT веб-сессии: в aud нет wallet-mcp.
inline nlohmann::json web_claims() {
    std::time_t now = std::time(nullptr);
    return {{"iss", "google-auth"},
            {"sub", kSub},
            {"aud", nlohmann::json::array({"wallet-api", "wallet-report", "wallet-ai"})},
            {"scope", "reports:read expense:write ai:use"},
            {"iat", now},
            {"exp", now + 900}};
}

inline std::string mcp_jwt(long ttl = 300) { return google_auth().sign(mcp_claims(ttl)); }

} // namespace testkeys
