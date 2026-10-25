#include "aaptls.h"

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

AapTls::~AapTls()
{
    // SSL_free also frees the BIOs handed to SSL_set_bio
    if (m_ssl)
        SSL_free(m_ssl);
    if (m_ctx)
        SSL_CTX_free(m_ctx);
}

void AapTls::setOpenSslError(const QString &what)
{
    char buf[256] = {};
    const unsigned long e = ERR_get_error();
    if (e)
        ERR_error_string_n(e, buf, sizeof buf);
    ERR_clear_error();
    m_error = e ? QStringLiteral("%1: %2").arg(what, QString::fromLatin1(buf)) : what;
}

bool AapTls::init(const QByteArray &certPem, const QByteArray &keyPem)
{
    m_ctx = SSL_CTX_new(TLS_client_method());
    if (!m_ctx) {
        setOpenSslError(QStringLiteral("could not create a TLS context"));
        return false;
    }
    SSL_CTX_set_min_proto_version(m_ctx, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(m_ctx, TLS1_2_VERSION);
    // The phone's certificate is not checked (aasdk doesn't either), and the
    // head-unit certificate is RSA 2048 from 2014: allow it at any level
    SSL_CTX_set_verify(m_ctx, SSL_VERIFY_NONE, nullptr);
    SSL_CTX_set_security_level(m_ctx, 0);

    BIO *cb = BIO_new_mem_buf(certPem.constData(), int(certPem.size()));
    X509 *cert = cb ? PEM_read_bio_X509(cb, nullptr, nullptr, nullptr) : nullptr;
    BIO_free(cb);
    if (!cert) {
        setOpenSslError(QStringLiteral("the head-unit certificate is not a PEM certificate"));
        return false;
    }
    const int certOk = SSL_CTX_use_certificate(m_ctx, cert);
    X509_free(cert);
    if (certOk != 1) {
        setOpenSslError(QStringLiteral("OpenSSL refused the head-unit certificate"));
        return false;
    }
    BIO *kb = BIO_new_mem_buf(keyPem.constData(), int(keyPem.size()));
    // No passphrase callback would make OpenSSL prompt on the terminal (from
    // the worker thread); an encrypted key is simply refused
    pem_password_cb *noPassphrase = [](char *, int, int, void *) { return 0; };
    EVP_PKEY *key = kb ? PEM_read_bio_PrivateKey(kb, nullptr, noPassphrase, nullptr) : nullptr;
    BIO_free(kb);
    if (!key) {
        setOpenSslError(QStringLiteral("no usable private key for the head-unit certificate "
                                       "(a passphrase-protected key is not supported)"));
        return false;
    }
    const int keyOk = SSL_CTX_use_PrivateKey(m_ctx, key);
    EVP_PKEY_free(key);
    if (keyOk != 1 || SSL_CTX_check_private_key(m_ctx) != 1) {
        setOpenSslError(QStringLiteral("the private key does not match the head-unit certificate"));
        return false;
    }

    m_ssl = SSL_new(m_ctx);
    m_in = BIO_new(BIO_s_mem());
    m_out = BIO_new(BIO_s_mem());
    if (!m_ssl || !m_in || !m_out) {
        setOpenSslError(QStringLiteral("could not create the TLS session"));
        return false;
    }
    SSL_set_bio(m_ssl, m_in, m_out);
    SSL_set_connect_state(m_ssl);
    return true;
}

QByteArray AapTls::drainOut()
{
    QByteArray out;
    char buf[4096];
    int n;
    while ((n = BIO_read(m_out, buf, sizeof buf)) > 0)
        out.append(buf, n);
    return out;
}

AapTls::Step AapTls::handshake(const QByteArray &in, QByteArray &out)
{
    if (!m_ssl)
        return Step::Failed;
    if (!in.isEmpty())
        BIO_write(m_in, in.constData(), int(in.size()));
    const int r = SSL_do_handshake(m_ssl);
    out = drainOut();
    if (r == 1) {
        m_established = true;
        return Step::Done;
    }
    const int e = SSL_get_error(m_ssl, r);
    if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE)
        return Step::Continue;
    setOpenSslError(QStringLiteral("TLS handshake failed"));
    return Step::Failed;
}

bool AapTls::encrypt(const QByteArray &plain, QByteArray &cipher)
{
    if (!m_established)
        return false;
    if (!plain.isEmpty() && SSL_write(m_ssl, plain.constData(), int(plain.size())) <= 0) {
        setOpenSslError(QStringLiteral("TLS write failed"));
        return false;
    }
    cipher = drainOut();
    return true;
}

bool AapTls::decrypt(const QByteArray &cipher, QByteArray &plain)
{
    if (!m_established)
        return false;
    BIO_write(m_in, cipher.constData(), int(cipher.size()));
    plain.clear();
    char buf[16384];
    for (;;) {
        const int n = SSL_read(m_ssl, buf, sizeof buf);
        if (n > 0) {
            plain.append(buf, n);
            continue;
        }
        const int e = SSL_get_error(m_ssl, n);
        if (e == SSL_ERROR_WANT_READ)
            return true;
        if (e == SSL_ERROR_ZERO_RETURN)
            return true;
        setOpenSslError(QStringLiteral("TLS read failed"));
        return false;
    }
}

QString AapTls::peerSubject() const
{
    if (!m_ssl)
        return QString();
    X509 *peer = SSL_get1_peer_certificate(m_ssl);
    if (!peer)
        return QString();
    char buf[512] = {};
    X509_NAME_oneline(X509_get_subject_name(peer), buf, sizeof buf);
    X509_free(peer);
    return QString::fromUtf8(buf);
}

QString AapTls::cipherName() const
{
    return m_ssl ? QString::fromLatin1(SSL_get_cipher_name(m_ssl)) : QString();
}
