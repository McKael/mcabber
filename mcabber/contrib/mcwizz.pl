#!/usr/bin/perl
#
#  Copyright (C) 2006 Adam Wolk "Mulander" <netprobe@gmail.com>
#  Copyright (C) 2006 Mateusz Karkula "Karql"
#  A few tweaks by Mikael Berthe
#
# This script is provided under the terms of the GNU General Public License,
# see the file COPYING in the root mcabber source directory.
#

use warnings;
use strict;

use Getopt::Long;
use Term::ReadKey;

my %options;

Getopt::Long::Configure qw(bundling);
my $result = GetOptions(
		"help|h"	=> \$options{help},
		"version|V"	=> \$options{version},
		"ignore|i"	=> \$options{ignore},
		"ignore-auto|I"	=> \$options{ignore_auto},
		"output|o"	=> \$options{output},
		"passwd|p"	=> \$options{passwd},
		"color|c"	=> \$options{color},
		"ssl|s"		=> \$options{ssl},
		"status|S"	=> \$options{status},
		"proxy|P"	=> \$options{proxy},
		"keep|k"	=> \$options{keep},
		"log|l"		=> \$options{log},
		"tracelog|t"	=> \$options{tracelog},
);

##
# Allowed colors
my @bg_color	= qw( black red green yellow blue magenta cyan white );
my @fg_color	= (@bg_color, map("bright$_", @bg_color), map("bold$_", @bg_color));

