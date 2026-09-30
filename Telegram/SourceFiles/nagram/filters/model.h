#pragma once

#include "base/assertion.h"
#include "base/basic_types.h"
#include "ui/text/text_entity.h"
#include "nagram/core/options.h"

#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>

namespace Nagram::Filters {


struct Result {
	TextWithEntities text;
	bool hidden = false;
	QString error;
	int matches = 0;
};

[[nodiscard]] QJsonObject Defaults();
[[nodiscard]] bool Validate(const QByteArray &raw);
[[nodiscard]] Result Apply(
	const QByteArray &raw,
	const TextWithEntities &source,
	const QString &author,
	const QString &peer,
	bool blocked,
	bool outgoing,
	const QString &searchable = QString());

inline const auto kRules = Option<QByteArray>{
	"nagram.filters", Scope::Account, QByteArray(),
	Category::Rules, "lng_nagram_filter_rules",
	static_cast<unsigned>(Flag::RefreshMessageView), Validate };

inline constexpr auto kMaximumHiddenMessages = 1000;

[[nodiscard]] inline bool ValidHiddenMessages(const QString &value) {
	if (value.isEmpty()) {
		return true;
	}
	const auto entries = value.split(u',');
	if (entries.size() > kMaximumHiddenMessages) {
		return false;
	}
	for (const auto &entry : entries) {
		const auto parts = entry.split(u':');
		auto peerOk = false;
		auto msgOk = false;
		if (parts.size() != 2
			|| !parts[0].toULongLong(&peerOk) || !peerOk
			|| parts[1].toLongLong(&msgOk) <= 0 || !msgOk) {
			return false;
		}
	}
	return true;
}

inline const auto kHiddenMessages = Option<QString>{
	"nagram.hiddenMessages", Scope::Account, QString(),
	Category::Rules, "lng_nagram_hidden_messages",
	static_cast<unsigned>(Flag::RefreshMessageView), ValidHiddenMessages };

inline void RegisterOptions(Registry &registry) {
	Expects(registry.Add(kRules));
	Expects(registry.Add(kHiddenMessages));
}

} // namespace Nagram::Filters
