<?php
$br = (php_sapi_name() == 'cli')? '':'<br>';

if (!extension_loaded('handlersocket'))
{
    dl('handlersocket.' . PHP_SHLIB_SUFFIX);
}
$module = 'handlersocket';
$class = 'HandlerSocket';
if (!class_exists($class))
{
    echo "Module $module is not compiled into PHP\n";
}
else
{
    $methods = get_class_methods($class);
    echo "Class methods available in the handlersocket extension:$br\n";
    foreach((array)$methods as $method)
    {
        echo $method . "$br\n";
    }
}
