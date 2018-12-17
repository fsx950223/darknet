#include <iostream>
#include <memory>
#include <string>
#include <map>
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
            load_configs();
            std::ofstream outf(log_); 
            std::cout.rdbuf(outf.rdbuf()); 
            builder_.AddListeningPort(server_address_, grpc::InsecureServerCredentials());
            // Register "service_" as the instance through which we'll communicate with
            // clients. In this case it corresponds to an *asynchronous* service.
            Detector::AsyncService service;
            builder_.RegisterService(&service);
            // Finally assemble the server.
            server_=builder_.BuildAndStart();
            std::cout << "Server listening on " << server_address_ << std::endl;
            RegisterServices(&service);
            server_->Wait();
        }

        ~ServerImpl(){
            std::cout<<"Shutting down server"<<std::endl;
            server_->Shutdown();
            cq_->Shutdown();
        }
    private:
        void RegisterServices(Detector::AsyncService* service){
            new PredictImage(service,net_,&srv_,names_,cq_.get(),cq_.get(),METHOD::PREDICT_IMAGE);
        }
        void load_configs(){
            YAML::Node config = YAML::LoadFile("chenyun/config.yaml");
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
        }
        DetectorRequest request_;
        std::unique_ptr<Server> server_;
        std::unique_ptr<ServerCompletionQueue> cq_;
        std::string srv_;
        std::string server_address_;
        std::string log_;
        ServerBuilder builder_;
        network *net_=nullptr;
        char **names_=nullptr;
};



int main(int argc, char** argv) {
    ServerImpl server(argc,argv);
    return 0;
}