package Plugins::SqueezeESP32::Plugin;

use strict;

use base qw(Slim::Plugin::Base);

use Slim::Utils::Prefs;
use Slim::Utils::Log;
use Slim::Web::ImageProxy;

use Plugins::SqueezeESP32::FirmwareHelper;

my $prefs = preferences('plugin.squeezeesp32');

my $log = Slim::Utils::Log->addLogCategory({
	'category'     => 'plugin.squeezeesp32',
	'defaultLevel' => 'INFO',
	'description'  => 'PLUGIN_SQUEEZEESP32',
});

# migrate 'eq' pref, as that's a reserved word and could cause problems in the future
$prefs->migrateClient(1, sub {
	my ($cprefs, $client) = @_;
	$cprefs->set('equalizer', $cprefs->get('eq'));
	$cprefs->remove('eq');
	1;
});

$prefs->migrateClient(2, sub {
	my ($cprefs, $client) = @_;
	$cprefs->set('artwork', undef) if $cprefs->get('artwork') && ref $cprefs->get('artwork') ne 'HASH';
	1;
});

$prefs->setChange(sub {
	$_[2]->send_equalizer;
}, 'equalizer');

sub initPlugin {
	my $class = shift;

	if ( main::WEBUI ) {
		require Plugins::SqueezeESP32::PlayerSettings;
		Plugins::SqueezeESP32::PlayerSettings->new;

		# require Plugins::SqueezeESP32::Settings;
		# Plugins::SqueezeESP32::Settings->new;
	}

	$class->SUPER::initPlugin(@_);
	# no name can be a subset of others due to a bug in addPlayerClass
	Slim::Networking::Slimproto::addPlayerClass($class, 100, 'squeezeesp32-basic', { client => 'Plugins::SqueezeESP32::Player', display => 'Plugins::SqueezeESP32::Graphics' });
	Slim::Networking::Slimproto::addPlayerClass($class, 101, 'squeezeesp32-graphic', { client => 'Plugins::SqueezeESP32::Player', display => 'Slim::Display::NoDisplay' });
	main::INFOLOG && $log->is_info && $log->info("Added class 100 and 101 for SqueezeESP32");

	# register a command to set the EQ - without saving the values! Send params as single comma separated list of values
	Slim::Control::Request::addDispatch(['squeezeesp32', 'seteq', '_eq'], [1, 0, 0, \&setEQ]);

	# Note for some forgetful know-it-all: we need to wrap the callback to make it unique. Otherwise subscriptions would overwrite each other.
	Slim::Control::Request::subscribe( sub { onNotification(@_) }, [ ['newmetadata'] ] );
	Slim::Control::Request::subscribe( sub { onNotification(@_) }, [ ['playlist'], ['open', 'newsong'] ]);
	Slim::Control::Request::subscribe( \&onStopClear, [ ['playlist'], ['stop', 'clear'] ]);

	Plugins::SqueezeESP32::FirmwareHelper->init();
}

sub onStopClear {
	my $request = shift;
	my $client  = $request->client || return;

	if ($client->isa('Plugins::SqueezeESP32::Player')) {
		$client->clear_artwork(0, $request->getRequestString());
	}
}

sub onNotification {
	my $request = shift;
	my $client  = $request->client || return;

	foreach my $player ($client->syncGroupActiveMembers) {
		next unless $player->isa('Plugins::SqueezeESP32::Player');
		$player->update_artwork;
	}
}

sub setEQ {
	my $request = shift;

	# check this is the correct command.
	if ($request->isNotCommand([['squeezeesp32'],['seteq']])) {
		$request->setStatusBadDispatch();
		return;
	}

	# get our parameters
	my $client   = $request->client();
	my @eqParams = split(/,/, $request->getParam('_eq') || '');

	for (my $x = 0; $x < 10; $x++) {
		$eqParams[$x] ||= 0;
	}

	$client->send_equalizer(\@eqParams);
}

1;
