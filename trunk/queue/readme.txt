Copyright (C) 2012 Dr.NP

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

===================================================================================

软件ID：fakesae.queue
软件版本：0.1 Alpha
协议：GPLv2

本软件用于模拟SinaAppEngine的队列服务，配合redis，实现队列中的URL请求。
本软件基于libhiredis和libcurl，编译前安装好相应的库和头文件

[安装]
本软件基于Linux epoll，需要准备2.5.44以上版本内核。考虑到pthread的nptl性能，推荐2.6.9以上版本。
首先安装libhiredis，源文件位于redis源码包中。
安装libcurl：http://curl.haxx.se/libcurl/
安装本软件：解压之后执行make，将生成的二进制文件复制到目标目录，无需执行make install，如有编译错误，请检查当前系统环境。如有特殊需求，请修改Makefile。

[运行]
执行fakesae_queue程序，支持的参数有：
    -h [OPTION] ： Redis服务器地址，默认为localhost
    -p [OPTION] ： Redis服务器端口，默认为6379
    -D [OPTION] ： Redis数据库，默认为1
    -a [OPTION] ： Redis连接密码，如无密码则留空
    -d ： 作为后台服务，默认为前台程序
    -s [OPTION] ： 队列获取延迟时间。当便利所有队列都没有获取到新数据，程序会中断一段时间，单位为毫秒，默认为100
    -c [OPTION] ： 配置文件（队列列表）文件（LUA格式）
    -l [OPTION] ： 错误日志文件，程序将保存所有无法正常访问的URL到日志，默认为/var/log/fakesae_queue.log
    -q [OPTION] ： 队列读写方向：1为rpush/lpop，2为lpush/rpop，默认为1
    -H ： 显示帮助信息

[特性]
以下参数可以在fakesae_queue.h中修改

静态工作线程数（cURL句柄）：64
初始队列个数：1024
队列数据长度：4096
队列名长度：256
每次申请任务内存块大小：4MB

[使用]
首先需要登记队列列表，登记在-c配置的LUA脚本中，数组名为fakesae_queue_list
    队列名 = 队列类型
对列明为标准字符串，队列类型为1表示顺序队列，2表示并发队列。顺序队列保证cURL的顺序维持插入顺序，并发队列将会不确定地将任务发布给一个工作线程，由于cURL本身的非线程安全，不保证成功顺序。
列表可以在程序执行时随时修改加载。
列表写入后，启动本程序，可又其它redis客户端向redis相应队列名的key中写入url数据。
本程序使用libcurl的标准参数，未做UA和来源修饰。

[命令]
本程序每次便利队列都会读一个特殊的队列FAKESAE_QUEUE_SIGNAL，可根据需要向该队列中推送命令，命令包括：
    stop ： 中止遍历（不包括FAKESAE_QUEUE_SIGNAL）
    resume ： 恢复遍历
    reload ： 重载队列列表（重载lua脚本）
    shutdown ： 关闭程序

[SAE]
对应于SAE本身的TaskQueue服务，本程序未做队列分级，也没有队列长度限制，默认的并发队列并发度全部都是64。
本程序不对外监听，所有操作均基于Redis。
尽量在内网中使用，由于程序中对libcurl未做超时处理，使用者可以自行在受访端修改参数。
访问结果状态不为200的URL会被记录在log中。
本程序忽略URL返回的body。
