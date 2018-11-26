#! /usr/bin/perl
# -*- fill-column: 100; -*-
#
# FUTUREBASES - lists all futurebases required to build a tablebase
#
# This is mostly just parsing the XML and pulling out the futurebase
# elements, but some extra work is needed for Syzygy tablebases, which
# require not only the named tablebase, but all tablebases which can
# be reached from it.

use strict;

use XML::LibXML;

my $XMLparser = XML::LibXML->new();
#$XMLparser->validation(1);
$XMLparser->load_ext_dtd(0);

# B > N
# RP > BN
# RP > NN

my %values = (K => 100000,
	      Q => 10000,
	      R => 1000,
	      B => 100,
	      N => 10,
	      P => 1);


my @sort_order = ('K', 'Q', 'R', 'B', 'N', 'P');

# from https://stackoverflow.com/a/3222157

use List::Util qw(first);
sub sortfunc {
    return first { $sort_order[$_] eq $_ } 0..$#sort_order;
}

sub normalize_Syzygy_tablebase {
    my ($tb) = @_;
    $tb =~ /([KQRBNP]+)v([KQRBNP]+)(.rtb[wz])/;

    #print $tb, "\n";

    my $whites = join('', sort { $values{$b} <=> $values{$a} } split(//, $1));
    my $blacks = join('', sort { $values{$b} <=> $values{$a} } split(//, $2));

    #my $whites = join '', sort sortfunc split('', $1);
    #my $blacks = join '', sort sortfunc split('', $2);

    my $white_value = 0;
    for my $white_piece (split(//, $whites)) {
	$white_value += $values{$white_piece};
    }

    my $black_value = 0;
    for my $black_piece (split(//, $blacks)) {
	$black_value += $values{$black_piece};
    }

    #print $whites . 'v' . $blacks . $3, "\n";
    if (length($blacks) > length($whites)) {
	return $blacks . 'v' . $whites . $3;
    } elsif (length($whites) > length($blacks)) {
	return $whites . 'v' . $blacks . $3;
    } elsif ($white_value > $black_value) {
	return $whites . 'v' . $blacks . $3;
    } else {
	return $blacks . 'v' . $whites . $3;
    }
}

sub expand_Syzygy_tablebase {
    if ($_ =~ '.rtbw') {
	my @result = ($_);
	while ($_ =~ /([QRBNP])/g) {
	    push @result, &normalize_Syzygy_tablebase(substr($_, 0, pos()-1) . substr($_,pos()));
	}
	if ($_ =~ /PvK/g) {
	    foreach my $promotion ('Q', 'R', 'B', 'N') {
		push @result, &normalize_Syzygy_tablebase(substr($_, 0, pos()-3) . $promotion . substr($_,pos()-2));
	    }
	}
	if ($_ =~ /P\.r/g) {
	    foreach my $promotion ('Q', 'R', 'B', 'N') {
		push @result, &normalize_Syzygy_tablebase(substr($_, 0, pos()-3) . $promotion . substr($_,pos()-2));
	    }
	}
	return @result;
    } else {
	return $_;
    }
}

sub extract_futurebase {
    return $_->findvalue('@filename');
}

# From perlfaq4, via https://stackoverflow.com/a/7657

sub uniq {
    my %seen;
    grep !$seen{$_}++, @_;
}

# Extract any futurebases from the input control file and assign them to any positions that they
# match.

foreach my $control_file (@ARGV) {

    my $XMLdocument = $XMLparser->parse_file($control_file);
    my $XMLcontrolFile = $XMLdocument->getDocumentElement();

    print join ' ', uniq(map(expand_Syzygy_tablebase, map(extract_futurebase, $XMLcontrolFile->findnodes("futurebase")))), "\n";
}
