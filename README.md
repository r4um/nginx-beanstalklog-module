nginx-beanstalklog-module
=========================

Log all http requests to [beanstalkd](http://kr.github.io/beanstalkd/). This module is installed in preaccess phase.
Replay them to [gor](https://github.com/buger/gor) server using [nginx-beanstalklog-replay](https://github.com/r4um/nginx-beanstalklog-replay).

To build install [beanstalk-client](https://github.com/deepfryed/beanstalk-client)

```shell
# under nginx source
$ ./configure --add-module=/path/to/nginx-beanstalklog-module
$ make
```

Configuration is valid under HTTP context

```
http {
    # enable beanstalklog
    beanstalklog on;
    # set host running beanstalkd (required)
    beanstalklog_host "127.0.0.1";
    # set beanstalkd tube to use (required)
    beanstalklog_tube "nginx-log";
    # set beanstalkd port to connect to (optional), default 11300
    beanstalklog_port 11300;
    # set connect timeout (optional), default 2 seconds
    beanstalklog_connect_timeout 2;
    # set delay (optional), default 0
    beanstalklog_delay 0;
    # set priority (optional), default 0
    beanstalklog_priority 0;
    # set ttr (optional), default 60
    beanstalklog_ttr 60;
    ...
}
```
