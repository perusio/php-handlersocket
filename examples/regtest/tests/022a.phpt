--TEST--
IN and filters (multi method)
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
    'k varchar(30) primary key, ' .
    'v varchar(30) not null, ' .
    'v2 int not null) ' .
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
    $v = 'v' . _rand($i) . '-' . $i;
    $v2 = ($i / 10) % 2;

    $sql = sprintf(
        'INSERT INTO ' . $table . ' values (\'%s\', \'%s\', \'%s\')',
        mysql_real_escape_string($k),
        mysql_real_escape_string($v),
        mysql_real_escape_string($v2));
    if (!mysql_query($sql, $mysql))
    {
        break;
    }

    $valmap[$k] = $v;
}

$hs = new HandlerSocket(MYSQL_HOST, MYSQL_HANDLERSOCKET_PORT);
if (!$hs->openIndex(1, MYSQL_DBNAME, $table, '', 'k,v,v2', 'v2'))
{
    die();
}

$vs = array('k10', 'k20x', 'k30', 'k40', 'k50');

// select k, v, v2 from $table where k in $vs and v2 = 1
$retval = $hs->executeMulti(
    array(array(1, '=', array(''), 10000, 0, null, null,
                array(array('F', '=', 0, '1')), 0, $vs)));

echo 'HS', PHP_EOL;
if (!$retval)
{
    echo $hs->getError(), PHP_EOL;
}
else
{
    foreach ($retval as $values)
    {
        foreach ($values as $val)
        {
            echo $val[0], ' ', $val[1], ' ', $val[2], PHP_EOL;
        }
    }
}

echo 'SQL', PHP_EOL;
$sql = "SELECT k, v, v2 FROM $table"
     . " WHERE k IN ('k10', 'k20x', 'k30', 'k40', 'k50')"
     . " AND v2 = '1' ORDER BY k";
$result = mysql_query($sql, $mysql);
if ($result)
{
    while ($row = mysql_fetch_assoc($result))
    {
        echo $row['k'], ' ', $row['v'], ' ', $row['v2'], PHP_EOL;
    }
    mysql_free_result($result);
}

mysql_close($mysql);

echo 'END', PHP_EOL;

function _rand($i = 0)
{
    $rand = array(102, 635, 803, 925, 775, 537, 592, 414, 590, 302, 704, 751,
                  367, 400, 397, 170, 719, 734, 587, 494, 523, 954, 433, 820,
                  283, 837, 205, 415, 545, 583, 52, 323, 614, 679, 805, 451,
                  115, 269, 218, 617, 878, 345, 512, 969, 408, 291, 858, 953,
                  710, 142, 682, 934, 621, 965, 574, 204, 298, 134, 983, 444,
                  144, 152, 187, 215, 8, 697, 651, 280, 701, 537, 413, 69, 86,
                  822, 670, 370, 806, 688, 26, 66, 802, 171, 557, 847, 777,
                  730, 987, 115, 646, 496, 120, 684, 374, 65, 370, 174, 828,
                  867, 759, 703);
    return $rand[$i];
}
--EXPECT--
HS
k10 v704-10 1
k30 v52-30 1
k50 v682-50 1
SQL
k10 v704-10 1
k30 v52-30 1
k50 v682-50 1
END
