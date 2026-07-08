# 1
## 编译
```
gcc mutex_speed.c -o mutex_speed -pthread
gcc semaphore_speed.c -o semaphore_speed -pthread
```

## 运行
```
./mutex_speed 50000000
./semaphore_speed 50000000
```

#2
## 编译
```
gcc shm_mutex.c -o shm_mutex -pthread

gcc shm_mutex_pagemap.c -o shm_mutex_pagemap -pthread
```
## 运行
```
./shm_mutex

./shm_mutex_pagemap 100000
```