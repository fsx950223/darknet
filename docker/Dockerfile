FROM fsx950223/grpc:latest
COPY ["./chenyun","/darknet/chenyun/"]
COPY ["./darknet","./server","/darknet/"]
EXPOSE 50051
WORKDIR /darknet
ENTRYPOINT [ "/darknet/server" ]