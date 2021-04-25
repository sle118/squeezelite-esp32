package Plugins::SqueezeESP32::PlayerSettings;

use strict;
use base qw(Slim::Web::Settings);
use List::Util qw(first);

use Slim::Utils::Log;
use Slim::Utils::Prefs;

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
	return ($prefs->client($client), @prefs);
}

sub handler {
	my ($class, $client, $paramRef) = @_;

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

		if ($client->depth == 16) {
			my $equalizer = $cprefs->get('equalizer');
			for my $i (0 .. $#{$equalizer}) {
				$equalizer->[$i] = $paramRef->{"pref_equalizer.$i"} || 0;
			}
			$cprefs->set('equalizer', $equalizer);
			$client->update_tones($equalizer);
		}		
	}
	
	if ($paramRef->{'led_vu_send'}) {
		my @command = ('dmx', $paramRef->{'pref_led_vu_cmd'}, $paramRef->{'pref_led_vu_x'});
		my $request = Slim::Control::Request::executeRequest($client, \@command);
		#my $validQuery = $test_request->isValidQuery();
	}

	if ($client->displayWidth) {
		# the Settings super class can't handle anything but scalar values
		# we need to populate the $paramRef for the other prefs manually
		$paramRef->{'pref_spectrum'} = $cprefs->get('spectrum');
		$paramRef->{'pref_artwork'} = $cprefs->get('artwork');
	}

	$paramRef->{'pref_equalizer'} = $cprefs->get('equalizer') if $client->depth == 16;

	return $class->SUPER::handler($client, $paramRef);
}

1;