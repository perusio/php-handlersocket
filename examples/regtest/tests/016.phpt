--TEST--
date/datetime types
--SKIPIF--
--FILE--
<?php
require_once dirname(__FILE__) . '/../common/config.php';

$datetime_types = array(
    array('DATE', '0000-00-00', '2011-01-01', '9999-12-31'),
    array('DATETIME', 0, '2011-01-01 18:30:25'),
    array('TIME', 0, '18:30:25'),
    array('YEAR(4)', 1901, 2011, 2155),
);

foreach ($datetime_types as $val)
{
    $type = array_shift($val);

    echo 'TYPE ', $type, PHP_EOL;

    test_one($type, $val);

    echo PHP_EOL;
}

function test_one($type, $values = array())
{
    $mysql = get_mysql_connection();

    init_mysql_testdb($mysql);

    $table = 'hstesttbl';
    $sql = sprintf(
        'CREATE TABLE %s ( ' .
        'k ' . $type . ' PRIMARY KEY, ' .
        'v1 varchar(512), ' .
        'v2 ' . $type . ', ' .
        'index i1(v1), index i2(v2, v1)) ' .
        'Engine = myisam default charset = binary',
        mysql_real_escape_string($table));
    if (!mysql_query($sql, $mysql))
    {
        die(mysql_error());
    }

    $hs = new HandlerSocket(MYSQL_HOST, MYSQL_HANDLERSOCKET_PORT_WR);
    if (!($hs->openIndex(1, MYSQL_DBNAME, $table, '', 'k,v1,v2')))
    {
        die();
    }
    if (!($hs->openIndex(2, MYSQL_DBNAME, $table, 'i1', 'k,v1,v2')))
    {
        die();
    }
    if (!($hs->openIndex(3, MYSQL_DBNAME, $table, 'i2', 'k,v1,v2')))
    {
        die();
    }

    foreach ($values as $val)
    {
        $kstr = 's' . $val;

        $retval = $hs->executeSingle(1, '+', array($val, $kstr, $val), 0, 0);
        if ($retval === false)
        {
            echo $hs->getError(), PHP_EOL;
        }
    }

    dump_table($mysql, $table);

    foreach ($values as $val)
    {
        $kstr = 's' . $val;

        $retval = $hs->executeSingle(1, '=', array($val), 1, 0);
        if ($retval)
        {
            $retval = array_shift($retval);
            echo 'PK[', $val, ']';
            for ($j = 0; $j < 3; $j++)
            {
                if (isset($retval[$j]))
                {
                    echo ' ', $retval[$j];
                }
            }
            echo PHP_EOL;
        }
        else
        {
            echo $hs->getError(), PHP_EOL;
        }

        $retval = $hs->executeSingle(2, '=', array($kstr), 1, 0);
        if ($retval)
        {
            $retval = array_shift($retval);
            echo 'I1[', $kstr, ']';
            for ($j = 0; $j < 3; $j++)
            {
                if (isset($retval[$j]))
                {
                    echo ' ', $retval[$j];
                }
            }
            echo PHP_EOL;
        }
        else
        {
            echo $hs->getError(), PHP_EOL;
        }

        $retval = $hs->executeSingle(3, '=', array($val, $kstr), 1, 0);
        if ($retval)
        {
            $retval = array_shift($retval);
            echo 'I2[', $val, ', ', $kstr, ']';
            for ($j = 0; $j < 3; $j++)
            {
                if (isset($retval[$j]))
                {
                    echo ' ', $retval[$j];
                }
            }
            echo PHP_EOL;
        }
        else
        {
            echo $hs->getError(), PHP_EOL;
        }

        $retval = $hs->executeSingle(3, '=', array($val), 1, 0);
        if ($retval)
        {
            $retval = array_shift($retval);
            echo 'I2p[', $val, ']';
            for ($j = 0; $j < 3; $j++)
            {
                if (isset($retval[$j]))
                {
                    echo ' ', $retval[$j];
                }
            }
            echo PHP_EOL;
        }
        else
        {
            echo $hs->getError(), PHP_EOL;
        }
    }

    mysql_close($mysql);
}

