#include <unistd.h>
#include <grpcpp/grpcpp.h>
#include <thread>
#include <queue>
#include "darknet.h"
#include "../../proto/detector.grpc.pb.h"
#include "./base_service.h"

using grpc::Status;
using grpc::ServerAsyncReaderWriter;
using detector::DetectorRequest;
using detector::DetectorReply;
using detector::Detector;



enum METHOD { PREDICT_IMAGE };
/**
 * 图片识别类
 * 
 * 主要用于城管图像识别项目
 **/
class PredictImage:protected BaseService<METHOD>{
    public:
        PredictImage(Detector::AsyncService* service,network* net,std::string* srv,char** names,CompletionQueue* cq,ServerCompletionQueue* server_cq,METHOD method,std::queue<PredictImage*>* gpu_queue);
        ~PredictImage();
        void GrpcThread();
        void predict_detector(std::string file, float thresh, float hier_thresh);
        DetectorRequest request_;
    private:

        void ReadAsyncPredict();
        void WriteAsyncPredict();
       
        void detection_json(image im, detection *dets, int num, float thresh, char **names, int classes);
      
        uuid_t *uuid_;
        std::unique_ptr<ServerAsyncReaderWriter<DetectorReply,DetectorRequest>> stream_=nullptr;
     
        network* net_=nullptr;
        
        std::string* srv_=nullptr;
        char** names_=nullptr;
        bool gpu_busy_=false;
        std::queue<PredictImage*>* gpu_queue_=nullptr;
        Detector::AsyncService* service_=nullptr;
};