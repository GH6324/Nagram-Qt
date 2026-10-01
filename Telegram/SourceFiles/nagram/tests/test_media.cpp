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

void TestGroupCallRawAudio() {
	using namespace Nagram;
	using namespace Nagram::Media;
	auto registry = Registry();
	RegisterBackendOptions(registry);
	CheckDeviceOption(registry, kGroupCallRawAudio, Category::Media, true);
	Require(!kGroupCallRawAudio.fallback,
		"group call audio processing must stay on by default");
}

void TestCoverAddress() {
	using namespace Nagram;
	using namespace Nagram::Media;
	for (const auto &address : {
			u"https://covers.example/art?artist={artist}&title={title}"_q,
			u"https://covers.example/{title}.jpg"_q,
			u"https://covers.example/{title}/{title}"_q,
			u"https://covers.example:8443/search?q="_q,
			u"https://covers.example/search/"_q,
			u"http://localhost:8080/cover?q={title}"_q,
			u"http://127.0.0.1:8080/cover?q="_q,
			u"http://127.10.20.30/cover?q="_q,
			u"http://[::1]:8080/cover?q={title}"_q }) {
		Require(ValidCoverAddress(address) && ValidCoverUrl(address),
			"valid cover address rejected");
	}
	const auto tooLong = u"https://covers.example/"_q
		+ QString(kMaxCoverUrlLength, u'a');
	for (const auto &address : {
			QString(),
			u"http://covers.example/cover?q={title}"_q,
			u"http://127.evil.example/cover?q="_q,
			u"http://127.0.0.1.evil.example/cover?q="_q,
			u"http://1270.0.0.1/cover?q="_q,
			u"file:///etc/passwd"_q,
			u"file://localhost/c:/cover?q={title}"_q,
			u"ftp://covers.example/{title}"_q,
			u"covers.example/cover?q={title}"_q,
			u"https://"_q,
			u"https:///cover?q={title}"_q,
			u"https://user:pass@covers.example/{title}"_q,
			u"https://covers.example/{title}#part"_q,
			u"https://covers.example/cover\nHost: other?q={title}"_q,
			u"https://covers.example/co ver?q={title}"_q,
			u"https://covers.example/\tcover?q="_q,
			u"https://covers.example/cover?a={artist}"_q,
			u"https://covers.example/cover?q={title}&x={album}"_q,
			u"https://covers.example/cover?q={title"_q,
			u"https://covers.example/cover?q=title}"_q,
			u"https://covers.example/cover?q={}"_q,
			u"https://{title}.example/cover"_q,
			u"https://covers.example{title}"_q,
			u"https://covers.example:0/{title}"_q,
			u"https://covers.example:99999/{title}"_q,
			u" https://covers.example/{title}"_q,
			tooLong }) {
		Require(!ValidCoverAddress(address), "invalid cover address accepted");
	}
	Require(ValidCoverUrl(QString()), "empty cover address must be allowed");
	Require(CoverUrlHost(u"https://Covers.Example:8443/{title}"_q)
		== u"covers.example"_q
		&& CoverUrlHost(u"file:///etc/passwd"_q).isEmpty(),
		"cover address host");
}

