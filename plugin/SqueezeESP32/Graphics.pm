package Plugins::SqueezeESP32::Graphics;

use strict;

use base qw(Slim::Display::Squeezebox2);

use Slim::Utils::Prefs;
use Slim::Utils::Log;

my $prefs = preferences('plugin.squeezeesp32');
my $log   = logger('plugin.squeezeesp32');

my $VISUALIZER_NONE = 0;
my $VISUALIZER_VUMETER = 1;
my $VISUALIZER_SPECTRUM_ANALYZER = 2;
my $VISUALIZER_WAVEFORM = 3;
my $VISUALIZER_VUMETER_ESP32 = 0x11;
my $VISUALIZER_SPECTRUM_ANALYZER_ESP32 = 0x12;

my %SPECTRUM_DEFAULTS = (
	scale => 25,
	small => {
		size => 25,
		band => 5.33
	},
	full => {
		band => 8
	},
);

{
	#__PACKAGE__->mk_accessor('array', 'modes');
	__PACKAGE__->mk_accessor('rw', 'modes');
	__PACKAGE__->mk_accessor('rw', qw(vfdmodel));
}

sub new {
	my $class = shift;
	my $client = shift;
	
	my $display = $class->SUPER::new($client);
	my $cprefs = $prefs->client($client);
	
	$cprefs->init( { 
		width => 128,
		small_VU => 15,
		spectrum => \%SPECTRUM_DEFAULTS,
	} );

	$prefs->migrateClient(2, sub {
		my ($cprefs, $client) = @_;
		sanitizeSpectrum($cprefs->get('spectrum'));
		1;
	});

	$display->init_accessor(
		modes => $display->build_modes,
		# Only seems to matter for screensaver and update to decide font. Not 
		# any value is acceptable, so use Boom value which seems to be best 
		# compromise
		vfdmodel => 'graphic-160x32',	
	);	
	
	return $display;
}

=comment
sub modes {
	return \@modes;
}
=cut

