eightmfun
=========

```shell
    $ dd if=/dev/urandom of=payload bs=1k count=1
    $ ulimit -n 100000
    $ ./eightmfun
```

* На 81 порту -- nginx
* На 9999 порту -- eightmfun

github.com/seriyps/wrk

```shell
$ ulimit -n 100000

$ ./wrk -r5m -c64 -t4 -k http://localhost:9999/
Making 5000000 requests to http://localhost:9999/
  4 threads and 64 connections
Completed 785231 requests
Completed 1579657 requests
Completed 2367269 requests
Completed 3157036 requests
Completed 3942586 requests
Completed 4704347 requests
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   432.58us  474.09us   8.97ms   98.95%
    Req/Sec    38.89k   530.47    40.00k    92.21%
  5000012 requests, 64 sock connections in 31.94s, 619.89MB read
Requests/sec: 156533.51
Transfer/sec:     19.41MB


$ ./wrk -r5m -c64 -t4 -k http://localhost:81/  
Making 5000000 requests to http://localhost:81/
  4 threads and 64 connections
Completed 420826 requests
Completed 852135 requests
Completed 1286731 requests
Completed 1717914 requests
Completed 2137923 requests
Completed 2549526 requests
Completed 2903072 requests
Completed 3311875 requests
Completed 3716315 requests
Completed 4115285 requests
Completed 4510676 requests
Completed 4888640 requests
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     1.75ms    7.36ms 204.38ms   99.00%
    Req/Sec    20.38k     0.88k   21.00k    96.95%
  5000002 requests, 50029 sock connections in 1.02m, 1.70GB read
Requests/sec:  81317.08
Transfer/sec:     28.38MB

$ ./wrk -r5m -c64 -t4 -k http://localhost:9999/payload
Making 5000000 requests to http://localhost:9999/payload
  4 threads and 64 connections
Completed 399346 requests
Completed 803577 requests
Completed 1208047 requests
Completed 1602975 requests
Completed 2018041 requests
Completed 2425561 requests
Completed 2839943 requests
Completed 3242235 requests
Completed 3643409 requests
Completed 4041005 requests
Completed 4444777 requests
Completed 4836742 requests
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   802.02us  134.07us   4.74ms   83.22%
    Req/Sec    19.91k   330.15    22.00k    90.34%
  5000000 requests, 64 sock connections in 1.03m, 5.37GB read
Requests/sec:  80524.78
Transfer/sec:     88.62MB

$ ./wrk -r5m -c512 -t4 -k http://localhost:81/payload  
Making 5000000 requests to http://localhost:81/payload
  4 threads and 512 connections
Completed 431355 requests
Completed 866853 requests
Completed 1296790 requests
Completed 1722845 requests
Completed 2138090 requests
Completed 2546429 requests
Completed 2910958 requests
Completed 3310732 requests
Completed 3693069 requests
Completed 4091344 requests
Completed 4478964 requests
Completed 4872305 requests
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     6.00ms    4.41ms  57.06ms   77.20%
    Req/Sec    20.44k     0.90k   24.00k    94.63%
  5000088 requests, 50270 sock connections in 1.03m, 5.84GB read
Requests/sec:  81073.76
Transfer/sec:     97.03MB

$ ./wrk -r5m -c512 -t4 -k http://localhost:9999/payload
Making 5000000 requests to http://localhost:9999/payload
  4 threads and 512 connections
Completed 372609 requests
Completed 740409 requests
Completed 1098107 requests
Completed 1457786 requests
Completed 1804878 requests
Completed 2186635 requests
Completed 2539120 requests
Completed 2903804 requests
Completed 3256939 requests
Completed 3602365 requests
Completed 3947850 requests
Completed 4289788 requests
Completed 4625615 requests
Completed 4968393 requests
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     7.67ms   25.04ms   1.09s    99.95%
    Req/Sec    17.70k     0.85k   24.00k    91.08%
  5000000 requests, 512 sock connections in 1.17m, 5.37GB read
Requests/sec:  70963.91
Transfer/sec:     78.10MB


$ ./wrk -r5m -c1024 -t4 -k http://localhost:81/payload  
Making 5000000 requests to http://localhost:81/payload
  4 threads and 1024 connections
Completed 418936 requests
Completed 846295 requests
Completed 1273882 requests
Completed 1699262 requests
Completed 2115432 requests
Completed 2520669 requests
Completed 2909509 requests
Completed 3308476 requests
Completed 3707787 requests
Completed 4101035 requests
Completed 4491261 requests
Completed 4869723 requests
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     9.14ms   11.39ms 196.68ms   95.63%
    Req/Sec    20.18k     1.63k   29.00k    88.05%
  5000080 requests, 51360 sock connections in 1.03m, 5.84GB read
  Socket errors: connect 0, read 0, write 0, timeout 742
Requests/sec:  80881.10
Transfer/sec:     96.80MB

$ ./wrk -r5m -c1024 -t4 -k http://localhost:9999/payload
# core_dump
```

```shell
$ ./wrk -r5m -c2 -t2 -k http://localhost:81/payload
Making 5000000 requests to http://localhost:81/payload
  2 threads and 2 connections
Completed 233818 requests
Completed 472657 requests
Completed 708861 requests
Completed 943292 requests
Completed 1176655 requests
Completed 1402875 requests
Completed 1614925 requests
Completed 1848937 requests
Completed 2083412 requests
Completed 2311294 requests
Completed 2544200 requests
Completed 2777233 requests
Completed 3010917 requests
Completed 3227715 requests
Completed 3460639 requests
Completed 3697318 requests
Completed 3929443 requests
Completed 4157599 requests
Completed 4387042 requests
Completed 4623445 requests
Completed 4835393 requests
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   596.67us   10.67ms 204.60ms   99.73%
    Req/Sec    22.97k   173.72    23.00k    97.74%
  5000000 requests, 50000 sock connections in 1.81m, 5.84GB read
Requests/sec:  46057.03
Transfer/sec:     55.12MB

$ ./wrk -r5m -c2 -t2 -k http://localhost:9999/payload
Making 5000000 requests to http://localhost:9999/payload
  2 threads and 2 connections
Completed 393973 requests
Completed 814480 requests
Completed 1239714 requests
Completed 1672287 requests
Completed 2089085 requests
Completed 2496190 requests
Completed 2918348 requests
Completed 3335727 requests
Completed 3729186 requests
Completed 4102348 requests
Completed 4499133 requests
Completed 4903160 requests
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    27.14us  150.62us   3.20ms   99.63%
    Req/Sec    40.41k     0.97k   41.00k    88.24%
  5000000 requests, 2 sock connections in 1.02m, 5.37GB read
Requests/sec:  81530.06
Transfer/sec:     89.73MB
```

nginx.conf:

    worker_processes 4;
    sendfile on;
    tcp_nopush on;
    tcp_nodelay on;

uname -rmsv:

    Linux 3.5.0-25-generic #38-Ubuntu SMP Mon Feb 18 23:27:42 UTC 2013 x86_64


