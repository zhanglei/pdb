#!/usr/bin/perl

use strict;
use warnings;

use Test::More qw(no_plan);
use Socket;
use DBI;
use DBD::mysql;

use lib qw(test);
use MySQLTest;
use PDBTest;

my $port = 2139;

PDBTest::startup_with_inline_configuration(<<"ENDCFG");
log_file = test/pdb.log
log_level = DEBUG

listen_port = $port

$MySQLTest::database_configuration
ENDCFG

eval {
    my $dbh_pdb = DBI->connect("DBI:mysql:database=irrelevant;host=127.0.0.1;port=$port", 'root', '', { RaiseError => 1 });

    my $row = $dbh_pdb->selectall_arrayref('SELECT DATABASE(),USER()')->[0];
    ok($row);
    ok($row->[0] eq 'master');
    ok($row->[1] =~ /^root/);

    $row = $dbh_pdb->selectall_arrayref("LISTFIELDS widget_map");
    ok($row);
    $row = $dbh_pdb->selectall_arrayref("LISTFIELDS whatsit");
    ok($row);
    $row = $dbh_pdb->selectall_arrayref("LISTFIELDS widget");
    ok($row);

    $dbh_pdb->disconnect();
};
ok($@ eq '');

PDBTest::shutdown();