void TestCoverExpansion() {
	using namespace Nagram::Media;
	const auto artist = QString::fromUtf8("AC/DC & Friends #1");
	const auto title = QString::fromUtf8("周杰伦 🎵 a+b?");
	const auto encodedArtist = u"AC%2FDC%20%26%20Friends%20%231"_q;
	const auto encodedTitle = u"%E5%91%A8%E6%9D%B0%E4%BC%A6%20"
		"%F0%9F%8E%B5%20a%2Bb%3F"_q;
	Require(ExpandCoverUrl(
		u"https://covers.example/art?artist={artist}&title={title}"_q,
		artist,
		title) == u"https://covers.example/art?artist="_q + encodedArtist
			+ u"&title="_q + encodedTitle,
		"cover template expansion");
	Require(ExpandCoverUrl(
		u"https://covers.example/{title}/{title}.jpg"_q,
		artist,
		u"a b"_q) == u"https://covers.example/a%20b/a%20b.jpg"_q,
		"repeated cover placeholder");
	Require(ExpandCoverUrl(
		u"https://covers.example/?a={artist}&t={title}"_q,
		u"{title}"_q,
		u"{artist}"_q)
		== u"https://covers.example/?a=%7Btitle%7D&t=%7Bartist%7D"_q,
		"placeholder text inside a value must not expand again");
	Require(ExpandCoverUrl(
		u"https://covers.example/search?q="_q,
		u"Artist"_q,
		u"Song Name"_q)
		== u"https://covers.example/search?q=Artist%20-%20Song%20Name"_q
		&& ExpandCoverUrl(
			u"https://covers.example/search?q="_q,
			QString(),
			u"Song"_q) == u"https://covers.example/search?q=Song"_q,
		"address without placeholders must get the query appended");
	Require(ExpandCoverUrl(u"file:///cover?q="_q, artist, title).isEmpty()
		&& ExpandCoverUrl(QString(), artist, title).isEmpty(),
		"invalid cover address must not produce a request");
}

void TestCoverOption() {
	using namespace Nagram;
	using namespace Nagram::Media;
	auto registry = Registry();
	RegisterBackendOptions(registry);
	const auto info = registry.Find(kMusicCoverUrl.key);
	Require(info != nullptr
		&& info->scope == Scope::Device
		&& info->category == Category::Media
		&& info->type == OptionInfo::ValueType::String
		&& registry.HasFlag(kMusicCoverUrl.key, Flag::Hidden)
		&& !registry.HasFlag(kMusicCoverUrl.key, Flag::Exportable)
		&& !registry.HasFlag(kMusicCoverUrl.key, Flag::RequiresRestart),
		"cover address must be a hidden device option");
	Require(kMusicCoverUrl.fallback.isEmpty(),
		"covers must come from Telegram by default");

	auto prefs = MemoryPrefs();
	auto options = Options(prefs);
	Require(CoverRequestUrl(options, u"Artist"_q, u"Song"_q).isEmpty()
		&& prefs.values.empty(),
		"default cover address must keep the upstream location");
	Require(!options.Set(kMusicCoverUrl, u"http://covers.example/{title}"_q)
		&& !options.Set(kMusicCoverUrl, u"file:///cover?q={title}"_q)
		&& !options.Set(kMusicCoverUrl, u"https://covers.example/{album}"_q)
		&& prefs.values.empty(), "invalid cover address stored");
	const auto address = u"https://covers.example/?token=secret&q={title}"_q;
	Require(options.Set(kMusicCoverUrl, address)
		&& CoverRequestUrl(options, u"Artist"_q, u"Song"_q)
			== u"https://covers.example/?token=secret&q=Song"_q,
		"cover address write");
	const auto exported = Exchange::Export(options, registry);
	Require(!exported.data.contains("musicCoverUrl")
		&& !exported.data.contains("secret"),
		"cover address must not be exported");

	auto errors = 0;
	auto lifetime = rpl::lifetime();
	options.readErrors() | rpl::on_next([&](std::string_view key) {
		errors += (key == kMusicCoverUrl.key) ? 1 : 0;
	}, lifetime);
	prefs.values[std::string(kMusicCoverUrl.key)]
		= "shttp://covers.example/{title}";
	Require(CoverRequestUrl(options, u"Artist"_q, u"Song"_q).isEmpty()
		&& errors == 1
		&& options.invalidKeys().contains(kMusicCoverUrl.key),
		"stored invalid cover address must fall back and be reported");
}

} // namespace

void TestMedia() {
	TestVoiceBitrate();
	TestGroupCallRawAudio();
	TestCoverAddress();
	TestCoverExpansion();
	TestCoverOption();
	std::cout << "PASS: Nagram media backends" << std::endl;
}
