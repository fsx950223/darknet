#include <iostream>
#include <memory>
#include <string>
#include <map>
//#include <boost/any.hpp>
#include <grpcpp/grpcpp.h>
#include "darknet.h"
#include "../proto/detector.grpc.pb.h"
//using namespace std;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using detector::DetectorRequest;
using detector::DetectorReply;
using detector::Detector;
//using boost::any_cast;
//typedef boost::variant<int, std::string,float> Value;
void detection_json(image im, detection *dets, int num, float thresh, char **names, int classes,DetectorReply* reply)
{
    int i,j;

    for(i = 0; i < num; ++i){
        //识别对象的名称
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
            }
        }
        if(clazz >= 0){
            box b = dets[i].bbox;
            //获取选框坐标以及宽高
            int left  = (b.x-b.w/2.)*im.w;
            int right = (b.x+b.w/2.)*im.w;
            int top   = (b.y-b.h/2.)*im.h;
            int bot   = (b.y+b.h/2.)*im.h;

            if(left < 0) left = 0;
            if(right > im.w-1) right = im.w-1;
            if(top < 0) top = 0;
            if(bot > im.h-1) bot = im.h-1;
            //std::map<std::string,boost::any> result;
            reply->set_bottom(bot);
            reply->set_left(left);
            reply->set_right(right);
            reply->set_top(top);
            reply->set_name(labelstr);
            reply->set_rate(dets[i].prob[j]*100);
        }
    }
}
void predict_detector( char *filename, float thresh, float hier_thresh,DetectorReply* reply)
{
    std::string datacfgStr="cfg/coco.data";
    std::string cfgfileStr="cfg/yolov3.cfg";
    std::string weightfileStr="yolov3.weights";
    char *datacfg = new char[datacfgStr.length() + 1];
    char *cfgfile = new char[cfgfileStr.length() + 1];
    char *weightfile = new char[weightfileStr.length() + 1];
    strcpy(datacfg, datacfgStr.c_str());
    strcpy(cfgfile, cfgfileStr.c_str());
    strcpy(weightfile, weightfileStr.c_str());
    free(datacfg);
    free(cfgfile);
    free(weightfile);
    list *options = read_data_cfg(datacfg);
    char *name_list = option_find_str(options, "names", "data/names.list");
    char **names = get_labels(name_list);
    network *net = load_network(cfgfile, weightfile, 0);
    set_batch_network(net, 1);
    srand(2222222);
    double time;
    char buff[256];
    char *input = buff;
    float nms=.45;
    while(1){
        if(filename){
            strncpy(input, filename, 256);
        } else {
            printf("Enter Image Path: ");
            fflush(stdout);
            input = fgets(input, 256, stdin);
            if(!input) return;
            strtok(input, "\n");
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
        detection_json(im, dets, nboxes, thresh, names, l.classes,reply);
        free_detections(dets, nboxes);
        free_image(im);
        free_image(sized);
        if (filename) break;
    }
}
// Logic and data behind the server's behavior.
class DetectorServiceImpl final : public Detector::Service {
  Status Predict(ServerContext* context, const DetectorRequest* request,
                  DetectorReply* reply) override {
    std::string filename=request->filename();
    char *cstr = new char[filename.length() + 1];
    predict_detector(strcpy(cstr, filename.c_str()),request->thresh(),request->hier_thresh(),reply);
    free(cstr);
    return Status::OK;
  }
};

extern "C" void run_server();
void run_server() {
    std::string server_address("0.0.0.0:50051");
    DetectorServiceImpl service;

    ServerBuilder builder;
    // Listen on the given address without any authentication mechanism.
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    // Register "service" as the instance through which we'll communicate with
    // clients. In this case it corresponds to an *synchronous* service.
    builder.RegisterService(&service);
    // Finally assemble the server.
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_address << std::endl;

    // Wait for the server to shutdown. Note that some other thread must be
    // responsible for shutting down the server for this call to ever return.
    server->Wait();
}

// int main(int argc, char** argv) {
//   run_server();

//   return 0;
// }