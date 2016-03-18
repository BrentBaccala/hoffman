#!/usr/bin/perl
#
# This script writes control files for both standard and suicide
# tablebases with no pruning or move restrictions.
#
# Pass in the XML filenames you wish to generate.  All dependencies
# will be generated as well.  Files are written in the current
# directory.
#
# Filename format for standard chess is kWkB-option.xml, where W is a
# list of white pieces and B is a list of black pieces, both from the
# set qrnbp, and the optional option are:
#     'basic' or 'whitewins' to obtain bitbases instead of DTM
#     'naive', 'naive2', 'simple', or 'compact' to obtain alternate indices
#     'propNUM' to generate using proptables with the specified size in MB
#     '4x4' to restrict to a 4x4 board
#     '2x8' to restrict to a 2x8 board
#
# Filename format for suicide chess is WvB-option.xml where W and B
# now come from the set kqrnbp.
#
# To get the older behavior (write all 5-piece control files and their
# dependencies), call it as 'genctlfile.pl kppkp.xml'
#
# by Brent Baccala; no rights reserved

my %pieces = (k => 'king',
	      q => 'queen',
	      r => 'rook',
	      b => 'bishop',
	      n => 'knight',
	      p => 'pawn');

# %values is used in &mkfilename both to sort the pieces on one side
# into a definite order and to compare the pieces on opposite sides to
# decide when to invert a tablebase.

my %values = (k => 100000,
	      q => 10000,
	      r => 1000,
	      b => 100,
	      n => 10,
	      p => 1);

# @pieces is kept in a certain order that doesn't affect anything
# except the order in which futurebase elements are generated.

my @pieces = sort { $values{$b} <=> $values{$a} } keys(%pieces);

sub printnl {
    print XMLFILE @_, "\n";
}

my @normal_futurebases;
my @inverse_futurebases;

# The combinadic4 index does not encode side-to-move for color
# symmetric tablebases.
#
# For these tablebases, we don't need or use inverse futurebases
# (those with colors swapped).  This global variable affects the
# behavior of &mkfuturebase - does it print XML statements for inverse
# futurebases?

my $use_inverse_futurebases = true;

my $suicide;
my $option;
my @promotion_pieces;

# Make an unordered pair of piece listings into a properly ordered
# filename (that might be color inverted, i.e, kkq to kqk), but with
# no suffix (.xml or .htb).  In a list context, return the filename
# along with a flag indicating if we inverted.