function dump_table($mysql, $table)
{
    echo 'DUMP_TABLE_BEGIN', PHP_EOL;
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
    echo 'DUMP_TABLE_END', PHP_EOL;
}

--EXPECT--
TYPE DATE
DUMP_TABLE_BEGIN
0000-00-00 s0000-00-00 0000-00-00
2011-01-01 s2011-01-01 2011-01-01
9999-12-31 s9999-12-31 9999-12-31
DUMP_TABLE_END
PK[0000-00-00] 0000-00-00 s0000-00-00 0000-00-00
I1[s0000-00-00] 0000-00-00 s0000-00-00 0000-00-00
I2[0000-00-00, s0000-00-00] 0000-00-00 s0000-00-00 0000-00-00
I2p[0000-00-00] 0000-00-00 s0000-00-00 0000-00-00
PK[2011-01-01] 2011-01-01 s2011-01-01 2011-01-01
I1[s2011-01-01] 2011-01-01 s2011-01-01 2011-01-01
I2[2011-01-01, s2011-01-01] 2011-01-01 s2011-01-01 2011-01-01
I2p[2011-01-01] 2011-01-01 s2011-01-01 2011-01-01
PK[9999-12-31] 9999-12-31 s9999-12-31 9999-12-31
I1[s9999-12-31] 9999-12-31 s9999-12-31 9999-12-31
I2[9999-12-31, s9999-12-31] 9999-12-31 s9999-12-31 9999-12-31
I2p[9999-12-31] 9999-12-31 s9999-12-31 9999-12-31

TYPE DATETIME
DUMP_TABLE_BEGIN
0000-00-00 00:00:00 s0 0000-00-00 00:00:00
2011-01-01 18:30:25 s2011-01-01 18:30:25 2011-01-01 18:30:25
DUMP_TABLE_END
PK[0] 0000-00-00 00:00:00 s0 0000-00-00 00:00:00
I1[s0] 0000-00-00 00:00:00 s0 0000-00-00 00:00:00
I2[0, s0] 0000-00-00 00:00:00 s0 0000-00-00 00:00:00
I2p[0] 0000-00-00 00:00:00 s0 0000-00-00 00:00:00
PK[2011-01-01 18:30:25] 2011-01-01 18:30:25 s2011-01-01 18:30:25 2011-01-01 18:30:25
I1[s2011-01-01 18:30:25] 2011-01-01 18:30:25 s2011-01-01 18:30:25 2011-01-01 18:30:25
I2[2011-01-01 18:30:25, s2011-01-01 18:30:25] 2011-01-01 18:30:25 s2011-01-01 18:30:25 2011-01-01 18:30:25
I2p[2011-01-01 18:30:25] 2011-01-01 18:30:25 s2011-01-01 18:30:25 2011-01-01 18:30:25

TYPE TIME
DUMP_TABLE_BEGIN
00:00:00 s0 00:00:00
18:30:25 s18:30:25 18:30:25
DUMP_TABLE_END
PK[0] 00:00:00 s0 00:00:00
I1[s0] 00:00:00 s0 00:00:00
I2[0, s0] 00:00:00 s0 00:00:00
I2p[0] 00:00:00 s0 00:00:00
PK[18:30:25] 18:30:25 s18:30:25 18:30:25
I1[s18:30:25] 18:30:25 s18:30:25 18:30:25
I2[18:30:25, s18:30:25] 18:30:25 s18:30:25 18:30:25
I2p[18:30:25] 18:30:25 s18:30:25 18:30:25

TYPE YEAR(4)
DUMP_TABLE_BEGIN
1901 s1901 1901
2011 s2011 2011
2155 s2155 2155
DUMP_TABLE_END
PK[1901] 1901 s1901 1901
I1[s1901] 1901 s1901 1901
I2[1901, s1901] 1901 s1901 1901
I2p[1901] 1901 s1901 1901
PK[2011] 2011 s2011 2011
I1[s2011] 2011 s2011 2011
I2[2011, s2011] 2011 s2011 2011
I2p[2011] 2011 s2011 2011
PK[2155] 2155 s2155 2155
I1[s2155] 2155 s2155 2155
I2[2155, s2155] 2155 s2155 2155
I2p[2155] 2155 s2155 2155
