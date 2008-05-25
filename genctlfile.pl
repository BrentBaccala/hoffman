#!/usr/bin/perl
#
# This script writes a set of control files for all 5-piece tablebases
# into the current directory.
#
# by Brent Baccala; no rights reserved

my $pieces = "qrbnp";

my @pieces = ('q', 'r', 'b', 'n', 'p');
my @non_pawn_pieces = ('q', 'r', 'b', 'n');

my %pieces;
$pieces{q} = 'queen';
$pieces{r} = 'rook';
$pieces{b} = 'bishop';
$pieces{n} = 'knight';
$pieces{p} = 'pawn';

my %sortorder;
$sortorder{q} = 1;
$sortorder{r} = 2;
$sortorder{b} = 3;
$sortorder{n} = 4;
$sortorder{p} = 5;

# bishop is given a slightly higher value than knight here to ensure
# that kbkn is definitely preferred over knkb

my %values;
$values{q} = 9;
$values{r} = 5;
$values{b} = 3.1;
$values{n} = 3;
$values{p} = 1;

sub printnl {
    print XMLFILE @_, "\n";
}

my @normal_futurebases;
my @inverse_futurebases;

sub mkfuturebase {
    my ($type, $white_pieces, $black_pieces) = @_;

    $white_pieces = join('', sort { $sortorder{$a} <=> $sortorder{$b} } split(//, $white_pieces));
    $black_pieces = join('', sort { $sortorder{$a} <=> $sortorder{$b} } split(//, $black_pieces));

    my $white_value = 0;
    for my $white_piece (split(//, $white_pieces)) {
	$white_value += $values{$white_piece};
    }

    my $black_value = 0;
    for my $black_piece (split(//, $black_pieces)) {
	$black_value += $values{$black_piece};
    }

    if ((length($black_pieces) > length($white_pieces)) or
	((length($black_pieces) == length($white_pieces)) and ($black_value > $white_value))) {
	my $filename = "k" . $black_pieces . "k" . $white_pieces;
	if (grep($_ eq $filename, @inverse_futurebases) == 0) {
	    printnl '   <futurebase filename="' . $filename . '.htb" type="' . $type . '" colors="invert"/>';
	    push @inverse_futurebases, $filename;
	}
    } else {
	my $filename = "k" . $white_pieces . "k" . $black_pieces;
	if (grep($_ eq $filename, @normal_futurebases) == 0) {
	    printnl '   <futurebase filename="' . $filename . '.htb" type="' . $type . '"/>';
	    push @normal_futurebases, $filename;
	}
    }
}

sub print_cntl_file {
    my ($white_pieces, $black_pieces) = @_;

    @normal_futurebases = ();
    @inverse_futurebases = ();

    return if ($white_pieces ne join('', sort { $sortorder{$a} <=> $sortorder{$b} } split(//, $white_pieces)));
    return if ($black_pieces ne join('', sort { $sortorder{$a} <=> $sortorder{$b} } split(//, $black_pieces)));

    my $filename = "k" . $white_pieces . "k" . $black_pieces;
    print "Writing $filename.xml\n";
    open (XMLFILE, ">$filename.xml");

    printnl '<?xml version="1.0"?>';
    printnl '<!DOCTYPE tablebase SYSTEM "http://www.freesoft.org/software/hoffman/tablebase.dtd">';
    printnl '';
    printnl '<tablebase>';

    if (index($filename, 'p') == -1) {
	printnl '   <index type="compact" symmetry="8-way"/>';
	printnl '   <format><dtm bits="8"/></format>';
    } elsif ($filename ne "kppkp") {
	printnl '   <index type="compact" symmetry="2-way"/>';
	printnl '   <format><dtm bits="8"/></format>';
    } else {
	printnl '   <index type="compact" symmetry="2-way"/>';
	printnl '   <format><dtm bits="16"/></format>';
    }

    printnl '   <piece color="white" type="king"/>';
    printnl '   <piece color="black" type="king"/>';
    for my $piece (split(//, $white_pieces)) {
	printnl '   <piece color="white" type="' . $pieces{$piece} . '"/>';
    }
    for my $piece (split(//, $black_pieces)) {
	printnl '   <piece color="black" type="' . $pieces{$piece} . '"/>';
    }

    for my $captured_white_index (1 .. length($white_pieces)) {
	my $remaining_white_pieces = $white_pieces;
	substr($remaining_white_pieces, $captured_white_index-1, 1) = "";
	&mkfuturebase("capture", $remaining_white_pieces, $black_pieces);
    }

    for my $captured_black_index (1 .. length($black_pieces)) {
	my $remaining_black_pieces = $black_pieces;
	substr($remaining_black_pieces, $captured_black_index-1, 1) = "";
	&mkfuturebase("capture", $white_pieces, $remaining_black_pieces);
    }

    if (index($white_pieces, 'p') != -1) {
	my $remaining_white_pieces = $white_pieces;
	substr($remaining_white_pieces, index($white_pieces, 'p'), 1) = "";
	for my $white_promotion (@non_pawn_pieces) {
	    &mkfuturebase("promotion", $remaining_white_pieces . $white_promotion, $black_pieces);
	    for my $captured_black_index (1 .. length($black_pieces)) {
		if (substr($black_pieces, $captured_black_index-1, 1) ne "p") {
		    my $remaining_black_pieces = $black_pieces;
		    substr($remaining_black_pieces, $captured_black_index-1, 1) = "";
		    &mkfuturebase("capture-promotion", $remaining_white_pieces . $white_promotion, $remaining_black_pieces);
		}
	    }
	}
    }

    if (index($black_pieces, 'p') != -1) {
	my $remaining_black_pieces = $black_pieces;
	substr($remaining_black_pieces, index($black_pieces, 'p'), 1) = "";
	for my $black_promotion (@non_pawn_pieces) {
	    &mkfuturebase("promotion", $white_pieces, $remaining_black_pieces . $black_promotion);
	    for my $captured_white_index (1 .. length($white_pieces)) {
		if (substr($white_pieces, $captured_white_index-1, 1) ne "p") {
		    my $remaining_white_pieces = $white_pieces;
		    substr($remaining_white_pieces, $captured_white_index-1, 1) = "";
		    &mkfuturebase("capture-promotion", $remaining_white_pieces, $remaining_black_pieces . $black_promotion);
		}
	    }
	}
    }

    printnl '   <generation-controls>';
    printnl '      <output filename ="' . $filename . '.htb"/>';
    if ($filename eq "kppkp") {
	printnl '      <entries-format>';
	printnl '         <dtm bits="9" offset="0"/>';
	printnl '         <locking-bit offset="9"/>';
	printnl '         <movecnt bits="6" offset="10"/>';
	printnl '      </entries-format>';
    }
    printnl '   </generation-controls>';

    printnl '</tablebase>';
    close XMLFILE;
}

sub gen32 {
    my ($white_piece1, $white_piece2, $black_piece);

    for $white_piece1 (@pieces) {
	for $white_piece2 (@pieces) {
	    for $black_piece (@pieces) {
		&print_cntl_file($white_piece1 . $white_piece2, $black_piece);
	    }
	}
    }
}

&gen32;
