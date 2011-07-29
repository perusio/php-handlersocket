--TEST--
columns to be inserted are specified by open_index
--SKIPIF--
--FILE--
<?php
require_once dirname(__FILE__) . '/../common/config.php';

$mysql = get_mysql_connection();

init_mysql_testdb($mysql);

$table = 'hstesttbl';
$tablesize = 10;
$sql = sprintf(
    'CREATE TABLE %s ( ' .
    'k int primary key auto_increment, ' .
    'v1 varchar(30), ' .
    'v2 varchar(30)) ' .
    'Engine = myisam default charset = binary',
    mysql_real_escape_string($table));
if (!mysql_query($sql, $mysql))
{
    die(mysql_error());
}

$hs = new HandlerSocket(MYSQL_HOST, MYSQL_HANDLERSOCKET_PORT_WR);
if (!($hs->openIndex(1, MYSQL_DBNAME, $table, '', 'v1')))
{
    die();
}

// inserts with auto_increment
echo 'HSINSERT', PHP_EOL;
for ($i = 0; $i < $tablesize; $i++)
{
    $k = 0;
    $v1 = 'v1hs_' . $i;
    $v2 = 'v2hs_' . $i;

    $retval = $hs->executeInsert(1, array($v1));
    if ($retval === false)
    {
        echo $hs->getError(), PHP_EOL;
    }
    else
    {
        echo $retval, ' ', $v1, PHP_EOL;
    }
}

unset($hs);

dump_table($mysql, $table);

mysql_close($mysql);

function dump_table($mysql, $table)
{
    echo 'DUMP_TABLE', PHP_EOL;
    $sql = 'SELECT k,v1,v2 FROM ' . $table . ' ORDER BY k';
    $result = mysql_query($sql, $mysql);
    if ($result)
    {
        while ($row = mysql_fetch_assoc($result))
        {
            echo $row['k'], ' ', $row['v1'], ' ', $row['v2'], PHP_EOL;
        }
    }
    mysql_free_result($result);
}

--EXPECT--
HSINSERT
1 v1hs_0
2 v1hs_1
3 v1hs_2
4 v1hs_3
5 v1hs_4
6 v1hs_5
7 v1hs_6
8 v1hs_7
9 v1hs_8
10 v1hs_9
DUMP_TABLE
1 v1hs_0 
2 v1hs_1 
3 v1hs_2 
4 v1hs_3 
5 v1hs_4 
6 v1hs_5 
7 v1hs_6 
8 v1hs_7 
9 v1hs_8 
10 v1hs_9 