##
# info for specific settings
my %info = (
	# not grouped
	beep_on_message => { msg => 'Should mcabber beep when you receive a message?', allow => undef, type => 'yesno', anwsers => undef, default => 0},
	password	=> { msg => 'Enter your password (at your own risk, this will be saved in plain text)', allow=>'.+', type=>'pass',anwsers => undef, default => undef },
	pinginterval	=> { msg => 'Enter pinginterval in seconds for keepalive settings, set this to 0 to disable, ', allow =>'\d+', type=>'edit', anwsers => undef, default => 40 },
	hide_offline_buddies => {msg => 'Display only connected buddies in the roster?', allow => undef, type => 'yesno', anwsers => undef, default => 0 },
	iq_version_hide_os => { msg => 'Hide Your OS information?', allow => undef, type => 'yesno', anwsers => undef, default => 0 },
	# server settings
	username 	=> { msg => 'Your username', allow => '[^\s\@:<>&\'"]+', type => 'edit', anwsers => undef, default => undef },
	server		=> { msg => 'Your jabber server', allow => '\S+', type => 'edit', anwsers => undef, default => undef },
	resource	=> { msg => 'Resource (If your don\'t know what a resource is, use the default setting)', allow => '.{1,1024}', type => 'edit', anwsers => undef, default => 'mcabber' },
	nickname	=> { msg => 'Conference nickname (if you skip this setting your username will be used as a nickname in MUC chatrooms)', allow => '.+', type => 'edit', anwsers => undef, default => undef },
	# ssl settings
	ssl		=> { msg => 'Enable ssl?', allow => undef, type => 'yesno', anwsers => undef, default => 0 },
	port		=> { msg => 'Enter ssl port', allow => '\d+', type => 'edit', anwsers => undef, default => 5222 },
	# proxy settings
	proxy_host	=> { msg => 'Proxy host', allow => '\S+?\.\S+?', type => 'edit', anwsers => undef, default => undef },
	proxy_port	=> { msg => 'Proxy port', allow => '\d+', type => 'edit', anwsers => undef, default => 3128 },
	proxy_user	=> { msg => 'Proxy user (optional, you can skip this if not required)', allow => '.+', type => 'edit', anwsers => undef, default => undef },
	proxy_pass	=> { msg => 'Proxy pass (optional, you can skip this if not required)', allow => '.+', type => 'pass', anwsers => undef, default => undef },
	# trace logs
	tracelog_level => { msg => 'Specify level of advanced traces', allow => undef, type => 'multi', anwsers => ['lvl0: I don\'t want advanced tracing','lvl1: most events of the log window are written to the file','lvl2: debug logging (XML etc.)'], default => 0 },
	tracelog_file => { msg => 'Specify a file to which the logs will be written', allow => undef, type => 'edit', anwsers => undef, default => undef },
	# logging settings
	log_win_height => { msg => 'Set log window height (minimum 1)', allow => '[1-9]\d*', type => 'edit', anwsers => undef, default => 5 },
	log_display_sender => { msg => 'Display the message sender\'s jid in the log window?', allow => undef, type => 'yesno', anwsers => undef, default => 0 },
	logging		=> { msg => 'Enable logging?', allow => undef, type => 'yesno', anwsers => undef, default => 1 },
	load_logs 	=> { msg => 'Enable loading logs?', allow => undef, type => 'yesno', anwsers => undef, default => 1 },
	logging_dir	=> { msg => 'Enter logging directory', allow => '.+' , type => 'edit', anwsers => undef, default => undef },
	log_muc_conf	=> { msg => 'Log MUC chats?', allow => undef, type => 'yesno', anwsers => undef, default => 1 },
	load_muc_logs	=> { msg => 'Load MUC chat logs?', allow => undef, type => 'yesno', default => 0 },
	# status settings
	roster_width => { msg => 'Set buddylist window width (minimum 2)', allow => '[2-9]\d*', type => 'edit', anwsers => undef, default => 24 },
	buddy_format => { msg => 'What buddy name format (in status window) do you prefer?', allow => undef, type => 'multi', anwsers => ['<jid/resource>','name <jid/resource> (name is omitted if same as the jid)','name/resource (if the name is same as the jid, use <jid/res>','name (if the name is the same as the jid, use <jid/res>'], default => 0 },
	show_status_in_buffer => { msg => 'What status changes should be displayed in the buffer?', allow => undef, type => 'multi', anwsers => ['none','connect/disconnect','all'], default => 2 },
	autoaway => { msg => 'After how many seconds of inactivity should You become auto away? (0 for never)', allow => '\d+', type => 'edit', anwsers => undef, default => 0 },
	message		=> { msg => 'Skip this setting unless you want to override all other status messages', allow => undef, type => 'edit', default => 'Unique message status'},
	message_avail	=> { msg => 'Set avaible status', allow => undef, type =>'edit',anwsers => undef, default =>'I\'m avaible'},
	message_free	=> { msg => 'Set free for chat status', allow => undef, type =>'edit', anwsers => undef, default => 'I\'m free for chat'},
	message_dnd	=> { msg => 'Set do not disturb status', allow => undef, type => 'edit', anwsers => undef, default => 'Please do not disturb'},
	message_notavail=> { msg => 'Set not avaible status', allow => undef, type => 'edit', anwsers => undef, default => 'I\'m not avaible'},
	message_away	=> { msg => 'Set away status', allow => undef, type => 'edit', anwsers => undef, default => 'I\'m away' },
	message_autoaway=> { msg => 'Set auto-away status', allow => undef, type => 'edit', anwsers => undef, default => 'Auto-away'},
	# color settings
	color_background=> { msg => 'Select background color of the chat window and the log window', allow => undef, type => 'multi', anwsers => \@bg_color, default => 'black' },
	color_general	=> { msg => 'Select text color in the chat window and the log window', allow => undef, type =>'multi', anwsers => \@fg_color , default => 'white' },
	color_msgout	=> { msg => 'Select text color in the chat window for outgoing messages', allow => undef,  type => 'multi', anwsers => \@fg_color, default => 'cyan'},
	color_bgstatus	=> { msg => 'Select background color of the status lines', allow => undef, type => 'multi', anwsers => \@bg_color, default =>'blue'},
	color_status	=> { msg => 'Select text color of the status lines', allow => undef, type => 'multi', anwsers => \@fg_color, default => 'white' },
	color_roster	=> { msg => 'Select text color of the roster (buddylist) normal items', allow => undef, type => 'multi', anwsers => \@fg_color, default => 'green' },
	color_bgrostersel=>{ msg => 'Select background color of the selected roster item', allow => undef, type => 'multi', anwsers => \@bg_color, default => 'cyan' },
	color_rostersel	=> { msg => 'Select text color of the selected roster item', allow => undef, type => 'multi', anwsers => \@fg_color, default => 'blue' },
	color_rosterselmsg=>{ msg => 'Select text color of the selected roster item, if there is a new message', allow => undef, type => 'multi', anwsers => \@fg_color, default => 'red' },
	color_rosternewmsg=>{ msg => 'Select text color of items with unread messages', allow => undef, type => 'multi', anwsers => \@fg_color , default => 'red' },
);

