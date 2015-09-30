HandlerSocket extension for PHP
==============================

## Introduction

This is the **HandlerSocket** PECL PHP extension. It provides an
interface for accessing the
[HandlerSocket](http://github.com/ahiguti/HandlerSocket-Plugin-for-MySQL)
from PHP.

This repository is a mirror from the google code project
[php-handlersocket](https://code.google.com/p/php-handlersocket).

## Documentation

There's not much documentation about HandlerSocket. 

 1. There are some slides from the
    [slides](www.slideshare.net/akirahiguchi/handlersocket-20100629en-5698215)
from the MySQL plugin author.

 2. A blog
    [post](http://yoshinorimatsunobu.blogspot.com/2010/10/using-mysql-as-nosql-story-for.html)
    from the database infrastructure architect at
    [DeNA](http://www.dena.jp/en/index.html), the largest japanese
    social gaming platform.
    
 3. One more blog
    [post](http://golanzakai.blogspot.com/2010/10/installing-denas-handlersocket-nosql.html)
    about installing HandlerSocket on RedHat based systems and having
    it up and running.   
    
 4. Using this PHP extension detailed on another blog
    [post](http://mysqldba.blogspot.com/2010/12/handlersocket-mysqls-nosql-php-and.html).  
    
## Just the facts

HandlerSocket overrides all the CPU usage related with SQL
parsing. Instead it speaks a text protocoldirectly to InnoDB to
Create/Read/Update/Delete (CRUD) data. It can combine several
operations on the server side.

Supported SQL commands:
    
    SELECT, UPDATE, INSERT, DELETE

Supported SQL operators:

    IN, WHERE, ORDER BY, LIMIT, FROM, =, >=, <=, >, <

For it to work the tables being accessed **must** have and **index**
or/and a **primary key** since what HandlerSocket performs is either a
primary key lookup or an index scan.

## Debian, Percona Server and MariaDB support

The HandlerSocket MySQL plugin is included in
[Debian](http://packages.debian.org/search?keywords=handlersocket) and
comes also included in
[MariaDB 5.3](http://kb.askmonty.org/en/mariadb-530-release-notes) and
[Percona Server 5.1.52-12.3](http://www.percona.com/docs/wiki/percona-server:release_notes_51#percona_server_5152-123).

## Configuring the MySQL server

### HandlerSocket configuration

You must enable HandlerSocket in your instance of `mysqld`. Add the
following in `/etc/mysql/my.cnf`:
    
    ## Number of reader threads. The recommended value is the number
    ## of logical CPUs. 
    handlersocket_threads = 16
     
 You can get this number with `cat /proc/cpuinfo | grep 'processor' | wc -l`.
 
    ## Number of writer threads
    ## Recommended value is 1.
    handlersocket_thread_wr = 1
   
    ## Listening ports for reader requests.
    handlersocket_port = 9998
    ## Listening port for writer requests.
    handlersocket_port_wr = 9999
   
### InnoDB Configuration

Since HandlerSocket talks directly to InnoDB the engine runtime
parameters needs to be tweaked in order to extract the maximum
possible performance of HandlerSocket as well to maintain data
integrity when doing writes.

#### Performance Settings

    ## Set this as large as possible.
    innodb_buffer_pool_size

    ## As large as possible.
    innodb_log_file_size 
    innodb_log_files_in_group

    ## No concurrent threads. Handler socket uses a single thread with
    ## an event approach using epoll().
    innodb_thread_concurrency = 0

    ## HandlerSocket can handle up to 65000 concurrent
    ## connections. This needs to be supported at the OS level.
    open_files_limit = 65535

Check your current maximum number of simultaneosuly open file descriptors value with `ulimit`.

    ## Enable adaptive hash index.
    innodb_adaptive_hash_index = 1


### Data Integrity Settings

HandlerSocket has no transaction support 

    ## Flush the transaction log after each transaction.
    innodb_flush_log_at_trx_commit = 1    
    
    ## Guarantees that at most we lost a transaction. This has
    ## performance penalties. We're asking for MySQL to flush to disk
    ## every transaction log instance after being done. This enables
    ## us to recover almost all data upon a crash.
    sync_binlog = 1

    ## Enable two phase commit. First prepare then commit in XA
    ## transactions, i.e., 
    innodb_support_xa = 1
    

More information regarding InnoDB tunning can be found
[here](http://www.mysqlperformanceblog.com/2007/11/01/innodb-performance-optimization-basics).

An [explanation](http://sql.dzone.com/news/what-innodbsupportxa) of
`innodb_support_xa`.


## TODO

 1. Expand the documentation with more examples.
 
 2. Debianize the extension and provide a package at
    [debian.perusio.net](http://debian.perusio.net/unstable).
