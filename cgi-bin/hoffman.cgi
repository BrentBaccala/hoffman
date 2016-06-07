#!/usr/bin/perl -- -*-perl-*-

use DBI;

close(STDERR);
open(STDERR, ">>/tmp/errout.$$");

my $analysis = "analysis4";

# read into an array all of the analysis files that are complete

opendir(DIR, "/home/ftp/Hoffman/$analysis");
readdir DIR;  # ditch .
readdir DIR;  # ditch ..
my @done = readdir DIR;
closedir DIR;

# We will need to generate URLs that refer to our own location, which we
# figure out here, then strip off the name of this script, leaving us
# with just our URI directory path (without a trailing slash)

$myURI = "http://" . $ENV{HTTP_HOST} . $ENV{REQUEST_URI};
$myURI =~ s|/[^/]*$||;

# Lea la entrada

$buffer = $ENV{'QUERY_STRING'};

# Parte el nombre/valor pars
@pairs = split(/&/, $buffer);

foreach $pair (@pairs)
{
    ($name, $value) = split(/=/, $pair);

    # Un-Webify plus signs and %-encoding
    $value =~ tr/+/ /;
    $value =~ s/%([a-fA-F0-9][a-fA-F0-9])/pack("C", hex($1))/eg;

    # Stop people from using subshells to execute commands
    # Not a big deal when using sendmail, but very important
    # when using UCB mail (aka mailx).
    # $value =~ s/~!/ ~!/g; 
    # Uncomment for debugging purposes
    # print "Setting $name to $value<P>";

    $FORM{$name} = $value;
}

# Escribe el content-type
print "Content-type: text/xml\n\n";

# 'num' in the 'required' table takes on the following values:
#
# >0   number of direct dependancies that are needed before this file can be built
# 0    this file is ready to build
# -1   build in progress
# -2   build successfuly done

my $dbh = DBI->connect("DBI:mysql:baccala", "baccala", "", {RaiseError => 1});

