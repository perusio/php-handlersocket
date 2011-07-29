--TEST--
IN, filters and modifications (update method)
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

$hs = new HandlerSocket(MYSQL_HOST, MYSQL_HANDLERSOCKET_PORT_WR);
if (!$hs->openIndex(1, MYSQL_DBNAME, $table, '', 'k,v,v2', 'v2'))
{
    die();
}

if (!$hs->openIndex(2, MYSQL_DBNAME, $table, '', 'v', 'v2'))
{
    die();
}


// update $table set v= 'MOD' where k in $vs and v2 = '1'
$retval = $hs->executeUpdate(
    2, '=', array(''), array('MOD'), 10000, 0,
    array(array('F', '=', 0, '1')),
    0, array('k10', 'k20x', 'k30', 'k40', 'k50'));
if (!$retval)
{
    echo $hs->getError(), PHP_EOL;
}

echo 'HS', PHP_EOL;

$retval = $hs->executeSingle(1, '>=', array(''), 10000, 0);
if (!$retval)
{
    echo $hs->getError(), PHP_EOL;
}
else
{
    foreach ($retval as $val)
    {
        echo $val[0], ' ', $val[1], ' ', $val[2], PHP_EOL;
    }
}

mysql_close($mysql);

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
k0 v102-0 0
k1 v635-1 0
k10 MOD 1
k11 v751-11 1
k12 v367-12 1
k13 v400-13 1
k14 v397-14 1
k15 v170-15 1
k16 v719-16 1
k17 v734-17 1
k18 v587-18 1
k19 v494-19 1
k2 v803-2 0
k20 v523-20 0
k21 v954-21 0
k22 v433-22 0
k23 v820-23 0
k24 v283-24 0
k25 v837-25 0
k26 v205-26 0
k27 v415-27 0
k28 v545-28 0
k29 v583-29 0
k3 v925-3 0
k30 MOD 1
k31 v323-31 1
k32 v614-32 1
k33 v679-33 1
k34 v805-34 1
k35 v451-35 1
k36 v115-36 1
k37 v269-37 1
k38 v218-38 1
k39 v617-39 1
k4 v775-4 0
k40 v878-40 0
k41 v345-41 0
k42 v512-42 0
k43 v969-43 0
k44 v408-44 0
k45 v291-45 0
k46 v858-46 0
k47 v953-47 0
k48 v710-48 0
k49 v142-49 0
k5 v537-5 0
k50 MOD 1
k51 v934-51 1
k52 v621-52 1
k53 v965-53 1
k54 v574-54 1
k55 v204-55 1
k56 v298-56 1
k57 v134-57 1
k58 v983-58 1
k59 v444-59 1
k6 v592-6 0
k60 v144-60 0
k61 v152-61 0
k62 v187-62 0
k63 v215-63 0
k64 v8-64 0
k65 v697-65 0
k66 v651-66 0
k67 v280-67 0
k68 v701-68 0
k69 v537-69 0
k7 v414-7 0
k70 v413-70 1
k71 v69-71 1
k72 v86-72 1
k73 v822-73 1
k74 v670-74 1
k75 v370-75 1
k76 v806-76 1
k77 v688-77 1
k78 v26-78 1
k79 v66-79 1
k8 v590-8 0
k80 v802-80 0
k81 v171-81 0
k82 v557-82 0
k83 v847-83 0
k84 v777-84 0
k85 v730-85 0
k86 v987-86 0
k87 v115-87 0
k88 v646-88 0
k89 v496-89 0
k9 v302-9 0
k90 v120-90 1
k91 v684-91 1
k92 v374-92 1
k93 v65-93 1
k94 v370-94 1
k95 v174-95 1
k96 v828-96 1
k97 v867-97 1
k98 v759-98 1
k99 v703-99 1
