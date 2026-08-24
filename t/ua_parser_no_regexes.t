#!/usr/bin/perl

# Tests for ngx_http_ua_parser_module without a regexes file.

###############################################################################

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use Test::Nginx qw/ :DEFAULT /;

###############################################################################

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http ngx_http_ua_parser_module/)->plan(2);

$t->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        ua_parser on;

        location = / {
            return 200 "$ua_parser_browser_family";
        }
    }
}

EOF

$t->run();

###############################################################################

my $response = http_get('/', 'User-Agent' => 'Mozilla/5.0 Firefox/100.0');

like($response, qr/^HTTP\/1\.1 200 /,
	'enabled parser without a regexes file does not fail the request');
is(response_body($response), '',
	'variables are unavailable without a regexes file');

###############################################################################

sub response_body {
	my ($response) = @_;
	my ($body) = $response =~ /\x0d?\x0a\x0d?\x0a(.*)\z/s;

	return defined $body ? $body : '';
}

###############################################################################
