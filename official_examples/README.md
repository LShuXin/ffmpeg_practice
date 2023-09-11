# ffmpeg_examples

> 这里面的例子来源于 ffmpeg n4.4.1 （commit hash 7e0d640）
> 工程在 mac 上编译通过


## [添加注释](https://blog.jetbrains.com/clion/2016/05/keep-your-code-documented/)
1. 在函数名称上面一行输入 /*！然后回车，即可自动生成注释。
2. 在函数名称上面一行输入 /** 然后回车，即可自动生成注释。
3. 在函数名称上面一行输入 /*** 然后回车，即可自动生成注释。
4. 在函数名称上面一行输入 /// 然后回车，即可自动生成注释。


## 使用
```
mkdir build
cd build
cmake ..
make
```

## 各 demo 使用说明
make 成功，分别执行下面的命令

- muxing_demo
```
运行此命令将生成一个 mp4 视频
./muxing_demo mux.mp4
```

- metadata_demo
```
运行此命令将打印出指定视频的一些信息
./metadata_demo mux.mp4ß
```