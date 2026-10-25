#ifndef AAPTLS_H
#define AAPTLS_H

// TLS 1.2 inside AAP messages: OCTAVE is the TLS client, the phone the
// server, and the bytes travel in ENCAPSULATED_SSL messages and encrypted
// frames instead of a socket, so OpenSSL runs over a memory BIO pair.
// The client certificate must chain to Google's head-unit CA; OCTAVE ships
// none and loads the PEM the user points it at.

#include <QByteArray>
#include <QString>

struct ssl_ctx_st;
struct ssl_st;
struct bio_st;

class AapTls
{
public:
    AapTls() = default;
    ~AapTls();
    AapTls(const AapTls &) = delete;
    AapTls &operator=(const AapTls &) = delete;

    // Certificate and key, PEM (the key may follow the certificate in the
    // same buffer). False with error() set when OpenSSL rejects them.
    bool init(const QByteArray &certPem, const QByteArray &keyPem);
    QString error() const { return m_error; }

    enum class Step { Continue, Done, Failed };
    // Feeds the phone's handshake bytes (empty to start) and returns what to
    // send back in `out`
    Step handshake(const QByteArray &in, QByteArray &out);
    bool isEstablished() const { return m_established; }

    // Application data. decrypt() takes one frame's ciphertext and returns
    // whatever plaintext it completes (possibly empty); false on a TLS error.
    bool encrypt(const QByteArray &plain, QByteArray &cipher);
    bool decrypt(const QByteArray &cipher, QByteArray &plain);

    // The phone's certificate subject, once established (for the log)
    QString peerSubject() const;
    QString cipherName() const;

private:
    QByteArray drainOut();
    void setOpenSslError(const QString &what);

    ssl_ctx_st *m_ctx = nullptr;
    ssl_st *m_ssl = nullptr;
    bio_st *m_in = nullptr;      // phone → OpenSSL
    bio_st *m_out = nullptr;     // OpenSSL → phone
    bool m_established = false;
    QString m_error;
};

#endif // AAPTLS_H
