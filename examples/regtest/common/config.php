<?php
define('MYSQL_HOST', 'localhost');
define('MYSQL_PORT', 3306);
define('MYSQL_DBNAME', 'hstestdb');
define('MYSQL_USER', 'root');
define('MYSQL_PASSWORD', '');

define('MYSQL_HANDLERSOCKET_PORT', 9998);
define('MYSQL_HANDLERSOCKET_PORT_WR', 9999);

if (!extension_loaded('mysql'))
{
    die('Cann\'t load mysql extension');
}

function get_mysql_connection()
{
    $mysql = mysql_connect(
        sprintf('%s:%d', MYSQL_HOST, MYSQL_PORT), MYSQL_USER, MYSQL_PASSWORD);
    if (!$mysql)
    {
        die('Can\'t connect: ' . mysql_error());
    }

    return $mysql;
}

function init_mysql_testdb($mysql)
{
    $sql = sprintf(
        'DROP DATABASE IF EXISTS %s', mysql_real_escape_string(MYSQL_DBNAME));
    if (!mysql_query($sql, $mysql))
    {
        die('Can\'t drop dabases: ' . MYSQL_DBNAME . ': ' . mysql_error());
    }

    $sql = sprintf(
        'CREATE DATABASE %s', mysql_real_escape_string(MYSQL_DBNAME));
    if (!mysql_query($sql, $mysql))
    {
        die('Can\'t create dabases: ' . MYSQL_DBNAME . ': ' . mysql_error());
    }
    else
    {
        $db = mysql_select_db(MYSQL_DBNAME, $mysql);
        if (!$db)
        {
            die ('Can\'t use database: ' . MYSQL_DBNAME . ' : ' . mysql_error());
        }
    }
}

