#include "nagram/sync/service.h"

#include "nagram/core/exchange.h"

#include "base/unixtime.h"
#include "core/version.h"
#include "main/main_session.h"

namespace Nagram::Sync {
namespace {

[[nodiscard]] Error FromBackend(BackendError error) {
	switch (error) {
	case BackendError::None: return Error::None;
	case BackendError::Search: return Error::Search;
	case BackendError::Download: return Error::Download;
	case BackendError::TooLarge: return Error::TooLarge;
	case BackendError::Upload:
	case BackendError::Send: return Error::Upload;
	}
	Unexpected("Sync backend error.");
}

[[nodiscard]] Error FromEnvelope(EnvelopeError error) {
	switch (error) {
	case EnvelopeError::None: return Error::None;
	case EnvelopeError::TooLarge: return Error::TooLarge;
	case EnvelopeError::Damaged: return Error::Damaged;
	case EnvelopeError::Foreign: return Error::Foreign;
	case EnvelopeError::Newer: return Error::Newer;
	}
	Unexpected("Sync envelope error.");
}

} // namespace

Service::Service(not_null<Main::Session*> session)
: _session(session)
, _backend(MakeSavedMessagesBackend(session)) {
}

Service::~Service() = default;

Service &Service::For(not_null<Main::Session*> session) {
	struct Entry {
		base::weak_ptr<Main::Session> session;
		Service *service = nullptr;
	};
	static auto entries = std::vector<Entry>();
	std::erase_if(entries, [](const Entry &entry) {
		return !entry.session;
	});
	for (const auto &entry : entries) {
		if (entry.session.get() == session) {
			return *entry.service;
		}
	}
	const auto service = session->lifetime().make_state<Service>(session);
	entries.push_back({ base::make_weak(session), service });
	return *service;
}

bool Service::busy() const {
	return _busy;
}

std::optional<State> Service::state() const {
	const auto parsed = ParseState(ForAccount(_session).Get(kState));
	return (parsed && parsed->user == _session->userId().bare)
		? parsed
		: std::nullopt;
}

void Service::save(
		quint64 messageId,
		const QByteArray &hash,
		qint64 updatedAt) {
	Expects(ForAccount(_session).Set(kState, SerializeState({
		.user = _session->userId().bare,
		.messageId = messageId,
		.hash = hash,
		.updatedAt = updatedAt,
	})));
}

void Service::cancel() {
	_backend->cancel();
	_busy = false;
}

void Service::check(Fn<void(Checked)> done) {
	if (_busy) {
		done({ .error = Error::Busy });
		return;
	}
	const auto exported = Exchange::Export(
		ForDevice(),
		RegisteredOptions(),
		ExchangeTarget::Sync);
	if (!exported.invalidKeys.isEmpty()) {
		done({ .error = Error::LocalInvalid });
		return;
	}
	const auto local = PayloadHash(exported.data);
	const auto known = state();
	_busy = true;
	_remoteId = 0;
	_remoteIds.clear();
	_backend->fetch(known ? known->messageId : 0, [=](Fetched fetched) {
		_busy = false;
		_remoteIds = fetched.all;
		if (fetched.error != BackendError::None) {
			done({
				.error = FromBackend(fetched.error),
				.found = (fetched.error == BackendError::TooLarge),
			});
			return;
		} else if (!fetched.found) {
			done({});
			return;
		}
		_remoteId = fetched.id;
		const auto decoded = DecodeEnvelope(fetched.data);
		if (decoded.error != EnvelopeError::None) {
			done({ .error = FromEnvelope(decoded.error), .found = true });
			return;
		}
		const auto &remote = decoded.value;
		const auto action = Decide(
			local,
			known ? known->hash : QByteArray(),
			remote.hash);
		if (action == Action::UpToDate
			&& (!known
				|| known->hash != remote.hash
				|| known->messageId != fetched.id)) {
			save(fetched.id, remote.hash, base::unixtime::now());
		}
		done({ .found = true, .action = action, .remote = remote });
	});
}

void Service::upload(Fn<void(Error)> done) {
	if (_busy) {
		done(Error::Busy);
		return;
	}
	const auto exported = Exchange::Export(
		ForDevice(),
		RegisteredOptions(),
		ExchangeTarget::Sync);
	if (!exported.invalidKeys.isEmpty()) {
		done(Error::LocalInvalid);
		return;
	}
	const auto now = qint64(base::unixtime::now());
	const auto hash = PayloadHash(exported.data);
	const auto data = EncodeEnvelope(
		exported.data,
		now,
		QString::fromLatin1(AppVersionStr));
	_busy = true;
	_backend->upload(data, [=](Uploaded uploaded) {
		_busy = false;
		if (uploaded.error != BackendError::None) {
			done(FromBackend(uploaded.error));
			return;
		}
		auto old = base::take(_remoteIds);
		if (const auto known = state(); known && known->messageId) {
			old.push_back(known->messageId);
		}
		std::erase(old, uploaded.id);
		save(uploaded.id, hash, now);
		_remoteId = uploaded.id;
		_backend->remove(old);
		done(Error::None);
	});
}

void Service::removeRemote(Fn<void(Error)> done) {
	if (_busy) {
		done(Error::Busy);
		return;
	}
	const auto known = state();
	_busy = true;
	_backend->fetch(known ? known->messageId : 0, [=](Fetched fetched) {
		_busy = false;
		if (fetched.error == BackendError::Search) {
			done(Error::Search);
			return;
		}
		_backend->remove(fetched.all);
		_remoteId = 0;
		_remoteIds.clear();
		Expects(ForAccount(_session).Set(kState, QByteArray()));
		done(Error::None);
	});
}

void Service::applied(const Envelope &remote) {
	save(_remoteId, remote.hash, base::unixtime::now());
}

} // namespace Nagram::Sync