if ($FORM{status} eq "complete") {

    my $host;
    my $start_time;

    #$FORM{tb} =~ s/xml$/htb/;

    my $sth = $dbh->prepare("SELECT host, time FROM running WHERE analysis = ? AND file = ?");
    $sth->execute($analysis, $FORM{tb});
    if ( @row = $sth->fetchrow_array ) {
	$host = $row[0];
	$start_time = $row[1];
    }

    if (not defined $host) {
	if (exists $ENV{'REMOTE_HOST'}) {
	    $host = $ENV{'REMOTE_HOST'};
	} elsif (exists $ENV{'REMOTE_ADDR'}) {
	    $host = $ENV{'REMOTE_ADDR'};
	}
    }

    my $sth = $dbh->prepare("DELETE FROM running WHERE analysis = ? AND file = ?");
    $sth->execute($analysis, $FORM{tb});

    my $sth = $dbh->prepare("DELETE FROM errors WHERE analysis = ? AND file = ?");
    $sth->execute($analysis, $FORM{tb});

    my $sth = $dbh->prepare("UPDATE required SET num=-2 WHERE analysis = ? AND file = ?");
    $sth->execute($analysis, $FORM{tb});

    $dbh->do("LOCK TABLES required WRITE, depends READ");

    # first, get all filenames that depend on the one just finished
    # for each of them, count the number of their dependancies still incomplete
    # update with that number if the original number was greater than zero

    my $sth = $dbh->prepare("SELECT file FROM depends WHERE analysis = ? AND dependson = ?");
    my $sth2 = $dbh->prepare("SELECT count(*) FROM required, depends WHERE required.analysis = ? AND depends.analysis = required.analysis AND depends.file = ? AND depends.dependson = required.file AND required.num != -2");
    my $sth3 = $dbh->prepare("UPDATE required SET num=? WHERE analysis = ? AND file = ? AND num > 0");
    #my $sth3 = $dbh->prepare("UPDATE required SET num=? WHERE analysis = ? AND file = ?");

    $sth->execute($analysis, $FORM{tb});
    while ( @row = $sth->fetchrow_array ) {
	my $filename = $row[0];
	$sth2->execute($analysis, $filename);
	if ( @row2 = $sth2->fetchrow_array ) {
	    my $newnum = $row2[0];
	    $newnum = -2 if (($newnum == 0) and (grep /^$filename(.htb)?$/, @done));
	    #print STDERR "num='$newnum' analysis='$analysis' file='$filename'\n";
	    $sth3->execute($newnum, $analysis, $filename);
	    #$dbh->do("UPDATE required SET num='$newnum' WHERE analysis='$analysis' AND file='$filename'");
	}
    }

    $dbh->do("UNLOCK TABLES");

    # any undefined values (start_time or host) will insert as NULL

    my $sth3 = $dbh->prepare("INSERT INTO complete(analysis,file,host,start_time,end_time) VALUES (?,?,?,?,?)");
    $sth3->execute($analysis, $FORM{tb}, $host, $start_time, scalar localtime);
    
} elsif ($FORM{status} eq "error") {

    my $host;
    my $start_time;
    my $error;

    #$FORM{tb} =~ s/xml$/htb/;

    # Hopefully, there were error elements placed into the XML.  Grab the first one and record it.

    while (<>) {
	if (m:<error>:) {
	    $error = $_;
	    $error =~ s|^ *<error>||;
	    $error =~ s|</error> *$||;
	    last;
	}
    }

    my $sth = $dbh->prepare("SELECT host, time FROM running WHERE analysis = ? AND file = ?");
    $sth->execute($analysis, $FORM{tb});
    if ( @row = $sth->fetchrow_array ) {
	$host = $row[0];
	$start_time = $row[1];
    }

    if (not defined $host) {
	if (exists $ENV{'REMOTE_HOST'}) {
	    $host = $ENV{'REMOTE_HOST'};
	} elsif (exists $ENV{'REMOTE_ADDR'}) {
	    $host = $ENV{'REMOTE_ADDR'};
	}
    }

    my $sth = $dbh->prepare("DELETE FROM running WHERE analysis = ? AND file = ?");
    $sth->execute($analysis, $FORM{tb});

    # any undefined values (start_time, host, or error) will insert as NULL

    my $sth = $dbh->prepare("INSERT INTO errors(analysis,file,host,start_time,end_time,error) VALUES (?,?,?,?,?,?)");
    $sth->execute($analysis, $FORM{tb}, $host, $start_time, scalar localtime, $error);

} elsif (($FORM{status} eq "begin") or ($FORM{status} eq "begin-small")) {

    my $sth = $dbh->prepare("SELECT file FROM required WHERE analysis = ? AND num = 0");

    my $filename;

    do {

	$dbh->do("LOCK TABLES required WRITE");

	$sth->execute($analysis);

	if ($FORM{status} eq "begin") {
	    @row = $sth->fetchrow_array;
	    $filename = $row[0] if (@row);
	    while ( @row = $sth->fetchrow_array ) {
		$filename = $row[0];
		# comment this out to prefer white queen analysis on debian
		#last;
		if ($row[0] =~ m:^kq:i) {
		    $filename = $row[0];
		    last;
		}
	    }
	} else {
	    while ( @row = $sth->fetchrow_array ) {
		if ($row[0] =~ m:^k[^q]:i) {
		    $filename = $row[0];
		    last;
		}
	    }
	}

	if (not defined $filename) {
	    $dbh->do("UNLOCK TABLES");
	    sleep 60;
	    $dbh->do("LOCK TABLES required WRITE");
	}

    } until (defined $filename);

    #my $sth = $dbh->prepare("DELETE FROM required WHERE file = ?");
    my $sth = $dbh->prepare("UPDATE required SET num=-1 WHERE analysis = ? AND file = ?");
    $sth->execute($analysis, $filename);

    $dbh->do("UNLOCK TABLES");

    my $sth = $dbh->prepare("INSERT INTO running(analysis, file, host, time) VALUES (?, ?, ?, ?)");
    $sth->execute($analysis, $filename, $FORM{host}, scalar localtime);

    #$filename =~ s/htb/xml/;
    open(XML, "<$analysis/$filename.xml");
    while (<XML>) {
	print;
    }

}
