package Plugins::SqueezeESP32::FirmwareHelper;

use strict;

use File::Basename qw(basename);
use File::Spec::Functions qw(catfile);
use JSON::XS::VersionOneAndTwo;

use Slim::Utils::Log;
use Slim::Utils::Prefs;

use constant FIRMWARE_POLL_INTERVAL => 3600 * (5 + rand());
use constant GITHUB_RELEASES_URI => "https://api.github.com/repos/sle118/squeezelite-esp32/releases";
use constant GITHUB_ASSET_URI => GITHUB_RELEASES_URI . "/assets/";
use constant GITHUB_DOWNLOAD_URI => "https://github.com/sle118/squeezelite-esp32/releases/download/";
my $FW_DOWNLOAD_ID_REGEX = qr|plugins/SqueezeESP32/firmware/(-?\d+)|;
my $FW_DOWNLOAD_REGEX = qr|plugins/SqueezeESP32/firmware/([-a-z0-9-/.]+\.bin)$|i;
my $FW_FILENAME_REGEX = qr/^squeezelite-esp32-.*\.bin(\.tmp)?$/;
my $FW_TAG_REGEX = qr/\/(ESP32-A1S|SqueezeAmp|I2S-4MFlash)\.(16|32)\.(\d+)\.(.*)\//;

my $prefs = preferences('plugin.squeezeesp32');
my $log = logger('plugin.squeezeesp32');

sub init {
	Slim::Web::Pages->addRawFunction($FW_DOWNLOAD_ID_REGEX, \&handleFirmwareDownload);
	Slim::Web::Pages->addRawFunction($FW_DOWNLOAD_REGEX, \&handleFirmwareDownloadDirect);

	# start checking for firmware updates
	Slim::Utils::Timers::setTimer(undef, Time::HiRes::time() + 30 + rand(30), \&prefetchFirmware);
}

sub prefetchFirmware {
	Slim::Utils::Timers::killTimers(undef, \&prefetchFirmware);
	my $releaseInfo = $prefs->get('lastReleaseTagUsed');

	Slim::Networking::SimpleAsyncHTTP->new(
		sub {
			my $http = shift;
			my $content = eval { from_json( $http->content ) };

			if (!$content || !ref $content) {
				$@ && $log->error("Failed to parse response: $@");
			}

			my $regex = $releaseInfo->{model} . '\.' . $releaseInfo->{res} . '\.\d+\.' . $releaseInfo->{branch};
			my $url;
			foreach (@$content) {
				if ($_->{tag_name} =~ /$regex/ && $_->{assets} && ref $_->{assets}) {
					($url) = grep /\.bin$/, map {
						$_->{browser_download_url}
					} @{$_->{assets}};

					last if $url;
				}
			}

			downloadFirmwareFile(sub {
				main::INFOLOG && $log->is_info && $log->info("Pre-cached firmware file: " . $_[0]);
			}, sub {
				my ($http, $error, $url, $code) = @_;
				$error ||= ($http && $http->error) || 'unknown error';
				$url   ||= ($http && $http->url) || 'no URL';

				$log->error(sprintf("Failed to get firmware image from Github: %s (%s)", $error || $http->error, $url));
			}, $url) if $url && $url =~ /^https?/;

		},
		sub {
			my ($http, $error) = @_;
			$log->error("Failed to get releases from Github: $error");
		},
		{
			timeout => 10,
			cache => 1,
			expires => 3600
		}
	)->get(GITHUB_RELEASES_URI) if $releaseInfo;

	Slim::Utils::Timers::setTimer(undef, Time::HiRes::time() + FIRMWARE_POLL_INTERVAL, \&prefetchFirmware);
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

			downloadFirmwareFile(sub {
				my $firmwareFile = shift;
				$response->code(200);
				Slim::Web::HTTP::sendStreamingFile($httpClient, $response, 'application/octet-stream', $firmwareFile, undef, 1);
			}, $_errorDownloading, $content->{browser_download_url}, $content->{name});
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

	downloadFirmwareFile(sub {
		my $firmwareFile = shift;
		$response->code(200);
		Slim::Web::HTTP::sendStreamingFile($httpClient, $response, 'application/octet-stream', $firmwareFile, undef, 1);
	}, $_errorDownloading, GITHUB_DOWNLOAD_URI . $path);
}

sub downloadFirmwareFile {
	my ($cb, $ecb, $url, $name) = @_;

	# keep track of the last firmware we requested, to prefetch it in the future
	_getFirmwareTag($url);

	$name ||= basename($url);

	if ($name !~ $FW_FILENAME_REGEX) {
		return $ecb->(undef, 'Unexpected firmware image name: ' . $name, $url, 400);
	}

	my $updatesDir = Slim::Utils::OSDetect::dirsFor('updates');
	my $firmwareFile = catfile($updatesDir, $name);
	Slim::Utils::Misc::deleteFiles($updatesDir, $FW_FILENAME_REGEX, $firmwareFile);

	if (-f $firmwareFile) {
		main::INFOLOG && $log->is_info && $log->info("Found cached firmware file");
		return $cb->($firmwareFile);
	}

	Slim::Networking::SimpleAsyncHTTP->new(
		sub {
			my $http = shift;

			if ($http->code != 200 || !-e "$firmwareFile.tmp") {
				return $ecb->($http, $http->mess);
			}

			rename "$firmwareFile.tmp", $firmwareFile or return $ecb->($http, "Unable to rename temporary $firmwareFile file" );

			return $cb->($firmwareFile);
		},
		$ecb,
		{
			saveAs => "$firmwareFile.tmp",
		}
	)->get($url);

	return;
}

sub _getFirmwareTag {
	my ($url) = @_;

	if (my ($model, $resolution, $version, $branch) = $url =~ $FW_TAG_REGEX) {
		my $releaseInfo = {
			model => $model,
			res => $resolution,
			version => $version,
			branch => $branch
		};

		$prefs->set('lastReleaseTagUsed', $releaseInfo);

		return $releaseInfo;
	}
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