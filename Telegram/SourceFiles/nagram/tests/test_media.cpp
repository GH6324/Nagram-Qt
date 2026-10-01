#include "nagram/core/exchange.h"
#include "nagram/media/backend_options.h"
#include "base/basic_types.h"

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

template <typename Type>
void CheckDeviceOption(
		const Nagram::Registry &registry,
		const Nagram::Option<Type> &option,
		Nagram::Category category,
		const Type &changed) {
	using namespace Nagram;
	const auto info = registry.Find(option.key);
	Require(info != nullptr, "media backend option is not registered");
	Require(info->scope == Scope::Device
		&& info->category == category
		&& registry.HasFlag(option.key, Flag::Exportable)
		&& !registry.HasFlag(option.key, Flag::RequiresRestart),
		"media backend option must be an exportable device option");

	auto prefs = MemoryPrefs();
	auto options = Options(prefs);
	Require(options.Get(option) == option.fallback
		&& prefs.values.empty(), "media backend option default value");
	Require(options.Set(option, changed)
		&& options.Get(option) == changed, "media backend option write");
	const auto exported = Exchange::Export(options, registry);
	Require(exported.invalidKeys.empty(), "media backend option export");

	auto importedPrefs = MemoryPrefs();
	auto imported = Options(importedPrefs);
	const auto plan = Exchange::PlanImport(imported, registry, exported.data);
	Require(plan.error.isEmpty() && plan.changes.size() == 1,
		"media backend option import preview");
	Require(Exchange::Apply(imported, registry, plan).applied
		&& imported.Get(option) == changed, "media backend option import");

	importedPrefs.values[std::string(option.key)] = "broken";
	Require(imported.Get(option) == option.fallback
		&& imported.invalidKeys().contains(option.key)
		&& importedPrefs.values[std::string(option.key)] == "broken",
		"invalid media backend value must fall back and stay stored");
}

void TestVoiceBitrate() {
	using namespace Nagram;
	using namespace Nagram::Media;
	auto registry = Registry();
	RegisterBackendOptions(registry);
	CheckDeviceOption(registry, kVoiceRecordBitrate, Category::Media, 64);
	Require(kVoiceRecordBitrate.fallback == 0,
		"voice bitrate must follow Telegram by default");
	for (const auto value : { 0, 16, 24, 48, 64, 96, 128 }) {
		Require(kVoiceRecordBitrate.validate(value),
			"voice bitrate rejected");
	}
	for (const auto value : { 32, 100, -16, 8, 129, 16000 }) {
		Require(!kVoiceRecordBitrate.validate(value),
			"unsupported voice bitrate accepted");
	}

	auto prefs = MemoryPrefs();
	auto options = Options(prefs);
	Require(VoiceRecordBitrate(options, 32000) == 32000
		&& VoiceRecordBitrate(options, 12345) == 12345,
		"default voice bitrate must return the upstream value");
	Require(!options.Set(kVoiceRecordBitrate, 32)
		&& !options.Set(kVoiceRecordBitrate, 100)
		&& !options.Set(kVoiceRecordBitrate, -16)
		&& prefs.values.empty(), "unsupported voice bitrate stored");
	for (const auto value : { 16, 24, 48, 64, 96, 128 }) {
		Require(options.Set(kVoiceRecordBitrate, value)
			&& VoiceRecordBitrate(options, 32000) == value * 1000,
			"selected voice bitrate");
	}
	Require(options.Set(kVoiceRecordBitrate, 0)
		&& prefs.values.empty()
		&& VoiceRecordBitrate(options, 32000) == 32000,
		"voice bitrate reset");

	auto errors = 0;
	auto lifetime = rpl::lifetime();
	options.readErrors() | rpl::on_next([&](std::string_view key) {
		errors += (key == kVoiceRecordBitrate.key) ? 1 : 0;
	}, lifetime);
	prefs.values[std::string(kVoiceRecordBitrate.key)] = "100";
	Require(VoiceRecordBitrate(options, 32000) == 32000 && errors == 1,
		"stored unsupported bitrate must fall back and be reported");
	Require(ResolveVoiceBitrate(100, 32000) == 32000
		&& ResolveVoiceBitrate(-1, 32000) == 32000,
		"unsupported bitrate must never reach the encoder");
}

} // namespace

void TestMedia() {
	TestVoiceBitrate();
	std::cout << "PASS: Nagram media backends" << std::endl;
}
