#include <iostream>
#include <memory>
#include <string.h>
#include <map>
#include <queue>
#include <string>
#include <unistd.h>
#include <grpcpp/grpcpp.h>
#include <fstream>
#include <thread>
#include <vector>
#include <functional>
#include "yaml-cpp/yaml.h"
#include "darknet.h"
#include "../proto/detector.grpc.pb.h"
#include "services/predict_image.h"
using namespace std;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::ServerWriter;
using grpc::ServerAsyncReaderWriter;
using grpc::ServerCompletionQueue;
using detector::DetectorRequest;
using detector::DetectorReply;
using detector::Detector;

class ServerImpl{
    public:
        ServerImpl(int argc, char** argv) {
            #ifdef DEBUG 
                printf("\nGPU usage rate:%f\n",get_gpu_percentage());
            #endif
            auto args=ParseArguments(argc,argv);
            string gpus=args.find("gpus")!=args.end()?args.find("gpus")->second:"0";
            istringstream gpus_input(gpus);
            vector<int> gpus_arr;
            for (string gpu; getline(gpus_input, gpu,','); ){
                gpus_arr.push_back(stoi(gpu));
                gpu_queues_->push_back(make_shared<queue<function<void()>>>());
            } 
            LoadConfig(args.find("config")!=args.end()?args.find("config")->second:"",gpus_arr);
            ofstream outf(log_); 
            cout.rdbuf(outf.rdbuf()); 
            builder_.AddListeningPort(server_address_, grpc::InsecureServerCredentials());
            // Register "service_" as the instance through which we'll communicate with
            // clients. In this case it corresponds to an *asynchronous* service.
            Detector::AsyncService service;
            builder_.RegisterService(&service);
            for(unsigned int i=0;i<10;i++){
                cqs_.push_back(move(builder_.AddCompletionQueue()));
            }
            // Finally assemble the server.
            server_=builder_.BuildAndStart();
            cout << "Server listening on " << server_address_ << endl;
            RegisterServices(&service);
            t_=make_unique<thread>(bind(&ServerImpl::QueueThread, this));
            server_->Wait();
        }

        ~ServerImpl(){
            cout<<"Shutting down server"<<endl;
            server_->Shutdown();
            for(auto& cq:cqs_){
                cq->Shutdown();
            }
            t_->join();
        }

    private:
        void QueueThread(){
            while(true){
                for(auto& gpu_queue:*gpu_queues_){
                    if(!gpu_queue->empty()){
                        auto lambda=move(gpu_queue->front());
                        //if(instance!=nullptr){
                            //instance->DoTask();
                        //}
                        lambda();
                        gpu_queue->pop();
                    }
                }
            }
        }
        void RegisterServices(Detector::AsyncService* service){
            for(auto& cq:cqs_){
                new PredictImage(service,nets_,&srv_,names_,cqs_,gpu_queues_);
            }
        }
        map<string,string> ParseArguments(int argc,char** argv){
            map<string,string> result;
            for(int i=1;i<argc;i++){
                auto k=strtok(argv[i],"=");
                auto v=strtok(NULL,"=");
                result.insert(make_pair(k,v));
            }
            return result;
        }
        void LoadConfig(string config_path,vector<int> gpus_arr){
            #ifdef DEBUG
                printf("\nConfig Path:%s\n",(config_path!=""?config_path:"chenyun/config.yaml").c_str());
            #endif
            auto config = YAML::LoadFile(config_path!=""?config_path:"chenyun/config.yaml");
            server_address_=config["address"]?config["address"].as<string>():"0.0.0.0:50051";
            log_=config["log"]?config["log"].as<string>():"out.log";
            srv_=config["srv"]?config["srv"].as<string>():"/usr/local/srv/";
            auto data_path = config["data"].as<string>();
            auto cfg_path = config["cfg"].as<string>();
            auto weight_path = config["weight"].as<string>();
            auto datacfg = const_cast<char*>(data_path.c_str());
            auto cfgfile = const_cast<char*>(cfg_path.c_str());
            auto weightfile = const_cast<char*>(weight_path.c_str());
            #ifdef DEBUG
                puts(datacfg);
                puts(cfgfile);
                puts(weightfile);
            #endif
            unique_ptr<darknet_list> options=move(unique_ptr<darknet_list>(read_data_cfg(datacfg)));
            auto name_list = option_find_str(options.get(), const_cast<char*>("names"), const_cast<char*>("chenyun/obj.names"));
            names_ = get_labels(name_list);
            for(auto& gpu_index:gpus_arr){
                auto net=unique_ptr<network>(load_network(cfgfile, weightfile, 0,gpu_index));
                set_batch_network(net.get(), 1);
                nets_->push_back(move(net));
            }
        }
        unique_ptr<Server> server_=nullptr;
        vector<unique_ptr<ServerCompletionQueue>> cqs_;
        string srv_;
        string server_address_;
        string log_;
        ServerBuilder builder_;
        shared_ptr<vector<shared_ptr<queue<function<void()>>>>> gpu_queues_=make_shared<vector<shared_ptr<queue<function<void()>>>>>();
        shared_ptr<vector<unique_ptr<network>>> nets_=make_shared<vector<unique_ptr<network>>>();
        char **names_=nullptr;
        unique_ptr<thread> t_=nullptr;
};

int main(int argc, char** argv) {
    ServerImpl server(argc,argv);
    return 0;
}