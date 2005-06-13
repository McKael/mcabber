#!/usr/bin/perl -w
#
# usage: cicq2mcabber.pl cicqhistoryfile > mcabberhistoryfile
# Convert a centericq history file to mcabber format
#
# See histolog.c for the mcabber format.
#
# MiKael, 2005/05/05

use strict;

my $line;
my $inblock = 0;

my %bdata = ();

sub print_entry()
{
  my ($type, $info, $date, $len, $data);

  return  unless(defined $bdata{"msg"});

  if ($bdata{"type"} eq "MSG") {
    $type = "M";
    if ($bdata{"inout"} eq "OUT") {
      $info = "S";
    } elsif ($bdata{"inout"} eq "IN") {
      $info = "R";
    } else {
      print STDERR "Neither IN nor OUT!?\n";
      return;
    }
  } else {
    print STDERR "Data type not handled.\n";
    return;
  }
  $date = $bdata{"timestamp"};
  $len  = $bdata{"nb"};
  $data = $bdata{"msg"};

  # Date conversion to iso8601
  my ($ss,$mm,$hh,$DD,$MM,$YYYY) = gmtime($date);
  $date = sprintf "%04d%02d%02dT%02d:%02d:%02dZ",
                  $YYYY+1900, $MM+1,$DD,$hh,$mm,$ss;

  printf("%s%s %18.18s %03d %s", $type, $info, $date, $len, $data);
}

while ($line = <>) {
  chomp $line;
  # The separator is ^L ; I use substr to exclude the EOL
  if ($line eq "") {
    print_entry() if ($inblock);
    # reset data for new block
    %bdata = ();
    $inblock = 1;
    next;
  }
  # Skip garbage or unrecognized stuff...
  if (!$inblock) {
    print STDERR "Skipping line\n";
    next;
  }

  # 1 IN/OUT line
  unless (exists $bdata{"inout"}) {
    if (($line eq "IN") || ($line eq "OUT")) {
      $bdata{"inout"} = $line;
    } else {
      print STDERR "No IN/OUT information ($line)!\n";
      $inblock = 0;
    }
    next;
  }
  # 1 type line (MSG...)
  unless (exists $bdata{"type"}) {
    # We don't handle NOTE and AUTH yet.
    if ($line eq "MSG") {
      $bdata{"type"} = $line;
    } else {
      print STDERR "Not a MSG type ($line)\n";
      $inblock = 0;
    }
    next;
  }
  # 2 date lines
  unless (exists $bdata{"timestamp"}) {
    $bdata{"timestamp"} = $line;
    last unless defined ($line = <>);
    chomp $line;
    $bdata{"timestamp2"} = $line;
    if (! $bdata{"timestamp2"} eq $bdata{"timestamp2"}) {
      print STDERR "Timestamps differ...\n";
    }
    next;
  }
  # ... The message itself
  $line =~ s/$//;
  if (exists $bdata{"msg"}) {
    $bdata{"msg"} .= $line."\n";
    $bdata{"nb"}++;
  } else {
    $bdata{"msg"} = $line."\n";
    $bdata{"nb"} = 0;
  }
}
print_entry() if ($inblock);

# vim:set sw=2 sts=2 et si cinoptions="":
