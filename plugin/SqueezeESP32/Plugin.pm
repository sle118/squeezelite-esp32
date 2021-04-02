package Plugins::SqueezeESP32::Plugin;

use strict;

use base qw(Slim::Plugin::Base);
use File::Basename qw(basename);
use File::Spec::Functions qw(catfile);
use JSON::XS::VersionOneAndTwo;

use Slim::Utils::Prefs;
use Slim::Utils::Log;
use Slim::Web::ImageProxy;

my $prefs = preferences('plugin.squeezeesp32');

my $log = Slim::Utils::Log->addLogCategory({
	'category'     => 'plugin.squeezeesp32',
	'defaultLevel' => 'INFO',
	'description'  => 'PLUGIN_SQUEEZEESP32',
});

use constant GITHUB_ASSET_URI => "https://api.github.com/repos/sle118/squeezelite-esp32/releases/assets/";
use constant GITHUB_DOWNLOAD_URI => "https://github.com/sle118/squeezelite-esp32/releases/download/";
my $FW_DOWNLOAD_ID_REGEX = qr|plugins/SqueezeESP32/firmware/(-?\d+)|;
my $FW_DOWNLOAD_REGEX = qr|plugins/SqueezeESP32/firmware/([-a-z0-9-/.]+\.bin)$|i;
my $FW_FILENAME_REGEX = qr/^squeezelite-esp32-.*\.bin(\.tmp)?$/;

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

	Slim::Web::Pages->addRawFunction($FW_DOWNLOAD_ID_REGEX, \&handleFirmwareDownload);
	Slim::Web::Pages->addRawFunction($FW_DOWNLOAD_REGEX, \&handleFirmwareDownloadDirect);
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

sub handleFirmwareDownload {
	my ($httpClient, $response) = @_;

	my $request = $response->request;

	my $_errorDownloading = sub {
		_errorDownloading($httpClient, $response, @_);
	};

	my $id;
	if (!defined $request || !(($id) = $request->uri =~ $FW_DOWNLOAD_ID_REGEX)) {
		return $_errorDownloading->(undef, 'Invalid request', $request->uri, 400);
	}

	# this is the magic number used on the client to figure out whether the plugin does support download proxying
	if ($id == -99) {
		$response->code(204);
		$response->header('Access-Control-Allow-Origin' => '*');

		$httpClient->send_response($response);
		return Slim::Web::HTTP::closeHTTPSocket($httpClient);
	}

	Slim::Networking::SimpleAsyncHTTP->new(
		sub {
			my $http = shift;
			my $content = eval { from_json( $http->content ) };

			if (!$content || !ref $content) {
				$@ && $log->error("Failed to parse response: $@");
				return $_errorDownloading->($http);
			}
			elsif (!$content->{browser_download_url} || !$content->{name}) {
				return $_errorDownloading->($http, 'No download URL found');
			}

			downloadAndStreamFirmware($httpClient, $response, $content->{browser_download_url}, $content->{name});
		},
		$_errorDownloading,
		{
			timeout => 10,
			cache => 1,
			expires => 86400
		}
	)->get(GITHUB_ASSET_URI . $id);

	return;
}

sub handleFirmwareDownloadDirect {
	my ($httpClient, $response) = @_;

	my $request = $response->request;

	my $_errorDownloading = sub {
		_errorDownloading($httpClient, $response, @_);
	};

	my $path;
	if (!defined $request || !(($path) = $request->uri =~ $FW_DOWNLOAD_REGEX)) {
		return $_errorDownloading->(undef, 'Invalid request', $request->uri, 400);
	}

	main::INFOLOG && $log->is_info && $log->info("Requesting firmware from: $path");

	downloadAndStreamFirmware($httpClient, $response, GITHUB_DOWNLOAD_URI . $path);
}

sub downloadAndStreamFirmware {
	my ($httpClient, $response, $url, $name) = @_;

	my $_errorDownloading = sub {
		_errorDownloading($httpClient, $response, @_);
	};

	$name ||= basename($url);

	if ($name !~ $FW_FILENAME_REGEX) {
		return $_errorDownloading->(undef, 'Unexpected firmware image name: ' . $name, $url, 400);
	}

	my $updatesDir = Slim::Utils::OSDetect::dirsFor('updates');
	my $firmwareFile = catfile($updatesDir, $name);
	Slim::Utils::Misc::deleteFiles($updatesDir, $FW_FILENAME_REGEX, $firmwareFile);

	if (-f $firmwareFile) {
		main::INFOLOG && $log->is_info && $log->info("Found cached firmware version");
		$response->code(200);
		return Slim::Web::HTTP::sendStreamingFile($httpClient, $response, 'application/octet-stream', $firmwareFile, undef, 1);
	}

	Slim::Networking::SimpleAsyncHTTP->new(
		sub {
			my $http = shift;

			if ($http->code != 200 || !-e "$firmwareFile.tmp") {
				return $_errorDownloading->($http, $http->mess);
			}

			rename "$firmwareFile.tmp", $firmwareFile or return $_errorDownloading->($http, "Unable to rename temporary $firmwareFile file" );

			$response->code(200);
			Slim::Web::HTTP::sendStreamingFile($httpClient, $response, 'application/octet-stream', $firmwareFile, undef, 1);
		},
		$_errorDownloading,
		{
			saveAs => "$firmwareFile.tmp",
		}
	)->get($url);

	return;
}

sub _errorDownloading {
	my ($httpClient, $response, $http, $error, $url, $code) = @_;

	$error ||= ($http && $http->error) || 'unknown error';
	$url   ||= ($http && $http->url) || 'no URL';
	$code  ||= ($http && $http->code) || 500;

	$log->error(sprintf("Failed to get data from Github: %s (%s)", $error || $http->error, $url));

	$response->headers->remove_content_headers;
	$response->code($code);
	$response->content_type('text/plain');
	$response->header('Connection' => 'close');
	$response->content('');

	$httpClient->send_response($response);
	Slim::Web::HTTP::closeHTTPSocket($httpClient);
};


1;
