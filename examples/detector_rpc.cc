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
#include "darknet.h"
#include "../proto/detector.grpc.pb.h"
#include "yaml-cpp/yaml.h"

#include "./services/predict_image.h"
//using namespace std;
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

enum class Type { READ, WRITE, CONNECT,DONE, FINISH};

class ServerImpl{
    public:
        ServerImpl(int argc, char** argv) {
            // #ifdef GPU 
            //     printf("%f",get_gpu_percentage());
            // #endif
            auto args=ParseArguments(argc,argv);
            LoadConfig(args.find("config")!=args.end()?args.find("config")->second:nullptr);
            std::ofstream outf(log_); 
            std::cout.rdbuf(outf.rdbuf()); 
            builder_.AddListeningPort(server_address_, grpc::InsecureServerCredentials());
            // Register "service_" as the instance through which we'll communicate with
            // clients. In this case it corresponds to an *asynchronous* service.
            Detector::AsyncService service;
            builder_.RegisterService(&service);
            for(unsigned int i=0;i<10;i++){
                cqs_.push_back(std::move(builder_.AddCompletionQueue()));
            }
            // Finally assemble the server.
            server_=builder_.BuildAndStart();
            std::cout << "Server listening on " << server_address_ << std::endl;
            for(unsigned int i=0;i<cqs_.size();i++){
                RegisterServices(&service,cqs_[i].get());
            }
            t_.reset(new std::thread(std::bind(&ServerImpl::predict_thread, this)));
            server_->Wait();
        }

        ~ServerImpl(){
            std::cout<<"Shutting down server"<<std::endl;
            server_->Shutdown();
            for(unsigned int i=0;i<cqs_.size();i++){
                cqs_[i]->Shutdown();
            }
            t_->join();
        }
        void predict_thread(){
            while(true){
                if(!gpu_queue_.empty()){
                    auto instance=gpu_queue_.front();
                    if(instance!=nullptr){
                        instance->predict_detector(instance->request_.file(),instance->request_.thresh(),instance->request_.hier_thresh());
                    }
                    gpu_queue_.pop();
                }
            }
        }
    private:
        void RegisterServices(Detector::AsyncService* service,ServerCompletionQueue* cq){
            new PredictImage(service,net_,&srv_,names_,cq,cq,METHOD::PREDICT_IMAGE,&gpu_queue_);
        }
        std::map<char*,char*> ParseArguments(int argc,char** argv){
            std::map<char*,char*> result;
            for(int i=1;i<argc;i++){
                auto k=strtok(argv[i],"=");
                std::cout<<k<<std::endl;
                auto v=strtok(NULL,"=");
                std::cout<<v<<std::endl;
                result.insert(std::make_pair(k,v));
            }
            return result;
        }
        void LoadConfig(char* config_path){
            std::cout<<config_path<<std::endl;
            YAML::Node config = YAML::LoadFile(config_path!=nullptr?config_path:"chenyun/config.yaml");
            std::string datacfgStr=config["data"].as<std::string>();
            std::string cfgfileStr=config["cfg"].as<std::string>();
            std::string weightfileStr=config["weight"].as<std::string>();
            server_address_=config["address"]?config["address"].as<std::string>():"0.0.0.0:50051";
            log_=config["log"]?config["log"].as<std::string>():"out.log";
            srv_=config["srv"]?config["srv"].as<std::string>():"/usr/local/srv/";
            char *datacfg = new char[datacfgStr.length() + 1];
            char *cfgfile = new char[cfgfileStr.length() + 1];
            char *weightfile = new char[weightfileStr.length() + 1];
            strcpy(datacfg, datacfgStr.c_str());
            strcpy(cfgfile, cfgfileStr.c_str());
            strcpy(weightfile, weightfileStr.c_str());
            list *options = read_data_cfg(datacfg);
            char *name_list = option_find_str(options, "names", "chenyun/obj.names");
            names_ = get_labels(name_list);
            net_ = load_network(cfgfile, weightfile, 0);
            set_batch_network(net_, 1);
            delete datacfg;
            delete cfgfile;
            delete weightfile;
        }
        std::unique_ptr<Server> server_;
        std::vector<std::unique_ptr<ServerCompletionQueue>> cqs_;
        std::string srv_;
        std::string server_address_;
        std::string log_;
        ServerBuilder builder_;
        std::queue<PredictImage*> gpu_queue_;
        network *net_=nullptr;
        char **names_=nullptr;
        std::unique_ptr<std::thread> t_;
};

int main(int argc, char** argv) {
    ServerImpl server(argc,argv);
    return 0;
}