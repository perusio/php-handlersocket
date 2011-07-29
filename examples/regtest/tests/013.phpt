--TEST--
auto_increment
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
    'k int PRIMARY KEY AUTO_INCREMENT, ' .
    'v1 varchar(30), ' .
    'v2 varchar(30)) ' .
    'Engine = myisam default charset = binary',
    mysql_real_escape_string($table));
if (!mysql_query($sql, $mysql))
{
    die(mysql_error());
}

$valmap = array();

for ($i = 0; $i < $tablesize; $i++)
{
    $k = 0;
    $v1 = 'v1sql_' . $i;
    $v2 = 'v2sql_' . $i;

    $sql = sprintf(
        'INSERT INTO ' . $table . ' VALUES (\'%s\', \'%s\', \'%s\')',
        mysql_real_escape_string($k),
        mysql_real_escape_string($v1),
        mysql_real_escape_string($v2));
    if (!mysql_query($sql, $mysql))
    {
        break;
    }
}


$hs = new HandlerSocket(MYSQL_HOST, MYSQL_HANDLERSOCKET_PORT_WR);
if (!($hs->openIndex(1, MYSQL_DBNAME, $table, '', 'k,v1,v2')))
{
    die();
}

echo 'HSINSERT', PHP_EOL;
for ($i = 0; $i < $tablesize; $i++)
{
    $k = 0;
    $v1 = 'v1hs_' . $i;
    $v2 = 'v2hs_' . $i;

    $retval = $hs->executeInsert(1, array($k, $v1, $v2));
    if ($retval)
    {
        echo $retval, ' ', $v1, PHP_EOL;
    }
    else
    {
        echo $hs->getError(), PHP_EOL;
    }
}

// make sure that it works even when inserts are pipelined. these requests
// are possibly executed in a single transaction.
for ($i = 0; $i < $tablesize; $i++)
{
    $k = 0;
    $v1 = 'v1hs3_' . $i;
    $v2 = 'v2hs3_' . $i;

    $retval = $hs->executeMulti(
        array(
            array(1, '+', array($k, $v1, $v2)),
            array(1, '+', array($k, $v1, $v2)),
            array(1, '+', array($k, $v1, $v2)),
        )
    );
    if ($retval)
    {
        for ($j = 0; $j < 3; $j++)
        {
            if (isset($retval[$j], $retval[$j][0], $retval[$j][0][0]))
            {
                echo $retval[$j][0][0], ' ', $v1, PHP_EOL;
            }
        }
    }
    else
    {
        echo $hs->getError(), PHP_EOL;
    }
}

echo 'DUMP_TABLE', PHP_EOL;
$sql = sprintf('SELECT k, v1, v2 FROM ' . $table . ' ORDER BY k');
$result = mysql_query($sql, $mysql);
if ($result)
{
    while ($row = mysql_fetch_assoc($result))
    {
        echo $row['k'], ' ', $row['v1'], ' ', $row['v2'], PHP_EOL;
    }
    mysql_free_result($result);
}

mysql_close($mysql);

--EXPECT--
HSINSERT
11 v1hs_0
12 v1hs_1
13 v1hs_2
14 v1hs_3
15 v1hs_4
16 v1hs_5
17 v1hs_6
18 v1hs_7
19 v1hs_8
20 v1hs_9
21 v1hs3_0
22 v1hs3_0
23 v1hs3_0
24 v1hs3_1
25 v1hs3_1
26 v1hs3_1
27 v1hs3_2
28 v1hs3_2
29 v1hs3_2
30 v1hs3_3
31 v1hs3_3
32 v1hs3_3
33 v1hs3_4
34 v1hs3_4
35 v1hs3_4
36 v1hs3_5
37 v1hs3_5
38 v1hs3_5
39 v1hs3_6
40 v1hs3_6
41 v1hs3_6
42 v1hs3_7
43 v1hs3_7
44 v1hs3_7
45 v1hs3_8
46 v1hs3_8
47 v1hs3_8
48 v1hs3_9
49 v1hs3_9
50 v1hs3_9
DUMP_TABLE
1 v1sql_0 v2sql_0
2 v1sql_1 v2sql_1
3 v1sql_2 v2sql_2
4 v1sql_3 v2sql_3
5 v1sql_4 v2sql_4
6 v1sql_5 v2sql_5
7 v1sql_6 v2sql_6
8 v1sql_7 v2sql_7
9 v1sql_8 v2sql_8
10 v1sql_9 v2sql_9
11 v1hs_0 v2hs_0
12 v1hs_1 v2hs_1
13 v1hs_2 v2hs_2
14 v1hs_3 v2hs_3
15 v1hs_4 v2hs_4
16 v1hs_5 v2hs_5
17 v1hs_6 v2hs_6
18 v1hs_7 v2hs_7
19 v1hs_8 v2hs_8
20 v1hs_9 v2hs_9
21 v1hs3_0 v2hs3_0
22 v1hs3_0 v2hs3_0
23 v1hs3_0 v2hs3_0
24 v1hs3_1 v2hs3_1
25 v1hs3_1 v2hs3_1
26 v1hs3_1 v2hs3_1
27 v1hs3_2 v2hs3_2
28 v1hs3_2 v2hs3_2
29 v1hs3_2 v2hs3_2
30 v1hs3_3 v2hs3_3
31 v1hs3_3 v2hs3_3
32 v1hs3_3 v2hs3_3
33 v1hs3_4 v2hs3_4
34 v1hs3_4 v2hs3_4
35 v1hs3_4 v2hs3_4
36 v1hs3_5 v2hs3_5
37 v1hs3_5 v2hs3_5
38 v1hs3_5 v2hs3_5
39 v1hs3_6 v2hs3_6
40 v1hs3_6 v2hs3_6
41 v1hs3_6 v2hs3_6
42 v1hs3_7 v2hs3_7
43 v1hs3_7 v2hs3_7
44 v1hs3_7 v2hs3_7
45 v1hs3_8 v2hs3_8
46 v1hs3_8 v2hs3_8
47 v1hs3_8 v2hs3_8
48 v1hs3_9 v2hs3_9
49 v1hs3_9 v2hs3_9
50 v1hs3_9 v2hs3_9
