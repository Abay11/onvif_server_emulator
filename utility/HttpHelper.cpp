#include "HttpHelper.h"

namespace utility
{
	namespace http
	{
		const char HEADER_AUTHORIZATION[] = "Authorization";
		const char HEADER_WWW_AUTHORIZATION[] = "WWW-Authenticate";
		
		const char DIGEST_USERNAME[] = "Digest username";
		const char DIGEST_REALM[] = "realm";
		const char DIGEST_MESSAGE_QOP[] = "qop";
		const char DIGEST_ALGORITHM[] = "algorithm";
		const char DIGEST_URI[] = "uri";
		const char DIGEST_NONCE[] = "nonce";
		const char DIGEST_NONCE_COUNT[] = "nc";
		const char DIGEST_CNONCE[] = "cnonce";
		const char DIGEST_RESPONSE[] = "response";
		const char DIGEST_OPAQUE[] = "opaque";

		const char RESPONSE_UNAUTHORIZED[] = "HTTP/1.1 401 Unauthorized";
		
	}
}