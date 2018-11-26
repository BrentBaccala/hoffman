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

# This hash tracks the tablebases.  It's really a set (a unique list);
# the values are largely unimportant; only the keys are used.

my %seen;

# %values is used both to sort the pieces on one side into a definite
# order and to compare the pieces on opposite sides to decide when to
# invert colors when naming a tablebase.
#
# B > N;  RP > BN;  RP > NN

my %values = (K => 100000,
	      Q => 10000,
	      R => 1000,
	      B => 100,
	      N => 10,
	      P => 1);

# "normalize" the name of a Syzygy tablebase by sorting its pieces
# into a standard order and ensuring that the superior side is always
# white.

sub normalize_Syzygy_tablebase {
    my ($tb) = @_;
    $tb =~ /([KQRBNP]+)v([KQRBNP]+)(.rtb[wz])/;

    my $whites = join('', sort { $values{$b} <=> $values{$a} } split(//, $1));
    my $blacks = join('', sort { $values{$b} <=> $values{$a} } split(//, $2));

    my $white_value = 0;
    for my $white_piece (split(//, $whites)) {
	$white_value += $values{$white_piece};
    }

    my $black_value = 0;
    for my $black_piece (split(//, $blacks)) {
	$black_value += $values{$black_piece};
    }

    my $normed;

    if (length($blacks) > length($whites)) {
	$normed = $blacks . 'v' . $whites . $3;
    } elsif (length($whites) > length($blacks)) {
	$normed = $whites . 'v' . $blacks . $3;
    } elsif ($white_value > $black_value) {
	$normed = $whites . 'v' . $blacks . $3;
    } else {
	$normed = $blacks . 'v' . $whites . $3;
    }

    return $normed;
}

# Add a tablebase to %seen hash and, if this is the first time we've
# "seen" it and it's a Syzygy tablebase, recurse on all possible
# decendants.

sub add_tablebase {
    my ($tb) = @_;

    if ((! $seen{$tb} ++) and ($tb =~ '.rtb[wz]')) {
	# consider all possible captures
	while ($tb =~ /([QRBNP])/g) {
	    &add_tablebase(&normalize_Syzygy_tablebase(substr($tb, 0, pos($tb)-1) . substr($tb,pos($tb))));
	}
	# consider a possible white pawn promotion
	if ($tb =~ /PvK/g) {
	    foreach my $promotion ('Q', 'R', 'B', 'N') {
		&add_tablebase(&normalize_Syzygy_tablebase(substr($tb, 0, pos($tb)-3) . $promotion . substr($tb,pos($tb)-2)));
	    }
	}
	# consider a possible black pawn promotion
	if ($tb =~ /P\.r/g) {
	    foreach my $promotion ('Q', 'R', 'B', 'N') {
		&add_tablebase(&normalize_Syzygy_tablebase(substr($tb, 0, pos($tb)-3) . $promotion . substr($tb,pos($tb)-2)));
	    }
	}
    }
}



if ($ARGV[0] eq '-t') {

    # We want to ensure that our normalization matches Roland De Man's normalization!
    #
    # Test Syzygy normalization code, like this:
    #
    # for dir in 3-4-5 6-WDL 6-DTZ 7-WDL 7-DTZ; do
    #   ./futurebases.pl -t $(GET http://tablebase.sesse.net/syzygy/$dir/ |
    #      xidel --input-format=html --xpath2 'string-join(.//a[matches(@href, "K.*rt")], " ")' -s -)
    # done

    shift;
    foreach my $tb (@ARGV) {
	if ($tb ne &normalize_Syzygy_tablebase($tb)) {
	    print "$tb ne " . &normalize_Syzygy_tablebase($tb) . "\n";
	}
    }
    print $#ARGV, " tested\n";

} else {

    # Extract any futurebases from the input control file and print them out.

    foreach my $control_file (@ARGV) {

	my $XMLdocument = $XMLparser->parse_file($control_file);
	my $XMLcontrolFile = $XMLdocument->getDocumentElement();

	map { &add_tablebase($_->findvalue('@filename')) } $XMLcontrolFile->findnodes("futurebase");

	delete $seen{"KvK.rtbw"};
	delete $seen{"KvK.rtbz"};

	print join ' ', keys %seen, "\n";
    }
}
