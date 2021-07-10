package Plugins::SqueezeESP32::Player;

use strict;
use base qw(Slim::Player::SqueezePlay);

use Digest::MD5 qw(md5);
use List::Util qw(min);

use Slim::Utils::Log;
use Slim::Utils::Prefs;

use Plugins::SqueezeESP32::FirmwareHelper;
use Plugins::SqueezeESP32::RgbLed;

my $sprefs = preferences('server');
my $prefs = preferences('plugin.squeezeesp32');
my $log   = logger('plugin.squeezeesp32');

{
	__PACKAGE__->mk_accessor('rw', qw(tone_update depth));
}

sub new {
	my $class = shift;
	my $client = $class->SUPER::new(@_);
	$client->init_accessor(
		tone_update	=> 0,
	);
	return $client;
}

our $defaultPrefs = {
	'analogOutMode'        => 0,
	'bass'                 => 0,
	'treble'               => 0,
	'lineInAlwaysOn'       => 0, 
	'lineInLevel'          => 50, 
	'menuItem'             => [qw(
		NOW_PLAYING
		BROWSE_MUSIC
		RADIO
		PLUGIN_MY_APPS_MODULE_NAME
		PLUGIN_APP_GALLERY_MODULE_NAME
		FAVORITES
		GLOBAL_SEARCH
		PLUGIN_LINE_IN
		PLUGINS
		SETTINGS
		SQUEEZENETWORK_CONNECT
	)],
};

my $handlersAdded;

sub model { 'squeezeesp32' }
sub modelName { 'SqueezeESP32' }

sub hasScrolling { 1 }
sub hasIR { 1 }
# TODO: add in settings when ready
sub hasLineIn { 0 }
sub hasHeadSubOut { 1 }
sub maxTreble {	20 }
sub minTreble {	-13 }
sub maxBass { 20 }
sub minBass { -13 }

sub hasLED { 
	my $client = shift;
	return $prefs->client($client)->get('led_config') || 0;
}

sub init {
	my $client = shift;
	my ($id, $caps) = @_;
	
	my ($depth) = $caps =~ /Depth=(\d+)/;
	$client->depth($depth || 16);
	
	if (!$handlersAdded) {
	
		# Add a handler for line-in/out status changes
		Slim::Networking::Slimproto::addHandler( LIOS => \&lineInOutStatus );
	
		# Create a new event for sending LIOS updates
		Slim::Control::Request::addDispatch(
			['lios', '_state'],
			[1, 0, 0, undef],
		   );
		
		Slim::Control::Request::addDispatch(
			['lios', 'linein', '_state'],
			[1, 0, 0, undef],
		   );
		
		Slim::Control::Request::addDispatch(
			['lios', 'lineout', '_state'],
			[1, 0, 0, undef],
		   );
		
		$handlersAdded = 1;

	}
	
	$client->SUPER::init(@_);
	Plugins::SqueezeESP32::FirmwareHelper::init($client);
	Plugins::SqueezeESP32::RgbLed::init($client);

	main::INFOLOG && $log->is_info && $log->info("SqueezeESP player connected: " . $client->id);
}	

sub initPrefs {
	my $client = shift;
	
	$sprefs->client($client)->init($defaultPrefs);
	
	$prefs->client($client)->init( { 
		equalizer => [(0) x 10],
		artwork => undef,
		led_config => 0,
		led_visualizer => 0,
		led_brightness => 20,
	} );

	$client->SUPER::initPrefs;
}

sub power {
	my $client = shift;
	my $on     = shift;

	my $res = $client->SUPER::power($on, @_);
	return $res unless defined $on;
	
	if ($on) {
		$client->update_artwork(1);
	} else {
		$client->clear_artwork(1);
	}
	
	return $res;
}	

