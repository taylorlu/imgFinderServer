# outline
一个精准的图像检索系统， 基于**AKAZE feature** 和 **LSH algorithm**.</br>
支持多台机器分布式处理，检索中间结果通过RabbitMQ汇总。
# prerequisites
[OpenCV3.4.0](https://github.com/taylorlu/opencv-3.4.0)，修改了lsh处理哈希表的算法，增加了几个Api接口</br>
RabbitMQ，用于分布式通信</br>
Java后台，仅用来操作mysql数据库</br>
# compile
`./compile.sh`
编译c++文件生成imgFinderServer文件。
imgFinderServer有多个执行入口：
1. `./imgFinderServer  --img2hashtable  path_of_image  BASIC_Hash.dat  path_of_tmp  id_of_image`</br>
提取某个图片的feature，保存到一个二进制哈希表中，id_of_image是图片的id，和mysql数据库中的对应，BASIC_Hash.dat是一个模板，里面有哈希表个数、哈希值位数、probe次数等meta信息。
执行完之后，会生成2个文件：*id_of_image*_LSHashTable.dat、*id_of_image*_LSHashTable.dat，二进制哈希和坐标，放在path_of_tmp文件夹下。
2. `./imgFinderServer  --mergeFromDir  path_of_tmp  DayLSHashTable.dat  DayIndexMap.dat`</br>
将path_of_tmp目录下的所有二进制哈希表合并到一个大的哈希表中，DayLSHashTable.dat和DayIndexMap.dat是合并成的大哈希文件。
3. `./imgFinderServer --serve retrieve.json`</br>
这是一个服务，后续检索的时候用。
# register
配置文件register.json

    {"MachineId": "02",
    "HashFolder": "/u01/dataset/imgFinderFolder3",
    "TmpFolder": "./tmp",`
    "TaskUrl": "http://172.18.250.30:9011/arImageHandle"}

1. `MachineId` 当前的机器Id，和mysql数据库中的对应</br>
2. `HashFolder` 存储的Hash表路径，这个路径下会根据当前时间生成文件夹，每个文件夹下有2个文件：`DayLSHashTable.dat和DayIndexMap.dat`（二进制哈希和坐标）</br>

├── imgFinderFolder3</br>
│   ├── 20101100121212</br>
│   │   ├── DayLSHashTable.dat</br>
│   │   └── DayIndexMap.dat</br>
│   └── 20101100121256</br>
│   │   ├── DayLSHashTable.dat</br>
│   │   └── DayIndexMap.dat</br>

    可能还会有一个reload.dat空文件，用来标识当前注册进程正在merge的目录，检索进程需经常reload最新的哈希表确保数据的完整性。
    每次合并生成的`DayLSHashTable.dat`如果>90M（在`reg.py`中配置），会在`HashFolder`下新建一个目录。</br>
3. `TmpFolder` 注册过程中暂存的IndexMap和dat和LSHashTable文件，每隔一段时间（reg.py中指定）会自动合并到大文件中</br>
4. `TaskUrl` Java服务器接口，将多个Machine处理的图片info信息存储到mysql数据库中</br>

**启动：**
`python reg.py register.json`
# retrieve
配置文件retrieve.json

    {"MachineId": "02",
    "HashFolder": "/u01/dataset/imgFinderFolder3",
    "MQ_IP": "192.168.179.119",
    "MQ_Port": 5672,
    "MQ_Username": "admin",
    "MQ_Password": "test123",
    "MQ_Exchange_in": "fanoutExchange",
    "MQ_Exchange_out": "callbackExchange"}

1. `MachineId，HashFolder` 和注册是对应的。
2. `MQ_*` RabbitMQ的配置。
3. 每一个哈希表启动一个线程，没有用线程池的方式实现。
   RabbitMQ获取任务（设置TTL，采用pull的方式）-->LSH检索当前哈希表-->RANSAC过滤-->查询结果JSON提交到RabbitMQ。

**启动：**
`./imgFinderServer --serve retrieve.json`

# baseline
1. 配置：`Intel(R) Xeon(R) CPU E5-2630 v3 @ 2.40GHz, 8 Cores, 32 processors`，检索量100,000张图片。
2. 耗时：每张图在600ms左右。
3. 准确率：手机扫图的准确率100%。[iOS手机扫图Demo](https://github.com/taylorlu/imgFinder)
4. 内存占用：占用10G RAM，平均1万张1G内存。

优点：
1. 准确率非常高，不同于Fisher Vector和VLAD等需要使用GMM聚类进行词典生成，直接采用点匹配。可参考[FVector](https://github.com/taylorlu/FVector)。
2. 传输量非常小，每张图在20kB以下，包含(4B*5 + 2B*2) * 800点 = 19.2KB，20B表示每个点的哈希值占用字节数，4B表示每个点的x,y坐标占用字节数，点数不同，传输量不同。

缺点：
1. 内存量占用太大，因为要多对多匹配，100,000张图片共有将近1亿个特征点。
2. 检索速度慢，分拆成多台服务器能解决这个问题。
