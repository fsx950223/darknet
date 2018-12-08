#include <iostream>
#include <memory>
#include <string>
#include <map>
#include <unistd.h>
#include <grpcpp/grpcpp.h>
#include "darknet.h"
#include "../proto/detector.grpc.pb.h"
#include "yaml-cpp/yaml.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerCompletionQueue;
using detector::DetectorRequest;
using detector::DetectorReply;
using detector::Detector;
network *net=nullptr;
char **names=nullptr;
std::string srv="/usr/local/srv/";
void detection_json(image im, detection *dets, int num, float thresh, char **names, int classes,ServerAsyncResponseWriter<DetectorReply>* writer)
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
            DetectorReply* reply;
            std::string name=labelstr;
            reply->set_bottom(bot);
            reply->set_left(left);
            reply->set_right(right);
            reply->set_top(top);
            reply->set_name(name);
            reply->set_rate(rate);
            writer->Write(reply);
        }
    }
}
void predict_detector( ServerAsyncResponseWriter<DetectorReply>* writer,std::string file, float thresh=.5, float hier_thresh=.5){
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

class ServerImpl final {
 public:
  ~ServerImpl() {
    server_->Shutdown();
    // Always shutdown the completion queue after the server.
    cq_->Shutdown();
  }

  // There is no shutdown handling in this code.
  void Run(int argc, char** argv) {
    std::string server_address("0.0.0.0:50051");
    std::string datacfgStr="chenyun/voc.data";
    std::string cfgfileStr="chenyun/chenyun.cfg";
    std::string weightfileStr="chenyun/chenyun.weights";
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
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    // Register "service_" as the instance through which we'll communicate with
    // clients. In this case it corresponds to an *asynchronous* service.
    builder.RegisterService(&service_);
    // Get hold of the completion queue used for the asynchronous communication
    // with the gRPC runtime.
    cq_ = builder.AddCompletionQueue();
    // Finally assemble the server.
    server_ = builder.BuildAndStart();
    std::cout << "Server listening on " << server_address << std::endl;

    // Proceed to the server's main loop.
    HandleRpcs();
  }

 private:
  // Class encompasing the state and logic needed to serve a request.
  class CallData {
   public:
    // Take in the "service" instance (in this case representing an asynchronous
    // server) and the completion queue "cq" used for asynchronous communication
    // with the gRPC runtime.
    CallData(Detector::AsyncService* service, ServerCompletionQueue* cq)
        : service_(service), cq_(cq), responder_(&ctx_), status_(CREATE) {
      // Invoke the serving logic right away.
      Proceed();
    }

    void Proceed() {
      if (status_ == CREATE) {
        // Make this instance progress to the PROCESS state.
        status_ = PROCESS;

        // As part of the initial CREATE state, we *request* that the system
        // start processing SayHello requests. In this request, "this" acts are
        // the tag uniquely identifying the request (so that different CallData
        // instances can serve different requests concurrently), in this case
        // the memory address of this CallData instance.
        service_->RequestPredict(&ctx_, &request_, &responder_, cq_, cq_,
                                  this);
      } else if (status_ == PROCESS) {
        // Spawn a new CallData instance to serve new clients while we process
        // the one for this CallData. The instance will deallocate itself as
        // part of its FINISH state.
        new CallData(service_, cq_);

        // The actual processing.
        predict_detector(&responder_,request_.file(),request_.thresh(),request_.hier_thresh());
        // And we are done! Let the gRPC runtime know we've finished, using the
        // memory address of this instance as the uniquely identifying tag for
        // the event.
        status_ = FINISH;
        responder_.Finish(responder_, Status::OK, this);
      } else {
        GPR_ASSERT(status_ == FINISH);
        // Once in the FINISH state, deallocate ourselves (CallData).
        delete this;
      }
    }

   private:
    // The means of communication with the gRPC runtime for an asynchronous
    // server.                                                                                                                                                                                                                                                                                                                                                                                                                                    
    Detector::AsyncService* service_;
    // The producer-consumer queue where for asynchronous server notifications.
    ServerCompletionQueue* cq_;
    // Context for the rpc, allowing to tweak aspects of it such as the use
    // of compression, authentication, as well as to send metadata back to the
    // client.
    ServerContext ctx_;
    // What we get from the client.
    DetectorRequest request_;
    // What we send back to the client.
    // DetectorReply reply_;

    // The means to get back to the client.
    ServerAsyncResponseWriter<DetectorReply> responder_;

    // Let's implement a tiny state machine with the following states.
    enum CallStatus { CREATE, PROCESS, FINISH };
    CallStatus status_;  // The current serving state.                                                                                
  };

  // This can be run in multiple threads if needed.
  void HandleRpcs() {
    // Spawn a new CallData instance to serve new clients.
    new CallData(&service_, cq_.get());
    void* tag;  // uniquely identifies a request.
    bool ok;
    while (true) {
      // Block waiting to read the next event from the completion queue. The
      // event is uniquely identified by its tag, which in this case is the
      // memory address of a CallData instance.
      // The return value of Next should always be checked. This return value
      // tells us whether there is any kind of event or cq_ is shutting down.
      GPR_ASSERT(cq_->Next(&tag, &ok));
      GPR_ASSERT(ok);
      static_cast<CallData*>(tag)->Proceed();
    }
  }

  std::unique_ptr<ServerCompletionQueue> cq_;
  Detector::AsyncService service_;
  std::unique_ptr<Server> server_;
};



int main(int argc, char** argv) {
  ServerImpl server;
  server.Run(argc,argv);
  return 0;
}