# Allow the player to define it's display width (and probably more)
sub playerSettingsFrame {
	my $client   = shift;
	my $data_ref = shift;

	my $value;
	my $id = unpack('C', $$data_ref);

	# New SETD command 0xfe for display width & height
	if ($id == 0xfe) {
		$value = (unpack('Cn', $$data_ref))[1];
		if ($value > 100 && $value < 400) {
			$prefs->client($client)->set('width', $value);

			my $height = (unpack('Cnn', $$data_ref))[2];
			$prefs->client($client)->set('height', $height || 0);

			$client->display->modes($client->display->build_modes);
			$client->display->widthOverride(1, $value);
			$client->update;

			main::INFOLOG && $log->is_info && $log->info("Setting player $value" . "x" . "$height for ", $client->name);
		}
		my $led_config = (unpack('Cnnn',$$data_ref))[3];
		$prefs->client($client)->set('led_config', $led_config);
		main::INFOLOG && $log->is_info && $led_config && $log->info("Setting led length $led_config for ", $client->name);
	}

	$client->SUPER::playerSettingsFrame($data_ref);
}

sub bass {
	my ($client, $new) = @_;
	my $value = $client->SUPER::bass($new);
	
	$client->update_equalizer($value, [2, 1, 3]) if defined $new;
	
	return $value;
}

sub treble {
	my ($client, $new) = @_;
	my $value = $client->SUPER::treble($new);
	
	$client->update_equalizer($value, [8, 9, 7]) if defined $new;

	return $value;
}

sub send_equalizer {
	my ($client, $equalizer) = @_;

	$equalizer ||= $prefs->client($client)->get('equalizer') || [(0) x 10];
	my $size = @$equalizer;
	my $data = pack("c[$size]", @{$equalizer});
	$client->sendFrame( eqlz => \$data );
}

sub update_equalizer {
	my ($client, $value, $index) = @_;
	return if $client->tone_update;
	
	my $equalizer = $prefs->client($client)->get('equalizer');	
	$equalizer->[$index->[0]] = $value;
	$equalizer->[$index->[1]] = int($value / 2 + 0.5);
	$equalizer->[$index->[2]] = int($value / 4 + 0.5);
	$prefs->client($client)->set('equalizer', $equalizer);
}

sub update_tones {
	my ($client, $equalizer) = @_;

	$client->tone_update(1);
	$sprefs->client($client)->set('bass', int(($equalizer->[1] * 2 + $equalizer->[2] + $equalizer->[3] * 4) / 7 + 0.5));
	$sprefs->client($client)->set('treble', int(($equalizer->[7] * 4 + $equalizer->[8] + $equalizer->[9] * 2) / 7 + 0.5));
	$client->tone_update(0);	
}

sub update_artwork {
	my $client = shift;
	my $cprefs = $prefs->client($client);

	my $artwork = $cprefs->get('artwork') || return;
	return unless $artwork->{'enable'} && $client->display->isa("Plugins::SqueezeESP32::Graphics");
	
	my $header = pack('Nnn', $artwork->{'enable'}, $artwork->{'x'}, $artwork->{'y'});
	$client->sendFrame( grfa => \$header );
	$client->display->update;

	my $s = min($cprefs->get('height') - $artwork->{'y'}, $cprefs->get('width') - $artwork->{'x'});
	my $params = { force => shift || 0 };
	my $path = 'music/current/cover_' . $s . 'x' . $s . '_o.jpg';
	my $body = Slim::Web::Graphics::artworkRequest($client, $path, $params, \&send_artwork, undef, HTTP::Response->new);

	send_artwork($client, undef, \$body) if $body;
}