sub mkfilename {
    my ($white_pieces, $black_pieces) = @_;
    my ($invert, $filename);

    $white_pieces = join('', sort { $values{$b} <=> $values{$a} } split(//, $white_pieces));
    $black_pieces = join('', sort { $values{$b} <=> $values{$a} } split(//, $black_pieces));

    my $white_value = 0;
    for my $white_piece (split(//, $white_pieces)) {
	$white_value += $values{$white_piece};
    }

    my $black_value = 0;
    for my $black_piece (split(//, $black_pieces)) {
	$black_value += $values{$black_piece};
    }

    if (($option !~ /-[24]x[48]/) && ((length($black_pieces) > length($white_pieces)) or
				      ((length($black_pieces) == length($white_pieces)) and ($black_value > $white_value)))) {
	$invert = 1;
	if ($suicide) {
	    $filename = $black_pieces . "v" . $white_pieces . $option;
	} else {
	    $filename = "k" . $black_pieces . "k" . $white_pieces . $option;
	}
    } else {
	$invert = 0;
	if ($suicide) {
	    $filename = $white_pieces . "v" . $black_pieces . $option;
	} else {
	    $filename = "k" . $white_pieces . "k" . $black_pieces . $option;
	}
    }

    return (wantarray ? ($invert, $filename) : $filename);
}

sub mkfuturebase {
    my ($white_pieces, $black_pieces) = @_;

    return if $suicide and ($white_pieces eq '' or $black_pieces eq '');

    my ($invert, $filename) = &mkfilename($white_pieces, $black_pieces);

    if ($invert) {
	if ($use_inverse_futurebases) {
	    if (grep($_ eq $filename, @inverse_futurebases) == 0) {
		printnl '   <futurebase filename="' . $filename . '.htb" colors="invert"/>';
		push @inverse_futurebases, $filename;
	    }
	}
    } else {
	if (grep($_ eq $filename, @normal_futurebases) == 0) {
	    printnl '   <futurebase filename="' . $filename . '.htb"/>';
	    push @normal_futurebases, $filename;
	}
    }
}

sub write_cntl_file {
    my ($cntl_filename) = @_;

    my $indices = "-naive|-naive2|-simple|-compact";
    my $opts = "-basic|-whitewins|-prop(\\d+)|-4x4|-2x8|$indices";

    die "Invalid control filename $cntl_filename\n"
	unless ($cntl_filename =~ m/^k([qrbnp]*)(k)([qrbnp.]*)($opts)*.xml$/
		or $cntl_filename =~ m/^([kqrbnp]*)(v)([kqrbnp.]*)($opts)*.xml$/);

    my ($white_pieces, $black_pieces) = ($1, $3);
    $suicide = ($2 eq 'v');
    $option = $4;

    my $filename = &mkfilename($white_pieces, $black_pieces);

    die "Improper control filename $cntl_filename\n" if $cntl_filename ne "$filename.xml";

    print "Writing $filename.xml\n";
    open (XMLFILE, ">$filename.xml");

    printnl '<?xml version="1.0"?>';
    printnl '<!DOCTYPE tablebase SYSTEM "http://www.freesoft.org/software/hoffman/tablebase.dtd">';
    printnl '<!-- Created by genctlfile.pl -->';
    printnl '';
    printnl '<tablebase>';

    printnl '   <variant name="suicide"/>' if $suicide;

    if ($option =~ /-[24]x[48]/) {
	printnl '    <prune-enable color="white" type="discard"/>';
	printnl '    <prune-enable color="black" type="discard"/>';
    }

    printnl '   <dtm/>' if ($option !~ /-basic/ and $option !~ /-whitewins/);
    printnl '   <basic/>' if ($option =~ /-basic/);
    printnl '   <flag type="white-wins"/>' if ($option =~ /-whitewins/);

    my $index = "";

    if ($option =~ /($indices)/) {
	$index = substr($1, 1);
	printnl "   <index type=\"$index\"/>";
    }

    my $location = "";
    my $pawn_location = "";

    if ($option =~ /-4x4/) {
	$location = " location='a1 a2 a3 a4 b1 b2 b3 b4 c1 c2 c3 c4 d1 d2 d3 d4'";
	$pawn_location = " location='a2 a3 a4 b2 b3 b4 c2 c3 c4 d2 d3 d4'";
    }

    if ($option =~ /-2x8/) {
	$location = " location='a1 a2 a3 a4 a5 a6 a7 a8 b1 b2 b3 b4 b5 b6 b7 b8'";
	$pawn_location = " location='a2 a3 a4 a5 a6 a7 b2 b3 b4 b5 b6 b7'";
    }

    if (not $suicide) {
	printnl '   <piece color="white" type="king"' . $location . '/>';
	printnl '   <piece color="black" type="king"' . $location . '/>';
    }

    for my $piece (split(//, $white_pieces)) {
	if ($piece eq 'p') {
	    printnl '   <piece color="white" type="' . $pieces{$piece} . '"' . $pawn_location . '/>';
	} else {
	    printnl '   <piece color="white" type="' . $pieces{$piece} . '"' . $location . '/>';
	}
    }
    for my $piece (split(//, $black_pieces)) {
	if ($piece eq 'p') {
	    printnl '   <piece color="black" type="' . $pieces{$piece} . '"' . $pawn_location . '/>';
	} else {
	    printnl '   <piece color="black" type="' . $pieces{$piece} . '"' . $location . '/>';
	}
    }

    # Don't use inverse futurebases with combinadic4 indices on color
    # symmetric tablebases (those with identical white and black
    # pieces).

    if ((($index eq "") or ($index eq "combinadic4")) and ($white_pieces eq $black_pieces)) {
	$use_inverse_futurebases = undef;
    } else {
	$use_inverse_futurebases = true;
    }

    # These are global variables that will updated by the various
    # calls to &mkfuturebase().

    @normal_futurebases = ();
    @inverse_futurebases = ();
    @promotion_pieces = grep { $_ ne 'p' and ($_ ne 'k' or $suicide) } @pieces;

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
	for my $white_promotion (@promotion_pieces) {
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
	for my $black_promotion (@promotion_pieces) {
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

    if ($option =~ /-prop(\d+)/) {
	printnl "   <enable-proptables MB=\"$1\"/>" ;
    }
    printnl '   <output filename="' . $filename . '.htb"/>';

    if ($option =~ /-4x4/) {
	printnl '    <prune color="white" type="discard" move="*[e-h]*"/>';
	printnl '    <prune color="white" type="discard" move="*[a-d][5-8]*"/>';
	printnl '    <prune color="black" type="discard" move="*[e-h]*"/>';
	printnl '    <prune color="black" type="discard" move="*[a-d][5-8]*"/>';
    }

    if ($option =~ /-2x8/) {
	printnl '    <prune color="white" type="discard" move="*[c-h]*"/>';
	printnl '    <prune color="black" type="discard" move="*[c-h]*"/>';
    }

    printnl '</tablebase>';
    close XMLFILE;

    return (@normal_futurebases, @inverse_futurebases);
}

# Generate all tablebases with n white pieces and m black ones
#
# These routines are no longer used.  If you want all 3/2 five piece
# tablebases, for example, request this script to generate "kppkp.xml"
# You'll also get all of the four, three, and two piece tablebases
# that "kppkp.xml" depends on, which is a little different from what
# &gen(3,2) does.

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

sub gen {
    my ($white_n, $black_m) = @_;

    for my $white_pieces (&all_combos_of_n_pieces($white_n - 1)) {
	for my $black_pieces (&all_combos_of_n_pieces($black_m - 1)) {
	    &write_cntl_file(&mkfilename($white_pieces, $black_pieces));
	}
    }
}


# Write all control files passed in as a list of filenames, recursing
# to write all dependencies as well.

sub write_cntl_files {
    my @original_cntl_files = @_;
    my @cntl_files = @_;

    while ($#cntl_files >= 0) {
	my $cntl_filename = pop @cntl_files;
	if (-r $cntl_filename) {
	    # Print this message only if file was explicitly requested on command line
	    print "$cntl_filename exists, skipping\n"
		if grep($_ eq $cntl_filename, @original_cntl_files);
	} else {
	    push @cntl_files, map { $_ . '.xml' } write_cntl_file($cntl_filename);
	}
    }
}

&write_cntl_files(@ARGV);
