#pragma once

#include "nagram/sync/backend.h"
#include "nagram/sync/model.h"
#include "base/weak_ptr.h"

namespace Nagram::Sync {

enum class Error {
	None,
	Busy,
	LocalInvalid,
	Search,
	Download,
	TooLarge,
	Upload,
	Damaged,
	Foreign,
	Newer,
};

struct Checked {
	Error error = Error::None;
	bool found = false;
	Action action = Action::Upload;
	Envelope remote;
};

class Service final : public base::has_weak_ptr {
public:
	explicit Service(not_null<Main::Session*> session);
	~Service();

	[[nodiscard]] static Service &For(not_null<Main::Session*> session);

	[[nodiscard]] bool busy() const;
	void check(Fn<void(Checked)> done);
	void upload(Fn<void(Error)> done);
	void removeRemote(Fn<void(Error)> done);
	void applied(const Envelope &remote);
	void cancel();

private:
	[[nodiscard]] std::optional<State> state() const;
	void save(quint64 messageId, const QByteArray &hash, qint64 updatedAt);

	const not_null<Main::Session*> _session;
	const std::unique_ptr<Backend> _backend;
	bool _busy = false;
	quint64 _remoteId = 0;
	std::vector<quint64> _remoteIds;

};

} // namespace Nagram::Sync