sub nmodes {
	return scalar($#{shift->modes()});
}

sub displayWidth {
	my $display = shift;
	my $client = $display->client;
	
	# if we're showing the always-on visualizer & the current buttonmode 
	# hasn't overridden, then use the playing display mode to index
	# into the display width, otherwise, it's fullscreen.
	my $mode = 0;
	
	if ( $display->showVisualizer() && !defined($client->modeParam('visu')) ) {
		my $cprefs = preferences('server')->client($client);
		$mode = $cprefs->get('playingDisplayModes')->[ $cprefs->get('playingDisplayMode') ];
	}
	
	if ($display->widthOverride) {
		my $artwork = $prefs->client($client)->get('artwork');
		if ($artwork->{'enable'} && $artwork->{'y'} < 32 && ($client->isPlaying || $client->isPaused)) {
			return ($artwork->{x} || $display->widthOverride) + ($display->modes->[$mode || 0]{_width} || 0);
		} else {
			return $display->widthOverride + ($display->modes->[$mode || 0]{_width} || 0);
		}	
	} else {
		return $display->modes->[$mode || 0]{width};
	}	
}

sub brightnessMap {
	return (0 .. 5);
}

=comment
sub bytesPerColumn {
	return 4;
}
=cut

# I don't think LMS renderer handles properly screens other than 32 pixels. It
# seems that all we get is a 32 pixel-tall data with anything else padded to 0
# i.e. if we try 64 pixels height, bytes 0..3 and 4..7 will contains the same 
# pattern than the 32 pixels version, where one would have expected bytes 4..7
# to be empty
sub displayHeight {
	return 32;
}

sub sanitizeSpectrum {
	my ($spectrum) = shift;

	$spectrum->{small}->{size} ||= $SPECTRUM_DEFAULTS{small}->{size};
	$spectrum->{small}->{band} ||= $SPECTRUM_DEFAULTS{small}->{band};
	$spectrum->{full}->{band} ||= $SPECTRUM_DEFAULTS{full}->{band};

	return $spectrum;
}

sub build_modes {
	my $client = shift->client;
	my $cprefs = $prefs->client($client);
	
	my $artwork = $cprefs->get('artwork');
	my $disp_width = $cprefs->get('width') || 128;

	# if artwork is in main display, reduce width but when artwork is (0,0) fake it
	my $width = ($artwork->{'enable'} && $artwork->{'y'} < 32 && $artwork->{'x'}) ? $artwork->{'x'} : $disp_width;
	my $width_low = ($artwork->{'enable'} && $artwork->{'x'} && ($artwork->{'y'} >= 32 || $disp_width - $artwork->{'x'} > 32)) ? $artwork->{'x'} : $disp_width;
			
	my $small_VU = $cprefs->get('small_VU');
	my $spectrum = sanitizeSpectrum($cprefs->get('spectrum'));

	my $small_spectrum_pos = { x => $width - int ($spectrum->{small}->{size} * $width / 100),
						 width => int ($spectrum->{small}->{size} * $width / 100),
			};
	my $small_VU_pos = { x => $width - int ($small_VU * $width / 100), 
						 width => int ($small_VU * $width / 100),
			};		
	
	my @modes = (
		# mode 0
		{ desc => ['BLANK'],
		bar => 0, secs => 0,  width => $width, 
		params => [$VISUALIZER_NONE] },
		# mode 1
		{ desc => ['PROGRESS_BAR'],
		bar => 1, secs => 0,  width => $width,
		params => [$VISUALIZER_NONE] },
		# mode 2
		{ desc => ['ELAPSED'],
		bar => 0, secs => 1,  width => $width,
		params => [$VISUALIZER_NONE] },
		# mode 3
		{ desc => ['ELAPSED', 'AND', 'PROGRESS_BAR'],
		bar => 1, secs => 1,  width => $width, 
		params => [$VISUALIZER_NONE] },
		# mode 4
		{ desc => ['REMAINING'],
		bar => 0, secs => -1, width => $width,
		params => [$VISUALIZER_NONE] },
		# mode 5  
		{ desc => ['CLOCK'],
		bar => 0, secs => 0, width => $width, clock => 1,
		params => [$VISUALIZER_NONE] },
		# mode 6	  
		{ desc => ['SETUP_SHOWBUFFERFULLNESS'],
		bar => 0, secs => 0,  width => $width, fullness => 1,
		params => [$VISUALIZER_NONE] },
		# mode 7
		{ desc => ['VISUALIZER_VUMETER_SMALL'],
		bar => 0, secs => 0,  width => $width, _width => -$small_VU_pos->{'width'},
		# extra parameters (width, height, col (< 0 = from right), row (< 0 = from bottom), left_space)
		params => [$VISUALIZER_VUMETER_ESP32, $small_VU_pos->{'width'}, 32, $small_VU_pos->{'x'}, 0, 2] },
		# mode 8
		{ desc => ['VISUALIZER_SPECTRUM_ANALYZER_SMALL'],
		bar => 0, secs => 0,  width => $width, _width => -$small_spectrum_pos->{'width'},
		# extra parameters (width, height, col (< 0 = from right), row (< 0 = from bottom), left_space, #bars, scale)
		params => [$VISUALIZER_SPECTRUM_ANALYZER_ESP32, $small_spectrum_pos->{width}, 32, $small_spectrum_pos->{'x'}, 0, 2, $small_spectrum_pos->{'width'} / $spectrum->{small}->{band}, $spectrum->{scale}] },  
		# mode 9	 
		{ desc => ['VISUALIZER_VUMETER'],
		bar => 0, secs => 0,  width => $width,
		params => [$VISUALIZER_VUMETER_ESP32, $width_low, 0] },
		# mode 10	
		{ desc => ['VISUALIZER_ANALOG_VUMETER'],
		bar => 0, secs => 0,  width => $width,
		params => [$VISUALIZER_VUMETER_ESP32, $width_low, 1] },
		# mode 11
		{ desc => ['VISUALIZER_SPECTRUM_ANALYZER'],
		bar => 0, secs => 0,  width => $width,
		# extra parameters (bars)
		params => [$VISUALIZER_SPECTRUM_ANALYZER_ESP32, $width_low, int ($width/$spectrum->{full}->{band}), $spectrum->{scale}] },	  
	);

my @extra = (
		# mode E1
		{ desc => ['VISUALIZER_VUMETER', 'AND', 'ELAPSED'],
		bar => 0, secs => 1,  width => $width,
		params => [$VISUALIZER_VUMETER_ESP32, $width_low, 0] },
		# mode E2	 
		{ desc => ['VISUALIZER_ANALOG_VUMETER', 'AND', 'ELAPSED'],
		bar => 0, secs => 1,  width => $width,
		params => [$VISUALIZER_VUMETER_ESP32, $width_low, 1] },
		# mode E3
		{ desc => ['VISUALIZER_SPECTRUM_ANALYZER', 'AND', 'ELAPSED'],
		bar => 0, secs => 1,  width => $width,
		# extra parameters (bars)
		params => [$VISUALIZER_SPECTRUM_ANALYZER_ESP32, $width_low, int ($width/$spectrum->{full}->{band}), $spectrum->{scale}] },	  
		# mode E4	 
		{ desc => ['VISUALIZER_VUMETER', 'AND', 'REMAINING'],
		bar => 0, secs => -1,  width => $width,
		params => [$VISUALIZER_VUMETER_ESP32, $width_low, 0] },
		# mode E5
		{ desc => ['VISUALIZER_ANALOG_VUMETER', 'AND', 'REMAINING'],
		bar => 0, secs => -1,  width => $width,
		params => [$VISUALIZER_VUMETER_ESP32, $width_low, 1] },
		# mode E6
		{ desc => ['VISUALIZER_SPECTRUM_ANALYZER', 'AND', 'REMAINING'],
		bar => 0, secs => -1,  width => $width,
		# extra parameters (bars)
		params => [$VISUALIZER_SPECTRUM_ANALYZER_ESP32, $width_low, int ($width/$spectrum->{full}->{band}), $spectrum->{scale}] },	
		# mode E7	 
		{ desc => ['VISUALIZER_VUMETER', 'AND', 'PROGRESS_BAR', 'AND', 'REMAINING'],
		bar => 1, secs => -1,  width => $width,
		params => [$VISUALIZER_VUMETER_ESP32, $width_low, 0] },
		# mode E8
		{ desc => ['VISUALIZER_ANALOG_VUMETER', 'AND', 'PROGRESS_BAR', 'AND', 'REMAINING'],
		bar => 1, secs => -1,  width => $width,
		params => [$VISUALIZER_VUMETER_ESP32, $width_low, 1] },
		# mode E9
		{ desc => ['VISUALIZER_SPECTRUM_ANALYZER', 'AND', 'PROGRESS_BAR', 'AND', 'REMAINING'],
		bar => 1, secs => -1,  width => $width,
		# extra parameters (bars)
		params => [$VISUALIZER_SPECTRUM_ANALYZER_ESP32, $width_low, int ($width/$spectrum->{full}->{band}), $spectrum->{scale}] },	
		# mode E10
		{ desc => ['VISUALIZER_SPECTRUM_ANALYZER', 'PLUGIN_SQUEEZEESP32_TAUDIO'],
		bar => 1, secs => -1,  width => $width,
		# extra parameters (19 bars for led vu)
		params => [$VISUALIZER_SPECTRUM_ANALYZER_ESP32, $width_low, 19, $spectrum->{scale}] },	
	);		
	
	@modes = (@modes, @extra) if $cprefs->get('height') > 32;

	return \@modes;
}	

1;