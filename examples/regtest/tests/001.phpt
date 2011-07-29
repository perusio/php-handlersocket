--TEST--
libmysql
--SKIPIF--
--FILE--
<?php
require_once dirname(__FILE__) . '/../common/config.php';

$mysql = get_mysql_connection();

init_mysql_testdb($mysql);

$table = 'hstesttbl';
$tablesize = 100;
$sql = sprintf(
    'CREATE TABLE %s (k varchar(30) primary key, v varchar(30) not null) ' .
    'Engine = innodb', mysql_real_escape_string($table));
if (!mysql_query($sql, $mysql))
{
    die(mysql_error());
}

$valmap = array();

for ($i = 0; $i < $tablesize; ++$i)
{
    $k = 'k' . $i;
    $v = 'v' . _rand($i) . $i;

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

$sql = 'SELECT k,v FROM ' . $table . ' ORDER BY k';
$result = mysql_query($sql, $mysql);
if ($result)
{
    while ($row = mysql_fetch_assoc($result))
    {
        echo $row['k'], ' ', $row['v'], PHP_EOL;
    }
    mysql_free_result($result);
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
k0 v1020
k1 v6351
k10 v70410
k11 v75111
k12 v36712
k13 v40013
k14 v39714
k15 v17015
k16 v71916
k17 v73417
k18 v58718
k19 v49419
k2 v8032
k20 v52320
k21 v95421
k22 v43322
k23 v82023
k24 v28324
k25 v83725
k26 v20526
k27 v41527
k28 v54528
k29 v58329
k3 v9253
k30 v5230
k31 v32331
k32 v61432
k33 v67933
k34 v80534
k35 v45135
k36 v11536
k37 v26937
k38 v21838
k39 v61739
k4 v7754
k40 v87840
k41 v34541
k42 v51242
k43 v96943
k44 v40844
k45 v29145
k46 v85846
k47 v95347
k48 v71048
k49 v14249
k5 v5375
k50 v68250
k51 v93451
k52 v62152
k53 v96553
k54 v57454
k55 v20455
k56 v29856
k57 v13457
k58 v98358
k59 v44459
k6 v5926
k60 v14460
k61 v15261
k62 v18762
k63 v21563
k64 v864
k65 v69765
k66 v65166
k67 v28067
k68 v70168
k69 v53769
k7 v4147
k70 v41370
k71 v6971
k72 v8672
k73 v82273
k74 v67074
k75 v37075
k76 v80676
k77 v68877
k78 v2678
k79 v6679
k8 v5908
k80 v80280
k81 v17181
k82 v55782
k83 v84783
k84 v77784
k85 v73085
k86 v98786
k87 v11587
k88 v64688
k89 v49689
k9 v3029
k90 v12090
k91 v68491
k92 v37492
k93 v6593
k94 v37094
k95 v17495
k96 v82896
k97 v86797
k98 v75998
k99 v70399
