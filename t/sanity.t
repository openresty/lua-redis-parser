# vi:ft=

use strict;
use warnings;

use Test::Base 'no_plan';
use IPC::Run3;
use Cwd;

use Test::LongString;

$ENV{LUA_CPATH} = ($ENV{LUA_CPATH} || "") . ';' . "/home/lz/luax/?.so;;";
#$ENV{LUA_PATH} = ($ENV{LUA_PATH} || "" ) . ';' . getcwd . "/runtime/?.lua" . ';;';

run {
    #print $json_xs->pretty->encode(\@new_rows);
    #my $res = #print $json_xs->pretty->encode($res);
    my $block = shift;
    my $name = $block->name;

    my $lua = $block->lua or
        die "No --- lua specified for test $name\n";

    open my $fh, ">test_case.lua";
    print $fh $lua;
    close $fh;

    my ($res, $err);

    my @cmd;

    if ($ENV{TEST_LUA_USE_VALGRIND}) {
        @cmd =  ('valgrind', '-q', '--leak-check=full', 'lua', 'test_case.lua');
    } else {
        @cmd =  ('lua', 'test_case.lua');
    }

    run3 \@cmd, undef, \$res, \$err;

    print "res:$res\nerr:$err\n";

    if (defined $block->err) {
        $err =~ /.*:.*:.*: (.*\s)?/;
        $err = $1;
        is $err, $block->err, "$name - err expected";
    } elsif ($?) {
        die "Failed to execute --- lua for test $name: $err\n";
    } else {
        #is $res, $block->out, "$name - output ok";
        is_string $res, $block->out, "$name - output ok";
    }
    unlink 'test_case.lua' or warn "could not delete \'test_case.lua\':$!";
}

__DATA__

=== TEST 1: no crlf in status reply
--- sql
--- lua
parser = require("redis.parser")
reply = '+OK'
res, typ = parser.parse_reply(reply)
print("typ == " .. typ .. ' == ' .. parser.BAD_REPLY)
print("res == " .. res)
--- out
typ == 0 == 0
res == bad status reply



=== TEST 2: good status reply
--- sql
--- lua
parser = require("redis.parser")
reply = '+OK\r\n'
res, typ = parser.parse_reply(reply)
print("typ == " .. typ .. ' == ' .. parser.STATUS_REPLY)
print("res == " .. res)
--- out
typ == 1 == 1
res == OK



=== TEST 3: good error reply
--- sql
--- lua
parser = require("redis.parser")
reply = '-Bad argument\rHey\r\nblah blah blah\r\n'
res, typ = parser.parse_reply(reply)
print("typ == " .. typ .. ' == ' .. parser.ERROR_REPLY)
print("res == " .. res)
--- out eval
"typ == 2 == 2
res == Bad argument\rHey\n"



=== TEST 3: good integer reply
--- sql
--- lua
parser = require("redis.parser")
reply = ':-32\r\n'
res, typ = parser.parse_reply(reply)
print("typ == " .. typ .. ' == ' .. parser.INTEGER_REPLY)
print("res == " .. res)
print("res type == " .. type(res))
--- out
typ == 3 == 3
res == -32
res type == number