sub send_artwork {
	my ($client, $params, $dataref) = @_;

	# I'm not sure why we are called so often, so only send when needed
	my $md5 = md5($$dataref);
	return if $client->pluginData('artwork_md5') eq $md5 && !$params->{'force'};

	$client->pluginData('artwork', $dataref);
	$client->pluginData('artwork_md5', $md5);

	my $artwork = $prefs->client($client)->get('artwork') || {};
	my $length = length $$dataref;
	my $offset = 0;

	$log->info("got resized artwork (length: ", length $$dataref, ")");

	my $header = pack('Nnn', $length, $artwork->{'x'}, $artwork->{'y'});

	while ($length > 0) {
		$length = 1280 if $length > 1280;
		$log->info("sending grfa $length");

		my $data = $header . pack('N', $offset) . substr( $$dataref, 0, $length, '' );

		$client->sendFrame( grfa => \$data );
		$offset += $length;
		$length = length $$dataref;
	}
}

sub clear_artwork {
	my ($client, $force, $from) = @_;

	my $artwork = $prefs->client($client)->get('artwork');

	if ($artwork && $artwork->{'enable'}) {
		main::INFOLOG && $log->is_info && $log->info("artwork stop/clear " . ($from || ""));
		$client->pluginData('artwork_md5', '');
		# refresh screen and disable artwork when artwork was full screen (hack)
		if ((!$artwork->{'x'} && !$artwork->{'y'}) || $force) {
			$client->sendFrame(grfa => \("\x00"x4));
			$client->display->update;
		}	
	}
}

sub config_artwork {
	my ($client) = @_;

	if ( my $artwork = $prefs->client($client)->get('artwork') ) {
		my $header = pack('Nnn', $artwork->{'enable'}, $artwork->{'x'}, $artwork->{'y'});
		$client->sendFrame( grfa => \$header );
		$client->display->update;
	}
}

sub reconnect {
	my $client = shift;
	$client->SUPER::reconnect(@_);
	
	$client->pluginData('artwork_md5', '');
	$client->config_artwork if $client->display->isa("Plugins::SqueezeESP32::Graphics");
	$client->send_equalizer;
}

# Change the analog output mode between headphone and sub-woofer
# If no mode is specified, the value of the client's analogOutMode preference is used.
# Otherwise the mode is temporarily changed to the given value without altering the preference.
sub setAnalogOutMode {
	my $client = shift;
	# 0 = headphone (internal speakers off), 1 = sub out,
	# 2 = always on (internal speakers on), 3 = always off
	my $mode = shift;

	if (! defined $mode) {
		$mode = $sprefs->client($client)->get('analogOutMode');
	}

	my $data = pack('C', $mode);
	$client->sendFrame('audo', \$data);
}

# LINE_IN 	0x01
# LINE_OUT	0x02
# HEADPHONE	0x04

sub lineInConnected {
	my $state = Slim::Networking::Slimproto::voltage(shift) || return 0;
	return $state & 0x01 || 0;
}

sub lineOutConnected {
	my $state = Slim::Networking::Slimproto::voltage(shift) || return 0;
	return $state & 0x02 || 0;
}

sub lineInOutStatus {
	my ( $client, $data_ref ) = @_;
	
	my $state = unpack 'n', $$data_ref;

	my $oldState = {
		in  => $client->lineInConnected(),
		out => $client->lineOutConnected(),
	};
	
	Slim::Networking::Slimproto::voltage( $client, $state );

	Slim::Control::Request::notifyFromArray( $client, [ 'lios', $state ] );
	
	if ($oldState->{in} != $client->lineInConnected()) {
		Slim::Control::Request::notifyFromArray( $client, [ 'lios', 'linein', $client->lineInConnected() ] );
		if ( Slim::Utils::PluginManager->isEnabled('Slim::Plugin::LineIn::Plugin')) {
			Slim::Plugin::LineIn::Plugin::lineInItem($client, 1);
		}
	}

	if ($oldState->{out} != $client->lineOutConnected()) {
		Slim::Control::Request::notifyFromArray( $client, [ 'lios', 'lineout', $client->lineOutConnected() ] );
	}
}

sub voltage {
	my $voltage = Slim::Networking::Slimproto::voltage(shift) || return 0;
	return sprintf("%.2f", ($voltage >> 4) / 128);
}

1;
