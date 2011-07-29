--TEST--
increment/decrement
--SKIPIF--
--FILE--
<?php
require_once dirname(__FILE__) . '/../common/config.php';

$mysql = get_mysql_connection();

init_mysql_testdb($mysql);

$table = 'hstesttbl';
$tablesize = 100;
$sql = sprintf(
    'CREATE TABLE %s ( ' .
    'k varchar(30) PRIMARY KEY, ' .
    'v varchar(30) NOT NULL) ' .
    'Engine = innodb',
    mysql_real_escape_string($table));
if (!mysql_query($sql, $mysql))
{
    die(mysql_error());
}

$valmap = array();

for ($i = 0; $i < $tablesize; $i++)
{
    $k = 'k' . $i;
    $v = $i;

    $sql = sprintf(
        'INSERT INTO ' . $table . ' values (\'%s\', \'%s\')',
        mysql_real_escape_string($k),
        mysql_real_escape_string($v));
    if (!mysql_query($sql, $mysql))
    {
        break;
    }

    $valmap[$k] = $v;
}

$hs = new HandlerSocket(MYSQL_HOST, MYSQL_HANDLERSOCKET_PORT_WR);
if (!($hs->openIndex(1, MYSQL_DBNAME, $table, '', 'k,v')))
{
    die();
}
if (!($hs->openIndex(2, MYSQL_DBNAME, $table, '', 'v')))
{
    die();
}

echo 'VAL', PHP_EOL;
$retval = $hs->executeMulti(
    array(
        array(1, '=', array('k5'), 1, 0),
        array(1, '=', array('k6'), 1, 0),
        array(1, '=', array('k7'), 1, 0),
        array(1, '=', array('k8'), 1, 0),
    )
);
_dump($retval);

echo 'INCREMENT', PHP_EOL;
$retval = $hs->executeMulti(
    array(
        array(2, '=', array('k5'), 1, 0, '+', array(3)),
        array(2, '=', array('k6'), 1, 0, '+', array(12)),
        array(2, '=', array('k7'), 1, 0, '+', array(-11)),
        array(2, '=', array('k8'), 1, 0, '+', array(-15)),
    )
);
_dump($retval);

echo 'VAL', PHP_EOL;
$retval = $hs->executeMulti(
    array(
        array(1, '=', array('k5'), 1, 0),
        array(1, '=', array('k6'), 1, 0),
        array(1, '=', array('k7'), 1, 0),
        array(1, '=', array('k8'), 1, 0),
    )
);
_dump($retval);

echo 'DECREMENT', PHP_EOL;
$retval = $hs->executeMulti(
    array(
        array(2, '=', array('k5'), 1, 0, '-', array(2)),
        array(2, '=', array('k6'), 1, 0, '-', array(24)),
        array(2, '=', array('k7'), 1, 0, '-', array(80)),
        array(2, '=', array('k8'), 1, 0, '-', array(-80)),
    )
);
_dump($retval);

echo 'VAL', PHP_EOL;
$retval = $hs->executeMulti(
    array(
        array(1, '=', array('k5'), 1, 0),
        array(1, '=', array('k6'), 1, 0),
        array(1, '=', array('k7'), 1, 0),
        array(1, '=', array('k8'), 1, 0),
    )
);
_dump($retval);

echo 'INCREMENT', PHP_EOL;
$retval = $hs->executeMulti(
    array(
        array(2, '=', array('k5'), 1, 0, '+?', array(1)),
        array(2, '=', array('k5'), 1, 0, '+?', array(1)),
        array(2, '=', array('k5'), 1, 0, '+?', array(1)),
        array(2, '=', array('k5'), 1, 0, '+?', array(1)),
        array(2, '=', array('k5'), 1, 0, '+?', array(1)),
        array(2, '=', array('k5'), 1, 0, '+?', array(1)),
        array(2, '=', array('k5'), 1, 0, '+?', array(1)),
        array(2, '=', array('k5'), 1, 0, '+?', array(1)),
        array(2, '=', array('k5'), 1, 0, '+?', array(1)),
    )
);
_dump($retval);

echo 'VAL', PHP_EOL;
$retval = $hs->executeMulti(
    array(
        array(1, '=', array('k5'), 1, 0),
    )
);
_dump($retval);

mysql_close($mysql);

function _dump($data = array())
{
    foreach ($data as $value)
    {
        foreach ($value as $key => $val)
        {
            echo '[', $key, ']';
            if (is_array($val))
            {
                foreach ($val as $v)
                {
                    echo '[', $v, ']';
                }
            }
            else
            {
                echo '[', $val, ']';
            }
        }
        echo PHP_EOL;
    }
}

--EXPECT--
VAL
[0][k5][5]
[0][k6][6]
[0][k7][7]
[0][k8][8]
INCREMENT
[0][1]
[0][1]
[0][1]
[0][1]
VAL
[0][k5][8]
[0][k6][18]
[0][k7][-4]
[0][k8][-7]
DECREMENT
[0][1]
[0][0]
[0][1]
[0][0]
VAL
[0][k5][6]
[0][k6][18]
[0][k7][-84]
[0][k8][-7]
INCREMENT
[0][6]
[0][7]
[0][8]
[0][9]
[0][10]
[0][11]
[0][12]
[0][13]
[0][14]
VAL
[0][k5][15]
