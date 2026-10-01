#include "nagram/settings/network.h"

#include "nagram/settings/home.h"
#include "nagram/network/options.h"
#include "lang/lang_keys.h"
#include "settings/settings_builder.h"
#include "settings/settings_common_session.h"
#include "ui/vertical_list.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "window/window_session_controller.h"

#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Nagram {
namespace {

using namespace ::Settings;
using namespace ::Settings::Builder;

class NetworkSection final : public Section<NetworkSection> {
public:
	NetworkSection(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
	: Section(parent, controller) {
		const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
		build(content, kBuild);
		Ui::ResizeFitChild(this, content);
	}

	[[nodiscard]] rpl::producer<QString> title() override {
		return tr::lng_nagram_network();
	}

	static const SectionBuildMethod kBuild;
};

void AddToggle(
		SectionBuilder &builder,
		const Option<bool> &option,
		rpl::producer<QString> title,
		QString id,
		QStringList keywords) {
	const auto button = builder.addButton({
		.id = std::move(id),
		.title = std::move(title),
		.st = &st::settingsButtonNoIcon,
		.toggled = ForDevice().Value(option),
		.keywords = std::move(keywords),
	});
	if (button) {
		button->toggledChanges(
		) | rpl::on_next([option](bool value) {
			Expects(ForDevice().Set(option, value));
		}, button->lifetime());
	}
}

QString IpStrategyLabel(int value) {
	using Strategy = Network::IpStrategy;
	switch (static_cast<Strategy>(value)) {
	case Strategy::Follow: return tr::lng_nagram_preview_follow(tr::now);
	case Strategy::Ipv4Only:
		return tr::lng_nagram_ip_strategy_ipv4_only(tr::now);
	case Strategy::PreferIpv4:
		return tr::lng_nagram_ip_strategy_prefer_ipv4(tr::now);
	case Strategy::PreferIpv6:
		return tr::lng_nagram_ip_strategy_prefer_ipv6(tr::now);
	case Strategy::Ipv6Only:
		return tr::lng_nagram_ip_strategy_ipv6_only(tr::now);
	}
	Unexpected("Invalid Nagram IP strategy.");
}

void IpStrategyBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(tr::lng_nagram_ip_strategy());
	const auto group = std::make_shared<Ui::RadiobuttonGroup>(
		ForDevice().Get(Network::kIpStrategy));
	for (auto value = 0; value != Network::kIpStrategyCount; ++value) {
		box->addRow(object_ptr<Ui::Radiobutton>(
			box, group, value, IpStrategyLabel(value),
			st::settingsSendType), st::settingsSendTypePadding);
	}
	group->setChangedCallback([=](int value) {
		Expects(ForDevice().Set(Network::kIpStrategy, value));
		box->closeBox();
	});
}

const auto kMeta = BuildHelper({
	.id = NetworkSection::Id(),
	.parentId = HomeId(),
	.title = &tr::lng_nagram_network,
	.icon = &st::menuIconNetwork,
}, [](SectionBuilder &builder) {
	const auto controller = builder.controller();
	builder.addSubsectionTitle({
		.id = u"nagram/network/connection"_q,
		.title = tr::lng_nagram_network_connection(),
		.keywords = { u"connection"_q, u"network"_q },
	});
	builder.addButton({
		.id = u"nagram/network/ip-strategy"_q,
		.title = tr::lng_nagram_ip_strategy(),
		.st = &st::settingsButtonNoIcon,
		.label = ForDevice().Value(Network::kIpStrategy)
			| rpl::map(IpStrategyLabel),
		.onClick = [=] {
			controller->show(Box(IpStrategyBox));
		},
		.keywords = { u"IP version"_q, u"IPv4"_q, u"IPv6"_q },
	});
	builder.addDividerText(tr::lng_nagram_ip_strategy_about());
	AddToggle(builder, Network::kDisableBackupAddresses,
		tr::lng_nagram_disable_backup_addresses(),
		u"nagram/network/disable-backup-addresses"_q,
		{ u"backup"_q, u"server"_q, u"address"_q });
	builder.addDividerText(tr::lng_nagram_disable_backup_addresses_about());
});

const SectionBuildMethod NetworkSection::kBuild = kMeta.build;

} // namespace

Settings::Type NetworkId() {
	return NetworkSection::Id();
}

} // namespace Nagram
