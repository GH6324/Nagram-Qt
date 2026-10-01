#include "nagram/core/exchange.h"
#include "nagram/privacy/options.h"

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

void CheckSwitch(
		const Nagram::Registry &registry,
		const Nagram::Option<bool> &option,
		bool refreshesMessages) {
	using namespace Nagram;
	const auto info = registry.Find(option.key);
	Require(info != nullptr, "protection option is not registered");
	Require(info->type == OptionInfo::ValueType::Boolean
		&& info->fallbackRaw == "0"
		&& !option.fallback, "protection option must default to off");
	Require(info->scope == Scope::Device
		&& info->category == Category::Privacy
		&& registry.HasFlag(option.key, Flag::Exportable),
		"protection option must be an exportable device option");
	Require(registry.HasFlag(option.key, Flag::RefreshMessageView)
		== refreshesMessages, "protection option refresh flag");

	auto prefs = MemoryPrefs();
	auto options = Options(prefs);
	Require(!options.Get(option), "protection option default value");
	Require(options.Set(option, true), "protection option write");
	const auto exported = Exchange::Export(options, registry);
	Require(exported.invalidKeys.empty(), "protection option export");
	const auto key = QString::fromUtf8(option.key.data(), option.key.size());
	Require(QJsonDocument::fromJson(exported.data).object().value(
		QString::fromLatin1("options")).toObject().value(key)
			== QJsonValue(true), "protection option missing from export");

	auto importedPrefs = MemoryPrefs();
	auto imported = Options(importedPrefs);
	const auto plan = Exchange::PlanImport(imported, registry, exported.data);
	Require(plan.error.isEmpty() && plan.changes.size() == 1,
		"protection option import preview");
	Require(Exchange::Apply(imported, registry, plan).applied
		&& imported.Get(option), "protection option import");

	importedPrefs.values[std::string(option.key)] = "broken";
	Require(!imported.Get(option)
		&& imported.invalidKeys().contains(option.key)
		&& importedPrefs.values[std::string(option.key)] == "broken",
		"invalid protection value must fall back and stay stored");
}

} // namespace

void TestPrivacy() {
	using namespace Nagram;
	auto registry = Registry();
	Privacy::RegisterOptions(registry);
	CheckSwitch(registry, Privacy::kDoNotSharePhone, false);

	auto prefs = MemoryPrefs();
	auto options = Options(prefs);
	Require(!options.Get(Privacy::kDoNotSharePhone),
		"phone number sharing must stay ticked by default");
	Require(options.Set(Privacy::kDoNotSharePhone, true)
		&& options.Get(Privacy::kDoNotSharePhone),
		"phone number sharing must start unticked when enabled");
	std::cout << "PASS: Nagram content protection" << std::endl;
}
