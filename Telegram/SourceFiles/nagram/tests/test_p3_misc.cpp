#include "nagram/core/exchange.h"
#include "nagram/links/options.h"

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

template <typename Type>
void CheckDeviceOption(
		const Nagram::Registry &registry,
		const Nagram::Option<Type> &option,
		Nagram::Category category,
		const Type &changed) {
	using namespace Nagram;
	const auto info = registry.Find(option.key);
	Require(info != nullptr, "P3-09 option is not registered");
	Require(info->scope == Scope::Device
		&& info->category == category
		&& registry.HasFlag(option.key, Flag::Exportable),
		"P3-09 option must be an exportable device option");

	auto prefs = MemoryPrefs();
	auto options = Options(prefs);
	Require(options.Get(option) == option.fallback
		&& prefs.values.empty(), "P3-09 option default value");
	Require(options.Set(option, changed)
		&& options.Get(option) == changed, "P3-09 option write");
	const auto exported = Exchange::Export(options, registry);
	Require(exported.invalidKeys.empty(), "P3-09 option export");

	auto importedPrefs = MemoryPrefs();
	auto imported = Options(importedPrefs);
	const auto plan = Exchange::PlanImport(imported, registry, exported.data);
	Require(plan.error.isEmpty() && plan.changes.size() == 1,
		"P3-09 option import preview");
	Require(Exchange::Apply(imported, registry, plan).applied
		&& imported.Get(option) == changed, "P3-09 option import");

	importedPrefs.values[std::string(option.key)] = "broken";
	Require(imported.Get(option) == option.fallback
		&& imported.invalidKeys().contains(option.key)
		&& importedPrefs.values[std::string(option.key)] == "broken",
		"invalid P3-09 value must fall back and stay stored");
}

void TestLinkBehavior() {
	using namespace Nagram;
	using namespace Nagram::Links;
	auto registry = Registry();
	RegisterBehaviorOptions(registry);
	CheckDeviceOption(registry, kDisableOfficialAutoLogin,
		Category::Rules, true);
	CheckDeviceOption(registry, kHashtagSearchPageChannel,
		Category::Rules, 2);
	CheckDeviceOption(registry, kHashtagSearchPageChat,
		Category::Rules, 1);
	for (const auto option : {
			&kHashtagSearchPageChannel,
			&kHashtagSearchPageChat }) {
		Require(option->fallback == 0
			&& option->validate(0)
			&& option->validate(1)
			&& option->validate(2)
			&& !option->validate(3)
			&& !option->validate(-1), "hashtag search page bounds");
	}

	using Page = HashtagPage;
	for (const auto broadcast : { false, true }) {
		Require(ResolveHashtagPage(true, broadcast, 0, 0) == Page::Follow,
			"default hashtag page must follow Telegram");
		for (auto channel = 0; channel != 3; ++channel) {
			for (auto chat = 0; chat != 3; ++chat) {
				Require(ResolveHashtagPage(false, broadcast, channel, chat)
					== Page::Follow,
					"hashtag page changed outside of a hashtag click");
			}
		}
	}
	Require(ResolveHashtagPage(true, true, 1, 2) == Page::ThisChat
		&& ResolveHashtagPage(true, true, 2, 1) == Page::MyMessages
		&& ResolveHashtagPage(true, true, 0, 2) == Page::Follow,
		"channels must use the channel hashtag page");
	Require(ResolveHashtagPage(true, false, 1, 2) == Page::MyMessages
		&& ResolveHashtagPage(true, false, 2, 1) == Page::ThisChat
		&& ResolveHashtagPage(true, false, 2, 0) == Page::Follow,
		"other chats must use the chat hashtag page");

	auto prefs = MemoryPrefs();
	auto options = Options(prefs);
	Require(!options.Get(kDisableOfficialAutoLogin),
		"official auto-login must stay enabled by default");
	Require(options.Set(kDisableOfficialAutoLogin, true)
		&& options.Get(kDisableOfficialAutoLogin),
		"official auto-login switch");
}

void TestWebAppSize() {
	using namespace Nagram;
	using namespace Nagram::Links;
	auto registry = Registry();
	RegisterBehaviorOptions(registry);
	CheckDeviceOption(registry, kWebAppWidthScale, Category::Rules, 150);
	CheckDeviceOption(registry, kWebAppHeightScale, Category::Rules, 200);
	for (const auto option : { &kWebAppWidthScale, &kWebAppHeightScale }) {
		Require(option->fallback == 100
			&& option->validate(100)
			&& option->validate(125)
			&& option->validate(200)
			&& !option->validate(75)
			&& !option->validate(110)
			&& !option->validate(225), "web app scale bounds");
	}

	const auto base = QSize(384, 694);
	const auto screen = QSize(1440, 860);
	Require(ScaledPanelSize(base, 100, 100, screen) == base
		&& ScaledPanelSize(base, 100, 100, QSize()) == base
		&& ScaledPanelSize(base, 100, 100, QSize(300, 500)) == base,
		"default web app size must stay the upstream size");
	Require(ScaledPanelSize(base, 150, 100, screen) == QSize(576, 694)
		&& ScaledPanelSize(base, 200, 125, screen) == QSize(768, 860),
		"web app size must scale and stay inside the available area");
	Require(ScaledPanelSize(base, 200, 200, QSize()) == QSize(768, 1388),
		"web app size without a screen must return the scaled size");
	Require(ScaledPanelSize(base, 200, 200, QSize(300, 500)) == base,
		"web app size must not shrink below the upstream size");
}

} // namespace

void TestP3Misc() {
	TestLinkBehavior();
	TestWebAppSize();
	std::cout << "PASS: Nagram P3-09 options" << std::endl;
}
