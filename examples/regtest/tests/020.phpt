--TEST--
bug that table mdl is not released when open_index is failed
--SKIPIF--
--FILE--
<?php
require_once dirname(__FILE__) . '/../common/config.php';

$mysql = get_mysql_connection();

init_mysql_testdb($mysql);

$table = 'hstesttbl';

$sql = sprintf('DROP TABLE IF EXISTS %s', mysql_real_escape_string($table));
if (!mysql_query($sql, $mysql))
{
    die(mysql_error());
}

$hs = new HandlerSocket(MYSQL_HOST, MYSQL_HANDLERSOCKET_PORT);
$retval = $hs->openIndex(1, MYSQL_DBNAME, $table, '', 'k,v');
echo 'open_index 1st r=', var_export($retval, true), PHP_EOL;
unset($hs);

$sql = sprintf(
    'CREATE TABLE %s ( ' .
    'k varchar(30) primary key, ' .
    'v varchar(30) not null) ' .
    'Engine = innodb',
    mysql_real_escape_string($table));
if (!mysql_query($sql, $mysql))
{
    die(mysql_error());
}

$hs = new HandlerSocket(MYSQL_HOST, MYSQL_HANDLERSOCKET_PORT);
$retval = $hs->openIndex(1, MYSQL_DBNAME, $table, '', 'k,v');
echo 'open_index 2nd r=', var_export($retval, true), PHP_EOL;
unset($hs);

--EXPECT--
open_index 1st r=false
open_index 2nd r=true