##
# question groups
my %groups = ( 	required => [qw(username server resource nickname ssl port)],
		proxy_settings  => [qw(proxy_host proxy_port proxy_user proxy_pass)],
		logging_settings=> [qw(logging log_win_height log_display_sender load_logs logging_dir log_muc_conf load_muc_logs)],
		status_settings => [qw(buddy_format roster_width show_status_in_buffer autoaway message message_avail message_free message_dnd message_notavail message_away message_autoaway )],
		color_settings  => [qw(color_background color_general color_msgout color_bgstatus color_status color_roster color_bgrostersel color_rostersel color_rosterselmsg color_rosternewmsg)],
		tracelog_settings => [qw(tracelog_level tracelog_file)],
);

my (%conf,@old);
##
# regexp for valid keys
my $key_reg = join '|', keys %info;

help() 		if $options{help};
version() 	if $options{version};

prepare();
ask('password') if $options{passwd};

ask($_) for @{ $groups{required} };
if($options{proxy}) { ask($_) for @{ $groups{proxy_settings} } };
ask('pinginterval') if $options{keep};
ask('beep_on_message');
ask('hide_offline_buddies');
ask('iq_version_hide_os');
ask('autoaway');
if($options{log}   	)   { ask($_) for @{ $groups{logging_settings} } };
if($options{status}	)   { ask($_) for @{ $groups{status_settings}  } };
if($options{color} 	)   { ask($_) for @{ $groups{color_settings}   } };
if($options{tracelog}	)   { ask($_) for @{ $groups{tracelog_settings}} };
build_config();

##
# Prepare for work
sub prepare
{
	mkdir "$ENV{HOME}/.mcabber", 0700 unless ( -d "$ENV{HOME}/.mcabber" );

	parse_config() if ( -e "$ENV{HOME}/.mcabber/mcabberrc" && !$options{ignore} );
}

##
# Parse current user configuration and save it
sub parse_config
{
	my $conf_file = "$ENV{HOME}/.mcabber/mcabberrc";
	my $flag = 1;

	open CONF, "<$conf_file" or return;

	my ($key,$value);
	while(<CONF>)
	{
		push @old, $_;
		$flag = 0 if $options{ignore_auto} && m/^#BEGIN AUTO GENERATED SECTION/;
		$flag = 1 if $options{ignore_auto} && m/^#END AUTO GENERATED SECTION/;
		if ( $flag && m/^set\s+($key_reg)\s*=\s*(.+)$/ )
		{
			($key,$value) = ($1,$2);

			$conf{$key} = $value if ( exists $info{$key} );
		}
	}

	close CONF;
	return 1;
}

##
# Ask the user for a setting
sub ask
{
	my ($key) = @_;

	my %dispatch = (
		edit 	=> \&_ask_edit,
		yesno 	=> \&_ask_yesno,
		multi	=> \&_ask_multi,
		pass	=> \&_ask_pass
	);

	my $lp = 1;

	print 	"\n'$key'\n",
		$info{$key}->{msg},"\n",
		( defined $info{$key}->{default} ) ?
			( $lp++, '. ', ( exists $conf{$key} ) ? 'Reset' : 'Set', " to Default [",show($key,'default'),"]\n" ) : '',
		( exists $conf{$key} ) ? ( $lp++, ". Leave Current setting [",show($key,'current'),"]\n", $lp++ ) : $lp++ , ". ",
		( $info{$key}->{type} eq 'pass') ? 'Enter Passowrd' : ( ( $info{$key}->{type} eq 'edit' ) ? 'Edit' : 'Set' ),
		"\n$lp. Skip\n[choice]: ";

	chomp(my $action = <STDIN>);
	unless ( $action =~ /^\d$/ && $action >= 1 && $action <= $lp ) {
		ask($key);
		return;
	}

	##
	# Edit
	if ( $lp -1  == $action )
	{
		&{ $dispatch{ $info{$key}->{type} } }($key);
	}

	##
	# Default
	elsif ( $action == 1 )
	{
		$conf{$key} = $info{$key}->{default}  if defined $info{$key}->{default};
	}

	##
	# Skip
	elsif ( $lp == $action )
	{
		delete $conf{$key};
	}

	##
	# Nothing for Leave Current setting

	return 1;
}

