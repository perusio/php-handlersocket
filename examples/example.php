<?php
$host = 'localhost';
$port = 9998;
$port_wr = 9999;
$dbname = 'hstestdb';
$table = 'hstesttbl';


//GET
$hs = new HandlerSocket($host, $port);
if (!($hs->openIndex(1, $dbname, $table, HandlerSocket::PRIMARY, 'k,v')))
{
    echo __LINE__, ':', $hs->getError(), ':', PHP_EOL;
    die();
}

$retval = $hs->executeSingle(1, '=', array('k1'), 1, 0);

var_dump($retval);

$retval = $hs->executeMulti(
    array(array(1, '=', array('k1'), 1, 0),
          array(1, '=', array('k2'), 1, 0)));

var_dump($retval);

unset($hs);


//UPDATE
$hs = new HandlerSocket($host, $port_wr);
if (!($hs->openIndex(2, $dbname, $table, '', 'v')))
{
    echo __LINE__, ':', $hs->getError(), ':', PHP_EOL;
    die();
}

if ($hs->executeUpdate(2, '=', array('k1'), array('V1'), 1, 0) === false)
{
    echo __LINE__, ':', $hs->getError(), ':', PHP_EOL;
    die();
}

unset($hs);


//INSERT
$hs = new HandlerSocket($host, $port_wr);
if (!($hs->openIndex(3, $dbname, $table, '', 'k,v')))
{
    echo __LINE__, ':', $hs->getError(), ':', PHP_EOL;
    die();
}

if ($hs->executeInsert(3, array('k2', 'v2')) === false)
{
    echo __LINE__, ':', $hs->getError(), ':', PHP_EOL;
}

if ($hs->executeInsert(3, array('k3', 'v3')) === false)
{
    echo __LINE__, ':', $hs->getError(), ':', PHP_EOL;
}
if ($hs->executeInsert(3, array('k4', 'v4')) === false)
{
    echo __LINE__, ':', $hs->getError(), ':', PHP_EOL;
}

unset($hs);


//DELETE
$hs = new HandlerSocket($host, $port_wr);
if (!($hs->openIndex(4, $dbname, $table, '', '')))
{
    echo __LINE__, ':', $hs->getError(), ':', PHP_EOL;
    die();
}

if (!($hs->executeDelete(4, '=', array('k2'))))
{
    echo __LINE__, ':', $hs->getError(), ':', PHP_EOL;
    die();
}

