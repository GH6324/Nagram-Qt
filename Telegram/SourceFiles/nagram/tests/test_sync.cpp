#include "nagram/core/exchange.h"
#include "nagram/snapshot/cloud_theme_model.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

namespace {

class MemoryPrefs final : public Nagram::RawPrefs {
public:
	[[nodiscard]] QByteArray read(std::string_view key) override {
		const auto found = values.find(std::string(key));
		return (found == values.end()) ? QByteArray() : found->second;
	}
	void write(std::string_view key, const QByteArray &value) override {
		values[std::string(key)] = value;
	}
	void clear(std::string_view key) override {
		values.erase(std::string(key));
	}

	std::map<std::string, QByteArray> values;
};

void Require(bool condition, const char *message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

constexpr auto kUser = quint64(777000111);
constexpr auto kOtherUser = quint64(777000222);

void TestCloudThemeRef() {
	using namespace Nagram;
	using namespace Nagram::Snapshot;

	const auto ref = CloudThemeRef{
		.user = kUser,
		.themeId = 0xFFFFFFFFFFFFFFF0ULL,
		.accessHash = 0x8000000000000001ULL,
		.documentId = 5,
		.title = QString::fromUtf8("Night\nsky"),
		.slug = QString::fromLatin1("night"),
	};
	const auto raw = SerializeCloudThemeRef(ref);
	const auto parsed = ParseCloudThemeRef(raw);
	Require(parsed
		&& parsed->user == kUser
		&& parsed->themeId == ref.themeId
		&& parsed->accessHash == ref.accessHash
		&& parsed->documentId == 5
		&& parsed->title == QString::fromLatin1("Night sky")
		&& parsed->slug == ref.slug,
		"cloud theme reference round trip");
	Require(ValidCloudThemeRef(raw) && ValidCloudThemeRef(QByteArray()),
		"cloud theme reference accepted");

	const auto with = [&](const char *key, const QJsonValue &value) {
		auto object = QJsonDocument::fromJson(raw).object();
		object.insert(QString::fromLatin1(key), value);
		return QJsonDocument(object).toJson(QJsonDocument::Compact);
	};
	const auto without = [&](const char *key) {
		auto object = QJsonDocument::fromJson(raw).object();
		object.remove(QString::fromLatin1(key));
		return QJsonDocument(object).toJson(QJsonDocument::Compact);
	};
	for (const auto key : {
		"version", "user", "themeId", "accessHash", "documentId", "title",
		"slug" }) {
		Require(!ValidCloudThemeRef(without(key)),
			"cloud theme reference with a missing field accepted");
	}
	for (const auto key : { "user", "themeId", "accessHash", "documentId" }) {
		Require(!ValidCloudThemeRef(with(key, QString::fromLatin1("0")))
			&& !ValidCloudThemeRef(with(key, QString::fromLatin1("12x")))
			&& !ValidCloudThemeRef(with(key, QString::fromLatin1("-4")))
			&& !ValidCloudThemeRef(with(key, QString::fromLatin1("007")))
			&& !ValidCloudThemeRef(with(key, 12)),
			"cloud theme reference with a bad id accepted");
	}
	Require(!ValidCloudThemeRef(with("title",
			QString(kCloudThemeTitleLimit + 1, QChar(u'x'))))
		&& ValidCloudThemeRef(with("title",
			QString(kCloudThemeTitleLimit, QChar(u'x'))))
		&& !ValidCloudThemeRef(with("title", QString::fromLatin1("a\nb")))
		&& !ValidCloudThemeRef(with("title", 1)),
		"cloud theme title limits");
	Require(!ValidCloudThemeRef(with("extra", true))
		&& !ValidCloudThemeRef(with("version", 2))
		&& !ValidCloudThemeRef("[]")
		&& !ValidCloudThemeRef("{"),
		"malformed cloud theme reference accepted");

	Require(ValidCloudAccount(QString())
		&& ValidCloudAccount(QString::fromLatin1("777000111"))
		&& ValidCloudAccount(QString::fromLatin1("72057594038705039"))
		&& !ValidCloudAccount(QString::fromLatin1("0"))
		&& !ValidCloudAccount(QString::fromLatin1("0123"))
		&& !ValidCloudAccount(QString::fromLatin1("-5"))
		&& !ValidCloudAccount(QString::fromLatin1("12 "))
		&& !ValidCloudAccount(QString::fromLatin1("abc")),
		"cloud theme account pointer format");

	auto registry = Registry();
	RegisterCloudThemeOptions(registry);
	const auto themeInfo = registry.Find(kCloudTheme.key);
	const auto accountInfo = registry.Find(kCloudAccount.key);
	Require(themeInfo
		&& themeInfo->scope == Scope::Account
		&& themeInfo->fallbackRaw.isEmpty()
		&& !registry.HasFlag(kCloudTheme.key, Flag::Exportable),
		"cloud theme reference must be account data, empty by default");
	Require(accountInfo
		&& accountInfo->scope == Scope::Device
		&& accountInfo->fallbackRaw == "s"
		&& registry.HasFlag(kCloudAccount.key, Flag::Hidden)
		&& !registry.HasFlag(kCloudAccount.key, Flag::Exportable),
		"cloud theme pointer must be hidden device data, empty by default");

	auto devicePrefs = MemoryPrefs();
	auto device = Options(devicePrefs);
	Require(device.Set(kCloudAccount, QString::number(kUser))
		&& !device.Set(kCloudAccount, QString::fromLatin1("me")),
		"cloud theme pointer write");
	const auto exported = Exchange::Export(device, registry);
	Require(exported.invalidKeys.isEmpty()
		&& !exported.data.contains("snapshotCloud")
		&& !exported.data.contains("777000111"),
		"cloud theme pointer leaked into the export");
	const auto plan = Exchange::PlanImport(device, registry,
		R"({"version":1,"options":{"nagram.snapshotCloudAccount":"5","nagram.snapshotCloudTheme":{"version":1}}})");
	Require(plan.error.isEmpty()
		&& plan.changes.empty()
		&& plan.skippedKeys.size() == 2,
		"cloud theme keys must not be importable");

	auto firstPrefs = MemoryPrefs();
	auto secondPrefs = MemoryPrefs();
	auto first = Options(firstPrefs, Scope::Account);
	auto second = Options(secondPrefs, Scope::Account);
	Require(first.Set(kCloudTheme, raw)
		&& !first.Set(kCloudTheme, QByteArray("{}")),
		"cloud theme reference write");
	Require(first.Get(kCloudTheme) == raw
		&& second.Get(kCloudTheme).isEmpty(),
		"cloud theme reference crossed accounts");
	auto other = ref;
	other.user = kOtherUser;
	Require(second.Set(kCloudTheme, SerializeCloudThemeRef(other))
		&& ParseCloudThemeRef(first.Get(kCloudTheme))->user == kUser
		&& ParseCloudThemeRef(second.Get(kCloudTheme))->user == kOtherUser,
		"cloud theme references are kept per account");
	Require(first.Set(kCloudTheme, QByteArray())
		&& first.Get(kCloudTheme).isEmpty()
		&& !second.Get(kCloudTheme).isEmpty(),
		"clearing one account's cloud theme touched another");
}

} // namespace

void TestSync() {
	TestCloudThemeRef();
	std::cout << "PASS: Nagram screenshot cloud theme reference" << std::endl;
}