sub _ask_yesno
{
	my ($key) = @_;

	print "1. yes\n2. no\n[choice]: ";

	chomp(my $set = <STDIN>);
	unless ( $set =~ /^[12]$/ ) {
		ask($key);
		return;
	}

	$conf{$key} = $set;
	$conf{$key} = 0 if $set eq 2;
}

sub _ask_multi
{
	my ($key) = @_;
	my $count = scalar @{$info{$key}->{anwsers}};
	my $row = sprintf("%0.f",($count/3+0.5));

	for (my $i = 0; $i < $row; ++$i)
	{
		printf("%-25s", ($i+1) . ". " . $info{$key}->{anwsers}->[$i]);
		printf("%-25s", ($i+$row+1) . ". " . $info{$key}->{anwsers}->[$i+$row]) if ($i+$row < $count);
		printf("%-25s", ($i+2*$row+1) . ". " . $info{$key}->{anwsers}->[$i+2*$row]) if ($i+2*$row < $count);
		print "\n";
	}

	print '[choice]: ';
	chomp(my $set = <STDIN>);
	unless ( $set =~ /^\d+$/ && $set >= 1 && $set <= $count ) {
		ask($key);
		return;
	}
	$conf{$key} = $info{$key}->{anwsers}->[$set-1];
}

sub _ask_edit
{
	my ($key) = @_;
	print '[edit]: ';
	chomp(my $set = <STDIN>);
	unless ( $set =~ /^$info{$key}->{allow}$/ ) {
		ask($key);
		return;
	}

	$conf{$key} = $set;
}

sub _ask_pass
{
	my ($key) = @_;
	print "Characters you type in will not be shown\n[password]: ";

	ReadMode(2);
	my $anws = ReadLine(0);
	ReadMode(0);

	ask($key) unless $anws =~ /^$info{$key}->{allow}$/;
	chomp($anws);

	$anws =~ s/^((?:\s.+)|(?:.+\s))$/"$1"/;
	$conf{$key} = $anws;
}

##
# Build configuration file
sub build_config
{
	my $config_file = "$ENV{HOME}/.mcabber/mcabberrc";

	local *STDOUT unless $options{output} ;

	unless($options{output})
	{
		open STDOUT,">$config_file" or die "Can't create config file";
		chmod 0600, $config_file
	}

	my ($flag,$dumped) = (1,0);
	for (@old)
	{
		$flag = 0 if m/^#BEGIN AUTO GENERATED SECTION/;
		$flag = 1 if m/^#END AUTO GENERATED SECTION/;
		if ( $flag )
		{
			print
		}
		elsif( !$flag && !$dumped )
		{
			print "#BEGIN AUTO GENERATED SECTION\n\n";
			print "set $_ = $conf{$_}\n" for sort keys %conf;
			print "\n";
			$dumped = 1;
		}
	}

	unless($dumped)
	{
		print "\n#BEGIN AUTO GENERATED SECTION\n\n";
		print "set $_ = $conf{$_}\n" for sort keys %conf;
		print "\n#END AUTO GENERATED SECTION\n";
	}

	close STDOUT unless $options{output};
}

sub show
{
	my ($key,$name) = @_;
	my $value;

	$value = $info{$key}->{default} 	if $name eq 'default';
	$value = $conf{$key}		 	if $name eq 'current';

	if ( $info{$key}->{type} eq 'yesno' )
	{
		return ( $value ) ? 'yes' : 'no';
	}

	elsif ( $info{$key}->{type} eq 'multi' )
	{
		return $info{$key}->{anwsers}->[$value];
	}

	else
	{
		return $value;
	}
}

sub help
{
print<<EOF;
Usage: $0 options

This script generates configuration files for mcabber jabber client

-h, --help		display this help screen
-v, --version		display version information
-i, --ignore		ignore previous user configuration
-I, --ignore-auto	ignore auto generated section
-o, --output		output to stdout instead of file
-p, --passwd		save password in the config file (not recommended)
-s, --ssl		ask for ssl settings
-c, --color		ask for color settings
-S, --status		ask for status settings
-P, --proxy		ask for proxy settings
-k, --keep		ping/keepalive connection settings
-l, --log		ask for logging settings
-t, --tracelog		ask for trace log settings
EOF
exit;
}

sub version
{
print<<EOF;
mcwizz v0.02 coded by Karql & mulander <netprobe\@gmail.com>
EOF
exit;
}

# vim: set noexpandtab sts=8:
