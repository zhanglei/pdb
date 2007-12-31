#!/usr/bin/perl

use strict;
use warnings;

use Test;
BEGIN { plan tests => 1 }

use lib qw(test);
use MySQLTest ();

foreach my $server (@MySQLTest::servers) {
    print "$server\n";
    if (-d $server->{'dir'}) {
        ok(0);
        exit;
    }
    if (! mkdir $server->{'dir'}) {
        ok(0);
        exit;
    }
    system($MySQLTest::basedir . '/scripts/mysql_install_db' .
           ' --basedir=' . $MySQLTest::basedir .
           ' --datadir=' . $server->{'dir'} .
           ' 2>/dev/null');
    my $pid = fork();
    if (not defined $pid) {
        ok(0);
        exit;
    }
    if ($pid == 0) {
        close(STDOUT);
        close(STDERR);
        close(STDIN);
        system($MySQLTest::basedir . '/bin/mysqld' .
               ' --datadir=' . $server->{'dir'} .
               ' --port=' . $server->{'port'} .
               ' --socket=' . $server->{'dir'} . $MySQLTest::socket_file .
               ' --pid-file=' . $server->{'dir'} . $MySQLTest::pid_file .
               ' 2>/dev/null &');
        exit(0);
    }
}

WAIT_FOR_MYSQLD: while (1) {
    sleep(1);
    foreach my $server (@MySQLTest::servers) {
        unless (-e $server->{'dir'} . $MySQLTest::pid_file) {
            next WAIT_FOR_MYSQLD;
        }
    }
    last WAIT_FOR_MYSQLD;
}

foreach my $server (@MySQLTest::servers) {
    system($MySQLTest::basedir . '/bin/mysql' .
           ' --user=root' .
           ' --protocol=tcp' .
           ' --port=' . $server->{'port'} .
           ' -B' .
           ' < ' . $server->{'init'});
}

ok(1);
