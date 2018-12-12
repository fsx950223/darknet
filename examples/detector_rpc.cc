#include <iostream>
#include <memory>
#include <string>
#include <map>
#include <unistd.h>
#include <grpcpp/grpcpp.h>
#include "darknet.h"
#include "../proto/detector.grpc.pb.h"
#include "yaml-cpp/yaml.h"
#include <fstream>

//using namespace std;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::ServerWriter;
using detector::DetectorRequest;
using detector::DetectorReply;
using detector::Detector;

network *net=nullptr;
char **names=nullptr;
std::string srv="/usr/local/srv/";

void detection_json(image im, detection *dets, int num, float thresh, char **names, int classes,ServerWriter<DetectorReply>* writer)
{
    int i,j;
    float rate;
    for(i = 0; i < num; ++i){
        char labelstr[4096] = {0};
        int clazz = -1;
        for(j = 0; j < classes; ++j){
            if (dets[i].prob[j] > thresh){
                if (clazz < 0) {
                    strcat(labelstr, names[j]);
                    clazz = j;
                } else {
                    strcat(labelstr, ", ");
                    strcat(labelstr, names[j]);
                }
                printf("%s: %.0f%%\n", names[j], dets[i].prob[j]*100);
                rate=dets[i].prob[j]*100;
            }
        }
        if(clazz >= 0){
            
            box b = dets[i].bbox;
            int left  = (b.x-b.w/2.)*im.w;
            int right = (b.x+b.w/2.)*im.w;
            int top   = (b.y-b.h/2.)*im.h;
            int bot   = (b.y+b.h/2.)*im.h;

            if(left < 0) left = 0;
            if(right > im.w-1) right = im.w-1;
            if(top < 0) top = 0;
            if(bot > im.h-1) bot = im.h-1;
            std::string name;
            name=labelstr;
            DetectorReply reply;
            reply.set_bottom(bot);
            reply.set_left(left);
            reply.set_right(right);
            reply.set_top(top);
            reply.set_name(name);
            reply.set_rate(rate);
            writer->Write(reply);
        }
    }
}
void predict_detector( ServerWriter<DetectorReply>* writer,std::string file, float thresh=.5, float hier_thresh=.5){
    srand(2222222);
    double time;
    std::string str=srv+file;
    int lenOfStr = str.length();
    char* input = new char[lenOfStr];
    strcpy(input,str.c_str());
    float nms=.45;
    if(access(input,0)){
      puts("Cannot load image");
      return;
    }
    image im = load_image_color(input,0,0);
    image sized = letterbox_image(im, net->w, net->h);
    layer l = net->layers[net->n-1];
    float *X = sized.data;
    time=what_time_is_it_now();
    network_predict(net, X);
    printf("%s: Predicted in %f seconds.\n", input, what_time_is_it_now()-time);
    int nboxes = 0;
    detection *dets = get_network_boxes(net, im.w, im.h, thresh, hier_thresh, 0, 1, &nboxes);
    if (nms) do_nms_sort(dets, nboxes, l.classes, nms);
    detection_json(im, dets, nboxes, thresh, names, l.classes,writer);
    free_detections(dets, nboxes);
    free_image(im);
    free_image(sized);
}

class ServerImpl final : public Detector::Service {
 public:
  Status Predict(ServerContext* context,
                      const detector::DetectorRequest* request,
                      ServerWriter<DetectorReply>* writer) override {
                          
    if(context->client_metadata().find("x-custom-auth-token")->first!="detector-grpc-token"){
        return Status::CANCELLED;
    }else{
        predict_detector(writer,request->file(),request->thresh(),request->hier_thresh());
        return Status::OK;
    }
  }
};


void RunServer(int argc, char** argv) {
    std::string server_address("0.0.0.0:50051");
    YAML::Node config = YAML::LoadFile("chenyun/config.yaml");
    std::string datacfgStr=config["data"].as<std::string>();
    std::string cfgfileStr=config["cfg"].as<std::string>();
    std::string weightfileStr=config["weight"].as<std::string>();
    srv=config["srv"].as<std::string>();
    char *datacfg = new char[datacfgStr.length() + 1];
    char *cfgfile = new char[cfgfileStr.length() + 1];
    char *weightfile = new char[weightfileStr.length() + 1];
    strcpy(datacfg, datacfgStr.c_str());
    strcpy(cfgfile, cfgfileStr.c_str());
    strcpy(weightfile, weightfileStr.c_str());
    list *options = read_data_cfg(datacfg);
    char *name_list = option_find_str(options, "names", "chenyun/obj.names");
    names = get_labels(name_list);
    net = load_network(cfgfile, weightfile, 0);
    set_batch_network(net, 1);
    ServerBuilder builder;
    // Listen on the given address without any authentication mechanism.
    //builder.AddListeningPort(server_address, grpc::SslServerCredentials(grpc::SslServerCredentialsOptions()));
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    // Register "service_" as the instance through which we'll communicate with
    // clients. In this case it corresponds to an *asynchronous* service.
    ServerImpl service_;
    builder.RegisterService(&service_);
    // Get hold of the completion queue used for the asynchronous communication
    // with the gRPC runtime.
    //cq_ = builder.AddCompletionQueue();
    // Finally assemble the server.
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_address << std::endl;

    // Proceed to the server's main loop.
    server->Wait();
}
int main(int argc, char** argv) {
  //ServerImpl server;
  RunServer(argc,argv);
  return 0;
}