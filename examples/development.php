<?php
$hs = new HandlerSocket('localhost', 9999);
//HandlerSocketException

//var_dump($hs->openIndex(1, 'ns_session', 'session', '', ''));
//var_dump($hs->openIndex(2, 'ns_session', 'session', 'index_modified', ''));
//var_dump($hs->openIndex(3, 'ns_session', 'session', 'index_user_id', ''));

//var_dump(get_class_methods($hs));

$index1 = $hs->openIndex(1, 'hstestdb', 'hstesttbl', 'PRIMARY', 'v1,v2');


//OK
$index2 = new HandlerSocketIndex(
    $hs,           //HandlerSocket オブジェクト
    2,             //index 番号
    'hstestdb',    //データベース名
    'hstesttbl',   //テーブル名
    'PRIMARY',     //インデックス名
    'k,v2',        //フィールドリスト (=>ここは配列の方がよい ?)
                   //テキストと配列両方がよいかな
    array('filter' => array('v1', 'k')) //オプション
    //array('filter' => array('k'))
    //array('filter' => 'k')
);

/*
array('filter' => array(...))
*/


//NG
//$index3 = new HandlerSocketIndex($hs, 3, 'hstestdb', 'hsngtbl', '', '');

//var_dump($hs);

//var_dump($index1);
//var_dump($index2);
//var_dump($index3);

//$obj = $index1;
$obj = $index2;

var_dump(get_class_methods($obj));
var_dump($obj->getId());
var_dump($obj->getDatabase());
var_dump($obj->getTable());
var_dump($obj->getColumn());
var_dump($obj->getFilter());


/*
find(); //NG

find('k1')        ==> find(array('=' => array('k1'))
find(array('k1')) ==> find(array('=' => array('k1')))

find(array('=' => 'k1')) ==> find(array('=' => array('k1')))

find(array('=' => array('k1', ...))) //基本

find(array('=' => array('k1')), 1, 0) //LIMIT OFFSET
*/

//$find = $obj->find('k3');
//$find = $obj->find(array('k3'));
//$find = $obj->find(array('>=' => array('k3')), 5);

//var_dump($find);


/*
$obj = $index2;
$insert = $obj->insert('a', 'てすと', 'TEST');
//$insert = $obj->insert();
var_dump($insert);

$insert = $obj->insert(array('b', "TEST\ntest", "ほげ\tてすと"));
//var_dump($insert);
*/

/*
$obj = $index1;
$find = $obj->find(
    array('>=' => array(1)), 3, 0,
    array('IN' => arra(0 => array(2, 4, 6)))
);
var_dump($find);

//@ 1 3 2 4 6
*/
/*
$obj = $index2;
$find = $obj->find(
    array('>=' => array(1)), 3, 0,
    array(
        'in'   => array(0, array(2, 4, 6, 8)),

        'filter' => array(array('<=', 'k', '3')),
                       // array('>', 'v1', '1')
        'while'  => array('>', 'v1', '1')  //op column val

    //        'filter' => array('<=' => array(0, '2')))
*/
    /*
        'filter' => array(array('<=' => array(0, '4'))))
                            array('>=' => array(0, '6'))),
          'while'  => array(array('>=' => array(0, '4')),
                            array('>=' => array(0, '6')))),
    */
/*
    )
);
*/
/*
'filter' => array(array('<=', 'k', '2'),
                  array('>=', 'k', '3'))
*/

/*
//[filter]
//    <ftyp> <fop> <fcol> <fval>
//F >= 0 0


//array('in' => array(array(2, 4, 6, 8))));

var_dump($find);

//2   =   2   1   2   3   0
//[in]
//@ 0 3 2 4 6
*/


$obj = $index1;

$find = $obj->find(
    array('>' => 9),
    3,
    0);

var_dump($find);

/*
$retval = $obj->update(
    array('U' => array('テストテスト', 'ほげほげ')),
    //array('U' => array(111, 'ほげ 1')),
    //array('U?' => array(111, 'ほげ 1')),
    //array('+' => array(111, 'ほげ 1')),
    //array('+?' => array(111, 'ほげ 1')),
    //array('-' => array(1, 'ほげ 1')),
    //array('-?' => array(1, 'ほげ 1')),
    //array('D' => array(111, 'ほげ 1')),
    //array('D?' => array(111, 'ほげ 1')),
    //array('D' => null),
    //array('D?' => null),
    array('=' => array(1)),
    1,
    0
);

var_dump($retval);


$retval = $obj->remove(
    array('=' => array(10)),
    1,
    0
);

var_dump($retval);
*/