#pragma once

#include <string>
#include <map>
#include <chrono>
#include <memory>

#include <boost/optional/optional_io.hpp>

namespace utility
{
	namespace string
	{
		/*
		Search for associative key-value expressions and return a value matches to a key,
		expected that the key values are unique into the passed source
		If not found a key, empty value is returned
		example: key=value
		*/
		std::string search_value(const std::string& /*source*/, std::string /*key*/);
	}

	namespace digest
	{
		class INonceValueGeneratorStrategy
		{
		public:
			virtual std::string generate() = 0;
		};

		class NonceGeneratorConcrete : public INonceValueGeneratorStrategy
		{
		public:
			// Inherited via INonceValueGeneratorStrategyresponse
			virtual std::string generate() override;
		};

		struct DigestResponseHeader
		{
			std::string realm;
			std::string nonce;
			std::string qop;
		};

		struct DigestRequestHeader
		{
			std::string username;
			std::string realm;
			std::string nonce;
			std::string digest_uri;

			//this fialds are optional actually, so they could be empty
			std::string response;
			std::string algorithm;
			std::string cnonce;
			std::string opaque;
			std::string message_qop;
			std::string nonce_count;
		};

		DigestRequestHeader extract_DA(const std::string& /*www_auth_line*/);

		class IDigestSession
		{
		public:
			//should generate a new nonce and add it to the pool
			virtual DigestResponseHeader generateDigest() = 0;

			//returns result of verification
			//when result is false, implementation should indicate
			//whether a nonce is staled or not using @isStaled flag
			virtual bool verifyDigest(const DigestRequestHeader& digestInfo, bool& isStaled) = 0;

		protected:
			IDigestSession(const std::string& realm, const std::string& qop)
				:realm_(realm), qop_(qop) {}

			//the pool is used to hold all generated nonce
			std::map<std::string, std::chrono::milliseconds> nonce_pool;

			const std::string realm_;
			const std::string qop_;
		};


		
		class DigestSessionImpl : public utility::digest::IDigestSession
		{
		public:
			DigestSessionImpl(const std::string& realm = "Realm", const std::string& qop = "auth")
				: IDigestSession(realm, qop)
			{
			}

			DigestResponseHeader generateDigest() override
			{
				nonce_generator_->generate();
			}

			void setGenerator(const std::shared_ptr<INonceValueGeneratorStrategy>& generator)
			{
				nonce_generator_ = generator;
			}

			bool verifyDigest(const DigestRequestHeader& digestInfo, bool& isStaled) override
			{
				return false;
			}

			std::shared_ptr<INonceValueGeneratorStrategy> nonce_generator_;
		};

	}
}