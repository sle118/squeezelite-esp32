package Plugins::SqueezeESP32::RgbLed;

=head1 NAME

Plugins::SqueezeESP32::RgbLed

=head1 DESCRIPTION

L<Plugins::SqueezeESP32::RgbLed>

=cut

use strict;
use Slim::Utils::Strings qw(string cstring);

use Slim::Utils::Log;
use Slim::Utils::Prefs;
use Plugins::SqueezeESP32::Player

my $log = logger('player.RgbLed');

my $prefs = preferences('plugin.squeezeesp32');
my $log   = logger('plugin.squeezeesp32');

sub init {
	Slim::Control::Request::subscribe( sub { onNotification(@_) }, [ ['playlist'], ['open', 'pause', 'resume', 'stop', 'clear'] ]);

	# register led visualizer comands to allow independant update and command line controls.
	Slim::Control::Request::addDispatch([ 'dmx', '_data', '_xoff'], [1, 0, 0, \&sendDMX]);
	Slim::Control::Request::addDispatch([ 'led_visual', '_mode', '_bright'], [1, 0, 0, \&setLEDVisu]);
}

my $VISUALIZER_NONE = 0;
my $VISUALIZER_VUMETER = 1;
my $VISUALIZER_SPECTRUM_ANALYZER = 2;
my $VISUALIZER_WAVEFORM = 3;
my @ledvisualizers = (
	{ desc => ['BLANK'],
	  params => [$VISUALIZER_NONE],
    },
	{ desc => ['VISUALIZER_ANALOG_VUMETER'],
	  params => [$VISUALIZER_VUMETER, 0],
    },
	{ desc => ['VISUALIZER_DIGITAL_VUMETER'],
	  params => [$VISUALIZER_VUMETER, 1],
    },
	{ desc => ['VISUALIZER_SPECTRUM_ANALYZER'],
	  params => [$VISUALIZER_SPECTRUM_ANALYZER, 0],
    },
	{ desc => ['VISUALIZER_SPECTRUM_ANALYZER','2'],
	  params => [$VISUALIZER_SPECTRUM_ANALYZER, 1],
    },
	{ desc => ['PLUGIN_SQUEEZEESP32_WAVEFORM'],
	  params => [$VISUALIZER_WAVEFORM, 0],
    },
	{ desc => ['PLUGIN_SQUEEZEESP32_WAVEFORM','2'],
	  params => [$VISUALIZER_WAVEFORM, 1],
    },
);

my $nledvisualizers = $#ledvisualizers;

sub ledVisualizerModes {
	return \@ledvisualizers;
}

sub ledVisualizerNModes {
	return $nledvisualizers;
}

sub updateLED {
	my $client = shift;
	my $cprefs = $prefs->client($client);
	
	my $visu = $cprefs->get('led_visualizer') || 0;
	my $bright = $cprefs->get('led_brightness') || 20;
	
	$visu = 0 if ($visu < 0 || $visu > ledVisualizerNModes || !(Slim::Player::Source::playmode($client) eq 'play'));
	my $modes  = ledVisualizerModes;
	my $params = $modes->[$visu]{'params'};
	my $data = pack('CCC', $params->[0], $params->[1], $bright);
	main::INFOLOG && $log->is_debug && $log->info("Sending visu mode $visu ", $client->name);

	$client->sendFrame( ledv => \$data );
}

sub ledVisualParams {
	my $client = shift;
	
	my $visu = $prefs->client($client)->get('led_visualizer') || 0;
	
	return $ledvisualizers[$visu]{params};
}

sub ledVisualModeOptions {
	my $client = shift;

	my $display = {
		'-1' => ' '
	};

	my $modes  = ledVisualizerModes; 
	my $nmodes = ledVisualizerNModes; 

	for (my $i = 0; $i <= $nmodes; $i++) {

		my $desc = $modes->[$i]{'desc'};

		for (my $j = 0; $j < scalar @$desc; $j++) {

			$display->{$i} .= ' ' if ($j > 0);
			$display->{$i} .= string(@{$desc}[$j]) || @{$desc}[$j];
		}
	}

	return $display;
}

sub sendDMX {
	my $request = shift;

	# check this is the correct command.
	if ($request->isNotCommand([['dmx']])) {
		$request->setStatusBadDispatch();
		return;
	}

	# get our parameters
	my $client   = $request->client();
	
	my $count = 0;
	my $outData;
	my @values = split(',', $request->getParam('_data') || '');
	foreach my $val (@values) {
		$outData .= pack ( 'C', $val);
		$count++;
	}
	$count /= 3;

	my $data = pack('nn', $request->getParam('_xoff') || 0, $count ) . $outData;
	
	# changed from dmxt to ledd (matches 'ledc' for tricolor led in receiver player)
	$client->sendFrame( ledd => \$data );
}

sub setLEDVisu {
	my $request = shift;

	# check this is the correct command.
	if ($request->isNotCommand([['led_visual']])) {
		$request->setStatusBadDispatch();
		return;
	}

	my $client   = $request->client();
	return if (!$client->hasLED);
	
	my $cprefs = $prefs->client($client);
	
	my $visu = $cprefs->get('led_visualizer') || 0;
	my $mode = $request->getParam('_mode') || -1;
	if ($mode == -1) {
		$visu+=1;
	} else {
		$visu = $mode;
	} 
	$visu = 0 if ($visu < 0 || $visu > ledVisualizerNModes);
	$cprefs->set('led_visualizer', $visu);
	
	my $bright = $request->getParam('_bright') || -1;
	if ($bright >= 0 && $bright < 256) {
		$cprefs->set('led_brightness', $bright);
	}
	
	updateLED($client);
}

sub onNotification {
	my $request = shift;
	my $client  = $request->client || return;
	
	foreach my $player ($client->syncGroupActiveMembers) {
		next unless $player->isa('Plugins::SqueezeESP32::Player');
		updateLED($player) if $player->hasLED;
	}
}

1;
