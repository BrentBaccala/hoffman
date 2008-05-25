#!/usr/bin/perl
#
# This script writes control files for 'standard' tablebases (i.e, no
# pruning or move restrictions) into the current directory.
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

# Make an unordered pair of piece listings into a properly ordered
# filename (that might be color inverted, i.e, kkq to kqk).  Return
# the filename along with a flag indicating if we inverted.

sub mkfilename {
    my ($white_pieces, $black_pieces) = @_;

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
	return (1, "k" . $black_pieces . "k" . $white_pieces);
    } else {
	return (0, "k" . $white_pieces . "k" . $black_pieces);
    }
}

sub mkfuturebase {
    my ($white_pieces, $black_pieces) = @_;

    my ($invert, $filename) = &mkfilename($white_pieces, $black_pieces);

    if ($invert) {
	if (grep($_ eq $filename, @inverse_futurebases) == 0) {
	    printnl '   <futurebase filename="' . $filename . '.htb" colors="invert"/>';
	    push @inverse_futurebases, $filename;
	}
    } else {
	if (grep($_ eq $filename, @normal_futurebases) == 0) {
	    printnl '   <futurebase filename="' . $filename . '.htb"/>';
	    push @normal_futurebases, $filename;
	}
    }
}

sub print_cntl_file {
    my ($white_pieces, $black_pieces) = @_;

    @normal_futurebases = ();
    @inverse_futurebases = ();

    my ($invert, $filename) = &mkfilename($white_pieces, $black_pieces);

    return if $invert or $filename ne "k" . $white_pieces . "k" . $black_pieces;

    print "Writing $filename.xml\n";
    open (XMLFILE, ">$filename.xml");

    printnl '<?xml version="1.0"?>';
    printnl '<!DOCTYPE tablebase SYSTEM "http://www.freesoft.org/software/hoffman/tablebase.dtd">';
    printnl '<!-- Created by genctlfile.pl -->';
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
	&mkfuturebase($remaining_white_pieces, $black_pieces);
    }

    for my $captured_black_index (1 .. length($black_pieces)) {
	my $remaining_black_pieces = $black_pieces;
	substr($remaining_black_pieces, $captured_black_index-1, 1) = "";
	&mkfuturebase($white_pieces, $remaining_black_pieces);
    }

    if (index($white_pieces, 'p') != -1) {
	my $remaining_white_pieces = $white_pieces;
	substr($remaining_white_pieces, index($white_pieces, 'p'), 1) = "";
	for my $white_promotion (@non_pawn_pieces) {
	    &mkfuturebase($remaining_white_pieces . $white_promotion, $black_pieces);
	    for my $captured_black_index (1 .. length($black_pieces)) {
		if (substr($black_pieces, $captured_black_index-1, 1) ne "p") {
		    my $remaining_black_pieces = $black_pieces;
		    substr($remaining_black_pieces, $captured_black_index-1, 1) = "";
		    &mkfuturebase($remaining_white_pieces . $white_promotion, $remaining_black_pieces);
		}
	    }
	}
    }

    if (index($black_pieces, 'p') != -1) {
	my $remaining_black_pieces = $black_pieces;
	substr($remaining_black_pieces, index($black_pieces, 'p'), 1) = "";
	for my $black_promotion (@non_pawn_pieces) {
	    &mkfuturebase($white_pieces, $remaining_black_pieces . $black_promotion);
	    for my $captured_white_index (1 .. length($white_pieces)) {
		if (substr($white_pieces, $captured_white_index-1, 1) ne "p") {
		    my $remaining_white_pieces = $white_pieces;
		    substr($remaining_white_pieces, $captured_white_index-1, 1) = "";
		    &mkfuturebase($remaining_white_pieces, $remaining_black_pieces . $black_promotion);
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

sub all_combos_of_n_pieces {
    my ($n) = @_;

    if ($n == 0) {
	return ("");
    } elsif ($n == 1) {
	return @pieces;
    } else {
	my @result;

	for my $recursion (&all_combos_of_n_pieces($n - 1)) {
	    for my $piece (@pieces) {
		unshift @result, $recursion . $piece;
	    }
	}

	return @result;
    }
}

# Generate all tablebases with n white pieces and m black ones

sub gen {
    my ($white_n, $black_m) = @_;

    for my $white_pieces (&all_combos_of_n_pieces($white_n - 1)) {
	for my $black_pieces (&all_combos_of_n_pieces($black_m - 1)) {
	    &print_cntl_file($white_pieces, $black_pieces);
	}
    }
}

# kk
&gen(1,1);

# 3-piece TBs
&gen(2,1);

# 4-piece TBs
&gen(3,1);
&gen(2,2);

# 5-piece TBs
# &gen(3,2);
# &gen(4,1);
