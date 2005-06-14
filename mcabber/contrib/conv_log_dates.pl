#!/usr/bin/perl -w
#
# usage: conv_log_dates.pl historyfile.old > historyfile.new
# Convert the dates to the new logfile format (mcabber v. >= 0.6.1)
#
# See histolog.c for the mcabber format.
#
# MiKael, 2005/06/14

use strict;

my $line;
my $linesleft = 0;

while ($line = <>) {
  if ($linesleft) {
    print $line;
    $linesleft--;
    next;
  }
  my $type = substr($line, 0, 2);
  my $off_format = 0;
  my $date;

  if (substr($line, 11, 1) eq "T") {
    $off_format = 8; # Offset
  }

  if ($off_format) {
    # Already using the new format, nothing to do
    $date = substr($line, 3, 18);
  } else {
    # Date conversion to iso8601
    my ($ss,$mm,$hh,$DD,$MM,$YYYY) = gmtime(substr($line, 3, 10));
    $date = sprintf "%04d%02d%02dT%02d:%02d:%02dZ",
                    $YYYY+1900, $MM+1,$DD,$hh,$mm,$ss;
  }
  $linesleft = substr($line, 14 + $off_format, 3);
  $line      = substr($line, 14 + $off_format);

  print $type." ".$date." ".$line;

  # Is there something better to cast to integer?
  $linesleft = 0 + $linesleft;
}

# vim:set sw=2 sts=2 et si cinoptions="":
