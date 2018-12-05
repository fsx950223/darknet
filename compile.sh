#!/bin/bash
rm -rf $(pwd)/proto/*.pb.h $(pwd)/proto/*.pb.cc
protoc -I=$(pwd)/proto --grpc_out=$(pwd)/proto --plugin=protoc-gen-grpc=$(which grpc_cpp_plugin) $(pwd)/proto/*.proto
protoc -I=$(pwd)/proto --cpp_out=$(pwd)/proto $(pwd)/proto/*.proto