#include "nagram/services/model.h"
#include "base/basic_types.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>

#include <iostream>
#include <stdexcept>

namespace {

void Require(bool value, const char *message) {
	if (!value) {
		throw std::runtime_error(message);
	}
}

} // namespace

void TestServices() {
	using namespace Nagram;
	const auto service = ServiceDefinition{
		.id = u"00000000-0000-0000-0000-000000000001"_q,
		.name = u"Local translation"_q,
		.kind = ServiceKind::Translation,
		.protocol = u"openai"_q,
		.baseUrl = QUrl(u"http://127.0.0.1:18765/v1/"_q),
		.endpoint = u"chat/completions"_q,
		.model = u"stub"_q,
		.credentialRef = u"00000000-0000-0000-0000-000000000002"_q,
	};
	const auto serialized = SerializeService(service);
	Require(ParseService(serialized).has_value(), "valid local service rejected");
	Require(!serialized.contains(u"apiKey"_q)
		&& !serialized.contains(u"secret"_q), "secret serialized");
	auto config = ServicesDefaults();
	config.insert(u"instances"_q, QJsonArray{ serialized });
	config.insert(u"translation"_q, service.id);
	Require(ValidServices(config), "valid service selection rejected");
	Require(ValidServicesBytes(QJsonDocument(config).toJson()),
		"valid service bytes rejected");
	const auto account = CredentialAccount(service);
	auto changed = service;
	changed.endpoint = u"other"_q;
	Require(CredentialAccount(changed) != account,
		"credential binding ignored endpoint");
	auto invalid = serialized;
	invalid.insert(u"baseUrl"_q, u"http://example.com/v1/"_q);
	Require(!ParseService(invalid), "remote HTTP accepted");
	invalid = serialized;
	invalid.insert(u"endpoint"_q, u"../other"_q);
	Require(!ParseService(invalid), "path traversal accepted");
	invalid = serialized;
	invalid.insert(u"apiKey"_q, u"must-not-store"_q);
	Require(!ParseService(invalid), "secret field accepted");
	config.insert(u"transcription"_q, service.id);
	Require(!ValidServices(config), "cross-kind selection accepted");
	Require(!ValidServicesBytes("{broken"), "invalid JSON accepted");

	auto deepl = service;
	deepl.protocol = u"deepl"_q;
	deepl.model = u"prefer_less"_q;
	Require(ParseService(SerializeService(deepl)).has_value(),
		"DeepL formality rejected");
	deepl.model = u"formal"_q;
	Require(!ParseService(SerializeService(deepl)),
		"unknown DeepL formality accepted");
	deepl.model = u"more"_q;
	const auto deeplCall = BuildTranslationCall(deepl, { u"Hi"_q }, u"zh"_q);
	Require(deeplCall.body.object().value(u"formality"_q) == u"more"_q
		&& deeplCall.body.object().value(u"target_lang"_q) == u"ZH"_q,
		"DeepL request body");

	auto microsoft = service;
	microsoft.protocol = u"microsoft"_q;
	microsoft.model = u"eastasia"_q;
	Require(ParseService(SerializeService(microsoft)).has_value(),
		"Microsoft region rejected");
	microsoft.model = u"east asia"_q;
	Require(!ParseService(SerializeService(microsoft)),
		"invalid Microsoft region accepted");
	microsoft.model = u"eastasia"_q;
	const auto microsoftCall = BuildTranslationCall(
		microsoft,
		{ u"A"_q, u"B"_q },
		u"ja"_q);
	Require(microsoftCall.body.isArray()
		&& microsoftCall.body.array().size() == 2
		&& microsoftCall.query.queryItemValue(u"to"_q) == u"ja"_q
		&& microsoftCall.query.queryItemValue(u"api-version"_q) == u"3.0"_q,
		"Microsoft request");
	const auto microsoftResult = ParseTranslationResult(
		microsoft,
		R"([{"translations":[{"text":"a"}]},{"translations":[{"text":"b"}]}])",
		2);
	Require(microsoftResult
		&& (*microsoftResult == QStringList{ u"a"_q, u"b"_q }),
		"Microsoft response");
	const auto headers = AuthHeaders(microsoft, "key");
	Require(headers.size() == 2
		&& headers[0].first == "Ocp-Apim-Subscription-Key"
		&& headers[1].second == "eastasia",
		"Microsoft headers");

	auto google = service;
	google.protocol = u"google"_q;
	google.model = QString();
	Require(ParseService(SerializeService(google)).has_value(),
		"Google service rejected");
	const auto googleResult = ParseTranslationResult(
		google,
		R"({"data":{"translations":[{"translatedText":"x"}]}})",
		1);
	Require(googleResult && googleResult->front() == u"x"_q,
		"Google response");
	Require(AuthHeaders(google, "k").front().first == "X-goog-api-key",
		"Google header");

	auto yandex = service;
	yandex.protocol = u"yandex"_q;
	yandex.model = u"b1gabc"_q;
	const auto yandexCall = BuildTranslationCall(yandex, { u"Hi"_q }, u"ru"_q);
	Require(yandexCall.body.object().value(u"folderId"_q) == u"b1gabc"_q
		&& yandexCall.body.object().value(u"targetLanguageCode"_q) == u"ru"_q,
		"Yandex request");
	Require(!ParseTranslationResult(
			yandex,
			R"({"translations":[{"text":""}]})",
			1),
		"empty translation accepted");
	auto yandexTranscription = yandex;
	yandexTranscription.kind = ServiceKind::Transcription;
	Require(!ParseService(SerializeService(yandexTranscription)),
		"non-OpenAI transcription accepted");
	std::cout << "PASS: Nagram service config and credential binding" << std::endl;
}
