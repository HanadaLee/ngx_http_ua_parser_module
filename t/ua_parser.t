#!/usr/bin/perl

# Tests for ngx_http_ua_parser_module.

###############################################################################

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use Test::Nginx qw/ :DEFAULT /;

###############################################################################

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http rewrite
	ngx_http_ua_parser_module/)->plan(9);

open my $fh, '<', '../uap-cpp/uap-core/regexes.yaml'
	or die "failed to open regexes.yaml: $!";
local $/;
my $regexes = <$fh>;
close $fh;

$t->write_file('regexes.yaml', $regexes);
$t->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    ua_parser_regexes_file %%TESTDIR%%/regexes.yaml;

    server {
        listen       127.0.0.1:8080;
        server_name  enabled;

        ua_parser on;

        location = /all {
            return 200 "$ua_parser_device_family|$ua_parser_device_model|$ua_parser_device_brand|$ua_parser_os_family|$ua_parser_os_version_major|$ua_parser_os_version_minor|$ua_parser_os_version_patch|$ua_parser_os_version_patch_minor|$ua_parser_browser_family|$ua_parser_browser_version_major|$ua_parser_browser_version_minor|$ua_parser_browser_version_patch|$ua_parser_browser_version_patch_minor";
        }

        location = /custom {
            ua_parser_source $http_x_parse_user_agent;
            return 200 "$ua_parser_browser_family|$ua_parser_os_family|$ua_parser_device_family";
        }

        location = /off {
            ua_parser off;
            return 200 "$ua_parser_browser_family";
        }
    }

    server {
        listen       127.0.0.1:8081;
        server_name  disabled;

        location = / {
            return 200 "$ua_parser_browser_family";
        }
    }

    server {
        listen       127.0.0.1:8082;
        server_name  source-inheritance;

        ua_parser on;
        ua_parser_source $http_x_parse_user_agent;

        location = / {
            return 200 "$ua_parser_browser_family";
        }
    }
}

EOF

$t->run();

###############################################################################

my $iphone = 'Mozilla/5.0 (iPhone; CPU iPhone OS 5_1_1 like Mac OS X) '
	. 'AppleWebKit/534.46 (KHTML, like Gecko) Version/5.1 '
	. 'Mobile/9B206 Safari/7534.48.3';

my $all = response('/all', 8080, "User-Agent: $iphone\x0d\x0a");
like($all, qr/^HTTP\/1\.1 200 /, 'request with default source succeeds');
is(response_body($all),
	'iPhone|iPhone|Apple|iOS|5|1|1||Mobile Safari|5|1||',
	'all device, OS, and browser variables are exposed');

is(response_body(response('/custom', 8080,
	"User-Agent: unknown\x0d\x0aX-Parse-User-Agent: $iphone\x0d\x0a")),
	'Mobile Safari|iOS|iPhone', 'custom source is parsed');

is(response_body(response('/custom', 8080,
	"User-Agent: $iphone\x0d\x0aX-Parse-User-Agent:\x0d\x0a")),
	'Mobile Safari|iOS|iPhone', 'empty custom source falls back to User-Agent');

is(response_body(response('/all', 8080,
	"User-Agent: $iphone\x0d\x0a")),
	'iPhone|iPhone|Apple|iOS|5|1|1||Mobile Safari|5|1||',
	'ua_parser setting is inherited by the location');

is(response_body(response('/off', 8080,
	"User-Agent: $iphone\x0d\x0a")), '',
	'location can disable an inherited parser');

is(response_body(response('/all', 8080, '')), '||||||||||||',
	'missing User-Agent leaves every parser variable unavailable');

is(response_body(response('/', 8081,
	"User-Agent: $iphone\x0d\x0a")), '',
	'parser is disabled by default');

is(response_body(response('/', 8082,
	"X-Parse-User-Agent: $iphone\x0d\x0a")), 'Mobile Safari',
	'custom source is inherited by the location');

###############################################################################

sub response {
	my ($uri, $port, $headers) = @_;

	$headers = '' unless defined $headers;

	return http("GET $uri HTTP/1.1\x0d\x0a"
		. "Host: localhost\x0d\x0a"
		. $headers
		. "Connection: close\x0d\x0a\x0d\x0a",
		PeerAddr => '127.0.0.1:' . port($port));
}


sub response_body {
	my ($response) = @_;
	my ($body) = $response =~ /\x0d?\x0a\x0d?\x0a(.*)\z/s;

	return defined $body ? $body : '';
}

###############################################################################
