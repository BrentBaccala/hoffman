#!/usr/bin/perl
#
# This script writes control files for 'standard' tablebases (i.e, no
# pruning or move restrictions) into the current directory.
#
# by Brent Baccala; no rights reserved
#
# modified by Laurent Bartholdi, 20090718

my $pieces = "kqrbnp";

my @pieces = ('k', 'q', 'r', 'b', 'n', 'p');
my @non_pawn_pieces = ('k', 'q', 'r', 'b', 'n');

my %pieces;
$pieces{k} = 'king';
$pieces{q} = 'queen';
$pieces{r} = 'rook';
$pieces{b} = 'bishop';
$pieces{n} = 'knight';
$pieces{p} = 'pawn';

my %sortorder;
$sortorder{k} = 1;
$sortorder{q} = 2;
$sortorder{r} = 3;
$sortorder{b} = 4;
$sortorder{n} = 5;
$sortorder{p} = 6;

my %values;
$values{k} = 100000;
$values{q} = 10000;
$values{r} = 1000;
$values{b} = 100;
$values{n} = 10;
$values{p} = 1;

my $targets = "target:";
my $dependencies = "";

sub printnl {
    print XMLFILE @_, "\n";
}

my @normal_futurebases;
my @inverse_futurebases;

# Make an unordered pair of piece listings into a properly ordered
# filename (that might be color inverted, i.e, kkq to kqk).  Return
# the filename along with a flag indicating if we inverted.

sub htbname {
    my ($name) = @_;
    return "htb/" . $name . ".htb";
}

sub xmlname {
    my ($name) = @_;
    return "xml/" . $name . ".xml";
}

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
	return (1, $black_pieces . "v" . $white_pieces);
    } else {
	return (0, $white_pieces . "v" . $black_pieces);
    }
}

sub mkfuturebase {
    my ($white_pieces, $black_pieces) = @_;

    return if $white_pieces eq "" and $black_pieces eq "";

    my ($invert, $filename) = &mkfilename($white_pieces, $black_pieces);

    $dependencies .= " " . &htbname($filename);

    if ($invert) {
	if (grep($_ eq $filename, @inverse_futurebases) == 0) {
	    printnl '   <futurebase filename="' . &htbname($filename) . '" colors="invert"/>';
	    push @inverse_futurebases, $filename;
	}
    } else {
	if (grep($_ eq $filename, @normal_futurebases) == 0) {
	    printnl '   <futurebase filename="' . &htbname($filename) . '"/>';
	    push @normal_futurebases, $filename;
	}
    }
}

sub print_cntl_file {
    my ($white_pieces, $black_pieces) = @_;

    @normal_futurebases = ();
    @inverse_futurebases = ();

    my ($invert, $filename) = &mkfilename($white_pieces, $black_pieces);

#    print $filename, "=",$white_pieces,"=",$black_pieces,"\n";
    return if $filename ne $white_pieces . "v" . $black_pieces and
	$filename ne $black_pieces . "v" . $white_pieces;

    $filename = $white_pieces . "v" . $black_pieces;

    $targets .= " " . &htbname($filename);
    $dependencies .= &htbname($filename) . ": " . &xmlname($filename) . " ";

    print "Writing " . &xmlname($filename) . " \r";
    open (XMLFILE, ">" . &xmlname($filename));

    printnl '<?xml version="1.0"?>';
    printnl '<!DOCTYPE tablebase SYSTEM "http://www.freesoft.org/software/hoffman/tablebase.dtd">';
    printnl '<!-- Created by genctlfile.pl -->';
    printnl '';
    printnl '<tablebase>';

    if ($white_pieces eq "" or $black_pieces eq "") {
	if (index($filename, 'p') == -1) {
	    printnl '   <index type="simple" symmetry="8-way"/>';
	    printnl '   <format><dtm bits="8"/></format>';
	} else {
	    printnl '   <index type="simple" symmetry="2-way"/>';
	    printnl '   <format><dtm bits="8"/></format>';
	}
    } else {
	printnl '   <index type="naive" symmetry="1"/>';
	printnl '   <format><dtm bits="8"/></format>';
    }

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
#	    print "WHITE PROMOTION ",$remaining_white_pieces . $white_promotion, "\n";
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
    printnl '      <output filename ="' . &htbname($filename) . '"/>';
    printnl '      <entries-format>';
    printnl '         <dtm bits="7" offset="0"/>';
    printnl '         <locking-bit offset="7"/>';
    printnl '         <movecnt bits="8" offset="8"/>';
    printnl '      </entries-format>';
    printnl '   </generation-controls>';

    printnl '</tablebase>';
    close XMLFILE;

    $dependencies .= "\n\t./hoffman -g " . &xmlname($filename) . "\n\n";
}

sub all_combos_of_n_pieces {
    my ($n) = @_;

    if ($n == -1) {
	return ("");
    } elsif ($n == 0) {
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

# degenerate
&gen(1,0);
&gen(2,0);
&gen(3,0);
&gen(0,1);
&gen(0,2);
&gen(0,3);

# 2-piece TBs
&gen(1,1);

# 3-piece TBs
#&gen(2,1);
#&gen(1,2);

# 4-piece TBs
#&gen(3,1);
#&gen(2,2);
#&gen(1,3);

# 5-piece TBs
#&gen(4,1);
#&gen(3,2);
#&gen(2,3);
#&gen(1,4);

print "Generating Makehtbs \n";

open (MAKEFILE, "> Makehtbs");
print MAKEFILE "# Make htbs from xml control files\n# automatically generated by genctlfile.pl\n\n", $targets, "\n\n", $dependencies;
close MAKEFILE
