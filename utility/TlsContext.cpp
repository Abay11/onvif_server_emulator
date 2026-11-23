#include "TlsContext.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <stdexcept>

namespace utility
{
std::shared_ptr<boost::asio::ssl::context> MakeTlsContext(const std::string& certificate,
																													const std::string& private_key,
																													const std::string& advertised_ipv4)
{
	auto context = std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tls_server);
	auto ssl = context->native_handle();
	if (SSL_CTX_set_min_proto_version(ssl, TLS1_2_VERSION) != 1)
		throw std::runtime_error("Could not require TLS 1.2");
	context->set_options(boost::asio::ssl::context::no_compression);
	if (!certificate.empty() || !private_key.empty())
	{
		if (certificate.empty() || private_key.empty())
			throw std::runtime_error("https.certificate and https.private_key must both be configured");
		context->use_certificate_chain_file(certificate);
		context->use_private_key_file(private_key, boost::asio::ssl::context::pem);
	}
	else
	{
		auto fail = [] { throw std::runtime_error("Could not generate HTTPS development certificate"); };
		std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> generator(EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr),
																																					EVP_PKEY_CTX_free);
		EVP_PKEY* generated = nullptr;
		if (!generator || EVP_PKEY_keygen_init(generator.get()) <= 0 ||
				EVP_PKEY_CTX_set_rsa_keygen_bits(generator.get(), 2048) <= 0 ||
				EVP_PKEY_keygen(generator.get(), &generated) <= 0)
			fail();
		std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(generated, EVP_PKEY_free);
		std::unique_ptr<X509, decltype(&X509_free)> cert(X509_new(), X509_free);
		unsigned char serial[16];
		if (!cert || RAND_bytes(serial, sizeof(serial)) != 1)
			fail();
		serial[0] &= 0x7f;
		std::unique_ptr<BIGNUM, decltype(&BN_free)> number(BN_bin2bn(serial, sizeof(serial), nullptr), BN_free);
		if (!number || !BN_to_ASN1_INTEGER(number.get(), X509_get_serialNumber(cert.get())) ||
				X509_set_version(cert.get(), 2) != 1 || !X509_gmtime_adj(X509_getm_notBefore(cert.get()), -60) ||
				!X509_gmtime_adj(X509_getm_notAfter(cert.get()), 365L * 86400) || X509_set_pubkey(cert.get(), key.get()) != 1)
			fail();
		auto name = X509_get_subject_name(cert.get());
		if (X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
																	 reinterpret_cast<const unsigned char*>("ONVIF emulator development"), -1, -1,
																	 0) != 1 ||
				X509_set_issuer_name(cert.get(), name) != 1)
			fail();
		const std::string names = "DNS:localhost,IP:127.0.0.1,IP:" + advertised_ipv4;
		std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)> san(
				X509V3_EXT_conf_nid(nullptr, nullptr, NID_subject_alt_name, names.c_str()), X509_EXTENSION_free);
		if (!san || X509_add_ext(cert.get(), san.get(), -1) != 1 || X509_sign(cert.get(), key.get(), EVP_sha256()) <= 0 ||
				SSL_CTX_use_certificate(ssl, cert.get()) != 1 || SSL_CTX_use_PrivateKey(ssl, key.get()) != 1)
			fail();
	}
	if (SSL_CTX_check_private_key(ssl) != 1)
		throw std::runtime_error("HTTPS certificate does not match its private key");
	return context;
}
} // namespace utility
