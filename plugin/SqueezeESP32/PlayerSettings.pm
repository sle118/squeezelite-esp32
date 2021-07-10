package Plugins::SqueezeESP32::PlayerSettings;

use strict;
use base qw(Slim::Web::Settings);
use JSON::XS::VersionOneAndTwo;
use List::Util qw(first);

use Slim::Utils::Log;
use Slim::Utils::Prefs;
use Slim::Utils::Strings qw(string cstring);

my $sprefs = preferences('server');
my $prefs = preferences('plugin.squeezeesp32');
my $log   = logger('plugin.squeezeesp32');

sub name {
	return Slim::Web::HTTP::CSRF->protectName('PLUGIN_SQUEEZEESP32_PLAYERSETTINGS');
}

sub needsClient {
	return 1;
}

sub validFor {
	my ($class, $client) = @_;
	return $client->model eq 'squeezeesp32';
}

sub page {
	return Slim::Web::HTTP::CSRF->protectURI('plugins/SqueezeESP32/settings/player.html');
}

sub prefs {
	my ($class, $client) = @_;
	my @prefs;
	push @prefs, qw(width small_VU) if $client->displayWidth;
	push @prefs, qw(led_config led_visualizer led_brightness);# if $client->hasLED;
	return ($prefs->client($client), @prefs);
}

sub handler {
	my ($class, $client, $paramRef, $callback, @args) = @_;

	my ($cprefs, @prefs) = $class->prefs($client);

	if ($paramRef->{'saveSettings'}) {
		if ($client->displayWidth) {
			$cprefs->set('small_VU', $paramRef->{'pref_small_VU'} || 15);

			require Plugins::SqueezeESP32::Graphics;
			my $spectrum = Plugins::SqueezeESP32::Graphics::sanitizeSpectrum({
				scale => $paramRef->{'pref_spectrum_scale'},
				small => {
					size => $paramRef->{'pref_spectrum_small_size'},
					band => $paramRef->{'pref_spectrum_small_band'}
				},
				full => {
					band => $paramRef->{'pref_spectrum_full_band'}
				},
			});
			$cprefs->set('spectrum', $spectrum);

			my $artwork = {
				enable => $paramRef->{'pref_artwork_enable'} eq 'on',
				x => $paramRef->{'pref_artwork_x'} || 0,
				y => $paramRef->{'pref_artwork_y'} || 0,
			};

			$cprefs->set('artwork', $artwork);
			$client->display->modes($client->display->build_modes);
			# the display update will be done below, after all is completed

			# force update or disable artwork
			if ($artwork->{'enable'}) {
				$client->update_artwork(1);
			} else {
				$client->config_artwork();
			}

		}

		if ($client->can('depth') && $client->depth == 16) {
			my $equalizer = $cprefs->get('equalizer');
			for my $i (0 .. $#{$equalizer}) {
				$equalizer->[$i] = $paramRef->{"pref_equalizer.$i"} || 0;
			}
			$cprefs->set('equalizer', $equalizer);
			$client->update_tones($equalizer);
		}

		if ($client->hasLED) {
			$cprefs->set('led_visualizer', $paramRef->{'pref_led_visualizer'} || 0);
			$cprefs->set('led_brightness', $paramRef->{'pref_led_brightness'} || 20);
			Plugins::SqueezeESP32::RgbLed::updateLED($client);
		}
	}

	if ($client->displayWidth) {
		# the Settings super class can't handle anything but scalar values
		# we need to populate the $paramRef for the other prefs manually
		$paramRef->{'pref_spectrum'} = $cprefs->get('spectrum');
		$paramRef->{'pref_artwork'} = $cprefs->get('artwork');
	}

	if ($client->hasLED) {
		$paramRef->{'ledVisualModes'} = Plugins::SqueezeESP32::RgbLed::ledVisualModeOptions($client);
	}

	$paramRef->{'pref_equalizer'} = $cprefs->get('equalizer') if $client->can('depth') &&  $client->depth == 16;
	$paramRef->{'player_ip'} = $client->ip;

	Plugins::SqueezeESP32::FirmwareHelper::initFirmwareDownload($client, sub {
		my ($currentFWInfo, $newFWUrl, $customFwUrl) = @_;

		$currentFWInfo ||= {};
		my $newFWInfo = Plugins::SqueezeESP32::FirmwareHelper::getFirmwareTag($newFWUrl) || {};

		if ($paramRef->{installUpdate} || $paramRef->{installCustomUpdate}) {
			my $http = Slim::Networking::SimpleAsyncHTTP->new(sub {
				main::INFOLOG && $log->is_info && $log->info("Firmware update triggered");
			}, sub {
				main::INFOLOG && $log->is_info && $log->info("Failed to trigger firmware update");
				main::DEBUGLOG && $log->is_debug && $log->debug(Data::Dump::dump(@_));
			})->post(sprintf('http://%s/config.json', $client->ip), to_json({
				timestamp => int(Time::HiRes::time() * 1000) * 1,
				config => {
					fwurl => {
						value => $paramRef->{installCustomUpdate} ? $customFwUrl : $newFWUrl,
						type => 33
					}
				}
			}));
		}
		else {
			if ($currentFWInfo->{version} && $newFWInfo->{version} && $currentFWInfo->{version} > $newFWInfo->{version}) {
				main::INFOLOG && $log->is_info && $log->info("There's an update for your SqueezeESP32 player: $newFWUrl");
				$paramRef->{fwUpdateAvailable} = sprintf($client->string('PLUGIN_SQUEEZEESP32_FIRMWARE_AVAILABLE'), $newFWInfo->{version}, $currentFWInfo->{version});
			}
			if ($customFwUrl) {
				main::INFOLOG && $log->is_info && $log->info("There's a custom firmware for your SqueezeESP32 player: $customFwUrl");
				$paramRef->{fwCustomUpdateAvailable} = 'PLUGIN_SQUEEZEESP32_CUSTOM_FIRMWARE_AVAILABLE';
			}
		}

		$callback->( $client, $paramRef, $class->SUPER::handler($client, $paramRef), @args );
	});

	return;
}

1;