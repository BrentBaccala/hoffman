#!/usr/bin/perl

my $pieces = "qrbnp";

my @pieces = ('q', 'r', 'b', 'n', 'p');
#my @pieces = ('q', 'r', 'b', 'n');
my @non_pawn_pieces = ('q', 'r', 'b', 'n');

my %pieces;
$pieces{q} = 'queen';
$pieces{r} = 'rook';
$pieces{b} = 'bishop';
$pieces{n} = 'knight';
$pieces{p} = 'pawn';

sub mkfilename {
    my ($white_piece1, $white_piece2, $black_piece) = @_;
    my $filename = "k" . $white_piece1 . $white_piece2 . "k" . $black_piece;
    return $filename;
}

sub printnl {
    print XMLFILE @_, "\n";
}

my @normal_futurebases;
my @inverse_futurebases;

sub mkfuturebase {
    my ($type, $white_piece1, $white_piece2, $black_piece) = @_;

    if ($white_piece2 ne '' and index($pieces, $white_piece1) > index($pieces, $white_piece2)) {
	my $tmp = $white_piece1;
	$white_piece1 = $white_piece2;
	$white_piece2 = $tmp;
    }

    if ($white_piece2 eq '' and index($pieces, $black_piece) < index($pieces, $white_piece1)) {
	my $filename = &mkfilename($black_piece, '', $white_piece1);
	if (grep($_ eq $filename, @inverse_futurebases) == 0) {
	    printnl '   <futurebase filename="' . $filename . '.htb" type="' . $type . '" colors="invert"/>';
	    push @inverse_futurebases, $filename;
	}
    } else {
	my $filename = &mkfilename($white_piece1, $white_piece2, $black_piece);
	if (grep($_ eq $filename, @normal_futurebases) == 0) {
	    printnl '   <futurebase filename="' . $filename . '.htb" type="' . $type . '"/>';
	    push @normal_futurebases, $filename;
	}
    }
}

sub print_cntl_file {
    my ($white_piece1, $white_piece2, $black_piece) = @_;

    @normal_futurebases = ();
    @inverse_futurebases = ();

    my $filename = "k" . $white_piece1 . $white_piece2 . "k" . $black_piece;
    print $filename, ".xml\n";
    open (XMLFILE, ">$filename.xml");

    printnl '<?xml version="1.0"?>';
    printnl '<!DOCTYPE tablebase SYSTEM "tablebase.dtd">';
    printnl '';
    printnl '<tablebase>';
    if (index($filename, 'p') == -1) {
	printnl '   <index type="compact" symmetry="8-way"/>';
	printnl '   <format><dtm bits="8"/></format>';
    } else {
	printnl '   <index type="compact" symmetry="2-way"/>';
	printnl '   <format><dtm bits="16"/></format>';
    }
    printnl '   <piece color="white" type="king"/>';
    printnl '   <piece color="black" type="king"/>';
    printnl '   <piece color="white" type="' . $pieces{$white_piece1} . '"/>';
    printnl '   <piece color="white" type="' . $pieces{$white_piece2} . '"/>';
    printnl '   <piece color="black" type="' . $pieces{$black_piece} . '"/>';

    &mkfuturebase("capture", $white_piece1, $white_piece2, '');
    &mkfuturebase("capture", $white_piece1, '', $black_piece);
    &mkfuturebase("capture", $white_piece2, '', $black_piece);

    if ($white_piece1 eq 'p') {
	for $white_promotion1 (@non_pawn_pieces) {
	    &mkfuturebase("promotion", $white_promotion1, $white_piece2, $black_piece);
	    if ($black_piece ne 'p') {
		&mkfuturebase("capture-promotion", $white_promotion1, $white_piece2, '');
	    }
	}
    }

    if ($white_piece2 eq 'p') {
	for $white_promotion2 (@non_pawn_pieces) {
	    &mkfuturebase("promotion", $white_piece1, $white_promotion2, $black_piece);
	    if ($black_piece ne 'p') {
		&mkfuturebase("capture-promotion", $white_piece1, $white_promotion2, '');
	    }
	}
    }

    if ($black_piece eq 'p') {
	for $black_promotion (@non_pawn_pieces) {
	    &mkfuturebase("promotion", $white_piece1, $white_piece2, $black_promotion);
	    if ($white_piece1 ne 'p') {
		&mkfuturebase("capture-promotion", $white_piece2, '', $black_promotion);
	    }
	    if ($white_piece2 ne 'p') {
		&mkfuturebase("capture-promotion", $white_piece1, '', $black_promotion);
	    }
	}
    }

    printnl '   <generation-controls>';
    printnl '      <output filename ="' . $filename . '.htb"/>';
    if (index($filename, 'p') == -1) {
	printnl '      <entries-format>';
	printnl '         <dtm bits="8" offset="0"/>';
	printnl '         <locking-bit offset="8"/>';
	printnl '         <movecnt bits="7" offset="9"/>';
	printnl '      </entries-format>';
    } else {
	printnl '      <entries-format>';
	printnl '         <dtm bits="24" offset="0"/>';
	printnl '         <locking-bit offset="24"/>';
	printnl '         <movecnt bits="7" offset="25"/>';
	printnl '      </entries-format>';
    }
    printnl '   </generation-controls>';

    printnl '</tablebase>';
    close XMLFILE;
}

for $white_piece1 (@pieces) {
    for $white_piece2 (@pieces) {
	next if index($pieces, $white_piece1) > index($pieces, $white_piece2);
	for $black_piece (@pieces) {
	    &print_cntl_file($white_piece1, $white_piece2, $black_piece);
	}
    }
}

