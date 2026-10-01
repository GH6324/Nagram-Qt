#pragma once

#include "nagram/core/options.h"

#include <optional>

namespace Nagram::Sync {

inline constexpr auto kMaxBackupBytes = 8 * 1024 * 1024;

[[nodiscard]] QString FileName();
[[nodiscard]] QString Caption();

enum class EnvelopeError {
	None,
	TooLarge,
	Damaged,
	Foreign,
	Newer,
};

struct Envelope {
	qint64 updatedAt = 0;
	QString app;
	QByteArray payload;
	QByteArray hash;
};

struct DecodedEnvelope {
	EnvelopeError error = EnvelopeError::None;
	Envelope value;
};

[[nodiscard]] QByteArray PayloadHash(const QByteArray &exported);
[[nodiscard]] QByteArray EncodeEnvelope(
	const QByteArray &exported,
	qint64 updatedAt,
	const QString &app);
[[nodiscard]] DecodedEnvelope DecodeEnvelope(const QByteArray &data);

enum class Action {
	UpToDate,
	Upload,
	Download,
	Conflict,
};

// Hashes of this device, the last synced backup and the cloud backup.
[[nodiscard]] Action Decide(
	const QByteArray &local,
	const QByteArray &synced,
	const QByteArray &remote);

struct State {
	quint64 user = 0;
	quint64 messageId = 0;
	QByteArray hash;
	qint64 updatedAt = 0;
};

[[nodiscard]] std::optional<State> ParseState(const QByteArray &raw);
[[nodiscard]] QByteArray SerializeState(const State &state);
[[nodiscard]] bool ValidState(const QByteArray &raw);

[[nodiscard]] QStringList LocalOnlyKeys(const Registry &registry);

inline const auto kState = Option<QByteArray>{
	"nagram.cloudSyncState", Scope::Account, QByteArray(),
	Category::Services, "lng_nagram_sync_title",
	static_cast<unsigned>(Flag::Hidden), ValidState };

inline void RegisterOptions(Registry &registry) {
	Expects(registry.Add(kState));
}

} // namespace Nagram::Sync
